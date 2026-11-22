#pragma once

namespace clashyuka::ui {

enum class ShapeToken { ExtraSmall, Small, Medium, Large, ExtraLarge, Full };

constexpr int ShapeDp(ShapeToken token) {
    switch (token) {
        case ShapeToken::ExtraSmall:
            return 4;
        case ShapeToken::Small:
            return 8;
        case ShapeToken::Medium:
            return 12;
        case ShapeToken::Large:
            return 16;
        case ShapeToken::ExtraLarge:
            return 28;
        case ShapeToken::Full:
            return -1;
    }
    return 0;
}

// Returns the radius in device pixels. Full uses half of the supplied height.
inline int ShapeRadiusPx(ShapeToken token, int dpi, int heightPx = 0) {
    if (token == ShapeToken::Full) {
        return heightPx / 2;
    }
    const int dp = ShapeDp(token);
    return (dp * dpi + 48) / 96;
}

}  // namespace clashyuka::ui
