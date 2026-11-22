#include "clashyuka/ui/theme/dpi.h"

#include <cmath>

namespace clashyuka::ui {

double DpiScale(int dpi) {
    if (dpi <= 0) {
        return 1.0;
    }
    return static_cast<double>(dpi) / 96.0;
}

int ScaleDp(int dp, int dpi) {
    if (dpi <= 0) {
        return dp;
    }
    return static_cast<int>(std::lround(static_cast<double>(dp) * DpiScale(dpi)));
}

}  // namespace clashyuka::ui
