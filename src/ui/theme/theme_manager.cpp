#include "clashyuka/ui/theme/theme_manager.h"

#include <algorithm>

namespace clashyuka::ui {

std::string_view ThemeName(ColorTheme theme) {
    switch (theme) {
        case ColorTheme::Purple:
            return "Purple";
        case ColorTheme::Blue:
            return "Blue";
        case ColorTheme::Teal:
            return "Teal";
        case ColorTheme::Orange:
            return "Orange";
    }
    return "Purple";
}

ThemeManager& ThemeManager::Instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager() {
    Rebuild();
}

void ThemeManager::SetColorTheme(ColorTheme theme) {
    if (theme_ == theme) {
        return;
    }
    theme_ = theme;
    Rebuild();
    Notify();
}

void ThemeManager::SetDarkMode(bool dark) {
    if (dark_ == dark) {
        return;
    }
    dark_ = dark;
    Rebuild();
    Notify();
}

void ThemeManager::SetAnimationsEnabled(bool enabled) {
    if (animationsEnabled_ == enabled) {
        return;
    }
    animationsEnabled_ = enabled;
    Notify();
}

void ThemeManager::SetReducedMotion(bool reduced) {
    if (reducedMotion_ == reduced) {
        return;
    }
    reducedMotion_ = reduced;
    Notify();
}

void ThemeManager::AddObserver(ThemeObserver* observer) {
    if (observer == nullptr) {
        return;
    }
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void ThemeManager::RemoveObserver(ThemeObserver* observer) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                     observers_.end());
}

void ThemeManager::Rebuild() {
    colors_ = MakeColorScheme(theme_, dark_);
}

void ThemeManager::Notify() {
    // Copy: an observer may unregister itself while being notified.
    const auto observers = observers_;
    for (ThemeObserver* observer : observers) {
        if (std::find(observers_.begin(), observers_.end(), observer) != observers_.end()) {
            observer->OnThemeChanged();
        }
    }
}

}  // namespace clashyuka::ui
