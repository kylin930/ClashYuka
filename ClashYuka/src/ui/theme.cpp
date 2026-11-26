#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/scrolwin.h>

#include "theme.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif

namespace clash_yuka::ui {

void RefreshWindowTree(wxWindow* win) {
    if (!win) return;

    win->Refresh();
    for (auto* child : win->GetChildren()) {
        RefreshWindowTree(child);
    }
}

ThemeManager& ThemeManager::Get() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    m_seedColor = wxColour(0x67, 0x50, 0xA4);
    m_isDarkActive = IsSystemDarkMode();
}

void ThemeManager::InitFromConfig(const clash_yuka::config::IVerge& verge) {
    if (verge.theme_mode.has_value()) {
        m_mode = ParseThemeMode(*verge.theme_mode);
    } else {
        m_mode = ThemeMode::System;
    }

    if (verge.theme_color.has_value() &&
        clash_yuka::config::is_hex_color(*verge.theme_color)) {
        m_seedColor = HexToColor(*verge.theme_color);
    }

    switch (m_mode) {
        case ThemeMode::Light:  m_isDarkActive = false; break;
        case ThemeMode::Dark:   m_isDarkActive = true;  break;
        case ThemeMode::System: m_isDarkActive = IsSystemDarkMode(); break;
    }
}

void ThemeManager::ApplyThemeMode(ThemeMode mode) {
    m_mode = mode;
    switch (m_mode) {
        case ThemeMode::Light:  m_isDarkActive = false; break;
        case ThemeMode::Dark:   m_isDarkActive = true;  break;
        case ThemeMode::System: m_isDarkActive = IsSystemDarkMode(); break;
    }
}

void ThemeManager::ApplySeedColor(const wxColour& color) {
    m_seedColor = color;
}

void ThemeManager::SaveToConfig(clash_yuka::config::IVerge& verge) const {
    verge.theme_mode = ThemeModeToString(m_mode);
    verge.theme_color = ColorToHex(m_seedColor);
}

void ThemeManager::RefreshAll() {
    for (auto node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
        RefreshWindowTree(static_cast<wxWindow*>(node->GetData()));
    }
}

void ThemeManager::LoadCustomCSS(const std::string& css_text) {
    m_customCSS = css_text;
    m_cssRules.clear();
    ParseCSSIntoRules(css_text);
}

bool ThemeManager::LoadCSSFromFile(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::stringstream ss;
    ss << ifs.rdbuf();
    LoadCustomCSS(ss.str());
    return true;
}

void ThemeManager::ParseCSSIntoRules(const std::string& css) {
    std::string body = css;

    auto braceOpen = body.find('{');
    if (braceOpen != std::string::npos) {
        auto braceClose = body.rfind('}');
        if (braceClose != std::string::npos && braceClose > braceOpen) {
            body = body.substr(braceOpen + 1, braceClose - braceOpen - 1);
        }
    }

    auto trim = [](std::string& s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    };

    size_t pos = 0;
    while (pos < body.size()) {
        size_t semi = body.find(';', pos);
        std::string decl;
        if (semi == std::string::npos) {
            decl = body.substr(pos);
            pos = body.size();
        } else {
            decl = body.substr(pos, semi - pos);
            pos = semi + 1;
        }

        auto colon = decl.find(':');
        if (colon == std::string::npos) continue;

        std::string prop = decl.substr(0, colon);
        std::string val = decl.substr(colon + 1);
        trim(prop);
        trim(val);

        std::transform(prop.begin(), prop.end(), prop.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (!prop.empty()) {
            m_cssRules[prop] = val;
        }
    }
}

ThemeMode ThemeManager::ParseThemeMode(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (unsigned char c : s) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    if (lower == "dark")  return ThemeMode::Dark;
    if (lower == "light") return ThemeMode::Light;
    return ThemeMode::System;
}

std::string ThemeManager::ThemeModeToString(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:  return "light";
        case ThemeMode::Dark:   return "dark";
        case ThemeMode::System: return "system";
    }
    return "system";
}

wxColour ThemeManager::HexToColor(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);

    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    if (h.length() == 3) {
        int r = hexVal(h[0]) * 17;
        int g = hexVal(h[1]) * 17;
        int b = hexVal(h[2]) * 17;
        return wxColour(r, g, b);
    }
    if (h.length() == 6) {
        int r = hexVal(h[0]) * 16 + hexVal(h[1]);
        int g = hexVal(h[2]) * 16 + hexVal(h[3]);
        int b = hexVal(h[4]) * 16 + hexVal(h[5]);
        return wxColour(r, g, b);
    }
    if (h.length() == 8) {
        int r = hexVal(h[0]) * 16 + hexVal(h[1]);
        int g = hexVal(h[2]) * 16 + hexVal(h[3]);
        int b = hexVal(h[4]) * 16 + hexVal(h[5]);
        int a = hexVal(h[6]) * 16 + hexVal(h[7]);
        return wxColour(r, g, b, a);
    }
    return wxColour(0x67, 0x50, 0xA4);
}

std::string ThemeManager::ColorToHex(const wxColour& color) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  color.Red(), color.Green(), color.Blue());
    return std::string(buf);
}

bool ThemeManager::IsSystemDarkMode() {
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU,
                 "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
    if (key.Open(wxRegKey::Read)) {
        long value = 1;
        if (key.QueryValue("AppsUseLightTheme", &value)) {
            return value == 0;
        }
    }
    return false;
#else
    return false;
#endif
}

} // namespace clash_yuka::ui
