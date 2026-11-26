// ClashYuka 运行时配置合并模块
// 对应 Rust 版 enhance/merge.rs 的 use_merge 合并策略
#pragma once

#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace clash_yuka::enhance {

// 深度合并：将 patch 合并进 base，返回合并后的节点（base 会被原地修改）。
//
// 合并策略（参考 nyanpasu merge profile）：
//   - 默认（override_recursive）：patch 标量覆盖 base；双方均为 map 时递归合并；
//     patch 的 list 直接替换 base 的 list。
//   - "prepend__" / "prepend-" 前缀：将 patch 的序列前插到 base 对应字段序列头部。
//   - "append__"  / "append-"  前缀：将 patch 的序列追加到 base 对应字段序列尾部。
//   - "override__" 前缀：直接替换 base 对应字段（支持 a.b.c 点分路径）。
//   - "filter__" 前缀：列表过滤（需要脚本引擎，本阶段忽略）。
//
// 前缀键支持 a.b.c 点分路径访问嵌套字段（与 Rust 版 find_field 行为一致）。
// 字段名匹配时整体转小写后再判断前缀，剥离前缀后用小写路径查找目标字段。
YAML::Node Merge(YAML::Node base, const YAML::Node& patch);

// 从文件读取 patch YAML 并合并到 base。
// 文件不存在或解析失败时返回 base 原样（不修改）。
YAML::Node MergeFile(YAML::Node base, const std::filesystem::path& patch_path);

} // namespace clash_yuka::enhance
