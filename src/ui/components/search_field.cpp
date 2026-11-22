#include "clashyuka/ui/components/search_field.h"

#include <utility>

#include "clashyuka/ui/drawing/icons.h"
#include "clashyuka/ui/theme/spacing_tokens.h"
#include "clashyuka/ui/theme/theme_manager.h"

namespace clashyuka::ui {
namespace {

constexpr int kHeightDp = 40;
constexpr int kIconDp = 18;
constexpr int kIconInsetDp = 14;
constexpr int kTextLeftDp = 42;
constexpr int kTextRightDp = 20;
constexpr int kTextHeightDp = 24;

}  // namespace

SearchField::SearchField(wxWindow* parent, const wxString& placeholder)
    : ControlBase(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize),
      placeholder_(placeholder) {
    input_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxBORDER_NONE | wxTE_PROCESS_ENTER);
    input_->SetHint(placeholder_);
    input_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        if (onChange_) {
            onChange_(input_->GetValue());
        }
        event.Skip();
    });
    input_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        focused_ = true;
        Refresh(false);
        event.Skip();
    });
    input_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        focused_ = false;
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        UpdateChildLayout();
        event.Skip();
    });

    SetMinSize(DoGetBestClientSize());
    ApplyColors();
    UpdateChildLayout();
}

wxString SearchField::GetValue() const {
    return input_ != nullptr ? input_->GetValue() : wxString();
}

void SearchField::Clear() {
    if (input_ != nullptr) {
        input_->ChangeValue(wxEmptyString);
    }
}

void SearchField::SetPlaceholder(const wxString& placeholder) {
    placeholder_ = placeholder;
    if (input_ != nullptr) {
        input_->SetHint(placeholder_);
    }
}

void SearchField::SetOnChange(std::function<void(const wxString&)> handler) {
    onChange_ = std::move(handler);
}

wxSize SearchField::DoGetBestClientSize() const {
    return wxSize(FromDIP(280), FromDIP(kHeightDp));
}

bool SearchField::Enable(bool enable) {
    const bool result = wxWindow::Enable(enable);
    if (input_ != nullptr) {
        input_->Enable(enable);
    }
    Refresh();
    return result;
}

void SearchField::OnThemeChanged() {
    ControlBase::OnThemeChanged();
    ApplyColors();
}

void SearchField::ApplyColors() {
    if (input_ == nullptr) {
        return;
    }
    const ColorScheme& colors = Colors();
    input_->SetBackgroundColour(ToWx(colors.surfaceContainerHigh));
    input_->SetForegroundColour(ToWx(colors.onSurface));
    input_->SetFont(Font(TypeStyle::Body));
    Refresh();
}

void SearchField::UpdateChildLayout() {
    if (input_ == nullptr) {
        return;
    }
    const wxSize size = GetClientSize();
    const int height = FromDIP(kTextHeightDp);
    const int top = (size.y - height) / 2;
    const int left = FromDIP(kTextLeftDp);
    const int width = size.x - left - FromDIP(kTextRightDp);
    if (width > 0 && height > 0) {
        input_->SetSize(wxRect(left, top, width, height));
    }
}

void SearchField::PaintContent(Painter& painter, const wxSize& size) {
    const ColorScheme& colors = Colors();
    const double radius = size.y / 2.0;
    const wxRect rect(0, 0, size.x, size.y);

    painter.FillRoundedRect(rect, radius, colors.surfaceContainerHigh);
    if (focused_) {
        painter.StrokeRoundedRect(wxRect(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2),
                                  radius, colors.primary, 1.5);
    }

    const int icon = FromDIP(kIconDp);
    const int inset = FromDIP(kIconInsetDp);
    const wxRect iconRect(inset, (size.y - icon) / 2, icon, icon);
    IconProvider::Draw(painter, IconId::Search, iconRect,
                       WithAlpha(colors.onSurface, IsEnabled() ? 150 : 80), 1.8);
}

}  // namespace clashyuka::ui
