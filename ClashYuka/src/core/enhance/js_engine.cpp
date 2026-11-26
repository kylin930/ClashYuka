// ClashYuka JavaScript enhance 脚本引擎实现
// 对应 Rust 版 enhance 中的 QuickJS 集成
#include "core/enhance/js_engine.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace clash_yuka::enhance {

namespace {

// 大小写不敏感比较（a 与 C 字符串 b）
bool eq_icase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

// 判断字符串是否为十进制整数/浮点数字面量（含可选符号、小数点、指数）
bool is_number_literal(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i == s.size()) return false;
    bool has_digit = false;
    bool has_dot = false;
    bool has_exp = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            has_digit = true;
        } else if (c == '.' && !has_dot && !has_exp) {
            has_dot = true;
        } else if ((c == 'e' || c == 'E') && has_digit && !has_exp) {
            has_exp = true;
            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) ++i;
        } else {
            return false;
        }
    }
    return has_digit;
}

// 转义字符串为 JSON 字符串字面量（含双引号）
std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

// YAML::Node -> nlohmann::json
nlohmann::json yaml_to_json(const YAML::Node& node) {
    if (!node || node.IsNull()) return nullptr;
    if (node.IsScalar()) {
        std::string s;
        try {
            s = node.as<std::string>();
        } catch (...) {
            return nullptr;
        }
        if (eq_icase(s, "true")) return true;
        if (eq_icase(s, "false")) return false;
        if (eq_icase(s, "null") || s == "~" || s.empty()) return nullptr;
        if (is_number_literal(s)) {
            try {
                if (s.find('.') != std::string::npos ||
                    s.find('e') != std::string::npos ||
                    s.find('E') != std::string::npos) {
                    return std::stod(s);
                }
                return std::stoll(s);
            } catch (...) {
                return escape_json_string(s);
            }
        }
        return s;
    }
    if (node.IsSequence()) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto it = node.begin(); it != node.end(); ++it) {
            arr.push_back(yaml_to_json(*it));
        }
        return arr;
    }
    if (node.IsMap()) {
        nlohmann::json obj = nlohmann::json::object();
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (!it->first.IsScalar()) continue;
            std::string key;
            try {
                key = it->first.as<std::string>();
            } catch (...) {
                continue;
            }
            obj[key] = yaml_to_json(it->second);
        }
        return obj;
    }
    return nullptr;
}

// nlohmann::json -> YAML::Node
YAML::Node json_to_yaml(const nlohmann::json& j) {
    if (j.is_null()) {
        return YAML::Node(YAML::NodeType::Null);
    }
    if (j.is_boolean()) {
        return YAML::Node(j.get<bool>());
    }
    if (j.is_number_integer()) {
        return YAML::Node(j.get<int64_t>());
    }
    if (j.is_number_float()) {
        return YAML::Node(j.get<double>());
    }
    if (j.is_string()) {
        return YAML::Node(j.get<std::string>());
    }
    if (j.is_array()) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (const auto& item : j) {
            seq.push_back(json_to_yaml(item));
        }
        return seq;
    }
    if (j.is_object()) {
        YAML::Node map(YAML::NodeType::Map);
        for (auto it = j.begin(); it != j.end(); ++it) {
            map[it.key()] = json_to_yaml(it.value());
        }
        return map;
    }
    return YAML::Node();
}

} // namespace

JsEngine::JsEngine() = default;
JsEngine::~JsEngine() = default;

std::string JsEngine::YamlToJsonString(const YAML::Node& node) {
    return yaml_to_json(node).dump();
}

YAML::Node JsEngine::JsonStringToYaml(const std::string& json) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        return json_to_yaml(j);
    } catch (...) {
        return YAML::Node();
    }
}

bool JsEngine::Execute(const std::filesystem::path& script_path,
                       YAML::Node& config,
                       std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(script_path, ec)) {
        error = "script file not found: " + script_path.string();
        return false;
    }

    std::ifstream ifs(script_path, std::ios::binary);
    if (!ifs) {
        error = "failed to open script file: " + script_path.string();
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string script = oss.str();

    // TODO: 接入 QuickJS 后实现以下流程：
    //   1. 创建 QuickJS runtime/context
    //   2. 注入脚本（编译并执行顶层声明，定义 main 函数）
    //   3. 构造入参：YamlToJsonString(config) 解析为 JS 对象
    //   4. 调用 main(config)
    //   5. 获取返回值并序列化为 JSON 字符串
    //   6. JsonStringToYaml 写回 config
    //
    // 当前阶段：不执行 JS，配置不变，记录日志并返回成功。
    (void)script;
    (void)config;
    error = "JS enhance not yet implemented (QuickJS pending)";
    return true;
}

} // namespace clash_yuka::enhance
