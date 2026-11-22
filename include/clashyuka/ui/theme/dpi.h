#pragma once

namespace clashyuka::ui {

// Logical DPI helpers. 96 DPI is the 100% baseline.
double DpiScale(int dpi);
int ScaleDp(int dp, int dpi);

}  // namespace clashyuka::ui
