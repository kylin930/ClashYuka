#include "clashyuka/ui/drawing/painter.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <wx/dc.h>
#include <wx/region.h>
#include <wx/settings.h>

namespace clashyuka::ui {

wxColour ToWx(const Color& color) {
    return wxColour(color.r, color.g, color.b, color.a);
}

void Painter::FillRoundedRect(const wxRect& rect, double radius, const Color& color) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const double limit = std::min(rect.width, rect.height) / 2.0;
    const double r = std::clamp(radius, 0.0, limit);
    gc_->SetBrush(wxBrush(ToWx(color)));
    gc_->SetPen(*wxTRANSPARENT_PEN);
    gc_->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
}

void Painter::StrokeRoundedRect(const wxRect& rect, double radius, const Color& color,
                                double strokeWidth) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const double limit = std::min(rect.width, rect.height) / 2.0;
    const double r = std::clamp(radius, 0.0, limit);
    wxGraphicsPenInfo info(ToWx(color), strokeWidth);
    info.Join(wxJOIN_ROUND);
    gc_->SetPen(gc_->CreatePen(info));
    gc_->SetBrush(*wxTRANSPARENT_BRUSH);
    gc_->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
}

void Painter::FillRect(const wxRect& rect, const Color& color) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    gc_->SetBrush(wxBrush(ToWx(color)));
    gc_->SetPen(*wxTRANSPARENT_PEN);
    gc_->DrawRectangle(rect.x, rect.y, rect.width, rect.height);
}

void Painter::FillCircle(double centerX, double centerY, double radius, const Color& color) {
    gc_->SetBrush(wxBrush(ToWx(color)));
    gc_->SetPen(*wxTRANSPARENT_PEN);
    gc_->DrawEllipse(centerX - radius, centerY - radius, radius * 2.0, radius * 2.0);
}

void Painter::StrokeCircle(double centerX, double centerY, double radius, const Color& color,
                           double strokeWidth) {
    wxGraphicsPenInfo info(ToWx(color), strokeWidth);
    gc_->SetPen(gc_->CreatePen(info));
    gc_->SetBrush(*wxTRANSPARENT_BRUSH);
    gc_->DrawEllipse(centerX - radius, centerY - radius, radius * 2.0, radius * 2.0);
}

void Painter::DrawLine(double x1, double y1, double x2, double y2, const Color& color,
                       double strokeWidth) {
    wxGraphicsPenInfo info(ToWx(color), strokeWidth);
    info.Cap(wxCAP_ROUND);
    gc_->SetPen(gc_->CreatePen(info));
    gc_->StrokeLine(x1, y1, x2, y2);
}

void Painter::FillVerticalGradient(const wxRect& rect, double radius, const Color& top,
                                   const Color& bottom) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    wxGraphicsBrush brush = gc_->CreateLinearGradientBrush(
        rect.x, rect.y, rect.x, rect.y + rect.height, ToWx(top), ToWx(bottom));
    const double limit = std::min(rect.width, rect.height) / 2.0;
    const double r = std::clamp(radius, 0.0, limit);
    gc_->SetBrush(brush);
    gc_->SetPen(*wxTRANSPARENT_PEN);
    gc_->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, r);
}

void Painter::DrawText(const std::string& text, const wxRect& rect, const wxFont& font,
                       const Color& color, int alignment) {
    if (text.empty()) {
        return;
    }
    const wxString value = wxString::FromUTF8(text);
    gc_->SetFont(font, ToWx(color));
    double width = 0;
    double height = 0;
    gc_->GetTextExtent(value, &width, &height, nullptr, nullptr);

    double x = rect.x;
    if ((alignment & wxALIGN_CENTER_HORIZONTAL) != 0) {
        x = rect.x + (rect.width - width) / 2.0;
    } else if ((alignment & wxALIGN_RIGHT) != 0) {
        x = rect.x + rect.width - width;
    }
    const double y = rect.y + (rect.height - height) / 2.0;
    gc_->DrawText(value, x, y);
}

wxSize Painter::MeasureText(const std::string& text, const wxFont& font) const {
    if (text.empty()) {
        return wxSize(0, 0);
    }
    gc_->SetFont(font, *wxBLACK);
    double width = 0;
    double height = 0;
    gc_->GetTextExtent(wxString::FromUTF8(text), &width, &height, nullptr, nullptr);
    return wxSize(static_cast<int>(width + 0.5), static_cast<int>(height + 0.5));
}

void Painter::FillPath(const wxGraphicsPath& path, const Color& color) {
    gc_->SetBrush(wxBrush(ToWx(color)));
    gc_->SetPen(*wxTRANSPARENT_PEN);
    gc_->FillPath(path);
}

void Painter::StrokePath(const wxGraphicsPath& path, const Color& color, double strokeWidth) {
    wxGraphicsPenInfo info(ToWx(color), strokeWidth);
    info.Cap(wxCAP_ROUND);
    info.Join(wxJOIN_ROUND);
    gc_->SetPen(gc_->CreatePen(info));
    gc_->SetBrush(*wxTRANSPARENT_BRUSH);
    gc_->StrokePath(path);
}

void Painter::SetClip(const wxRect& rect) {
    gc_->Clip(rect.x, rect.y, rect.width, rect.height);
}

void Painter::SetClipRoundedRect(const wxRect& rect, double radius) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    const double limit = std::min(rect.width, rect.height) / 2.0;
    const double r = std::clamp(radius, 0.0, limit);

    // wxGraphicsContext only clips rectangles/regions, so build the rounded
    // region as a stack of one-pixel rows.
    wxRegion region;
    for (int y = 0; y < rect.height; ++y) {
        double inset = 0.0;
        const double fromTop = y + 0.5;
        const double fromBottom = rect.height - y - 0.5;
        const double distance = std::min(fromTop, fromBottom);
        if (distance < r) {
            const double delta = r - distance;
            inset = r - std::sqrt(std::max(0.0, r * r - delta * delta));
        }
        const int left = rect.x + static_cast<int>(inset);
        const int right = rect.x + rect.width - static_cast<int>(inset);
        if (right > left) {
            region.Union(wxRect(left, y, right - left, 1));
        }
    }
    gc_->Clip(region);
}

void Painter::ResetClip() {
    gc_->ResetClip();
}

int FontPixelSize(const TextStyle& style, const wxWindow* window) {
    if (window != nullptr) {
        return window->FromDIP(style.sizeDp);
    }
    return style.sizeDp;
}

wxFont MakeFont(const TextStyle& style, const wxWindow* window) {
    wxFontWeight weight = wxFONTWEIGHT_NORMAL;
    if (style.weight >= 700) {
        weight = wxFONTWEIGHT_BOLD;
    } else if (style.weight >= 600) {
        weight = wxFONTWEIGHT_SEMIBOLD;
    } else if (style.weight >= 500) {
        weight = wxFONTWEIGHT_MEDIUM;
    }

    const wxFont systemFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    wxFontInfo info(wxSize(0, FontPixelSize(style, window)));
    if (!systemFont.GetFaceName().empty()) {
        info.FaceName(systemFont.GetFaceName());
    }
    info.Family(wxFONTFAMILY_SWISS).Weight(weight).AntiAliased(true);
    return wxFont(info);
}

std::unique_ptr<wxGraphicsContext> CreateGraphicsContext(wxDC& dc) {
    return std::unique_ptr<wxGraphicsContext>(wxGraphicsContext::CreateFromUnknownDC(dc));
}

}  // namespace clashyuka::ui
