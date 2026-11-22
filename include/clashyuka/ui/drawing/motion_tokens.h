#pragma once

#include "clashyuka/ui/drawing/animation.h"

namespace clashyuka::ui::motion {

// Shared motion timing. Components must use these instead of local magic numbers.
constexpr int HoverMs = 140;
constexpr int PressMs = 110;
constexpr int ReleaseMs = 130;
constexpr int FocusMs = 120;
constexpr int SwitchMs = 180;
constexpr int RailMs = 200;
constexpr int TabMs = 200;
constexpr int SurfaceInMs = 200;
constexpr int SurfaceOutMs = 160;
constexpr int DialogMs = 200;
constexpr int SnackbarMs = 200;
constexpr int PageMs = 180;
constexpr int RippleMs = 420;
constexpr int ProgressMs = 220;
constexpr int TextFadeMs = 160;
constexpr int BadgeMs = 160;

constexpr Easing Standard = Easing::Standard;
constexpr Easing Enter = Easing::EmphasizedDecelerate;
constexpr Easing Exit = Easing::EmphasizedAccelerate;
constexpr Easing Soft = Easing::EaseOutCubic;
constexpr Easing Balanced = Easing::EaseInOutCubic;

}  // namespace clashyuka::ui::motion
