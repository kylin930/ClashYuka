#pragma once

#include <wx/gdicmn.h>

#include "clashyuka/ui/drawing/painter.h"

namespace clashyuka::ui {

enum class IconId {
    Home,
    Dashboard,
    Activity,
    Settings,
    Palette,
    Sun,
    Moon,
    Check,
    Close,
    ChevronRight,
    Add,
    Search,
    Info,
    Refresh,
    Wifi,
    Menu,
    ArrowLeft,
};

// Vector icons drawn on a 24x24 grid. strokeWidth is expressed in grid units.
class IconProvider {
public:
    static void Draw(Painter& painter, IconId icon, const wxRect& bounds, const Color& color,
                     double strokeWidth = 1.8);
};

}  // namespace clashyuka::ui
