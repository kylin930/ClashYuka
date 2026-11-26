// ClashYuka 订阅配置读写实现
#include "profile.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <string>

namespace clash_yuka::config {

std::string ProfileTypeToString(ProfileType t) {
    switch (t) {
        case ProfileType::Remote: return "remote";
        case ProfileType::Local:  return "local";
        case ProfileType::Merge:  return "merge";
        case ProfileType::Script: return "script";
    }
    return "local";
}

std::optional<ProfileType> ProfileTypeFromString(const std::string& s) {
    if (s == "remote") return ProfileType::Remote;
    if (s == "local")  return ProfileType::Local;
    if (s == "merge")  return ProfileType::Merge;
    if (s == "script") return ProfileType::Script;
    return std::nullopt;
}

std::string GenerateProfileUid(ProfileType type) {
    static const char* alphabet =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static thread_local std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, 61);
    char prefix = 'L';
    switch (type) {
        case ProfileType::Remote: prefix = 'R'; break;
        case ProfileType::Local:  prefix = 'L'; break;
        case ProfileType::Merge:  prefix = 'M'; break;
        case ProfileType::Script: prefix = 'S'; break;
    }
    std::string s;
    s.reserve(12);
    s.push_back(prefix);
    for (int i = 0; i < 11; ++i) {
        s.push_back(alphabet[dist(gen)]);
    }
    return s;
}

namespace {

// 读取字符串序列
std::vector<std::string> read_string_seq(const YAML::Node& n) {
    std::vector<std::string> v;
    if (!n) return v;
    if (n.IsSequence()) {
        for (auto it = n.begin(); it != n.end(); ++it) {
            try { v.push_back(it->as<std::string>()); } catch (...) {}
        }
    } else if (n.IsScalar()) {
        // 单值形式（current 字段兼容）
        try { v.push_back(n.as<std::string>()); } catch (...) {}
    }
    return v;
}

ProfileOption read_option(const YAML::Node& n) {
    ProfileOption o;
    if (auto v = n["user_agent"]; v) try { o.user_agent = v.as<std::string>(); } catch (...) {}
    if (auto v = n["with_proxy"]; v) try { o.with_proxy = v.as<bool>(); } catch (...) {}
    if (auto v = n["self_proxy"]; v) try { o.self_proxy = v.as<bool>(); } catch (...) {}
    if (auto v = n["update_interval"]; v) {
        try { o.update_interval = v.as<uint64_t>(); } catch (...) {}
    }
    return o;
}

YAML::Node write_option(const ProfileOption& o) {
    YAML::Node n;
    if (o.user_agent) n["user_agent"] = *o.user_agent;
    if (o.with_proxy) n["with_proxy"] = *o.with_proxy;
    if (o.self_proxy) n["self_proxy"] = *o.self_proxy;
    n["update_interval"] = o.update_interval;
    return n;
}

SubscriptionInfo read_extra(const YAML::Node& n) {
    SubscriptionInfo s;
    if (auto v = n["upload"]; v) try { s.upload = v.as<uint64_t>(); } catch (...) {}
    if (auto v = n["download"]; v) try { s.download = v.as<uint64_t>(); } catch (...) {}
    if (auto v = n["total"]; v) try { s.total = v.as<uint64_t>(); } catch (...) {}
    if (auto v = n["expire"]; v) try { s.expire = v.as<uint64_t>(); } catch (...) {}
    return s;
}

YAML::Node write_extra(const SubscriptionInfo& s) {
    YAML::Node n;
    n["upload"] = s.upload;
    n["download"] = s.download;
    n["total"] = s.total;
    n["expire"] = s.expire;
    return n;
}

Profile read_profile(const YAML::Node& n) {
    Profile p;
    if (auto v = n["uid"]; v) try { p.uid = v.as<std::string>(); } catch (...) {}
    if (auto v = n["name"]; v) try { p.name = v.as<std::string>(); } catch (...) {}
    if (auto v = n["file"]; v) try { p.file = v.as<std::string>(); } catch (...) {}
    if (auto v = n["desc"]; v && !v.IsNull()) try { p.desc = v.as<std::string>(); } catch (...) {}
    if (auto v = n["updated"]; v) try { p.updated = v.as<uint64_t>(); } catch (...) {}
    if (auto v = n["type"]; v) {
        try {
            auto t = ProfileTypeFromString(v.as<std::string>());
            if (t) p.type = *t;
        } catch (...) {}
    }
    if (p.uid.empty()) {
        // 缺 uid 则生成一个（按 type 前缀）
        p.uid = GenerateProfileUid(p.type);
    }
    if (p.file.empty()) {
        // 默认文件名：uid.yaml（script 类型默认也用 .yaml，后续可按 script_type 调整）
        p.file = p.uid + ".yaml";
    }
    if (p.name.empty()) {
        switch (p.type) {
            case ProfileType::Remote: p.name = "Remote Profile"; break;
            case ProfileType::Local:  p.name = "Local Profile";  break;
            case ProfileType::Merge:  p.name = "Merge Profile";  break;
            case ProfileType::Script: p.name = "Script Profile"; break;
        }
    }

    // Remote
    if (p.type == ProfileType::Remote) {
        if (auto v = n["url"]; v) try { p.url = v.as<std::string>(); } catch (...) {}
        if (auto v = n["option"]; v && v.IsMap()) p.option = read_option(v);
        if (auto v = n["extra"]; v && v.IsMap()) p.extra = read_extra(v);
    }
    // Script
    if (p.type == ProfileType::Script) {
        if (auto v = n["script_type"]; v) try { p.script_type = v.as<std::string>(); } catch (...) {}
        // 修正文件扩展名
        if (p.script_type && *p.script_type == "javascript" && p.file.empty()) {
            p.file = p.uid + ".js";
        } else if (p.script_type && *p.script_type == "lua" && p.file.empty()) {
            p.file = p.uid + ".lua";
        }
    }
    // Local symlinks
    if (p.type == ProfileType::Local) {
        if (auto v = n["symlinks"]; v && !v.IsNull()) try { p.symlinks = v.as<std::string>(); } catch (...) {}
    }
    // chain（兼容 "chains" 别名）
    p.chain = read_string_seq(n["chain"]);
    if (p.chain.empty()) {
        p.chain = read_string_seq(n["chains"]);
    }
    return p;
}

YAML::Node write_profile(const Profile& p) {
    YAML::Node n;
    n["uid"] = p.uid;
    n["name"] = p.name;
    n["type"] = ProfileTypeToString(p.type);
    n["file"] = p.file;
    n["updated"] = p.updated;
    if (p.desc) n["desc"] = *p.desc;

    if (p.type == ProfileType::Remote) {
        if (p.url) n["url"] = *p.url;
        if (p.option) n["option"] = write_option(*p.option);
        if (p.extra) n["extra"] = write_extra(*p.extra);
    }
    if (p.type == ProfileType::Script && p.script_type) {
        n["script_type"] = *p.script_type;
    }
    if (p.type == ProfileType::Local && p.symlinks) {
        n["symlinks"] = *p.symlinks;
    }
    if (!p.chain.empty()) {
        YAML::Node seq;
        for (const auto& c : p.chain) seq.push_back(c);
        n["chain"] = seq;
    }
    return n;
}

} // namespace

Profiles Profiles::Default() {
    Profiles p;
    p.valid = {"dns", "unified-delay", "tcp-concurrent"};
    return p;
}

Profiles Profiles::Load(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return Default();
    YAML::Node root;
    try {
        root = YAML::Load(ifs);
    } catch (...) {
        return Default();
    }
    if (!root || !root.IsMap()) return Default();

    Profiles p;
    p.current = read_string_seq(root["current"]);
    p.chain = read_string_seq(root["chain"]);
    p.valid = read_string_seq(root["valid"]);
    if (p.valid.empty()) {
        p.valid = {"dns", "unified-delay", "tcp-concurrent"};
    }
    if (auto items = root["items"]; items && items.IsSequence()) {
        p.items.reserve(items.size());
        for (auto it = items.begin(); it != items.end(); ++it) {
            p.items.push_back(read_profile(*it));
        }
    }
    return p;
}

void Profiles::Save(const std::filesystem::path& path) const {
    YAML::Node root;
    if (!current.empty()) {
        YAML::Node seq;
        for (const auto& c : current) seq.push_back(c);
        root["current"] = seq;
    }
    if (!chain.empty()) {
        YAML::Node seq;
        for (const auto& c : chain) seq.push_back(c);
        root["chain"] = seq;
    }
    if (!valid.empty()) {
        YAML::Node seq;
        for (const auto& c : valid) seq.push_back(c);
        root["valid"] = seq;
    }
    if (!items.empty()) {
        YAML::Node seq;
        for (const auto& item : items) {
            seq.push_back(write_profile(item));
        }
        root["items"] = seq;
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return;
    ofs << "# Profiles Config for ClashYuka\n\n";
    ofs << YAML::Dump(root);
}

Profile* Profiles::Get(const std::string& uid) {
    for (auto& it : items) {
        if (it.uid == uid) return &it;
    }
    return nullptr;
}

const Profile* Profiles::Get(const std::string& uid) const {
    for (const auto& it : items) {
        if (it.uid == uid) return &it;
    }
    return nullptr;
}

bool Profiles::Add(Profile item) {
    if (Get(item.uid) != nullptr) return false;
    if (item.uid.empty()) item.uid = GenerateProfileUid(item.type);
    if (item.file.empty()) {
        item.file = item.uid + ".yaml";
    }
    if (item.name.empty()) {
        switch (item.type) {
            case ProfileType::Remote: item.name = "Remote Profile"; break;
            case ProfileType::Local:  item.name = "Local Profile";  break;
            case ProfileType::Merge:  item.name = "Merge Profile";  break;
            case ProfileType::Script: item.name = "Script Profile"; break;
        }
    }
    items.push_back(std::move(item));
    return true;
}

bool Profiles::Remove(const std::string& uid) {
    auto it = std::find_if(items.begin(), items.end(),
                           [&](const Profile& p) { return p.uid == uid; });
    if (it == items.end()) return false;
    items.erase(it);

    // 从 current 中移除该 uid
    auto cur_it = std::remove(current.begin(), current.end(), uid);
    bool was_current = (cur_it != current.end());
    current.erase(cur_it, current.end());

    // 若 current 清空，尝试激活首个 Local/Remote
    if (current.empty()) {
        for (const auto& p : items) {
            if (p.type == ProfileType::Local || p.type == ProfileType::Remote) {
                current.push_back(p.uid);
                break;
            }
        }
    }
    return was_current;
}

bool Profiles::Update(const std::string& uid, const Profile& patch) {
    Profile* p = Get(uid);
    if (!p) return false;
    // uid 与 type 不可变
    if (!patch.name.empty()) p->name = patch.name;
    if (!patch.file.empty()) p->file = patch.file;
    if (patch.desc.has_value()) p->desc = patch.desc;
    if (patch.updated != 0) p->updated = patch.updated;
    if (patch.url) p->url = patch.url;
    if (patch.option) p->option = patch.option;
    if (patch.extra) p->extra = patch.extra;
    if (patch.script_type) p->script_type = patch.script_type;
    if (patch.symlinks) p->symlinks = patch.symlinks;
    if (!patch.chain.empty()) p->chain = patch.chain;
    return true;
}

void Profiles::SetCurrent(const std::string& uid) {
    if (Get(uid) == nullptr) return;
    auto it = std::find(current.begin(), current.end(), uid);
    if (it == current.end()) {
        current.push_back(uid);
    }
}

} // namespace clash_yuka::config
