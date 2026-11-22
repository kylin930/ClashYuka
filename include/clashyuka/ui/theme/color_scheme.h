#pragma once

#include <array>
#include <cstdint>

namespace clashyuka::ui {

// UI colors are kept independent from wxWidgets so theme logic stays testable.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    constexpr bool operator==(const Color& other) const noexcept {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    constexpr bool operator!=(const Color& other) const noexcept { return !(*this == other); }
};

Color Lerp(const Color& from, const Color& to, double t);
Color WithAlpha(const Color& color, std::uint8_t alpha);

struct ColorScheme {
    Color primary;
    Color onPrimary;
    Color primaryContainer;
    Color onPrimaryContainer;

    Color secondary;
    Color onSecondary;
    Color secondaryContainer;
    Color onSecondaryContainer;

    Color tertiary;
    Color onTertiary;
    Color tertiaryContainer;
    Color onTertiaryContainer;

    Color error;
    Color onError;
    Color errorContainer;
    Color onErrorContainer;

    Color surface;
    Color onSurface;
    Color surfaceDim;
    Color surfaceBright;
    Color surfaceContainerLowest;
    Color surfaceContainerLow;
    Color surfaceContainer;
    Color surfaceContainerHigh;
    Color surfaceContainerHighest;

    Color outline;
    Color outlineVariant;

    Color inverseSurface;
    Color inverseOnSurface;
    Color inversePrimary;

    Color scrim;

    static constexpr std::size_t kTokenCount = 31;
    std::array<Color, kTokenCount> All() const;
};

}  // namespace clashyuka::ui
