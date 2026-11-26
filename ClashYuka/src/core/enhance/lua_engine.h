// ClashYuka Lua enhance 脚本引擎
// 对应 Rust 版 enhance 中的 LuaJIT 集成（阶段7）
#pragma once

#include <filesystem>
#include <string>
#include <yaml-cpp/yaml.h>

namespace clash_yuka::enhance {

// Lua 引擎封装：执行 Lua enhance 脚本。
//
// 脚本约定（与 clash-nyanpasu 一致）：
//   function main(config)
//     config["proxies"] = {}  -- 修改
//     return config            -- 返回修改后的配置
//   end
//
// 本阶段不依赖外部 LuaJIT 库，实现一个简化的内嵌 Lua 子集解释器，
// 支持：表读写、基本运算、字符串操作。复杂脚本仍需后续接入 LuaJIT。
class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    // 执行脚本并返回处理后的 YAML 配置。
    // script_path 为 .lua 文件路径，config 为输入配置（同时作为输出）。
    // 成功返回 true，失败返回 false 且 error 填充错误信息。
    bool Execute(const std::filesystem::path& script_path,
                 YAML::Node& config,
                 std::string& error);

private:
    // YAML -> Lua 表字符串表示
    static std::string YamlToLuaTable(const YAML::Node& node, int indent = 0);
    // Lua 表字符串 -> YAML
    static YAML::Node LuaTableToYaml(const std::string& lua_str);
};

} // namespace clash_yuka::enhance
