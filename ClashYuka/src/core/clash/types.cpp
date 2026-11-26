// ClashYuka mihomo API 强类型数据结构反序列化实现
#include "types.h"

#include <sstream>

namespace clash_yuka::clash {

namespace {

// 安全读取字符串字段；缺失或类型不匹配时返回默认值
std::string get_string(const nlohmann::json& j, const char* key,
                       const std::string& def = std::string()) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

// 安全读取可选字符串字段
std::optional<std::string> get_optional_string(const nlohmann::json& j, const char* key) {
    if (j.contains(key) && !j[key].is_null() && j[key].is_string())
        return j[key].get<std::string>();
    return std::nullopt;
}

// 安全读取可选整数字段（支持数字与字符串形式）
template <typename T>
std::optional<T> get_optional_int(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return std::nullopt;
    const auto& v = j[key];
    if (v.is_number()) return v.get<T>();
    if (v.is_string()) {
        try {
            return static_cast<T>(std::stoll(v.get<std::string>()));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

// Proxy：name/type 直接读取；alive 由 bool 转换；delay 从 history 末尾提取
void from_json(const nlohmann::json& j, Proxy& p) {
    p.name = get_string(j, "name");
    p.type = get_string(j, "type");
    // Provider 来源：mihomo 使用 "provider-name"（/proxies 与 /providers/proxies 均如此）
    p.provider = get_string(j, "provider-name");
    if (p.provider.empty()) p.provider = get_string(j, "provider");

    // alive：API 返回 bool，转换为 "alive"/"dead"
    if (j.contains("alive") && j["alive"].is_boolean()) {
        p.alive = j["alive"].get<bool>() ? "alive" : "dead";
    }

    // history：API 返回 [{time, delay}, ...]，提取 delay 值
    if (j.contains("history") && j["history"].is_array()) {
        int last_delay = -1;
        for (const auto& h : j["history"]) {
            if (h.contains("delay") && h["delay"].is_number_integer()) {
                int d = h["delay"].get<int>();
                p.history.push_back(std::to_string(d));
                last_delay = d;
            }
        }
        if (last_delay >= 0) {
            p.delay = last_delay;
        }
    }
}

// Connection：network/type/host/sourceIP 等来自 metadata 子对象；
// chains 是数组，合并为逗号分隔字符串
void from_json(const nlohmann::json& j, Connection& c) {
    c.id = get_string(j, "id");

    // metadata 子对象包含 network/type/host/sourceIP/destinationIP 等
    if (j.contains("metadata") && j["metadata"].is_object()) {
        const auto& m = j["metadata"];
        c.network = get_optional_string(m, "network");
        c.type = get_optional_string(m, "type");
        c.host = get_optional_string(m, "host");
        c.sourceIP = get_optional_string(m, "sourceIP");
        c.sourcePort = get_optional_string(m, "sourcePort");
        c.destinationPort = get_optional_string(m, "destinationPort");
        c.destinationIP = get_optional_string(m, "destinationIP");
    }

    // chains：API 返回数组，合并为逗号分隔字符串
    if (j.contains("chains") && j["chains"].is_array() && !j["chains"].empty()) {
        std::ostringstream oss;
        bool first = true;
        for (const auto& chain : j["chains"]) {
            if (chain.is_string()) {
                if (!first) oss << ",";
                oss << chain.get<std::string>();
                first = false;
            }
        }
        if (!first) c.chains = oss.str();
    }

    c.upload = get_optional_int<int64_t>(j, "upload");
    c.download = get_optional_int<int64_t>(j, "download");
    c.rule = get_optional_string(j, "rule");
    c.rulePayload = get_optional_string(j, "rulePayload");
    c.start = get_optional_int<int64_t>(j, "start");
}

// Provider：vehicleType/updatedAt/subscriptionInfo 为 camelCase
void from_json(const nlohmann::json& j, Provider& p) {
    p.name = get_string(j, "name");
    p.type = get_string(j, "type");
    p.vehicleType = get_string(j, "vehicleType");
    p.updatedAt = get_optional_string(j, "updatedAt");
    // subscriptionInfo 在 API 中为对象，简化处理：仅当为数字时取值
    if (j.contains("subscriptionInfo") && j["subscriptionInfo"].is_number_integer()) {
        p.subscriptionInfo = j["subscriptionInfo"].get<int>();
    }
}

// Version：meta/premium 在 API 中为 bool，转为字符串
void from_json(const nlohmann::json& j, Version& v) {
    v.version = get_string(j, "version");

    if (j.contains("meta")) {
        if (j["meta"].is_boolean())
            v.meta = j["meta"].get<bool>() ? "true" : "false";
        else if (j["meta"].is_string())
            v.meta = j["meta"].get<std::string>();
    }

    if (j.contains("premium")) {
        if (j["premium"].is_boolean())
            v.premium = j["premium"].get<bool>() ? "true" : "false";
        else if (j["premium"].is_string())
            v.premium = j["premium"].get<std::string>();
    }
}

// Memory：inuse 必填，oslimit 可选
void from_json(const nlohmann::json& j, Memory& m) {
    if (j.contains("inuse") && j["inuse"].is_number()) {
        m.inuse = j["inuse"].get<int64_t>();
    }
    m.oslimit = get_optional_int<int64_t>(j, "oslimit");
}

} // namespace clash_yuka::clash
