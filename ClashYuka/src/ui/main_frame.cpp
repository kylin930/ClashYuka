#include "ui/main_frame.h"

#include <wx/artprov.h>
#include <wx/imaglist.h>

#include <vector>

#include "ui/pages/dashboard/page.h"
#include "ui/pages/proxies/page.h"
#include "ui/pages/profiles/page.h"
#include "ui/pages/connections/page.h"
#include "ui/pages/rules/page.h"
#include "ui/pages/logs/page.h"
#include "ui/pages/providers/page.h"
#include "ui/pages/settings/page.h"

namespace clash_yuka::ui {

namespace {

struct NavItemDef {
    wxString label;
    const char* artId;
};

const std::vector<NavItemDef>& NavItems() {
    static const std::vector<NavItemDef> items = {
        { wxString::FromUTF8("仪表盘"), wxART_REPORT_VIEW     },
        { wxString::FromUTF8("代理"),   wxART_GO_FORWARD      },
        { wxString::FromUTF8("订阅"),   wxART_LIST            },
        { wxString::FromUTF8("连接"),   wxART_EXECUTABLE_FILE },
        { wxString::FromUTF8("规则"),   wxART_FIND            },
        { wxString::FromUTF8("日志"),   wxART_HELP            },
        { wxString::FromUTF8("提供者"), wxART_COPY            },
        { wxString::FromUTF8("设置"),   wxART_EDIT            },
    };
    return items;
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "ClashYuka",
              wxDefaultPosition, wxSize(1000, 700)),
      m_auiManager(this) {
    SetMinClientSize(wxSize(600, 400));

    CreateControls();

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    LoadWindowState();
}

MainFrame::~MainFrame() {
    m_auiManager.UnInit();
    if (m_imageList) {
        delete m_imageList;
    }
}

void MainFrame::CreateControls() {
    CreateToolbar();
    CreateNavigationPanel();
    CreateContentPages();

    m_auiManager.Update();
}

void MainFrame::CreateToolbar() {
    m_toolBar = CreateToolBar(wxTB_HORIZONTAL | wxNO_BORDER);
    m_toolBar->SetToolBitmapSize(wxSize(24, 24));

    m_toolBar->AddTool(wxID_EXIT, wxString::FromUTF8("退出"),
                       wxArtProvider::GetBitmap(wxART_QUIT, wxART_TOOLBAR),
                       wxString::FromUTF8("退出应用"));

    m_toolBar->AddSeparator();

    m_toolBar->AddTool(wxID_ABOUT, wxString::FromUTF8("关于"),
                       wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_TOOLBAR),
                       wxString::FromUTF8("关于 ClashYuka"));

    m_toolBar->Realize();
}

void MainFrame::CreateNavigationPanel() {
    m_imageList = new wxImageList(24, 24);
    const auto& items = NavItems();
    for (const auto& item : items) {
        wxBitmap bmp = wxArtProvider::GetBitmap(item.artId, wxART_MENU, wxSize(24, 24));
        if (bmp.IsOk()) {
            m_imageList->Add(bmp);
        } else {
            m_imageList->Add(wxBitmap(24, 24));
        }
    }

    m_navList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(180, -1),
                               wxLC_LIST | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_navList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);

    int idx = 0;
    for (const auto& item : items) {
        m_navList->InsertItem(idx, item.label, idx);
        idx++;
    }

    m_navList->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    m_navList->Bind(wxEVT_LIST_ITEM_SELECTED, &MainFrame::OnNavSelect, this);

    wxAuiPaneInfo navPane;
    navPane.Name("nav")
           .Caption(wxString::FromUTF8("导航"))
           .Left()
           .Position(0)
           .Layer(0)
           .CloseButton(false)
           .MaximizeButton(false)
           .BestSize(wxSize(180, -1))
           .MinSize(wxSize(100, -1));

    m_auiManager.AddPane(m_navList, navPane);
}

void MainFrame::CreateContentPages() {
    m_book = new wxAuiNotebook(this, wxID_ANY);

    wxBitmap bmp;

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Dashboard));
    if (bmp.IsOk()) {
        m_book->AddPage(new DashboardPage(m_book), wxString::FromUTF8("仪表盘"), false, bmp);
    } else {
        m_book->AddPage(new DashboardPage(m_book), wxString::FromUTF8("仪表盘"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Proxies));
    if (bmp.IsOk()) {
        m_book->AddPage(new ProxiesPage(m_book), wxString::FromUTF8("代理"), false, bmp);
    } else {
        m_book->AddPage(new ProxiesPage(m_book), wxString::FromUTF8("代理"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Profiles));
    if (bmp.IsOk()) {
        m_book->AddPage(new ProfilesPage(m_book), wxString::FromUTF8("订阅"), false, bmp);
    } else {
        m_book->AddPage(new ProfilesPage(m_book), wxString::FromUTF8("订阅"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Connections));
    if (bmp.IsOk()) {
        m_book->AddPage(new ConnectionsPage(m_book), wxString::FromUTF8("连接"), false, bmp);
    } else {
        m_book->AddPage(new ConnectionsPage(m_book), wxString::FromUTF8("连接"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Rules));
    if (bmp.IsOk()) {
        m_book->AddPage(new RulesPage(m_book), wxString::FromUTF8("规则"), false, bmp);
    } else {
        m_book->AddPage(new RulesPage(m_book), wxString::FromUTF8("规则"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Logs));
    if (bmp.IsOk()) {
        m_book->AddPage(new LogsPage(m_book), wxString::FromUTF8("日志"), false, bmp);
    } else {
        m_book->AddPage(new LogsPage(m_book), wxString::FromUTF8("日志"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Providers));
    if (bmp.IsOk()) {
        m_book->AddPage(new ProvidersPage(m_book), wxString::FromUTF8("提供者"), false, bmp);
    } else {
        m_book->AddPage(new ProvidersPage(m_book), wxString::FromUTF8("提供者"), false);
    }

    bmp = m_imageList->GetBitmap(static_cast<int>(NavPage::Settings));
    if (bmp.IsOk()) {
        m_book->AddPage(new SettingsPage(m_book), wxString::FromUTF8("设置"), false, bmp);
    } else {
        m_book->AddPage(new SettingsPage(m_book), wxString::FromUTF8("设置"), false);
    }

    if (m_book->GetPageCount() > 0) {
        m_book->SetSelection(0);
    }

    m_book->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &MainFrame::OnPageChanged, this);

    wxAuiPaneInfo contentPane;
    contentPane.Name("content")
               .Center()
               .Position(0)
               .Layer(0)
               .CloseButton(false)
               .MaximizeButton(true);

    m_auiManager.AddPane(m_book, contentPane);
}

void MainFrame::OnNavSelect(wxListEvent& event) {
    int index = event.GetIndex();
    if (index >= 0 && index < m_book->GetPageCount()) {
        m_book->SetSelection(index);
    }
}

void MainFrame::OnPageChanged(wxAuiNotebookEvent& event) {
    int index = event.GetSelection();
    if (index >= 0 && index < m_navList->GetItemCount()) {
        m_navList->SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
}

void MainFrame::SetCurrentPage(int index) {
    if (index >= 0 && index < m_book->GetPageCount()) {
        m_book->SetSelection(index);
        m_navList->SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
}

void MainFrame::SetAlwaysOnTop(bool on_top) {
    // 通过切换 wxSTAY_ON_TOP 样式标志实现窗口置顶
    long style = GetWindowStyleFlag();
    if (on_top) {
        style |= wxSTAY_ON_TOP;
    } else {
        style &= ~wxSTAY_ON_TOP;
    }
    SetWindowStyleFlag(style);
    // 刷新窗口样式（某些平台需要显式刷新）
    Refresh();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    // 关闭窗口（点 X 或 Alt+F4）时仅隐藏到托盘，不销毁窗口。
    if (event.CanVeto()) {
        SaveWindowState();
        Hide();
        event.Veto();
    } else {
        SaveWindowState();
        event.Skip();
    }
}

void MainFrame::LoadWindowState() {
}

void MainFrame::SaveWindowState() {
}

} // namespace clash_yuka::ui
