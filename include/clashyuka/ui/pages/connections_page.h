#pragma once

#include "clashyuka/ui/pages/page_base.h"

class wxStaticText;
class wxTextCtrl;

namespace clashyuka::ui {

class Button;
class ConnectionList;

class ConnectionsPage final : public PageBase {
public:
    ConnectionsPage(wxWindow* parent, app::AppController& controller);

protected:
    void ApplyState(const app::AppStateSnapshot& state) override;

private:
    enum class Filter { All, Tcp, Udp };
    void SetFilter(Filter filter);
    void RefreshRows();
    void RefreshDetails();

    Button* allButton_ = nullptr;
    Button* tcpButton_ = nullptr;
    Button* udpButton_ = nullptr;
    Button* closeButton_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* details_ = nullptr;
    ConnectionList* list_ = nullptr;
    Filter filter_ = Filter::All;
    wxString query_;
    app::LiveStreamsSnapshot live_;
    std::string selectedId_;
    bool updatingList_ = false;
};

}  // namespace clashyuka::ui
