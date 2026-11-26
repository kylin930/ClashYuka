// ClashYuka 应用上下文实现
#include "core/app_context.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "core/enhance/builder.h"
#include "core/migration.h"
#include "core/resources.h"
#include "core/service_manager.h"
#include "core/utils/dirs.h"

namespace clash_yuka {

namespace fs = std::filesystem;

namespace {

// 恢复运行时配置文件内容（用于配置生成/应用失败后的回滚）
void RestoreRuntimeConfigFile(const fs::path& path, const std::string& bytes) {
    if (bytes.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return;
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs) ofs << bytes;
}

// 比较两个 YAML 节点的内容（缺失视为不同）
bool YamlNodeEqual(const YAML::Node& a, const YAML::Node& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    try {
        return YAML::Dump(a) == YAML::Dump(b);
    } catch (...) {
        return false;
    }
}

// 判断新旧配置之间是否存在「必须重启内核才能生效」的字段变更。
// mihomo 的 PUT /configs 可以热加载代理组/规则/mode，但不会重建监听端口；
// 实测修改 mixed-port 后 /configs 仍返回旧端口，因此这些字段需要走重启流程。
bool NeedsCoreRestart(const YAML::Node& old_cfg, const YAML::Node& new_cfg) {
    if (!old_cfg || !old_cfg.IsMap() || !new_cfg || !new_cfg.IsMap()) return true;
    static const char* kRestartKeys[] = {
        "mixed-port", "port", "socks-port", "redir-port", "tproxy-port",
        "allow-lan", "bind-address", "external-controller", "secret",
        "tun", "ipv6", "interface-name", "find-process-mode", "sniffer",
    };
    for (const char* key : kRestartKeys) {
        if (!YamlNodeEqual(old_cfg[key], new_cfg[key])) return true;
    }
    return false;
}

} // namespace

AppContext& AppContext::Get() {
    static AppContext instance;
    return instance;
}

void AppContext::Init() {
    if (initialized_) return;

    using namespace config;

    // 1. 读取应用配置（verge.yaml / profiles.yaml）
    verge_ = IVerge::Load(utils::verge_config_path());

    // 1.1 执行弃用字段迁移（auto_close_connection / auto_log_clean / window_size_position）
    if (core::Migration::Migrate(verge_)) {
        verge_.Save(utils::verge_config_path());
    }

    profiles_ = Profiles::Load(utils::profiles_yaml_path());

    // 2. 读取 clash guard overrides 获取 mihomo 连接信息
    //    GetClientInfo 内部对 mixed-port / external-controller 做了 guard，
    //    即使文件缺失或字段为空也会返回默认值（port=7890, server="127.0.0.1:9090"）。
    auto guard = IClashTemp::Load(utils::clash_guard_overrides_path());
    clash_info_ = guard.GetClientInfo();

    // 3. 构造 HTTP API 客户端（base_url 用 external-controller 地址）
    std::string base_url = "http://" + clash_info_.server;
    api_ = std::make_unique<clash::ApiClient>(
        base_url, clash_info_.secret.value_or(""));

    // 4. 构造 WS 客户端（不连接，等核心启动后由上层 Connect）
    ws_ = std::make_unique<clash::WsClient>();

    // 5. 构造 Clash 进程管理器
    process_ = std::make_unique<clash::ClashProcess>();

    // 6. 构造内核版本管理器
    core_manager_ = std::make_unique<clash::CoreManager>();

    initialized_ = true;

    // 7. 自动启动内核（失败不阻塞，UI 层会提示用户）
    auto err = StartCore();
    if (!err.empty()) {
        std::cerr << "[AppContext] 内核启动失败: " << err << std::endl;
    }
}

fs::path AppContext::RuntimeConfigPath() const {
    if (verge_.enable_service_mode.value_or(false)) {
        return utils::app_resources_dir() / "config.yaml";
    }
    return utils::app_config_dir() / "config.yaml";
}

bool AppContext::BuildRuntimeConfig(const fs::path& config_file, std::string* err) {
    const bool for_service = verge_.enable_service_mode.value_or(false);

    // 通过 ConfigBuilder 从 profiles 生成运行时 config.yaml
    // （若 profiles.current 为空或 profile 文件缺失，则生成失败）
    enhance::ConfigBuilder builder;
    builder.SetVerge(verge_);
    auto build_result = builder.Build(profiles_, utils::profiles_dir(), config_file);
    if (!build_result.ok) {
        std::string msg = "生成配置失败";
        if (!build_result.logs.empty()) {
            msg += ": " + build_result.logs.back();
        }
        if (err) *err = msg;
        return false;
    }

    // ConfigBuilder 的字段过滤会移除 external-controller / secret / mixed-port
    // （它们属于 managed_fields）。这些字段由 AppContext 注入，确保 mihomo
    // 启动后 API 可用且与 ApiClient/WsClient 使用相同的 secret。
    try {
        YAML::Node cfg = YAML::LoadFile(config_file.string());
        if (!cfg || !cfg.IsMap()) cfg = YAML::Node(YAML::NodeType::Map);

        if (for_service) {
            // 服务模式沿用原有行为：端口来自 guard overrides
            cfg["mixed-port"] = clash_info_.port;
        } else {
            // 优先使用 verge 配置的端口，支持 random-port（0）和自定义端口
            uint16_t mixed_port = clash_info_.port;
            if (verge_.enable_random_port.value_or(false)) {
                mixed_port = 0;  // 0 = 随机端口，mihomo 会自动选择可用端口
            } else if (verge_.verge_mixed_port.has_value()) {
                mixed_port = *verge_.verge_mixed_port;
            }
            cfg["mixed-port"] = mixed_port;
            // 同步更新 clash_info_.port，确保 GetMixedPort 返回正确值
            // （随机端口模式下后续会通过 API 再次更新为实际端口）
            clash_info_.port = mixed_port;
        }

        cfg["external-controller"] = clash_info_.server;
        if (clash_info_.secret) {
            cfg["secret"] = *clash_info_.secret;
        } else {
            cfg["secret"] = "";
        }

        // 注入 geodata 相关配置：禁用 geodata 模式与自动更新，
        // 避免内核启动时尝试从 GitHub 下载资源（在国内通常失败）。
        cfg["geodata-mode"] = false;
        cfg["geo-auto-update"] = false;

        // TUN 模式：由 verge 的开关/堆栈统一控制（覆盖订阅里的 enable，
        // 避免出现「界面已关闭 TUN 但订阅配置仍开启」的不一致状态）。
        // device 使用独立名称，避免与 Nyanpasu 等客户端的 "Meta" 适配器冲突；
        // auto-route / dns-hijack 强制开启，否则 TUN 虽然创建了网卡但不会接管流量。
        {
            YAML::Node tun = cfg["tun"];
            if (!tun || !tun.IsMap()) tun = YAML::Node(YAML::NodeType::Map);
            tun["enable"] = verge_.enable_tun_mode.value_or(false);
            tun["device"] = "YukaTun";
            tun["stack"] = verge_.tun_stack.value_or("gvisor");
            tun["auto-route"] = true;
            tun["auto-detect-interface"] = true;
            YAML::Node hijack(YAML::NodeType::Sequence);
            hijack.push_back("any:53");
            tun["dns-hijack"] = hijack;
            cfg["tun"] = tun;
        }

        // 进程匹配 / Sniffer：留空表示跟随订阅配置。
        // find-process-mode=off 可减少内存占用（但无法按进程分流/匹配规则）。
        if (verge_.mihomo_find_process_mode &&
            !verge_.mihomo_find_process_mode->empty()) {
            cfg["find-process-mode"] = *verge_.mihomo_find_process_mode;
        }
        if (verge_.mihomo_sniffer_enable.has_value()) {
            YAML::Node sniffer = cfg["sniffer"];
            if (!sniffer || !sniffer.IsMap()) {
                sniffer = YAML::Node(YAML::NodeType::Map);
            }
            sniffer["enable"] = *verge_.mihomo_sniffer_enable;
            cfg["sniffer"] = sniffer;
        }

        // 检测 GEOIP 规则与 MMDB 状态：
        //  - 若订阅含 GEOIP 规则但本地无 Country.mmdb，启动后 mihomo 会因无法解析
        //    IP 地理位置而失败（不再自动移除规则，避免静默修改用户配置）。
        //  - 在日志中给出明确提示，引导用户到设置页面下载。
        fs::path mmdb_path = core::Resources::Get().GetGeoIPPath();
        std::error_code ec;
        bool has_mmdb = fs::exists(mmdb_path, ec) && fs::is_regular_file(mmdb_path, ec);
        bool has_geoip_rule = false;
        if (cfg["rules"] && cfg["rules"].IsSequence()) {
            for (const auto& rule_node : cfg["rules"]) {
                std::string rule_str;
                if (rule_node.IsScalar()) {
                    rule_str = rule_node.as<std::string>();
                } else if (rule_node.IsSequence() && rule_node.size() > 0) {
                    rule_str = rule_node[0].as<std::string>("");
                }
                std::string upper = rule_str;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper.rfind("GEOIP,", 0) == 0) {
                    has_geoip_rule = true;
                    break;
                }
            }
        }
        if (!has_mmdb && has_geoip_rule) {
            std::cerr << "[AppContext] 警告: 订阅含 GEOIP 规则但未找到 Country.mmdb，"
                      << "请前往 设置 > Clash 内核 下载 GeoIP 数据集" << std::endl;
        }

        std::ofstream ofs(config_file, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            if (err) *err = "无法写入运行时配置: " + config_file.string();
            return false;
        }
        ofs << "# Generated by ClashYuka\n\n";
        ofs << YAML::Dump(cfg);
    } catch (const std::exception& e) {
        if (err) *err = std::string("注入配置失败: ") + e.what();
        return false;
    }

    return true;
}

std::string AppContext::StartCore() {
    if (core_started_) {
        last_start_error_.clear();
        return "";
    }

    const fs::path config_file = RuntimeConfigPath();
    std::string build_err;
    if (!BuildRuntimeConfig(config_file, &build_err)) {
        last_start_error_ = build_err;
        std::cerr << "[AppContext] " << build_err << std::endl;
        return build_err;
    }

    std::string err = LaunchCore(config_file);
    if (!err.empty()) {
        last_start_error_ = err;
        return err;
    }
    last_start_error_.clear();
    return "";
}

std::string AppContext::LaunchCore(const fs::path& config_file) {
    // ---- 服务模式分支 ----
    if (verge_.enable_service_mode.value_or(false)) {
        auto svc_state = core::ServiceManager::Get().QueryStatus();
        if (svc_state.status == core::ServiceStatus::NotInstalled) {
            return "服务未安装，请在设置中安装服务";
        }

        if (svc_state.status == core::ServiceStatus::Running) {
            // 服务已在运行：通过 mihomo API 重载配置，避免触发 UAC 提示。
            // 之前调用 ServiceManager::Restart() 会导致每次启动 GUI 都弹 UAC。
            // mihomo 的 PUT /configs 接口可让运行中的内核重新加载 config.yaml。
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // 尝试重载配置（mihomo 可能刚启动，需要等它就绪）
            bool reloaded = false;
            for (int i = 0; i < 10 && !reloaded; ++i) {
                if (api_->ReloadConfig(config_file.string()).ok) {
                    reloaded = true;
                } else {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            if (!reloaded) {
                // API 重载失败（mihomo 可能未就绪），不阻塞，继续连接 WS
                std::cerr << "[AppContext] 警告: 服务模式 API 重载配置失败" << std::endl;
            }
        } else if (svc_state.status == core::ServiceStatus::Stopped ||
                   svc_state.status == core::ServiceStatus::Stopping) {
            // 服务已安装但未运行：启动服务（需要 UAC 提权）
            if (!core::ServiceManager::Get().Start()) {
                return "启动服务失败: " + core::ServiceManager::Get().last_error();
            }
            // 轮询等待服务运行
            for (int i = 0; i < 30; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                auto state = core::ServiceManager::Get().QueryStatus();
                if (state.status == core::ServiceStatus::Running) {
                    break;
                }
                if (state.status == core::ServiceStatus::Stopped ||
                    state.status == core::ServiceStatus::NotInstalled) {
                    return "服务启动失败";
                }
            }
        }
        core_started_ = true;
        NotifyCoreState(true);
        std::string ws_url = "ws://" + clash_info_.server;
        ws_->Connect(ws_url, clash_info_.secret.value_or(""));
        return "";
    }

    // ---- 普通模式（本地进程）----

    // 读取配置中的核心名称（默认 mihomo）
    std::string core_name = verge_.clash_core.value_or("mihomo");

    // 通过 CoreManager 获取内核路径
    fs::path core_path = core_manager_->GetCorePath(core_name);
    if (core_path.empty()) {
        return "未知的核心类型: " + core_name;
    }

    // 检查内核文件是否存在
    std::error_code ec;
    if (!fs::exists(core_path, ec) || !fs::is_regular_file(core_path, ec)) {
        return "内核未安装，请前往设置下载";
    }

    // 设置日志回调：转发到 stderr（后续可接入 Logs 页面），
    // 同时记录最后一行输出，配置启动失败时把内核的真实原因带回给调用方。
    {
        std::lock_guard<std::mutex> lk(core_log_mutex_);
        last_core_log_.clear();
    }
    process_->set_on_log([this](const std::string& line) {
        std::cerr << "[mihomo] " << line << std::endl;
        if (line.empty()) return;
        std::lock_guard<std::mutex> lk(core_log_mutex_);
        last_core_log_ = line;
    });

    // Go 运行时内存参数（0 = 使用 mihomo 默认）
    process_->set_go_memory_env(verge_.mihomo_gogc.value_or(0),
                                verge_.mihomo_memlimit_mb.value_or(0));

    // 启动内核
    std::string err;
    if (!process_->Start(core_path, fs::path(config_file).parent_path(), config_file, &err)) {
        return err;
    }

    core_started_ = true;
    NotifyCoreState(true);

    // 连接 WebSocket（流量/日志/连接流）
    std::string ws_url = "ws://" + clash_info_.server;
    ws_->Connect(ws_url, clash_info_.secret.value_or(""));

    return "";
}

bool AppContext::WaitForApiReady(int timeout_ms) {
    if (!api_) return false;
    const int safe_timeout = timeout_ms > 0 ? timeout_ms : 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(safe_timeout);
    for (;;) {
        if (!api_->GetVersion().version.empty()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

std::string AppContext::ApplyConfig() {
    if (!initialized_) return "应用未初始化";
    // 串行化：连续导入/激活时只允许一个配置应用流程在跑
    std::lock_guard<std::mutex> apply_lock(apply_config_mutex_);

    const fs::path config_file = RuntimeConfigPath();

    // 备份旧运行时配置：用于「重启失败回滚」以及「是否需要重启」判断。
    std::string old_bytes;
    YAML::Node old_cfg;
    {
        std::ifstream ifs(config_file, std::ios::binary);
        if (ifs) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            old_bytes = ss.str();
        }
        if (!old_bytes.empty()) {
            try {
                old_cfg = YAML::Load(old_bytes);
            } catch (...) {
                old_cfg = YAML::Node();
            }
        }
    }

    // 1. 重新生成运行时配置（唯一写入点；失败则恢复旧文件后如实报错）
    std::string build_err;
    if (!BuildRuntimeConfig(config_file, &build_err)) {
        RestoreRuntimeConfigFile(config_file, old_bytes);
        return build_err;
    }

    // 2. 内核未运行：直接使用新配置启动
    if (!IsCoreRunning()) {
        std::string start_err = LaunchCore(config_file);
        if (start_err.empty() && WaitForApiReady(20000)) {
            NotifyConfigApplied();
            return "";
        }
        if (start_err.empty()) start_err = "内核已启动但 API 未就绪";

        std::string failed_log;
        {
            std::lock_guard<std::mutex> lk(core_log_mutex_);
            failed_log = last_core_log_;
        }
        // 终止可能仍在自动重启的失败内核，并恢复旧配置
        StopCore();
        if (!old_bytes.empty()) RestoreRuntimeConfigFile(config_file, old_bytes);

        std::string msg = "内核启动失败: " + start_err;
        if (!failed_log.empty()) msg += "；内核输出: " + failed_log;
        return msg;
    }

    // 3. 读取新配置并判断是否需要重启内核（监听端口等无法热加载）
    YAML::Node new_cfg;
    try {
        new_cfg = YAML::LoadFile(config_file.string());
    } catch (const std::exception& e) {
        RestoreRuntimeConfigFile(config_file, old_bytes);
        return std::string("运行时配置非法: ") + e.what();
    }
    const bool restart_required = NeedsCoreRestart(old_cfg, new_cfg);

    if (verge_.enable_service_mode.value_or(false)) {
        if (restart_required) {
            // 服务模式无法在不触发 UAC 的情况下重启内核，明确告知而不是假装成功
            return "配置已写入，但监听/控制器设置需要重启系统服务后才能生效";
        }
        auto reload = api_->ReloadConfig(config_file.string());
        if (!reload.ok) {
            RestoreRuntimeConfigFile(config_file, old_bytes);
            return "配置热重载失败 (HTTP " + std::to_string(reload.http_status) +
                   "): " + reload.error;
        }
        if (!WaitForApiReady(10000)) return "配置已重载但 API 未就绪";
        NotifyConfigApplied();
        return "";
    }

    // 4. 可热加载：PUT /configs
    //    mihomo 会先完整校验配置，失败时不会改动运行中的状态，
    //    因此此时只需恢复旧配置文件并原样返回错误，无需重启内核。
    if (!restart_required) {
        auto reload = api_->ReloadConfig(config_file.string());
        if (reload.ok) {
            if (WaitForApiReady(8000)) {
                NotifyConfigApplied();
                return "";
            }
        } else {
            RestoreRuntimeConfigFile(config_file, old_bytes);
            return "配置热重载失败 (HTTP " + std::to_string(reload.http_status) +
                   "): " + reload.error;
        }
    }

    // 5. 安全重启内核：停止 -> 用新配置启动 -> 等待 API 就绪；失败则回滚旧配置
    StopCore();
    std::string start_err = LaunchCore(config_file);
    if (start_err.empty() && WaitForApiReady(20000)) {
        NotifyConfigApplied();
        return "";
    }
    if (start_err.empty()) start_err = "内核已启动但 API 未就绪";

    // 在回滚前抓取失败那次启动的内核输出（LaunchCore 回滚时会清空记录）
    std::string failed_core_log;
    {
        std::lock_guard<std::mutex> lk(core_log_mutex_);
        failed_core_log = last_core_log_;
    }

    std::string rollback_err;
    if (!old_bytes.empty()) {
        RestoreRuntimeConfigFile(config_file, old_bytes);
        StopCore();
        rollback_err = LaunchCore(config_file);
        if (rollback_err.empty() && !WaitForApiReady(15000)) {
            rollback_err = "内核已启动但 API 未就绪";
        }
    }

    std::string msg = std::string(restart_required ? "监听配置变更后重启内核失败" : "配置热重载失败") +
                      ": " + start_err;
    if (!failed_core_log.empty()) msg += "；内核输出: " + failed_core_log;
    if (!old_bytes.empty()) {
        msg += rollback_err.empty() ? "；已回滚到原配置" : ("；回滚失败: " + rollback_err);
    }
    return msg;
}

void AppContext::StopCore() {
    if (!core_started_) return;
    if (verge_.enable_service_mode.value_or(false)) {
        // 服务模式：不停止系统服务。
        // 服务的设计目的就是后台持续运行，即使 GUI 退出也保持 mihomo 活跃。
        // 这里仅断开 WebSocket 连接，让服务继续运行。
        // 若需要真正停止服务，用户应在设置页关闭"服务模式"开关（触发 Uninstall）。
        if (ws_) ws_->Disconnect();
    } else {
        process_->Stop();
    }
    core_started_ = false;
    NotifyCoreState(false);
}

std::string AppContext::RestartCore() {
    StopCore();
    return StartCore();
}

bool AppContext::IsCoreRunning() const {
    if (verge_.enable_service_mode.value_or(false)) {
        auto state = core::ServiceManager::Get().QueryStatus();
        return state.status == core::ServiceStatus::Running;
    }
    return core_started_ && process_ && process_->IsRunning();
}

clash::ApiClient& AppContext::api() { return *api_; }
clash::WsClient& AppContext::ws() { return *ws_; }
clash::ClashProcess& AppContext::process() { return *process_; }
clash::CoreManager& AppContext::core_manager() { return *core_manager_; }

config::IVerge& AppContext::verge() { return verge_; }
config::Profiles& AppContext::profiles() { return profiles_; }

const config::ClashInfo& AppContext::clash_info() const {
    return clash_info_;
}

uint16_t AppContext::GetMixedPort() const {
    if (verge_.enable_random_port.value_or(false)) {
        // 随机端口：返回 clash_info_.port（启动后可能已通过 API 更新）
        return clash_info_.port;
    }
    if (verge_.verge_mixed_port.has_value()) {
        return *verge_.verge_mixed_port;
    }
    return clash_info_.port;
}

// ---- 内核状态变化通知 ----

int AppContext::SubscribeCoreState(std::function<void(bool running)> cb) {
    std::lock_guard<std::mutex> lock(core_state_mutex_);
    int handle = ++next_core_state_handle_;
    core_state_cbs_.emplace_back(handle, std::move(cb));
    return handle;
}

void AppContext::UnsubscribeCoreState(int handle) {
    std::lock_guard<std::mutex> lock(core_state_mutex_);
    for (auto it = core_state_cbs_.begin(); it != core_state_cbs_.end(); ++it) {
        if (it->first == handle) {
            core_state_cbs_.erase(it);
            return;
        }
    }
}

void AppContext::NotifyCoreState(bool running) {
    // 锁内拷贝一份回调，锁外调用，避免回调中再次订阅/注销导致死锁
    std::vector<std::function<void(bool running)>> snapshot;
    {
        std::lock_guard<std::mutex> lock(core_state_mutex_);
        snapshot.reserve(core_state_cbs_.size());
        for (const auto& kv : core_state_cbs_) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& cb : snapshot) {
        if (cb) cb(running);
    }
}

// ---- 配置应用通知 ----

int AppContext::SubscribeConfigApplied(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(config_applied_mutex_);
    int handle = ++next_config_applied_handle_;
    config_applied_cbs_.emplace_back(handle, std::move(cb));
    return handle;
}

void AppContext::UnsubscribeConfigApplied(int handle) {
    std::lock_guard<std::mutex> lock(config_applied_mutex_);
    for (auto it = config_applied_cbs_.begin(); it != config_applied_cbs_.end(); ++it) {
        if (it->first == handle) {
            config_applied_cbs_.erase(it);
            return;
        }
    }
}

void AppContext::NotifyConfigApplied() {
    std::vector<std::function<void()>> snapshot;
    {
        std::lock_guard<std::mutex> lock(config_applied_mutex_);
        snapshot.reserve(config_applied_cbs_.size());
        for (const auto& kv : config_applied_cbs_) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& cb : snapshot) {
        if (cb) cb();
    }
}

} // namespace clash_yuka
