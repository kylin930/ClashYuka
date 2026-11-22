#include "clashyuka/ui/theme/color_scheme.h"

#include <algorithm>
#include <cmath>

#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {
namespace {

struct Hsl {
    double h;
    double s;
    double l;
};

double HueToRgb(double p, double q, double t) {
    if (t < 0.0) {
        t += 1.0;
    }
    if (t > 1.0) {
        t -= 1.0;
    }
    if (t < 1.0 / 6.0) {
        return p + (q - p) * 6.0 * t;
    }
    if (t < 1.0 / 2.0) {
        return q;
    }
    if (t < 2.0 / 3.0) {
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    }
    return p;
}

std::uint8_t ToByte(double value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
}

Color FromHsl(double hue, double saturation, double lightness) {
    const double h = std::fmod(hue, 360.0) < 0 ? std::fmod(hue, 360.0) + 360.0
                                               : std::fmod(hue, 360.0) / 360.0;
    const double s = std::clamp(saturation, 0.0, 1.0);
    const double l = std::clamp(lightness, 0.0, 1.0);

    if (s <= 0.0) {
        const auto value = ToByte(l);
        return Color{value, value, value, 255};
    }

    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;

    return Color{ToByte(HueToRgb(p, q, h + 1.0 / 3.0)),
                 ToByte(HueToRgb(p, q, h)),
                 ToByte(HueToRgb(p, q, h - 1.0 / 3.0)), 255};
}

struct Seed {
    double primaryHue;
    double secondaryHue;
    double tertiaryHue;
    double saturation;
};

Seed SeedFor(ColorTheme theme) {
    switch (theme) {
        case ColorTheme::Purple:
            return {268.0, 248.0, 330.0, 0.45};
        case ColorTheme::Blue:
            return {215.0, 200.0, 270.0, 0.55};
        case ColorTheme::Teal:
            return {175.0, 158.0, 135.0, 0.42};
        case ColorTheme::Orange:
            return {28.0, 12.0, 350.0, 0.62};
    }
    return {268.0, 248.0, 330.0, 0.45};
}

ColorScheme BuildScheme(const Seed& seed, bool dark) {
    const double p = seed.primaryHue;
    const double s = seed.saturation;

    ColorScheme scheme;
    if (!dark) {
        scheme.primary = FromHsl(p, s, 0.47);
        scheme.onPrimary = FromHsl(p, 0.30, 0.99);
        scheme.primaryContainer = FromHsl(p, s * 0.85, 0.91);
        scheme.onPrimaryContainer = FromHsl(p, s, 0.20);

        scheme.secondary = FromHsl(seed.secondaryHue, s * 0.35, 0.45);
        scheme.onSecondary = FromHsl(seed.secondaryHue, 0.25, 0.99);
        scheme.secondaryContainer = FromHsl(seed.secondaryHue, s * 0.45, 0.91);
        scheme.onSecondaryContainer = FromHsl(seed.secondaryHue, s * 0.5, 0.20);

        scheme.tertiary = FromHsl(seed.tertiaryHue, s * 0.75, 0.48);
        scheme.onTertiary = FromHsl(seed.tertiaryHue, 0.3, 0.99);
        scheme.tertiaryContainer = FromHsl(seed.tertiaryHue, s * 0.8, 0.91);
        scheme.onTertiaryContainer = FromHsl(seed.tertiaryHue, s * 0.8, 0.20);

        scheme.error = FromHsl(355.0, 0.62, 0.50);
        scheme.onError = FromHsl(355.0, 0.3, 0.99);
        scheme.errorContainer = FromHsl(355.0, 0.70, 0.92);
        scheme.onErrorContainer = FromHsl(355.0, 0.70, 0.20);

        scheme.surface = FromHsl(p, 0.06, 0.985);
        scheme.onSurface = FromHsl(p, 0.08, 0.12);
        scheme.surfaceDim = FromHsl(p, 0.05, 0.87);
        scheme.surfaceBright = FromHsl(p, 0.04, 0.995);
        scheme.surfaceContainerLowest = FromHsl(p, 0.05, 0.995);
        scheme.surfaceContainerLow = FromHsl(p, 0.06, 0.965);
        scheme.surfaceContainer = FromHsl(p, 0.07, 0.945);
        scheme.surfaceContainerHigh = FromHsl(p, 0.08, 0.925);
        scheme.surfaceContainerHighest = FromHsl(p, 0.08, 0.90);

        scheme.outline = FromHsl(p, 0.07, 0.58);
        scheme.outlineVariant = FromHsl(p, 0.04, 0.82);

        scheme.inverseSurface = FromHsl(p, 0.05, 0.20);
        scheme.inverseOnSurface = FromHsl(p, 0.04, 0.95);
        scheme.inversePrimary = FromHsl(p, s * 0.8, 0.78);
    } else {
        scheme.primary = FromHsl(p, s * 0.85, 0.78);
        scheme.onPrimary = FromHsl(p, s * 0.9, 0.14);
        scheme.primaryContainer = FromHsl(p, s * 0.75, 0.30);
        scheme.onPrimaryContainer = FromHsl(p, s * 0.55, 0.91);

        scheme.secondary = FromHsl(seed.secondaryHue, s * 0.3, 0.78);
        scheme.onSecondary = FromHsl(seed.secondaryHue, 0.3, 0.14);
        scheme.secondaryContainer = FromHsl(seed.secondaryHue, s * 0.4, 0.28);
        scheme.onSecondaryContainer = FromHsl(seed.secondaryHue, s * 0.4, 0.90);

        scheme.tertiary = FromHsl(seed.tertiaryHue, s * 0.65, 0.78);
        scheme.onTertiary = FromHsl(seed.tertiaryHue, 0.3, 0.14);
        scheme.tertiaryContainer = FromHsl(seed.tertiaryHue, s * 0.65, 0.30);
        scheme.onTertiaryContainer = FromHsl(seed.tertiaryHue, s * 0.6, 0.90);

        scheme.error = FromHsl(355.0, 0.55, 0.78);
        scheme.onError = FromHsl(355.0, 0.6, 0.15);
        scheme.errorContainer = FromHsl(355.0, 0.55, 0.30);
        scheme.onErrorContainer = FromHsl(355.0, 0.5, 0.90);

        scheme.surface = FromHsl(p, 0.05, 0.10);
        scheme.onSurface = FromHsl(p, 0.04, 0.92);
        scheme.surfaceDim = FromHsl(p, 0.05, 0.06);
        scheme.surfaceBright = FromHsl(p, 0.05, 0.24);
        scheme.surfaceContainerLowest = FromHsl(p, 0.05, 0.08);
        scheme.surfaceContainerLow = FromHsl(p, 0.05, 0.11);
        scheme.surfaceContainer = FromHsl(p, 0.05, 0.13);
        scheme.surfaceContainerHigh = FromHsl(p, 0.05, 0.17);
        scheme.surfaceContainerHighest = FromHsl(p, 0.05, 0.21);

        scheme.outline = FromHsl(p, 0.05, 0.55);
        scheme.outlineVariant = FromHsl(p, 0.04, 0.30);

        scheme.inverseSurface = FromHsl(p, 0.04, 0.92);
        scheme.inverseOnSurface = FromHsl(p, 0.05, 0.18);
        scheme.inversePrimary = FromHsl(p, s * 0.9, 0.42);
    }

    scheme.scrim = Color{0, 0, 0, 255};
    return scheme;
}

}  // namespace

Color Lerp(const Color& from, const Color& to, double t) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const auto mix = [clamped](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(
            std::lround(a + (static_cast<double>(b) - static_cast<double>(a)) * clamped));
    };
    return Color{mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
}

Color WithAlpha(const Color& color, std::uint8_t alpha) {
    return Color{color.r, color.g, color.b, alpha};
}

std::array<Color, ColorScheme::kTokenCount> ColorScheme::All() const {
    return {primary,          onPrimary,          primaryContainer,        onPrimaryContainer,
            secondary,        onSecondary,        secondaryContainer,      onSecondaryContainer,
            tertiary,         onTertiary,         tertiaryContainer,       onTertiaryContainer,
            error,            onError,            errorContainer,          onErrorContainer,
            surface,          onSurface,          surfaceDim,              surfaceBright,
            surfaceContainerLowest,               surfaceContainerLow,     surfaceContainer,
            surfaceContainerHigh,                 surfaceContainerHighest, outline,
            outlineVariant,   inverseSurface,     inverseOnSurface,        inversePrimary,
            scrim};
}

// Builds a coordinated scheme for the requested theme. Defined here so the
// seeds stay next to the tonal generation logic.
ColorScheme MakeColorScheme(ColorTheme theme, bool dark) {
    return BuildScheme(SeedFor(theme), dark);
}

}  // namespace clashyuka::ui
