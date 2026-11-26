#pragma once

#include <wx/colour.h>

#include <filesystem>
#include <string>
#include <unordered_map>

#include "core/config/verge_config.h"

namespace clash_yuka::ui {

enum class ThemeMode {
    Light,
    Dark,
    System
};

class ThemeManager {
public:
    static ThemeManager& Get();

    void InitFromConfig(const clash_yuka::config::IVerge& verge);
    void ApplyThemeMode(ThemeMode mode);
    void ApplySeedColor(const wxColour& color);
    void SaveToConfig(clash_yuka::config::IVerge& verge) const;
    void RefreshAll();

    ThemeMode GetThemeMode() const { return m_mode; }
    wxColour GetSeedColor() const { return m_seedColor; }
    bool IsDarkModeActive() const { return m_isDarkActive; }

    void LoadCustomCSS(const std::string& css_text);
    bool LoadCSSFromFile(const std::filesystem::path& path);

    const std::string& GetCustomCSS() const { return m_customCSS; }
    const std::unordered_map<std::string, std::string>& GetCSSRules() const { return m_cssRules; }

    static ThemeMode ParseThemeMode(const std::string& s);
    static std::string ThemeModeToString(ThemeMode mode);
    static wxColour HexToColor(const std::string& hex);
    static std::string ColorToHex(const wxColour& color);
    static bool IsSystemDarkMode();

private:
    ThemeManager();
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void ParseCSSIntoRules(const std::string& css);

    ThemeMode m_mode = ThemeMode::System;
    wxColour m_seedColor;
    bool m_isDarkActive = false;

    std::string m_customCSS;
    std::unordered_map<std::string, std::string> m_cssRules;
};

} // namespace clash_yuka::ui
