#pragma once

#include <functional>
#include <memory>

#include <wx/textctrl.h>

#include "clashyuka/ui/components/control_base.h"

namespace clashyuka::ui {

// MD3 search field: a rounded container with a leading search icon and a
// borderless native text control inside. The container paints the surface so
// the field stays consistent with the rest of the design system.
class SearchField final : public ControlBase {
public:
    SearchField(wxWindow* parent, const wxString& placeholder);

    wxString GetValue() const;
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
    bool focused_ = false;
    std::function<void(const wxString&)> onChange_;
};

}  // namespace clashyuka::ui
