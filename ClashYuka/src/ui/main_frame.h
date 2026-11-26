#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/frame.h>
#include <wx/aui/auibook.h>
#include <wx/aui/framemanager.h>
#include <wx/toolbar.h>
#include <wx/listctrl.h>

namespace clash_yuka::ui {

enum class NavPage : int {
    Dashboard = 0,
    Proxies,
    Profiles,
    Connections,
    Rules,
    Logs,
    Providers,
    Settings,
};

inline constexpr int kNavPageCount = 8;

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    void SetCurrentPage(int index);
    void SetAlwaysOnTop(bool on_top);

private:
    void CreateControls();
    void CreateToolbar();
    void CreateNavigationPanel();
    void CreateContentPages();
    void AddNavItem(const wxString& label, int imageIndex);

    void OnNavSelect(wxListEvent& event);
    void OnPageChanged(wxAuiNotebookEvent& event);
    void OnClose(wxCloseEvent& event);

    void LoadWindowState();
    void SaveWindowState();

    wxToolBar* m_toolBar = nullptr;
    wxListCtrl* m_navList = nullptr;
    wxAuiNotebook* m_book = nullptr;
    wxAuiManager m_auiManager;

    wxImageList* m_imageList = nullptr;

    static constexpr int kToolbarHeight = 32;
};

} // namespace clash_yuka::ui
