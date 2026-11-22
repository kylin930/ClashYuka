#include "clashyuka/ui/drawing/icons.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace clashyuka::ui {
namespace {

class Grid {
public:
    Grid(const wxRect& bounds) {
        scale_ = std::min(bounds.width, bounds.height) / 24.0;
        offsetX_ = bounds.x + (bounds.width - 24.0 * scale_) / 2.0;
        offsetY_ = bounds.y + (bounds.height - 24.0 * scale_) / 2.0;
    }

    double X(double value) const { return offsetX_ + value * scale_; }
    double Y(double value) const { return offsetY_ + value * scale_; }
    double Scale() const { return scale_; }
    wxGraphicsPath Path(wxGraphicsContext* gc) const { return gc->CreatePath(); }

private:
    double scale_;
    double offsetX_;
    double offsetY_;
};

void StrokeSegments(Painter& painter, const wxRect& bounds, const Color& color,
                    double strokeWidth, const std::vector<std::pair<double, double>>& points) {
    if (points.size() < 2) {
        return;
    }
    const Grid grid(bounds);
    wxGraphicsPath path = grid.Path(painter.Gc());
    path.MoveToPoint(grid.X(points[0].first), grid.Y(points[0].second));
    for (std::size_t i = 1; i < points.size(); ++i) {
        path.AddLineToPoint(grid.X(points[i].first), grid.Y(points[i].second));
    }
    painter.StrokePath(path, color, std::max(1.0, strokeWidth * grid.Scale()));
}

void StrokeCircleIcon(Painter& painter, const wxRect& bounds, double cx, double cy, double r,
                      const Color& color, double strokeWidth) {
    const Grid grid(bounds);
    painter.StrokeCircle(grid.X(cx), grid.Y(cy), r * grid.Scale(), color,
                         std::max(1.0, strokeWidth * grid.Scale()));
}

}  // namespace

void IconProvider::Draw(Painter& painter, IconId icon, const wxRect& bounds, const Color& color,
                        double strokeWidth) {
    const Grid grid(bounds);
    const double stroke = std::max(1.0, strokeWidth * grid.Scale());

    switch (icon) {
        case IconId::Home:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{4.0, 10.5}, {12.0, 3.5}, {20.0, 10.5}});
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{6.5, 9.5}, {6.5, 20.0}, {17.5, 20.0}, {17.5, 9.5}});
            break;
        case IconId::Dashboard: {
            wxGraphicsPath path = grid.Path(painter.Gc());
            path.AddRoundedRectangle(grid.X(3.5), grid.Y(3.5), 7.0 * grid.Scale(),
                                     7.0 * grid.Scale(), 1.5 * grid.Scale());
            path.AddRoundedRectangle(grid.X(13.5), grid.Y(3.5), 7.0 * grid.Scale(),
                                     7.0 * grid.Scale(), 1.5 * grid.Scale());
            path.AddRoundedRectangle(grid.X(3.5), grid.Y(13.5), 7.0 * grid.Scale(),
                                     7.0 * grid.Scale(), 1.5 * grid.Scale());
            path.AddRoundedRectangle(grid.X(13.5), grid.Y(13.5), 7.0 * grid.Scale(),
                                     7.0 * grid.Scale(), 1.5 * grid.Scale());
            painter.StrokePath(path, color, stroke);
            break;
        }
        case IconId::Activity:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{3.5, 12.0}, {7.0, 12.0}, {9.0, 6.0}, {13.0, 18.0}, {15.5, 10.0},
                            {20.5, 10.0}});
            break;
        case IconId::Settings:
            StrokeCircleIcon(painter, bounds, 12.0, 12.0, 3.0, color, strokeWidth);
            for (int i = 0; i < 8; ++i) {
                const double angle = i * 3.14159265358979323846 / 4.0;
                StrokeSegments(painter, bounds, color, strokeWidth,
                               {{12.0 + std::cos(angle) * 6.5, 12.0 + std::sin(angle) * 6.5},
                                {12.0 + std::cos(angle) * 9.0, 12.0 + std::sin(angle) * 9.0}});
            }
            break;
        case IconId::Palette:
            StrokeCircleIcon(painter, bounds, 12.0, 12.0, 8.0, color, strokeWidth);
            for (const auto& dot : std::vector<std::pair<double, double>>{
                     {9.0, 9.0}, {15.0, 9.5}, {9.5, 15.0}, {15.0, 14.5}}) {
                painter.FillCircle(grid.X(dot.first), grid.Y(dot.second),
                                   1.1 * grid.Scale(), color);
            }
            break;
        case IconId::Sun:
            StrokeCircleIcon(painter, bounds, 12.0, 12.0, 4.0, color, strokeWidth);
            for (int i = 0; i < 8; ++i) {
                const double angle = i * 3.14159265358979323846 / 4.0;
                StrokeSegments(painter, bounds, color, strokeWidth,
                               {{12.0 + std::cos(angle) * 6.5, 12.0 + std::sin(angle) * 6.5},
                                {12.0 + std::cos(angle) * 9.0, 12.0 + std::sin(angle) * 9.0}});
            }
            break;
        case IconId::Moon: {
            wxGraphicsPath path = grid.Path(painter.Gc());
            path.MoveToPoint(grid.X(20.0), grid.Y(14.5));
            path.AddCurveToPoint(grid.X(16.5), grid.Y(19.5), grid.X(9.0), grid.Y(19.0),
                                 grid.X(6.5), grid.Y(12.5));
            path.AddCurveToPoint(grid.X(4.5), grid.Y(6.5), grid.X(10.5), grid.Y(2.5),
                                 grid.X(17.0), grid.Y(5.5));
            path.AddCurveToPoint(grid.X(12.5), grid.Y(7.5), grid.X(14.0), grid.Y(12.0),
                                 grid.X(20.0), grid.Y(14.5));
            painter.StrokePath(path, color, stroke);
            break;
        }
        case IconId::Check:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{4.5, 12.5}, {9.5, 17.0}, {19.5, 6.5}});
            break;
        case IconId::Close:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{6.0, 6.0}, {18.0, 18.0}});
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{18.0, 6.0}, {6.0, 18.0}});
            break;
        case IconId::ChevronRight:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{9.5, 6.0}, {15.5, 12.0}, {9.5, 18.0}});
            break;
        case IconId::Add:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{12.0, 5.0}, {12.0, 19.0}});
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{5.0, 12.0}, {19.0, 12.0}});
            break;
        case IconId::Search:
            StrokeCircleIcon(painter, bounds, 10.5, 10.5, 6.0, color, strokeWidth);
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{15.0, 15.0}, {20.0, 20.0}});
            break;
        case IconId::Info:
            StrokeCircleIcon(painter, bounds, 12.0, 12.0, 8.5, color, strokeWidth);
            painter.FillCircle(grid.X(12.0), grid.Y(8.0), 1.1 * grid.Scale(), color);
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{12.0, 11.0}, {12.0, 16.5}});
            break;
        case IconId::Refresh: {
            wxGraphicsPath path = grid.Path(painter.Gc());
            path.AddArc(grid.X(12.0), grid.Y(12.0), 7.5 * grid.Scale(), -0.6, 4.2, true);
            painter.StrokePath(path, color, stroke);
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{17.5, 3.5}, {18.5, 8.0}, {14.0, 8.5}});
            break;
        }
        case IconId::Wifi:
            for (const double radius : {4.0, 8.0, 12.0}) {
                wxGraphicsPath path = grid.Path(painter.Gc());
                path.AddArc(grid.X(12.0), grid.Y(18.0), radius * grid.Scale(), -2.35, -0.79,
                            false);
                painter.StrokePath(path, color, stroke);
            }
            painter.FillCircle(grid.X(12.0), grid.Y(18.0), 1.2 * grid.Scale(), color);
            break;
        case IconId::Menu:
            for (const double y : {6.5, 12.0, 17.5}) {
                StrokeSegments(painter, bounds, color, strokeWidth, {{5.0, y}, {19.0, y}});
            }
            break;
        case IconId::ArrowLeft:
            StrokeSegments(painter, bounds, color, strokeWidth,
                           {{15.0, 6.0}, {9.0, 12.0}, {15.0, 18.0}});
            break;
    }
}

}  // namespace clashyuka::ui
