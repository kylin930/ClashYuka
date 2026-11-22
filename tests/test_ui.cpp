#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>

#include "clashyuka/ui/theme/color_scheme.h"
#include "clashyuka/ui/theme/dpi.h"
#include "clashyuka/ui/theme/shape_tokens.h"
#include "clashyuka/ui/theme/spacing_tokens.h"
#include "clashyuka/ui/theme/theme_manager.h"
#include "clashyuka/ui/theme/typography.h"
#include "clashyuka/ui/drawing/animation.h"
#include "clashyuka/ui/drawing/motion_tokens.h"

using namespace clashyuka::ui;

TEST_CASE("all color schemes are complete") {
    for (const ColorTheme theme : {ColorTheme::Purple, ColorTheme::Blue, ColorTheme::Teal,
                                   ColorTheme::Orange}) {
        for (const bool dark : {false, true}) {
            const ColorScheme scheme = MakeColorScheme(theme, dark);
            const auto tokens = scheme.All();
            CHECK(tokens.size() == ColorScheme::kTokenCount);
            for (const Color& token : tokens) {
                CHECK(token.a == 255);
            }
            CHECK(scheme.primary != scheme.surface);
            CHECK(scheme.onPrimary != scheme.primary);
            CHECK(scheme.onSurface != scheme.surface);
            CHECK(scheme.primary != scheme.secondary);
        }
    }
}

TEST_CASE("light and dark surfaces differ in luminance direction") {
    const ColorScheme light = MakeColorScheme(ColorTheme::Purple, false);
    const ColorScheme dark = MakeColorScheme(ColorTheme::Purple, true);

    const auto luminance = [](const Color& color) {
        return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
    };

    CHECK(luminance(light.surface) > luminance(light.onSurface));
    CHECK(luminance(dark.surface) < luminance(dark.onSurface));
    CHECK(luminance(light.surface) > 200);
    CHECK(luminance(dark.surface) < 50);
}

TEST_CASE("theme manager notifies observers and updates tokens") {
    ThemeManager& manager = ThemeManager::Instance();
    const ColorTheme originalTheme = manager.GetColorTheme();
    const bool originalDark = manager.IsDark();

    struct Observer final : ThemeObserver {
        int count = 0;
        void OnThemeChanged() override { ++count; }
    } observer;

    manager.AddObserver(&observer);
    const Color before = manager.Colors().primary;

    manager.SetColorTheme(ColorTheme::Blue);
    CHECK(manager.GetColorTheme() == ColorTheme::Blue);
    CHECK(observer.count == 1);
    CHECK(manager.Colors().primary != before);

    manager.SetDarkMode(true);
    CHECK(manager.IsDark());
    CHECK(observer.count == 2);

    manager.RemoveObserver(&observer);
    manager.SetColorTheme(originalTheme);
    manager.SetDarkMode(originalDark);
    CHECK(observer.count == 2);
}

TEST_CASE("motion preferences respect reduced motion") {
    ThemeManager& manager = ThemeManager::Instance();
    const bool animations = manager.AnimationsEnabled();
    const bool reduced = manager.ReducedMotion();

    manager.SetAnimationsEnabled(true);
    manager.SetReducedMotion(false);
    CHECK(manager.MotionEnabled());

    manager.SetReducedMotion(true);
    CHECK_FALSE(manager.MotionEnabled());

    manager.SetReducedMotion(false);
    manager.SetAnimationsEnabled(false);
    CHECK_FALSE(manager.MotionEnabled());

    manager.SetAnimationsEnabled(animations);
    manager.SetReducedMotion(reduced);
}

TEST_CASE("DPI scaling follows the 96 DPI baseline") {
    CHECK(DpiScale(96) == doctest::Approx(1.0));
    CHECK(DpiScale(120) == doctest::Approx(1.25));
    CHECK(DpiScale(144) == doctest::Approx(1.5));
    CHECK(DpiScale(192) == doctest::Approx(2.0));
    CHECK(DpiScale(0) == doctest::Approx(1.0));

    CHECK(ScaleDp(4, 96) == 4);
    CHECK(ScaleDp(12, 120) == 15);
    CHECK(ScaleDp(16, 144) == 24);
    CHECK(ScaleDp(20, 192) == 40);
}

TEST_CASE("shape tokens are ordered and Full uses half the height") {
    CHECK(ShapeDp(ShapeToken::ExtraSmall) == 4);
    CHECK(ShapeDp(ShapeToken::Small) == 8);
    CHECK(ShapeDp(ShapeToken::Medium) == 12);
    CHECK(ShapeDp(ShapeToken::Large) == 16);
    CHECK(ShapeDp(ShapeToken::ExtraLarge) == 28);
    CHECK(ShapeRadiusPx(ShapeToken::Medium, 96) == 12);
    CHECK(ShapeRadiusPx(ShapeToken::Large, 192) == 32);
    CHECK(ShapeRadiusPx(ShapeToken::Full, 96, 40) == 20);
}

TEST_CASE("spacing tokens follow the 4dp scale") {
    CHECK(SpaceDp(Space::Xs) == 4);
    CHECK(SpaceDp(Space::Sm) == 8);
    CHECK(SpaceDp(Space::Md) == 12);
    CHECK(SpaceDp(Space::Lg) == 16);
    CHECK(SpaceDp(Space::Xl) == 20);
    CHECK(SpaceDp(Space::Xxl) == 24);
    CHECK(SpaceDp(Space::Xxxl) == 32);
    CHECK(SpaceDp(Space::Huge) == 40);
    CHECK(SpaceDp(Space::Giant) == 48);
    CHECK(SpacePx(Space::Lg, 120) == 20);
}

TEST_CASE("typography exposes distinct styles") {
    const Typography& type = Typography::Default();
    CHECK(type.Get(TypeStyle::Display).sizeDp > type.Get(TypeStyle::Headline).sizeDp);
    CHECK(type.Get(TypeStyle::Headline).sizeDp > type.Get(TypeStyle::Title).sizeDp);
    CHECK(type.Get(TypeStyle::Title).sizeDp > type.Get(TypeStyle::Body).sizeDp);
    CHECK(type.Get(TypeStyle::Body).sizeDp > type.Get(TypeStyle::Label).sizeDp);
    CHECK(type.Get(TypeStyle::Display).weight == 700);
}

TEST_CASE("easing curves start and end at the expected values") {
    for (const Easing easing :
         {Easing::Linear, Easing::EaseOutCubic, Easing::EaseInOutCubic, Easing::Standard,
          Easing::EmphasizedDecelerate, Easing::EmphasizedAccelerate}) {
        CHECK(ApplyEasing(easing, 0.0) == doctest::Approx(0.0));
        CHECK(ApplyEasing(easing, 1.0) == doctest::Approx(1.0));
        CHECK(ApplyEasing(easing, -1.0) == doctest::Approx(0.0));
        CHECK(ApplyEasing(easing, 2.0) == doctest::Approx(1.0));

        double previous = 0.0;
        for (int step = 0; step <= 40; ++step) {
            const double value = ApplyEasing(easing, step / 40.0);
            CHECK(value >= previous - 1e-9);
            previous = value;
        }
    }
}

TEST_CASE("bezier based standard curve is smooth and monotonic") {
    const double a = ApplyEasing(Easing::Standard, 0.25);
    const double b = ApplyEasing(Easing::Standard, 0.5);
    const double c = ApplyEasing(Easing::Standard, 0.75);
    CHECK(a > 0.0);
    CHECK(b > a);
    CHECK(c > b);
    CHECK(b < 0.9);  // Standard is not a hard ease-out.

    CHECK(ApplyEasing(Easing::EmphasizedDecelerate, 0.1) >
          ApplyEasing(Easing::Linear, 0.1));
    CHECK(ApplyEasing(Easing::EmphasizedAccelerate, 0.1) <
          ApplyEasing(Easing::Linear, 0.1));
}

TEST_CASE("motion tokens stay within desktop timing ranges") {
    using namespace clashyuka::ui::motion;
    CHECK(HoverMs >= 120);
    CHECK(HoverMs <= 160);
    CHECK(PressMs >= 90);
    CHECK(PressMs <= 140);
    CHECK(SwitchMs >= 160);
    CHECK(SwitchMs <= 200);
    CHECK(RailMs >= 180);
    CHECK(RailMs <= 220);
    CHECK(TabMs >= 180);
    CHECK(TabMs <= 220);
    CHECK(PageMs >= 160);
    CHECK(PageMs <= 200);
    CHECK(DialogMs >= 180);
    CHECK(DialogMs <= 220);
    CHECK(SnackbarMs >= 180);
    CHECK(SnackbarMs <= 220);
}

TEST_CASE("animation values interpolate and clamp") {
    const AnimationSpec spec{10.0, 20.0, 400, Easing::Linear};
    CHECK(spec.ValueAt(0.0) == doctest::Approx(10.0));
    CHECK(spec.ValueAt(200.0) == doctest::Approx(15.0));
    CHECK(spec.ValueAt(1000.0) == doctest::Approx(20.0));

    const AnimationSpec instant{0.0, 5.0, 0, Easing::EaseOutCubic};
    CHECK(instant.ProgressAt(10.0) == doctest::Approx(1.0));
    CHECK(instant.ValueAt(10.0) == doctest::Approx(5.0));
}

TEST_CASE("retargeting starts from the current value without a jump") {
    // A new animation is always created from the value currently on screen.
    const AnimationSpec first{0.0, 1.0, 200, Easing::Standard};
    const double mid = first.ValueAt(80.0);
    CHECK(mid > 0.0);
    CHECK(mid < 1.0);

    const AnimationSpec retarget{mid, 0.0, 140, Easing::Standard};
    CHECK(retarget.ValueAt(0.0) == doctest::Approx(mid));
    CHECK(retarget.ValueAt(70.0) < mid);
    CHECK(retarget.ValueAt(140.0) == doctest::Approx(0.0));
}

TEST_CASE("multiple independent animations share one time model") {
    const AnimationSpec hover{0.0, 1.0, 140, Easing::Standard};
    const AnimationSpec press{0.0, 0.03, 110, Easing::EaseOutCubic};
    const AnimationSpec page{0.0, 1.0, 180, Easing::EmphasizedDecelerate};

    CHECK(hover.ValueAt(70.0) > 0.0);
    CHECK(press.ValueAt(70.0) > 0.0);
    CHECK(page.ValueAt(70.0) > 0.0);
    CHECK(press.ValueAt(110.0) == doctest::Approx(0.03));
    CHECK(hover.ValueAt(140.0) == doctest::Approx(1.0));
    CHECK(page.ValueAt(180.0) == doctest::Approx(1.0));
}

TEST_CASE("animation manager cancels entries and releases handles") {
    auto& theme = ThemeManager::Instance();
    const bool animations = theme.AnimationsEnabled();
    const bool reduced = theme.ReducedMotion();
    theme.SetAnimationsEnabled(true);
    theme.SetReducedMotion(false);

    auto& manager = AnimationManager::Instance();
    manager.StopAll();
    CHECK(manager.ActiveCount() == 0);

    auto handle = manager.Animate(0.0, 1.0, 10000, Easing::Linear, [](double) {});
    CHECK(manager.ActiveCount() == 1);
    CHECK(handle->IsRunning());
    handle->Cancel();
    CHECK_FALSE(handle->IsRunning());
    CHECK(manager.ActiveCount() == 0);

    {
        auto scoped = manager.Animate(0.0, 1.0, 10000, Easing::Linear, [](double) {});
        CHECK(manager.ActiveCount() == 1);
    }
    CHECK(manager.ActiveCount() == 0);

    auto first = manager.Animate(0.0, 1.0, 10000, Easing::Linear, [](double) {});
    auto second = manager.Animate(0.0, 1.0, 10000, Easing::Linear, [](double) {});
    CHECK(manager.ActiveCount() == 2);
    manager.StopAll();
    CHECK(manager.ActiveCount() == 0);
    CHECK_FALSE(first->IsRunning());
    CHECK_FALSE(second->IsRunning());

    theme.SetAnimationsEnabled(animations);
    theme.SetReducedMotion(reduced);
}

TEST_CASE("disabled motion applies the final value immediately") {
    auto& theme = ThemeManager::Instance();
    const bool animations = theme.AnimationsEnabled();
    const bool reduced = theme.ReducedMotion();
    auto& manager = AnimationManager::Instance();

    theme.SetAnimationsEnabled(false);
    double applied = -1.0;
    auto handle = manager.Animate(0.0, 1.0, 200, Easing::Standard,
                                  [&applied](double value) { applied = value; });
    CHECK(applied == doctest::Approx(1.0));
    CHECK_FALSE(handle->IsRunning());
    CHECK(manager.ActiveCount() == 0);

    theme.SetAnimationsEnabled(true);
    theme.SetReducedMotion(true);
    applied = -1.0;
    auto reducedHandle = manager.Animate(0.0, 1.0, 200, Easing::Standard,
                                         [&applied](double value) { applied = value; });
    CHECK(applied == doctest::Approx(1.0));
    CHECK_FALSE(reducedHandle->IsRunning());
    CHECK(manager.ActiveCount() == 0);

    theme.SetAnimationsEnabled(animations);
    theme.SetReducedMotion(reduced);
}
