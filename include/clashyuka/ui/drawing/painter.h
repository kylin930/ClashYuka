#pragma once

#include <wx/font.h>
#include <wx/graphics.h>
#include <wx/window.h>

#include <memory>
#include <string>

#include "clashyuka/ui/theme/color_scheme.h"
#include "clashyuka/ui/theme/typography.h"

namespace clashyuka::ui {

wxColour ToWx(const Color& color);

// Thin wrapper around wxGraphicsContext with anti-aliased MD3 primitives.
// The caller owns the graphics context.
class Painter {
public:
    explicit Painter(wxGraphicsContext* gc) : gc_(gc) {}

    wxGraphicsContext* Gc() const { return gc_; }

    void FillRoundedRect(const wxRect& rect, double radius, const Color& color);
    void StrokeRoundedRect(const wxRect& rect, double radius, const Color& color,
                           double strokeWidth);
    void FillRect(const wxRect& rect, const Color& color);
    void FillCircle(double centerX, double centerY, double radius, const Color& color);
    void StrokeCircle(double centerX, double centerY, double radius, const Color& color,
                      double strokeWidth);
    void DrawLine(double x1, double y1, double x2, double y2, const Color& color,
                  double strokeWidth);
    void FillVerticalGradient(const wxRect& rect, double radius, const Color& top,
                              const Color& bottom);
    void DrawText(const std::string& text, const wxRect& rect, const wxFont& font,
                  const Color& color, int alignment = wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    wxSize MeasureText(const std::string& text, const wxFont& font) const;

    void FillPath(const wxGraphicsPath& path, const Color& color);
    void StrokePath(const wxGraphicsPath& path, const Color& color, double strokeWidth);

    void SetClip(const wxRect& rect);
    void SetClipRoundedRect(const wxRect& rect, double radius);
    void ResetClip();

private:
    wxGraphicsContext* gc_;
};

int FontPixelSize(const TextStyle& style, const wxWindow* window);
wxFont MakeFont(const TextStyle& style, const wxWindow* window);

// Creates a buffered graphics context for a paint handler. Returns nullptr when
// the platform cannot provide one.
std::unique_ptr<wxGraphicsContext> CreateGraphicsContext(wxDC& dc);

}  // namespace clashyuka::ui
