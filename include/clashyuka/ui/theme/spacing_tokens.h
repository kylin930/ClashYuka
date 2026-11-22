#pragma once

namespace clashyuka::ui {

enum class Space {
    Xs = 4,
    Sm = 8,
    Md = 12,
    Lg = 16,
    Xl = 20,
    Xxl = 24,
    Xxxl = 32,
    Huge = 40,
    Giant = 48,
};

constexpr int SpaceDp(Space space) { return static_cast<int>(space); }

inline int SpacePx(Space space, int dpi) { return (SpaceDp(space) * dpi + 48) / 96; }

}  // namespace clashyuka::ui
