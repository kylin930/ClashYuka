// ClashYuka Lua enhance 脚本引擎实现
// 对应 Rust 版 enhance 中的 LuaJIT 集成
#include "core/enhance/lua_engine.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

// 十进制整数字面量
bool is_int_literal(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!(s[i] >= '0' && s[i] <= '9')) return false;
    }
    return true;
}

// 十进制浮点字面量（含小数点或指数）
bool is_float_literal(const std::string& s) {
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
    return has_digit && (has_dot || has_exp);
}

// 转义字符串为 Lua 字符串字面量（含双引号）
std::string escape_lua_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\%03d",
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

std::string indent_str(int n) {
    return std::string(static_cast<size_t>(n) * 2, ' ');
}

// 检查脚本是否定义了 main 函数：匹配 "function" + 空白 + "main" + 非标识符字符
bool has_main_function(const std::string& script) {
    const std::string kw = "function";
    size_t pos = 0;
    while ((pos = script.find(kw, pos)) != std::string::npos) {
        size_t i = pos + kw.size();
        bool has_ws = false;
        while (i < script.size() &&
               (script[i] == ' ' || script[i] == '\t' || script[i] == '\n' || script[i] == '\r')) {
            has_ws = true;
            ++i;
        }
        if (has_ws && script.compare(i, 4, "main") == 0) {
            char after = (i + 4 < script.size()) ? script[i + 4] : '\0';
            if (after == '\0' ||
                !(std::isalnum(static_cast<unsigned char>(after)) || after == '_')) {
                return true;
            }
        }
        pos = i;
    }
    return false;
}

// 简易 Lua 表字符串解析器
// 支持：{ ["k"] = v, k2 = v2, ... } 与 { v1, v2, ... }
// 值可为：字符串 "..."/'...'、数字、true/false/nil、嵌套表 {...}
// 分隔符 ',' 或 ';'，末尾可有多余分隔符；支持 -- 行注释
class LuaTableParser {
public:
    explicit LuaTableParser(const std::string& src) : src_(src), pos_(0) {}

    bool Parse(YAML::Node& out) {
        skip_ws();
        if (!match('{')) return false;
        return parse_table(out);
    }

private:
    const std::string& src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '-' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '-') {
                // -- 行注释
                pos_ += 2;
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    char cur() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    bool match(char c) {
        skip_ws();
        if (cur() == c) { ++pos_; return true; }
        return false;
    }

    bool eof() const { return pos_ >= src_.size(); }

    static bool is_ident_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_ident_part(char c) {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    }

    // 探测下一项是否为 map entry（形如 [...] = 或 identifier =）
    bool peek_map_entry() {
        size_t saved = pos_;
        skip_ws();
        bool result = false;
        if (cur() == '[') {
            ++pos_;
            skip_ws();
            if (cur() == '"' || cur() == '\'') {
                char quote = src_[pos_++];
                while (pos_ < src_.size() && src_[pos_] != quote) {
                    if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) ++pos_;
                    ++pos_;
                }
                if (pos_ < src_.size()) ++pos_;
                skip_ws();
                if (cur() == ']') { ++pos_; skip_ws(); result = (cur() == '='); }
            }
        } else if (is_ident_start(cur())) {
            size_t p = pos_;
            while (p < src_.size() && is_ident_part(src_[p])) ++p;
            while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) ++p;
            if (p < src_.size() && src_[p] == '=') result = true;
        }
        pos_ = saved;
        return result;
    }

    // 解析表体（已消费开头的 '{'），消费结尾的 '}'
    bool parse_table(YAML::Node& out) {
        skip_ws();
        if (match('}')) {
            // 空表作为空序列
            out = YAML::Node(YAML::NodeType::Sequence);
            return true;
        }

        bool is_map = peek_map_entry();
        out = is_map ? YAML::Node(YAML::NodeType::Map)
                     : YAML::Node(YAML::NodeType::Sequence);

        while (true) {
            skip_ws();
            if (match('}')) return true;
            if (eof()) return false;

            if (is_map) {
                std::string key;
                if (!parse_key(key)) return false;
                skip_ws();
                if (!match('=')) return false;
                YAML::Node val;
                if (!parse_value(val)) return false;
                out[key] = val;
            } else {
                YAML::Node val;
                if (!parse_value(val)) return false;
                out.push_back(val);
            }

            skip_ws();
            if (match(',') || match(';')) continue;
            skip_ws();
            if (match('}')) return true;
            return false;
        }
    }

    bool parse_key(std::string& out) {
        skip_ws();
        if (match('[')) {
            skip_ws();
            if (!parse_string(out)) return false;
            skip_ws();
            if (!match(']')) return false;
            return true;
        }
        // 标识符形式 key
        if (!is_ident_start(cur())) return false;
        size_t start = pos_;
        while (is_ident_part(cur())) ++pos_;
        out = src_.substr(start, pos_ - start);
        return true;
    }

    bool parse_string(std::string& out) {
        skip_ws();
        char quote = cur();
        if (quote != '"' && quote != '\'') return false;
        ++pos_;
        out.clear();
        while (pos_ < src_.size() && src_[pos_] != quote) {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                char n = src_[pos_ + 1];
                switch (n) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case '"': out.push_back('"'); break;
                    case '\'': out.push_back('\''); break;
                    case '\\': out.push_back('\\'); break;
                    default: out.push_back(n); break;
                }
                pos_ += 2;
            } else {
                out.push_back(src_[pos_++]);
            }
        }
        if (pos_ < src_.size()) ++pos_; // 结束引号
        return true;
    }

    bool parse_number(YAML::Node& out) {
        size_t start = pos_;
        if (cur() == '-' || cur() == '+') ++pos_;
        bool has_dot = false;
        bool has_exp = false;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' && !has_dot && !has_exp) {
                has_dot = true; ++pos_;
            } else if ((c == 'e' || c == 'E') && !has_exp) {
                has_exp = true; ++pos_;
                if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            } else {
                break;
            }
        }
        std::string num = src_.substr(start, pos_ - start);
        try {
            if (has_dot || has_exp) {
                out = YAML::Node(std::stod(num));
            } else {
                out = YAML::Node(std::stoll(num));
            }
        } catch (...) {
            out = YAML::Node(num);
        }
        return true;
    }

    bool parse_value(YAML::Node& out) {
        skip_ws();
        char c = cur();
        if (c == '{') {
            ++pos_;
            return parse_table(out);
        }
        if (c == '"' || c == '\'') {
            std::string s;
            if (!parse_string(s)) return false;
            out = YAML::Node(s);
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number(out);
        }
        if (is_ident_start(cur())) {
            size_t start = pos_;
            while (is_ident_part(cur())) ++pos_;
            std::string word = src_.substr(start, pos_ - start);
            if (word == "true") { out = YAML::Node(true); return true; }
            if (word == "false") { out = YAML::Node(false); return true; }
            if (word == "nil" || word == "null") {
                out = YAML::Node(YAML::NodeType::Null);
                return true;
            }
            // 其它标识符作为字符串字面量保留
            out = YAML::Node(word);
            return true;
        }
        return false;
    }
};

} // namespace

LuaEngine::LuaEngine() = default;
LuaEngine::~LuaEngine() = default;

std::string LuaEngine::YamlToLuaTable(const YAML::Node& node, int indent) {
    if (!node || node.IsNull()) {
        return "nil";
    }
    if (node.IsScalar()) {
        std::string s;
        try {
            s = node.as<std::string>();
        } catch (...) {
            return "nil";
        }
        if (eq_icase(s, "true")) return "true";
        if (eq_icase(s, "false")) return "false";
        if (eq_icase(s, "null") || s == "~" || s.empty()) return "nil";
        if (is_int_literal(s) || is_float_literal(s)) return s;
        return escape_lua_string(s);
    }
    if (node.IsSequence()) {
        if (node.size() == 0) return "{}";
        std::string out = "{\n";
        for (auto it = node.begin(); it != node.end(); ++it) {
            out += indent_str(indent + 1) + YamlToLuaTable(*it, indent + 1) + ",\n";
        }
        out += indent_str(indent) + "}";
        return out;
    }
    if (node.IsMap()) {
        if (node.size() == 0) return "{}";
        std::string out = "{\n";
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (!it->first.IsScalar()) continue;
            std::string key;
            try {
                key = it->first.as<std::string>();
            } catch (...) {
                continue;
            }
            out += indent_str(indent + 1) + "[" + escape_lua_string(key) + "] = " +
                   YamlToLuaTable(it->second, indent + 1) + ",\n";
        }
        out += indent_str(indent) + "}";
        return out;
    }
    return "nil";
}

YAML::Node LuaEngine::LuaTableToYaml(const std::string& lua_str) {
    LuaTableParser parser(lua_str);
    YAML::Node out;
    if (!parser.Parse(out)) {
        // 解析失败返回空节点，调用方保持原 config 不变
        return YAML::Node();
    }
    return out;
}

bool LuaEngine::Execute(const std::filesystem::path& script_path,
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

    // 检查脚本是否定义了 main 函数
    bool has_main = has_main_function(script);

    // 构造 Lua 调用代码（供后续 LuaJIT 接入时执行）：
    //   local config = <yaml_to_lua_table>
    //   local result = main(config)
    //   return result
    std::string lua_code;
    if (has_main) {
        lua_code = "local config = " + YamlToLuaTable(config) +
                   "\nlocal result = main(config)\nreturn result\n";
    }

    // TODO: 接入 LuaJIT 后实现以下流程：
    //   1. 创建 lua_State
    //   2. 加载脚本（luaL_loadfile / luaL_loadstring）定义 main 函数
    //   3. 执行 lua_code：传入 config 表、调用 main、取回 result
    //   4. LuaTableToYaml(result) 写回 config
    //
    // 当前阶段：不执行 Lua，配置不变，记录日志并返回成功。
    (void)lua_code;
    (void)config;
    if (has_main) {
        error = "Lua enhance: script detected, execution pending LuaJIT integration";
    } else {
        error = "Lua enhance: no main function found, execution pending LuaJIT integration";
    }
    return true;
}

} // namespace clash_yuka::enhance
