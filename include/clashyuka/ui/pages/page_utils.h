#pragma once

#include <wx/sizer.h>

#include "clashyuka/ui/components/surface_card.h"
#include "clashyuka/ui/components/text_block.h"
#include "clashyuka/ui/theme/spacing_tokens.h"

namespace clashyuka::ui::pages {

inline void AddSpacer(wxSizer* sizer, wxWindow* window, int dp) {
    sizer->AddSpacer(window->FromDIP(dp));
}

inline TextBlock* MakeSectionTitle(wxWindow* parent, const wxString& text) {
    return new TextBlock(parent, text, TypeStyle::Title);
}

inline TextBlock* MakeCaption(wxWindow* parent, const wxString& text) {
    return new TextBlock(parent, text, TypeStyle::Label, 170);
}

inline SurfaceCard* MakeCard(wxWindow* parent, SurfaceLevel level = SurfaceLevel::Low) {
    return new SurfaceCard(parent, level);
}

}  // namespace clashyuka::ui::pages
