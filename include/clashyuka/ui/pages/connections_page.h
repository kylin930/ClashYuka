#pragma once

#include "clashyuka/ui/pages/page_base.h"

namespace clashyuka::ui {

class Button;
class SearchField;
class TextBlock;

class ConnectionsPage final : public PageBase {
public:
    ConnectionsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    enum class Filter { All, Tcp, Udp };
    void UpdateFilters();

    Button* allButton_ = nullptr;
    Button* tcpButton_ = nullptr;
    Button* udpButton_ = nullptr;
    Filter filter_ = Filter::All;
    TextBlock* countValue_ = nullptr;
};

}  // namespace clashyuka::ui
