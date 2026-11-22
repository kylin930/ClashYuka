#pragma once

namespace clashyuka::ui {

enum class TypeStyle { Display, Headline, Title, Body, Label };

struct TextStyle {
    int sizeDp;
    int weight;  // 400 regular, 500 medium, 600 semibold, 700 bold
    double letterSpacing;
};

struct Typography {
    TextStyle display{34, 700, -0.25};
    TextStyle headline{26, 600, 0.0};
    TextStyle title{20, 600, 0.15};
    TextStyle body{15, 400, 0.25};
    TextStyle label{13, 500, 0.5};

    const TextStyle& Get(TypeStyle style) const {
        switch (style) {
            case TypeStyle::Display:
                return display;
            case TypeStyle::Headline:
                return headline;
            case TypeStyle::Title:
                return title;
            case TypeStyle::Body:
                return body;
            case TypeStyle::Label:
                return label;
        }
        return body;
    }

    static const Typography& Default() {
        static const Typography typography;
        return typography;
    }
};

}  // namespace clashyuka::ui
