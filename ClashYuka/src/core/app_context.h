// ClashYuka 应用上下文（Phase 4 Task 10）
//
// 核心服务（ApiClient / WsClient / ClashProcess）与应用配置（IVerge / Profiles）
// 的统一访问点。MainFrame 及各页面通过 AppContext::Get() 取用，避免在 UI 层
// 散落裸指针与生命周期管理。
//
// 设计要点：
//   - Meyers singleton（static 局部变量），首次 Get() 调用时构造。
//   - Init() 应在 wxApp::OnInit 中、创建主窗口前调用一次；重复调用安全。
//   - 持有的核心服务不可拷贝/移动，故用 std::unique_ptr 间接持有。
//   - WsClient 在 Init 中仅构造，不连接；连接时机由上层根据核心启动状态决定。
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/clash/api_client.h"
#include "core/clash/core_manager.h"
#include "core/clash/process.h"
#include "core/clash/ws_client.h"
#include "core/config/clash_config.h"
#include "core/config/profile.h"
#include "core/config/verge_config.h"

namespace clash_yuka {

class AppContext {
public:
    static AppContext& Get();

    // 不可拷贝/移动
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;

    // 初始化核心服务：读取 verge.yaml / profiles.yaml / clash-guard-overrides.yaml，
    // 构造 ApiClient / WsClient / ClashProcess。重复调用安全（仅首次执行）。
    // 注意：WsClient 在此仅构造，不连接；连接由调用方在核心启动后触发。
    void Init();

    // 是否已初始化（Init 已成功执行）。
    bool IsInitialized() const { return initialized_; }

    // ---- 核心服务访问（Init 后才可调用）----
    clash::ApiClient& api();
    clash::WsClient& ws();
    clash::ClashProcess& process();
    clash::CoreManager& core_manager();

    // ---- 内核生命周期管理 ----
    // 启动内核（Init 后自动调用，也可手动调用）。
    // 返回错误信息，空字符串表示成功。
    std::string StartCore();
    // 停止内核。
    void StopCore();
    // 重启内核（Stop + Start，不依赖上次 Start 参数）。
    // 返回错误信息，空字符串表示成功。
    std::string RestartCore();
    // 内核是否已启动。
    bool IsCoreRunning() const;

    // ---- 配置应用（导入/激活/设置变更的统一下发路径）----
    // 重新生成运行时 config.yaml，并应用到运行中的内核：
    //   1) 生成失败：恢复旧配置文件并返回错误；
    //   2) 监听端口等无法热加载的字段变更：安全重启内核（失败自动回滚旧配置）；
    //   3) 其余变更：PUT /configs 热重载，失败时保留运行中的内核并原样返回 mihomo 错误。
    // 成功后触发配置应用通知（SubscribeConfigApplied）。
    // 注意：会执行网络请求与进程启停，必须在后台线程调用，禁止阻塞 GUI 主线程。
    // 返回空字符串表示配置已真正生效；否则为可直接展示的错误信息。
    std::string ApplyConfig();

    // 运行时配置文件路径：
    //   普通模式 <app_config_dir>/config.yaml；服务模式 <resources>/config.yaml。
    std::filesystem::path RuntimeConfigPath() const;

    // 轮询等待 mihomo API 就绪（GET /version 成功）。超时返回 false。
    bool WaitForApiReady(int timeout_ms);

    // 配置应用成功通知。回调在触发线程中同步执行（通常是后台线程），
    // 若需更新 UI 请自行 wxTheApp->CallAfter。
    int SubscribeConfigApplied(std::function<void()> cb);
    void UnsubscribeConfigApplied(int handle);

    // ---- 配置访问 ----
    config::IVerge& verge();
    config::Profiles& profiles();

    // mihomo 连接信息（external-controller / mixed-port / secret）。
    // Init 后可用；未 Init 时返回默认空对象。
    const config::ClashInfo& clash_info() const;

    // 获取实际生效的 mixed-port（优先使用 verge 配置，兼容随机端口）。
    // 注意：随机端口模式下，内核启动前返回 0，启动后通过 API 拉取更新。
    uint16_t GetMixedPort() const;

    // 最后一次内核启动的错误信息（空字符串表示成功）。
    const std::string& last_start_error() const { return last_start_error_; }

    // ---- 内核状态变化通知 ----
    // 注册回调：当内核启动（running=true）或停止（running=false）时触发。
    // 返回 handle，用于注销。回调在触发线程中同步执行（通常是后台线程），
    // 若需更新 UI 请自行 wxTheApp->CallAfter。
    int SubscribeCoreState(std::function<void(bool running)> cb);
    void UnsubscribeCoreState(int handle);

private:
    AppContext() = default;
    ~AppContext() = default;

    std::unique_ptr<clash::ApiClient> api_;
    std::unique_ptr<clash::WsClient> ws_;
    std::unique_ptr<clash::ClashProcess> process_;
    std::unique_ptr<clash::CoreManager> core_manager_;
    config::IVerge verge_;
    config::Profiles profiles_;
    config::ClashInfo clash_info_;
    bool initialized_ = false;
    bool core_started_ = false;
    std::string last_start_error_;

    // 内核状态变化订阅
    mutable std::mutex core_state_mutex_;
    int next_core_state_handle_ = 0;
    std::vector<std::pair<int, std::function<void(bool running)>>> core_state_cbs_;

    // 配置应用成功订阅
    mutable std::mutex config_applied_mutex_;
    int next_config_applied_handle_ = 0;
    std::vector<std::pair<int, std::function<void()>>> config_applied_cbs_;

    // 内核最近一行输出（用于启动失败时给出具体原因）
    mutable std::mutex core_log_mutex_;
    std::string last_core_log_;

    // 串行化 ApplyConfig：避免用户快速连续激活订阅时并发写配置文件/启停内核
    std::mutex apply_config_mutex_;

    void NotifyCoreState(bool running);
    void NotifyConfigApplied();

    // 生成运行时配置（profiles -> config.yaml，并注入端口/控制器等字段）。
    // 不包含内核启停逻辑；失败时 err 写入具体原因。
    bool BuildRuntimeConfig(const std::filesystem::path& config_file, std::string* err);

    // 使用已生成的配置文件启动/接管内核（含服务模式分支）。
    // 返回错误信息，空字符串表示启动请求成功（API 就绪需另行 WaitForApiReady）。
    std::string LaunchCore(const std::filesystem::path& config_file);
};

} // namespace clash_yuka
