#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <wx/textctrl.h>

#include "clashyuka/ui/components/control_base.h"
#include "clashyuka/ui/drawing/icons.h"

namespace clashyuka::ui {

// MD3 search field: a rounded container with a leading search icon and a
// borderless native text control inside. The container paints the surface so
// the field stays consistent with the rest of the design system.
class SearchField final : public ControlBase {
public:
    // The leading icon and password mode are fixed for the lifetime of the
    // field. Pass std::nullopt for a plain text input.
    SearchField(wxWindow* parent, const wxString& placeholder,
                std::optional<IconId> leadingIcon = IconId::Search, bool password = false);

    wxString GetValue() const;
    void SetValue(const wxString& value);
    void Clear();
    void SetPlaceholder(const wxString& placeholder);
    void SetOnChange(std::function<void(const wxString&)> handler);

    wxSize DoGetBestClientSize() const override;
    bool Enable(bool enable = true) override;
    void OnThemeChanged() override;

protected:
    void PaintContent(Painter& painter, const wxSize& size) override;

private:
    void UpdateChildLayout();
    void ApplyColors();

    wxTextCtrl* input_ = nullptr;
    wxString placeholder_;
    std::optional<IconId> leadingIcon_;
    bool focused_ = false;
    std::function<void(const wxString&)> onChange_;
};

}  // namespace clashyuka::ui
