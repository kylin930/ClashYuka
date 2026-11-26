// ClashYuka 运行时配置生成实现
// 对应 Rust 版 enhance/mod.rs::enhance() 与 config/core.rs::generate()
#include "core/enhance/builder.h"

#include "core/enhance/js_engine.h"
#include "core/enhance/lua_engine.h"
#include "core/enhance/merge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace clash_yuka::enhance {

namespace {

// ClashYuka 自管理、不应出现在 profile 派生配置中的字段。
// 这些字段由 guard overrides / CLI 参数注入给 mihomo，不暴露给 profile。
const std::set<std::string>& managed_fields() {
    static const std::set<std::string> kFields = {
        "external-controller",
        "external-controller-tls",
        "external-controller-cors",
        "secret",
        "external-ui",
        "external-ui-name",
        "external-ui-url",
        "bind-address",
    };
    return kFields;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

ConfigBuilder::ConfigBuilder() = default;
ConfigBuilder::~ConfigBuilder() = default;

void ConfigBuilder::SetVerge(const clash_yuka::config::IVerge& verge) {
    verge_ = verge;
    verge_set_ = true;
}

YAML::Node ConfigBuilder::LoadProfileYaml(const std::filesystem::path& file_path) {
    YAML::Node node;
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec)) {
        return node; // 空
    }
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) return node;
    try {
        node = YAML::Load(ifs);
    } catch (...) {
        node.reset();
    }
    return node;
}

void ConfigBuilder::ApplyChain(YAML::Node& config,
                               const clash_yuka::config::Profiles& profiles,
                               const std::vector<std::string>& chain,
                               const std::filesystem::path& profiles_dir,
                               std::vector<std::string>& logs) {
    for (const auto& uid : chain) {
        const auto* p = profiles.Get(uid);
        if (p == nullptr) {
            logs.push_back("chain item not found: " + uid);
            continue;
        }
        std::filesystem::path file_path = profiles_dir / p->file;
        if (p->type == clash_yuka::config::ProfileType::Merge) {
            YAML::Node patch = LoadProfileYaml(file_path);
            if (!patch) {
                logs.push_back("merge profile file invalid or missing: " + p->file);
                continue;
            }
            config = Merge(std::move(config), patch);
        } else if (p->type == clash_yuka::config::ProfileType::Script) {
            // 脚本引擎：根据 script_type 调用 JsEngine 或 LuaEngine
            // 当前两个引擎均为接口预留（执行逻辑留待 QuickJS/LuaJIT 接入），
            // 配置不变，仅记录日志。
            auto script_path = profiles_dir / p->file;
            std::string err;
            if (p->script_type.value_or("javascript") == "lua") {
                LuaEngine lua;
                lua.Execute(script_path, config, err);
            } else {
                JsEngine js;
                js.Execute(script_path, config, err);
            }
            if (!err.empty()) {
                logs.push_back(err);
            }
            logs.push_back("script enhance processed: " + uid);
        } else {
            // Remote/Local 不应出现在 chain 中
            logs.push_back("unsupported chain item type, skipped: " + uid);
        }
    }
}

void ConfigBuilder::ApplyFieldFilter(YAML::Node& config, bool enable_clash_fields) {
    if (!enable_clash_fields) return;
    if (!config || !config.IsMap()) return;
    for (const auto& f : managed_fields()) {
        config.remove(f);
    }
}

BuildResult ConfigBuilder::Build(clash_yuka::config::Profiles& profiles,
                                 const std::filesystem::path& profiles_dir,
                                 const std::filesystem::path& output_path) {
    BuildResult result;

    bool enable_clash_fields = true;
    if (verge_set_ && verge_.enable_clash_fields.has_value()) {
        enable_clash_fields = *verge_.enable_clash_fields;
    }

    // 1. 收集 current profile（仅 Remote/Local 作为基础配置源）
    std::vector<const clash_yuka::config::Profile*> current_profiles;
    for (const auto& uid : profiles.current) {
        const auto* p = profiles.Get(uid);
        if (p == nullptr) {
            result.logs.push_back("current profile not found: " + uid);
            continue;
        }
        if (p->type != clash_yuka::config::ProfileType::Remote &&
            p->type != clash_yuka::config::ProfileType::Local) {
            result.logs.push_back("current profile not remote/local, skipped: " + uid);
            continue;
        }
        current_profiles.push_back(p);
    }

    if (current_profiles.empty()) {
        result.logs.push_back("no valid current profile");
        return result;
    }

    // 2. 读取每个 current profile 文件，并应用其自身 chain
    std::vector<YAML::Node> profile_configs;
    for (const auto* p : current_profiles) {
        std::filesystem::path file_path = profiles_dir / p->file;
        YAML::Node cfg = LoadProfileYaml(file_path);
        if (!cfg) {
            result.logs.push_back("profile file invalid or missing: " + p->file);
            continue;
        }
        if (!cfg.IsMap()) {
            result.logs.push_back("profile file is not a mapping: " + p->file);
            continue;
        }
        ApplyChain(cfg, profiles, p->chain, profiles_dir, result.logs);
        profile_configs.push_back(std::move(cfg));
    }

    if (profile_configs.empty()) {
        result.logs.push_back("no profile config loaded");
        return result;
    }

    // 3. 合并多个 current profile：首个完整作为基础，其余仅合并 proxies 列表
    //    （对应 Rust 版 utils::merge_profiles 的简化策略）
    YAML::Node config = profile_configs[0];
    for (size_t i = 1; i < profile_configs.size(); ++i) {
        const YAML::Node& extra = profile_configs[i];
        const YAML::Node extra_proxies = extra["proxies"];
        if (extra_proxies && extra_proxies.IsSequence()) {
            YAML::Node dst = config["proxies"];
            if (!dst.IsSequence()) {
                config["proxies"] = YAML::Clone(extra_proxies);
            } else {
                for (auto it = extra_proxies.begin(); it != extra_proxies.end(); ++it) {
                    dst.push_back(YAML::Clone(*it));
                }
            }
        }
    }

    // 4. 应用全局 chain
    ApplyChain(config, profiles, profiles.chain, profiles_dir, result.logs);

    // 5. 字段过滤
    ApplyFieldFilter(config, enable_clash_fields);

    // 6. 收集 exists_keys（小写）
    if (config && config.IsMap()) {
        for (auto it = config.begin(); it != config.end(); ++it) {
            if (it->first.IsScalar()) {
                try {
                    result.exists_keys.push_back(to_lower(it->first.as<std::string>()));
                } catch (...) {}
            }
        }
    }

    // 7. 写入 output_path
    std::ofstream ofs(output_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        result.logs.push_back("failed to open output file: " + output_path.string());
        return result;
    }
    ofs << "# Generated by ClashYuka\n\n";
    try {
        ofs << YAML::Dump(config);
    } catch (...) {
        result.logs.push_back("failed to dump config yaml");
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace clash_yuka::enhance
