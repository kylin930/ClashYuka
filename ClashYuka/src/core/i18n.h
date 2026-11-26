// ClashYuka 国际化管理（Phase 10 Task 25）
//
// 提供简体中文、繁体中文、英文 UI 字符串翻译。
// 加载顺序：内置默认（中文）→ 用户配置语言覆盖。
// 本阶段为简化版：硬编码中英文字符串表，通过 key 查找。
#pragma once

#include <string>
#include <unordered_map>

#include <wx/string.h>

namespace clash_yuka::core {

// 国际化管理：支持中英文切换。
class I18n {
public:
    static I18n& Get();

    // 不可拷贝/移动
    I18n(const I18n&) = delete;
    I18n& operator=(const I18n&) = delete;
    I18n(I18n&&) = delete;
    I18n& operator=(I18n&&) = delete;

    // 设置当前语言（"zh-cn" / "en" / "zh-tw"）
    void SetLanguage(const std::string& lang);
    // 获取当前语言
    const std::string& GetLanguage() const { return current_lang_; }

    // 翻译 key 到当前语言字符串。未找到则返回 key 本身。
    wxString Tr(const std::string& key) const;
    // 同上，带 printf 风格参数
    wxString Tr(const std::string& key, const wxString& arg1) const;

private:
    I18n();
    ~I18n() = default;

    // 初始化中英文翻译表
    void InitTables();

    std::string current_lang_ = "zh-cn";
    // key → {lang → translation}
    std::unordered_map<std::string, std::unordered_map<std::string, wxString>> table_;
};

} // namespace clash_yuka::core
