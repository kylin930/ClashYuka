#pragma once

#include <string_view>
#include <vector>

#include "clashyuka/ui/theme/color_scheme.h"
#include "clashyuka/ui/theme/typography.h"

namespace clashyuka::ui {

enum class ColorTheme { Purple, Blue, Teal, Orange };

std::string_view ThemeName(ColorTheme theme);

// Builds the full token set for a theme. Exposed for tests and previews.
ColorScheme MakeColorScheme(ColorTheme theme, bool dark);

class ThemeObserver {
public:
    virtual ~ThemeObserver() = default;
    virtual void OnThemeChanged() = 0;
};

// Single source of truth for colors, dark mode and motion preferences.
// Must be used from the GUI thread; theme changes take effect immediately.
class ThemeManager {
public:
    static ThemeManager& Instance();

    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void SetColorTheme(ColorTheme theme);
    ColorTheme GetColorTheme() const { return theme_; }

    void SetDarkMode(bool dark);
    bool IsDark() const { return dark_; }
    void ToggleDarkMode() { SetDarkMode(!dark_); }

    // Motion preferences. Reduced motion implies animations are effectively off.
    void SetAnimationsEnabled(bool enabled);
    bool AnimationsEnabled() const { return animationsEnabled_; }
    void SetReducedMotion(bool reduced);
    bool ReducedMotion() const { return reducedMotion_; }
    bool MotionEnabled() const { return animationsEnabled_ && !reducedMotion_; }

    // Tracks whether the last navigation came from the keyboard, so controls
    // only show the MD3 focus state layer for keyboard focus (not for focus
    // that the native dialog manager assigns when a window opens).
    void SetKeyboardNavigation(bool active) { keyboardNavigation_ = active; }
    bool KeyboardNavigation() const { return keyboardNavigation_; }

    const ColorScheme& Colors() const { return colors_; }
    const Typography& Type() const { return Typography::Default(); }

    void AddObserver(ThemeObserver* observer);
    void RemoveObserver(ThemeObserver* observer);

private:
    ThemeManager();

    void Rebuild();
    void Notify();

    ColorTheme theme_ = ColorTheme::Purple;
    bool dark_ = false;
    bool animationsEnabled_ = true;
    bool reducedMotion_ = false;
    bool keyboardNavigation_ = false;
    ColorScheme colors_;
    std::vector<ThemeObserver*> observers_;
};

}  // namespace clashyuka::ui
