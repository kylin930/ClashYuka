// ClashYuka JavaScript enhance 脚本引擎
// 对应 Rust 版 enhance 中的 QuickJS 集成（阶段7）
#pragma once

#include <filesystem>
#include <string>
#include <yaml-cpp/yaml.h>

namespace clash_yuka::enhance {

// QuickJS 引擎封装：执行 JavaScript enhance 脚本。
//
// 脚本约定（与 clash-nyanpasu 一致）：
//   function main(config) {
//     // config 为 YAML 配置的 JS 对象
//     config.proxies = [...];  // 修改
//     return config;           // 返回修改后的配置
//   }
//
// 本阶段提供接口与 YAML<->JSON 转换逻辑，实际 JS 执行标记为 TODO。
class JsEngine {
public:
    JsEngine();
    ~JsEngine();

    // 执行脚本并返回处理后的 YAML 配置。
    // script_path 为 .js 文件路径，config 为输入配置（同时作为输出）。
    // 成功返回 true，失败返回 false 且 error 填充错误信息。
    bool Execute(const std::filesystem::path& script_path,
                 YAML::Node& config,
                 std::string& error);

private:
    // YAML -> JSON 字符串（供 JS 引擎消费）
    static std::string YamlToJsonString(const YAML::Node& node);
    // JSON 字符串 -> YAML（JS 引擎返回值解析回 YAML）
    static YAML::Node JsonStringToYaml(const std::string& json);
};

} // namespace clash_yuka::enhance
