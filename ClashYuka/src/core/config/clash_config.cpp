// ClashYuka Clash 配置读写实现
#include "clash_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace clash_yuka::config {

namespace {

// 从环境变量或随机数生成简单 UUID v4（小写），用作默认 secret
std::string generate_uuid_v4() {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    unsigned char bytes[16] = {0};
    uint64_t a = gen();
    uint64_t b = gen();
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<unsigned char>((a >> (i * 8)) & 0xff);
    for (int i = 0; i < 8; ++i) bytes[8 + i] = static_cast<unsigned char>((b >> (i * 8)) & 0xff);
    // version & variant
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    char buf[37] = {0};
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

// trim 空白
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// 检查字符串是否为合法 IP+port，简化版本：仅做基本长度/字符校验
bool parse_socket_addr(const std::string& s, std::string& ip_out, uint16_t& port_out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    // 处理 [::]:port 形式
    if (t[0] == '[') {
        auto end = t.find(']');
        if (end == std::string::npos) return false;
        ip_out = t.substr(1, end - 1);
        if (end + 1 >= t.size() || t[end + 1] != ':') return false;
        std::string port_str = t.substr(end + 2);
        try {
            unsigned long p = std::stoul(port_str);
            if (p == 0 || p > 65535) return false;
            port_out = static_cast<uint16_t>(p);
            return true;
        } catch (...) { return false; }
    }
    // 普通 ip:port
    auto pos = t.rfind(':');
    if (pos == std::string::npos) return false;
    ip_out = t.substr(0, pos);
    std::string port_str = t.substr(pos + 1);
    try {
        unsigned long p = std::stoul(port_str);
        if (p == 0 || p > 65535) return false;
        port_out = static_cast<uint16_t>(p);
        return true;
    } catch (...) { return false; }
}

bool is_unspecified_ip(const std::string& ip) {
    return ip == "0.0.0.0" || ip == "::" || ip.empty();
}

// 从 YAML 节点读 uint16，可接受数字或字符串形式
bool read_port(const YAML::Node& n, uint16_t& out) {
    if (!n) return false;
    try {
        if (n.IsScalar()) {
            std::string s = n.as<std::string>();
            // 尝试纯数字
            unsigned long v = std::stoul(s);
            if (v > 65535) return false;
            out = static_cast<uint16_t>(v);
            return true;
        }
    } catch (...) {}
    return false;
}

} // namespace

// ---------------- IClash ----------------

IClash IClash::LoadFromYaml(const YAML::Node& root) {
    IClash c;
    if (!root || !root.IsMap()) return c;
    if (auto n = root["mixed-port"]) {
        uint16_t p = 0;
        if (read_port(n, p)) c.mixed_port = p;
    }
    if (auto n = root["allow-lan"]; n) try { c.allow_lan = n.as<bool>(); } catch (...) {}
    if (auto n = root["log-level"]; n) try { c.log_level = n.as<std::string>(); } catch (...) {}
    if (auto n = root["ipv6"]; n) try { c.ipv6 = n.as<bool>(); } catch (...) {}
    if (auto n = root["mode"]; n) try { c.mode = n.as<std::string>(); } catch (...) {}
    if (auto n = root["external-controller"]; n) try { c.external_controller = n.as<std::string>(); } catch (...) {}
    if (auto n = root["secret"]; n) {
        try { c.secret = n.as<std::string>(); }
        catch (...) { try { c.secret = n.as<bool>() ? "true" : "false"; } catch (...) {} }
    }
    if (auto n = root["interface-name"]; n) try { c.interface_name = n.as<std::string>(); } catch (...) {}

    if (auto dn = root["dns"]; dn && dn.IsMap()) {
        DNS dns;
        if (auto n = dn["enable"]; n) try { dns.enable = n.as<bool>(); } catch (...) {}
        if (auto n = dn["listen"]; n) try { dns.listen = n.as<std::string>(); } catch (...) {}
        if (auto n = dn["default-nameserver"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            dns.default_nameserver = std::move(v);
        }
        if (auto n = dn["enhanced-mode"]; n) try { dns.enhanced_mode = n.as<std::string>(); } catch (...) {}
        if (auto n = dn["fake-ip-range"]; n) try { dns.fake_ip_range = n.as<std::string>(); } catch (...) {}
        if (auto n = dn["use-hosts"]; n) try { dns.use_hosts = n.as<bool>(); } catch (...) {}
        if (auto n = dn["fake-ip-filter"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            dns.fake_ip_filter = std::move(v);
        }
        if (auto n = dn["nameserver"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            dns.nameserver = std::move(v);
        }
        if (auto n = dn["fallback"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            dns.fallback = std::move(v);
        }
        if (auto n = dn["nameserver-policy"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            dns.nameserver_policy = std::move(v);
        }
        if (auto ff = dn["fallback-filter"]; ff && ff.IsMap()) {
            FallbackFilter f;
            if (auto n = ff["geoip"]; n) try { f.geoip = n.as<bool>(); } catch (...) {}
            if (auto n = ff["geoip-code"]; n) try { f.geoip_code = n.as<std::string>(); } catch (...) {}
            if (auto n = ff["ipcidr"]; n && n.IsSequence()) {
                std::vector<std::string> v;
                for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
                f.ipcidr = std::move(v);
            }
            if (auto n = ff["domain"]; n && n.IsSequence()) {
                std::vector<std::string> v;
                for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
                f.domain = std::move(v);
            }
            dns.fallback_filter = std::move(f);
        }
        c.dns = std::move(dns);
    }

    if (auto tn = root["tun"]; tn && tn.IsMap()) {
        TUN tun;
        if (auto n = tn["enable"]; n) try { tun.enable = n.as<bool>(); } catch (...) {}
        if (auto n = tn["stack"]; n) try { tun.stack = n.as<std::string>(); } catch (...) {}
        if (auto n = tn["auto-route"]; n) try { tun.auto_route = n.as<bool>(); } catch (...) {}
        if (auto n = tn["auto-detect-interface"]; n) try { tun.auto_detect_interface = n.as<bool>(); } catch (...) {}
        if (auto n = tn["dns-hijack"]; n && n.IsSequence()) {
            std::vector<std::string> v;
            for (auto it = n.begin(); it != n.end(); ++it) v.push_back(it->as<std::string>());
            tun.dns_hijack = std::move(v);
        }
        c.tun = std::move(tun);
    }
    return c;
}

YAML::Node IClash::ToYaml() const {
    YAML::Node root;
    if (mixed_port) root["mixed-port"] = *mixed_port;
    if (allow_lan) root["allow-lan"] = *allow_lan;
    if (log_level) root["log-level"] = *log_level;
    if (ipv6) root["ipv6"] = *ipv6;
    if (mode) root["mode"] = *mode;
    if (external_controller) root["external-controller"] = *external_controller;
    if (secret) root["secret"] = *secret;
    if (interface_name) root["interface-name"] = *interface_name;
    if (dns) {
        YAML::Node n;
        const DNS& d = *dns;
        if (d.enable) n["enable"] = *d.enable;
        if (d.listen) n["listen"] = *d.listen;
        if (d.default_nameserver) n["default-nameserver"] = *d.default_nameserver;
        if (d.enhanced_mode) n["enhanced-mode"] = *d.enhanced_mode;
        if (d.fake_ip_range) n["fake-ip-range"] = *d.fake_ip_range;
        if (d.use_hosts) n["use-hosts"] = *d.use_hosts;
        if (d.fake_ip_filter) n["fake-ip-filter"] = *d.fake_ip_filter;
        if (d.nameserver) n["nameserver"] = *d.nameserver;
        if (d.fallback) n["fallback"] = *d.fallback;
        if (d.nameserver_policy) n["nameserver-policy"] = *d.nameserver_policy;
        if (d.fallback_filter) {
            YAML::Node ff;
            const FallbackFilter& f = *d.fallback_filter;
            if (f.geoip) ff["geoip"] = *f.geoip;
            if (f.geoip_code) ff["geoip-code"] = *f.geoip_code;
            if (f.ipcidr) ff["ipcidr"] = *f.ipcidr;
            if (f.domain) ff["domain"] = *f.domain;
            n["fallback-filter"] = ff;
        }
        root["dns"] = n;
    }
    if (tun) {
        YAML::Node n;
        const TUN& t = *tun;
        if (t.enable) n["enable"] = *t.enable;
        if (t.stack) n["stack"] = *t.stack;
        if (t.auto_route) n["auto-route"] = *t.auto_route;
        if (t.auto_detect_interface) n["auto-detect-interface"] = *t.auto_detect_interface;
        if (t.dns_hijack) n["dns-hijack"] = *t.dns_hijack;
        root["tun"] = n;
    }
    return root;
}

// ---------------- IClashTemp ----------------

IClashTemp IClashTemp::Load(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return Template();
    YAML::Node root;
    try {
        root = YAML::Load(ifs);
    } catch (...) {
        return Template();
    }
    if (!root || !root.IsMap()) return Template();
    return IClashTemp{Guard(root)};
}

void IClashTemp::Save(const std::filesystem::path& path) const {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return;
    ofs << "# Generated by ClashYuka\n\n";
    ofs << YAML::Dump(data_);
}

IClashTemp IClashTemp::Template() {
    YAML::Node root;
    root["mixed-port"] = 7890;
    root["log-level"] = "info";
    root["allow-lan"] = false;
    root["mode"] = "rule";
    root["external-controller"] = "127.0.0.1:17650";
    root["secret"] = generate_uuid_v4();
    root["ipv6"] = false;
    return IClashTemp{root};
}

void IClashTemp::PatchConfig(const YAML::Node& patch) {
    if (!patch || !patch.IsMap()) return;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        data_[it->first] = it->second;
    }
}

uint16_t IClashTemp::GetMixedPort() const {
    return GuardMixedPort(data_);
}

ClashInfo IClashTemp::GetClientInfo() const {
    ClashInfo info;
    info.port = GuardMixedPort(data_);
    info.server = GuardClientCtrl(data_);
    if (auto n = data_["secret"]; n) {
        try {
            if (n.IsScalar()) {
                info.secret = n.as<std::string>();
            }
        } catch (...) {}
    }
    return info;
}

uint16_t IClashTemp::GuardMixedPort(const YAML::Node& config) {
    uint16_t port = 0;
    if (read_port(config["mixed-port"], port)) {
        if (port == 0) port = 7890;
    } else {
        port = 7890;
    }
    return port;
}

std::string IClashTemp::GuardServerCtrl(const YAML::Node& config) {
    auto n = config["external-controller"];
    if (n && n.IsScalar()) {
        std::string s = trim(n.as<std::string>());
        if (!s.empty()) {
            std::string ip;
            uint16_t port = 0;
            // 处理 ":port" -> "127.0.0.1:port"
            if (s[0] == ':') {
                s = "127.0.0.1" + s;
            }
            if (parse_socket_addr(s, ip, port)) {
                return s;
            }
        }
    }
    return "127.0.0.1:9090";
}

std::string IClashTemp::GuardClientCtrl(const YAML::Node& config) {
    std::string s = GuardServerCtrl(config);
    std::string ip;
    uint16_t port = 0;
    if (!parse_socket_addr(s, ip, port)) {
        return "127.0.0.1:9090";
    }
    if (is_unspecified_ip(ip)) {
        ip = "127.0.0.1";
    }
    return ip + ":" + std::to_string(port);
}

YAML::Node IClashTemp::Guard(YAML::Node config) {
    if (!config || !config.IsMap()) {
        YAML::Node m;
        return m;
    }
    uint16_t port = GuardMixedPort(config);
    std::string ctrl = GuardServerCtrl(config);
    config["mixed-port"] = port;
    config["external-controller"] = ctrl;
    return config;
}

} // namespace clash_yuka::config
