#pragma once

#include "clashyuka/ui/theme/color_scheme.h"

namespace clashyuka::ui {

enum class ButtonVariant { Filled, FilledTonal, Outlined, Text };

struct ActionColors {
    Color background;
    Color foreground;
    Color border;
    bool hasBorder = false;
};

inline ActionColors ResolveActionColors(ButtonVariant variant, const ColorScheme& colors,
                                        bool enabled) {
    ActionColors result;
    if (!enabled) {
        result.foreground = WithAlpha(colors.onSurface, 100);
        result.border = WithAlpha(colors.onSurface, 30);
        if (variant == ButtonVariant::Filled || variant == ButtonVariant::FilledTonal) {
            result.background = WithAlpha(colors.onSurface, 30);
        } else {
            result.background = WithAlpha(colors.surface, 0);
        }
        result.hasBorder = variant == ButtonVariant::Outlined;
        return result;
    }

    switch (variant) {
        case ButtonVariant::Filled:
            result.background = colors.primary;
            result.foreground = colors.onPrimary;
            break;
        case ButtonVariant::FilledTonal:
            result.background = colors.secondaryContainer;
            result.foreground = colors.onSecondaryContainer;
            break;
        case ButtonVariant::Outlined:
            result.background = WithAlpha(colors.surface, 0);
            result.foreground = colors.primary;
            result.border = colors.outline;
            result.hasBorder = true;
            break;
        case ButtonVariant::Text:
            result.background = WithAlpha(colors.surface, 0);
            result.foreground = colors.primary;
            break;
    }
    return result;
}

}  // namespace clashyuka::ui
