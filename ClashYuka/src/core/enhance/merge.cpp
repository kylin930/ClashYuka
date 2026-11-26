// ClashYuka 运行时配置合并实现
// 对应 Rust 版 enhance/merge.rs
#include "core/enhance/merge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace clash_yuka::enhance {

namespace {

// 判断 map 中是否存在某个字符串 key（不创建 key）
bool node_has_key(const YAML::Node& map, const std::string& key) {
    if (!map.IsMap()) return false;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->first.IsScalar()) {
            try {
                if (it->first.as<std::string>() == key) return true;
            } catch (...) {}
        }
    }
    return false;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_dot(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '.') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// 导航到 dotted 路径的父节点，返回父节点与最后一段 key。
// 中间任一段不存在则返回 false（不会创建中间节点）。
bool navigate_to_parent(YAML::Node root, const std::string& dotted,
                        YAML::Node& parent, std::string& last_key) {
    auto segs = split_dot(dotted);
    if (segs.empty() || segs.back().empty()) return false;
    last_key = segs.back();
    YAML::Node current = root;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        const std::string& s = segs[i];
        if (s.empty()) return false;
        if (current.IsSequence()) {
            size_t idx = 0;
            try {
                idx = std::stoul(s);
            } catch (...) {
                return false;
            }
            if (idx >= current.size()) return false;
            current = current[idx];
        } else if (current.IsMap()) {
            if (!node_has_key(current, s)) return false;
            current = current[s];
        } else {
            return false;
        }
    }
    parent = current;
    return true;
}

// 默认递归覆盖：base 中已有同名 key 且双方均为 map 时递归合并，否则覆盖/插入。
void override_recursive(YAML::Node& base, const std::string& key, const YAML::Node& data) {
    if (!base.IsMap()) {
        base = YAML::Node(YAML::NodeType::Map);
    }
    YAML::Node existing = base[key];
    if (existing && existing.IsMap() && data && data.IsMap()) {
        for (auto it = data.begin(); it != data.end(); ++it) {
            if (!it->first.IsScalar()) continue;
            std::string k;
            try {
                k = it->first.as<std::string>();
            } catch (...) {
                continue;
            }
            override_recursive(existing, k, it->second);
        }
    } else {
        base[key] = data;
    }
}

// 前插/追加序列到 parent[key]。仅当 parent 为 map、key 存在且为序列时生效。
bool merge_sequence(YAML::Node parent, const std::string& key,
                    const YAML::Node& to_merge, bool append) {
    if (!parent.IsMap() || !node_has_key(parent, key)) return false;
    YAML::Node target = parent[key];
    if (!target.IsSequence() || !to_merge.IsSequence()) return false;
    if (append) {
        for (auto it = to_merge.begin(); it != to_merge.end(); ++it) {
            target.push_back(YAML::Clone(*it));
        }
        return true;
    }
    // prepend：构建新序列再写回 parent[key]
    YAML::Node new_seq;
    for (auto it = to_merge.begin(); it != to_merge.end(); ++it) {
        new_seq.push_back(YAML::Clone(*it));
    }
    for (auto it = target.begin(); it != target.end(); ++it) {
        new_seq.push_back(YAML::Clone(*it));
    }
    parent[key] = new_seq;
    return true;
}

// override__ 处理：要求完整路径存在，否则跳过（与 Rust find_field 返回 None 一致）
bool do_override(YAML::Node root, const std::string& field_str, const YAML::Node& value) {
    YAML::Node parent;
    std::string last_key;
    if (!navigate_to_parent(root, field_str, parent, last_key)) return false;
    if (parent.IsMap()) {
        if (!node_has_key(parent, last_key)) return false;
        parent[last_key] = YAML::Clone(value);
        return true;
    }
    if (parent.IsSequence()) {
        size_t idx = 0;
        try {
            idx = std::stoul(last_key);
        } catch (...) {
            return false;
        }
        if (idx >= parent.size()) return false;
        parent[idx] = YAML::Clone(value);
        return true;
    }
    return false;
}

// prepend__/append__ 处理
bool do_prepend_append(YAML::Node root, const std::string& field_str,
                       const YAML::Node& value, bool append) {
    if (!value.IsSequence()) return false;
    YAML::Node parent;
    std::string last_key;
    if (!navigate_to_parent(root, field_str, parent, last_key)) return false;
    if (parent.IsMap()) {
        return merge_sequence(parent, last_key, value, append);
    }
    return false;
}

} // namespace

YAML::Node Merge(YAML::Node base, const YAML::Node& patch) {
    if (!patch || !patch.IsMap()) return base;
    if (!base || !base.IsMap()) {
        base = YAML::Node(YAML::NodeType::Map);
    }

    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (!it->first.IsScalar()) continue;
        std::string key;
        try {
            key = it->first.as<std::string>();
        } catch (...) {
            continue;
        }
        const YAML::Node& val = it->second;

        // 整体转小写后判断前缀（与 Rust 版一致）
        std::string lower = to_lower(key);

        if (lower.starts_with("prepend__")) {
            do_prepend_append(base, lower.substr(9), val, /*append=*/false);
            continue;
        }
        if (lower.starts_with("prepend-")) {
            do_prepend_append(base, lower.substr(8), val, /*append=*/false);
            continue;
        }
        if (lower.starts_with("append__")) {
            do_prepend_append(base, lower.substr(8), val, /*append=*/true);
            continue;
        }
        if (lower.starts_with("append-")) {
            do_prepend_append(base, lower.substr(7), val, /*append=*/true);
            continue;
        }
        if (lower.starts_with("override__")) {
            do_override(base, lower.substr(10), val);
            continue;
        }
        if (lower.starts_with("filter__")) {
            // 列表过滤需要脚本引擎（阶段7实现），本阶段忽略
            continue;
        }
        // 默认递归合并（使用原始 key，保留大小写）
        override_recursive(base, key, val);
    }
    return base;
}

YAML::Node MergeFile(YAML::Node base, const std::filesystem::path& patch_path) {
    std::error_code ec;
    if (!std::filesystem::exists(patch_path, ec)) return base;
    std::ifstream ifs(patch_path, std::ios::binary);
    if (!ifs) return base;
    YAML::Node patch;
    try {
        patch = YAML::Load(ifs);
    } catch (...) {
        return base;
    }
    return Merge(std::move(base), patch);
}

} // namespace clash_yuka::enhance
