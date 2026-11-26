// ClashYuka Dashboard 页面实现
#include "ui/pages/dashboard/page.h"

#include <wx/gbsizer.h>
#include <wx/sizer.h>

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

#include "core/app_context.h"
#include "core/clash/types.h"

namespace clash_yuka::ui {

namespace {

// ---- 数值格式化 ----

// 将字节数格式化为带单位的字符串（B/KB/MB/GB）。
wxString FormatBytes(int64_t bytes) {
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;

    if (bytes <= 0) return "0 B";
    double v = static_cast<double>(bytes);
    if (v < kKB) return wxString::Format("%lld B", static_cast<long long>(bytes));
    if (v < kMB) return wxString::Format("%.2f KB", v / kKB);
    if (v < kGB) return wxString::Format("%.2f MB", v / kMB);
    return wxString::Format("%.2f GB", v / kGB);
}

// 速率格式化：FormatBytes + "/s"
wxString FormatRate(int64_t bytes_per_sec) {
    return FormatBytes(bytes_per_sec) + "/s";
}

wxStaticText* MakeTitleRow(wxWindow* parent, const wxString& title,
                           wxStaticText** value_out, int value_size = 20) {
    auto* title_text = new wxStaticText(parent, wxID_ANY, title);
    title_text->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    title_text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

    auto* value = new wxStaticText(parent, wxID_ANY, "--");
    value->SetFont(wxFont(value_size, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    value->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    *value_out = value;
    return title_text;
}

} // namespace

// ============================================================
// TrafficSparkline
// ============================================================

wxBEGIN_EVENT_TABLE(TrafficSparkline, wxPanel)
    EVT_PAINT(TrafficSparkline::OnPaint)
    EVT_ERASE_BACKGROUND(TrafficSparkline::OnEraseBackground)
wxEND_EVENT_TABLE()

TrafficSparkline::TrafficSparkline(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    SetMinSize(wxSize(200, 80));
}

void TrafficSparkline::PushSample(int64_t up, int64_t down) {
    std::lock_guard<std::mutex> lock(mutex_);
    up_samples_.push_back(up);
    down_samples_.push_back(down);
    while (up_samples_.size() > kMaxSamples) up_samples_.pop_front();
    while (down_samples_.size() > kMaxSamples) down_samples_.pop_front();
}

void TrafficSparkline::OnEraseBackground(wxEraseEvent&) {
    // 不擦除背景，由 OnPaint 全量重绘（避免闪烁）
}

void TrafficSparkline::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    const wxSize size = GetClientSize();
    if (size.GetWidth() <= 1 || size.GetHeight() <= 1) return;

    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    std::deque<int64_t> up_copy, down_copy;
    int64_t max_value = 1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        up_copy = up_samples_;
        down_copy = down_samples_;
    }
    for (auto v : up_copy) max_value = std::max(max_value, v);
    for (auto v : down_copy) max_value = std::max(max_value, v);

    const int pad_top = 8;
    const int pad_bottom = 2;
    const int usable_h = std::max(1, size.GetHeight() - pad_top - pad_bottom);

    DrawCurve(dc, down_copy, wxColour(0, 122, 255), max_value);
    DrawCurve(dc, up_copy, wxColour(52, 199, 89), max_value);
}

void TrafficSparkline::DrawCurve(wxPaintDC& dc, const std::deque<int64_t>& data,
                                 const wxColour& color, int64_t max_value) {
    if (data.empty() || max_value <= 0) return;

    const wxSize size = GetClientSize();
    const int w = size.GetWidth();
    const int h = size.GetHeight();
    const int pad_top = 8;
    const int pad_bottom = 2;
    const int usable_h = std::max(1, h - pad_top - pad_bottom);

    // 计算每个采样点的 x 坐标。当采样数 < kMaxSamples 时，
    // 曲线靠右对齐（左侧留空），更符合"最新值在右"的直觉。
    const size_t n = data.size();
    const size_t slots = kMaxSamples;
    const double dx = static_cast<double>(w) / static_cast<double>(slots);

    dc.SetPen(wxPen(color, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    // 折线：相邻两点连线
    if (n == 1) {
        double x = (slots - 1) * dx;
        double v = static_cast<double>(data[0]) / static_cast<double>(max_value);
        int y = pad_top + static_cast<int>(usable_h * (1.0 - v));
        dc.DrawLine(wxPoint(static_cast<int>(x), y),
                    wxPoint(static_cast<int>(x), y));
        return;
    }

    for (size_t i = 1; i < n; ++i) {
        double x0 = (slots - n + i - 1) * dx;
        double x1 = (slots - n + i) * dx;
        double v0 = static_cast<double>(data[i - 1]) / static_cast<double>(max_value);
        double v1 = static_cast<double>(data[i]) / static_cast<double>(max_value);
        int y0 = pad_top + static_cast<int>(usable_h * (1.0 - v0));
        int y1 = pad_top + static_cast<int>(usable_h * (1.0 - v1));
        dc.DrawLine(wxPoint(static_cast<int>(x0), y0),
                    wxPoint(static_cast<int>(x1), y1));
    }
}

// ============================================================
// TrafficWidget
// ============================================================

TrafficWidget::TrafficWidget(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSIMPLE_BORDER) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer(0);

    auto* title = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("实时流量"));
    title->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(title, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 12);

    auto* rate_sizer = new wxFlexGridSizer(2, 2, 8, 16);
    {
        auto* up_label = new wxStaticText(this, wxID_ANY,
            wxString::FromUTF8("上传"));
        up_label->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        up_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        auto* down_label = new wxStaticText(this, wxID_ANY,
            wxString::FromUTF8("下载"));
        down_label->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        down_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        up_value_ = new wxStaticText(this, wxID_ANY, "--");
        up_value_->SetFont(wxFont(16, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        up_value_->SetForegroundColour(wxColour(52, 199, 89));

        down_value_ = new wxStaticText(this, wxID_ANY, "--");
        down_value_->SetFont(wxFont(16, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        down_value_->SetForegroundColour(wxColour(0, 122, 255));

        rate_sizer->Add(up_label, 0, wxALIGN_CENTER_VERTICAL);
        rate_sizer->Add(down_label, 0, wxALIGN_CENTER_VERTICAL);
        rate_sizer->Add(up_value_, 0, wxALIGN_CENTER_VERTICAL);
        rate_sizer->Add(down_value_, 0, wxALIGN_CENTER_VERTICAL);
    }
    sizer->Add(rate_sizer, 0, wxLEFT | wxTOP, 12);

    sparkline_ = new TrafficSparkline(this);
    sizer->Add(sparkline_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    auto* total_title = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("累计"));
    total_title->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    total_title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    sizer->Add(total_title, 0, wxLEFT | wxTOP, 8);

    auto* total_sizer = new wxFlexGridSizer(2, 2, 4, 16);
    {
        auto* up_label = new wxStaticText(this, wxID_ANY,
            wxString::FromUTF8("总上传"));
        up_label->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        up_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        auto* down_label = new wxStaticText(this, wxID_ANY,
            wxString::FromUTF8("总下载"));
        down_label->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        down_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        total_up_value_ = new wxStaticText(this, wxID_ANY, "0 B");
        total_up_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        total_up_value_->SetForegroundColour(wxColour(52, 199, 89));

        total_down_value_ = new wxStaticText(this, wxID_ANY, "0 B");
        total_down_value_->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        total_down_value_->SetForegroundColour(wxColour(0, 122, 255));

        total_sizer->Add(up_label, 0, wxALIGN_CENTER_VERTICAL);
        total_sizer->Add(down_label, 0, wxALIGN_CENTER_VERTICAL);
        total_sizer->Add(total_up_value_, 0, wxALIGN_CENTER_VERTICAL);
        total_sizer->Add(total_down_value_, 0, wxALIGN_CENTER_VERTICAL);
    }
    sizer->Add(total_sizer, 0, wxLEFT | wxTOP | wxBOTTOM, 12);

    SetSizer(sizer);
    SetMinSize(wxSize(360, 220));
    Layout();
}

void TrafficWidget::UpdateTraffic(int64_t up, int64_t down) {
    // 累计总量（rate × 1s，mihomo traffic 流为每秒一个采样）
    total_up_ += up;
    total_down_ += down;

    up_value_->SetLabel(FormatRate(up));
    down_value_->SetLabel(FormatRate(down));
    total_up_value_->SetLabel(FormatBytes(total_up_));
    total_down_value_->SetLabel(FormatBytes(total_down_));

    sparkline_->PushSample(up, down);
    sparkline_->Refresh();
}

// ============================================================
// MemoryWidget
// ============================================================

MemoryWidget::MemoryWidget(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSIMPLE_BORDER) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("内存占用"));
    title->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(title, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 12);

    value_ = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("内核未运行"));
    value_->SetFont(wxFont(20, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    value_->SetForegroundColour(wxColour(0, 122, 255));
    sizer->Add(value_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 8);

    status_hint_ = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("mihomo 进程内存"));
    status_hint_->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    status_hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    status_hint_->Wrap(160);
    sizer->Add(status_hint_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 4);

    release_btn_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("释放内存"),
                                wxDefaultPosition, wxSize(110, -1));
    release_btn_->SetToolTip(wxString::FromUTF8(
        "重启 mihomo 内核以释放 Go 堆内存（连接会短暂中断）"));
    release_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (release_handler_) release_handler_();
    });
    sizer->Add(release_btn_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 12);

    sizer->AddStretchSpacer(1);
    sizer->Add(0, 0, 0, wxBOTTOM, 12);

    SetSizer(sizer);
    SetMinSize(wxSize(180, 220));
    Layout();
}

void MemoryWidget::SetReleaseHandler(std::function<void()> handler) {
    release_handler_ = std::move(handler);
}

void MemoryWidget::SetReleaseBusy(bool busy) {
    if (!release_btn_) return;
    release_btn_->Enable(!busy);
    release_btn_->SetLabel(busy ? wxString::FromUTF8("正在重启…")
                                : wxString::FromUTF8("释放内存"));
    Layout();
}

void MemoryWidget::SetHint(const wxString& text) {
    if (!status_hint_) return;
    status_hint_->SetLabel(text);
    status_hint_->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
}

void MemoryWidget::UpdateMemory(int64_t inuse_bytes) {
    value_->SetLabel(FormatBytes(inuse_bytes));
    value_->SetForegroundColour(wxColour(0, 122, 255));
    status_hint_->SetLabel(wxString::FromUTF8("mihomo 进程内存"));
    status_hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    if (release_btn_) release_btn_->Enable(true);
}

void MemoryWidget::UpdateError(const wxString& error_msg) {
    value_->SetLabel(wxString::FromUTF8("内核未运行"));
    value_->SetForegroundColour(wxColour(255, 149, 0));
    if (!error_msg.empty()) {
        status_hint_->SetLabel(error_msg);
        status_hint_->SetForegroundColour(wxColour(255, 149, 0));
    }
    if (release_btn_) release_btn_->Enable(false);
}

// ============================================================
// ConnectionsWidget
// ============================================================

ConnectionsWidget::ConnectionsWidget(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSIMPLE_BORDER) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("活跃连接"));
    title->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    sizer->Add(title, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 12);

    value_ = new wxStaticText(this, wxID_ANY, "--");
    value_->SetFont(wxFont(28, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    value_->SetForegroundColour(wxColour(0, 122, 255));
    sizer->Add(value_, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 8);

    auto* hint = new wxStaticText(this, wxID_ANY,
        wxString::FromUTF8("当前活跃连接总数"));
    hint->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    sizer->Add(hint, 0, wxALIGN_LEFT | wxLEFT | wxTOP, 4);

    sizer->AddStretchSpacer(1);
    sizer->Add(0, 0, 0, wxBOTTOM, 12);

    SetSizer(sizer);
    SetMinSize(wxSize(180, 220));
    Layout();
}

void ConnectionsWidget::UpdateConnections(int total) {
    value_->SetLabel(wxString::Format("%d", total));
}

// ============================================================
// DashboardPage
// ============================================================

wxBEGIN_EVENT_TABLE(DashboardPage, wxPanel)
    EVT_TIMER(wxID_HIGHEST + 1, DashboardPage::OnMemoryTimer)
wxEND_EVENT_TABLE()

DashboardPage::DashboardPage(wxWindow* parent)
    : wxPanel(parent),
      alive_(std::make_shared<std::atomic<bool>>(true)),
      memory_busy_(std::make_shared<std::atomic<bool>>(false)) {
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));

    // 外层垂直布局：顶部留 8px 边距 + 内容网格
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->AddSpacer(8);

    auto* grid = new wxGridBagSizer(8, 8);

    traffic_widget_ = new TrafficWidget(this);
    memory_widget_ = new MemoryWidget(this);
    connections_widget_ = new ConnectionsWidget(this);

    // 「释放内存」：重启内核（Go 运行时不会主动把堆内存归还给系统）
    memory_widget_->SetReleaseHandler([this]() { OnReleaseMemory(); });

    // 布局：
    //   行 0：[TrafficWidget     ][MemoryWidget    ]
    //   行 1：[TrafficWidget     ][ConnectionsWgt ]
    // TrafficWidget 跨两行（高一些以容纳 sparkline + 累计行）
    grid->Add(traffic_widget_, wxGBPosition(0, 0), wxGBSpan(2, 1),
              wxEXPAND | wxALL, 0);
    grid->Add(memory_widget_, wxGBPosition(0, 1), wxGBSpan(1, 1),
              wxEXPAND | wxALL, 0);
    grid->Add(connections_widget_, wxGBPosition(1, 1), wxGBSpan(1, 1),
              wxEXPAND | wxALL, 0);
    // 必须在条目加入后再设置可增长列/行（wxGridBagSizer 在有条目后才确定 m_cols）
    grid->AddGrowableCol(0);
    grid->AddGrowableCol(1);
    grid->AddGrowableRow(0);

    outer->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // WS 连接状态提示
    ws_status_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    ws_status_text_->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    ws_status_text_->SetForegroundColour(wxColour(255, 149, 0));
    outer->Add(ws_status_text_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    outer->AddSpacer(8);
    SetSizer(outer);
    Layout();

    // 仅在 AppContext 已初始化时注册回调；未初始化（如调试场景）静默跳过
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        // 初始化内核状态显示
        if (!clash_yuka::AppContext::Get().IsCoreRunning()) {
            const auto& err = clash_yuka::AppContext::Get().last_start_error();
            memory_widget_->UpdateError(
                wxString::FromUTF8(err.empty() ? "内核未启动" : err));
            connections_widget_->UpdateConnections(0);
        }

        RegisterCallbacks();
        // 每 2 秒拉取一次内存
        memory_timer_.SetOwner(this, wxID_HIGHEST + 1);
        // /memory 为流式接口，单次采样约需 2 秒（mihomo 每秒推送一次）；
        // 5 秒轮询一次即可，避免长期占着连接。
        memory_timer_.Start(5000);
    }
}

DashboardPage::~DashboardPage() {
    UnregisterCallbacks();
}

void DashboardPage::RegisterCallbacks() {
    auto& ctx = clash_yuka::AppContext::Get();
    auto alive = alive_;  // shared_ptr 拷贝，延长 bool 生命周期

    // ---- /traffic 回调：每秒一个采样，包含 up/down 速率（字节/秒）----
    traffic_handle_ = ctx.ws().SubscribeTraffic(
        [this, alive](const clash::Traffic& t) {
            last_ws_activity_ms_.store(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count(),
                std::memory_order_relaxed);
            const int64_t up = t.up;
            const int64_t down = t.down;
            wxTheApp->CallAfter([this, alive, up, down]() {
                if (!alive->load(std::memory_order_acquire)) return;
                traffic_widget_->UpdateTraffic(up, down);
            });
        });

    // ---- /connections 回调：参数为 (连接列表, total) ----
    connections_handle_ = ctx.ws().SubscribeConnections(
        [this, alive](const std::vector<clash::Connection>& /*conns*/, int total) {
            last_ws_activity_ms_.store(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count(),
                std::memory_order_relaxed);
            wxTheApp->CallAfter([this, alive, total]() {
                if (!alive->load(std::memory_order_acquire)) return;
                connections_widget_->UpdateConnections(total);
            });
        });
}

void DashboardPage::UnregisterCallbacks() {
    // 先标记 alive_=false，使已投递但未执行的 CallAfter 跳过 UI 访问；
    // 再注销 ws 订阅，阻止后台线程继续投递新事件。
    if (alive_) {
        alive_->store(false, std::memory_order_release);
    }
    if (memory_timer_.IsRunning()) {
        memory_timer_.Stop();
    }
    // 仅在 AppContext 仍持有 ws_ 时注销订阅（避免 Init 未执行时的崩溃）
    if (clash_yuka::AppContext::Get().IsInitialized()) {
        auto& ws = clash_yuka::AppContext::Get().ws();
        if (traffic_handle_ != -1) {
            ws.UnsubscribeTraffic(traffic_handle_);
            traffic_handle_ = -1;
        }
        if (connections_handle_ != -1) {
            ws.UnsubscribeConnections(connections_handle_);
            connections_handle_ = -1;
        }
    }
}

void DashboardPage::OnReleaseMemory() {
    auto& ctx = clash_yuka::AppContext::Get();
    if (!ctx.IsInitialized()) return;

    if (!ctx.IsCoreRunning()) {
        wxMessageBox(wxString::FromUTF8("内核未运行，无需释放内存。"),
                     wxString::FromUTF8("释放内存"),
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (ctx.verge().enable_service_mode.value_or(false)) {
        wxMessageBox(
            wxString::FromUTF8(
                "服务模式下内核由系统服务托管，需要重启服务才能释放内存。\n"
                "请在服务管理器中重启服务，或改用普通模式。"),
            wxString::FromUTF8("释放内存"),
            wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (wxMessageBox(
            wxString::FromUTF8(
                "将重启 mihomo 内核以释放其占用的内存。\n"
                "现有连接会短暂中断，随后自动恢复。是否继续？"),
            wxString::FromUTF8("释放内存"),
            wxYES_NO | wxICON_QUESTION, this) != wxYES) {
        return;
    }

    releasing_.store(true, std::memory_order_release);
    memory_widget_->SetReleaseBusy(true);
    memory_widget_->SetHint(wxString::FromUTF8("正在重启内核…"));

    auto alive = alive_;
    std::thread([this, alive]() {
        std::string err = clash_yuka::AppContext::Get().RestartCore();
        if (err.empty() &&
            !clash_yuka::AppContext::Get().WaitForApiReady(20000)) {
            err = "内核已重启但 API 未就绪";
        }
        wxTheApp->CallAfter([this, alive, err]() {
            releasing_.store(false, std::memory_order_release);
            if (!alive->load(std::memory_order_acquire)) return;
            memory_widget_->SetReleaseBusy(false);
            if (!err.empty()) {
                memory_widget_->SetHint(wxString::FromUTF8("释放内存失败"));
                wxMessageBox(wxString::FromUTF8("释放内存失败：\n") +
                                 wxString::FromUTF8(err.c_str()),
                             wxString::FromUTF8("释放内存"),
                             wxOK | wxICON_ERROR, this);
                return;
            }
            memory_widget_->SetHint(
                wxString::FromUTF8("内核已重启，等待内存采样…"));
        });
    }).detach();
}

void DashboardPage::OnMemoryTimer(wxTimerEvent&) {
    // 释放内存的重启流程进行中：不要用「内核未运行」覆盖状态提示
    if (releasing_.load(std::memory_order_relaxed)) return;

    auto& ctx = clash_yuka::AppContext::Get();
    // 内核未运行时更新错误提示
    if (!ctx.IsCoreRunning()) {
        const auto& err = ctx.last_start_error();
        auto alive = alive_;
        wxTheApp->CallAfter([this, alive, err]() {
            if (!alive->load(std::memory_order_acquire)) return;
            memory_widget_->UpdateError(
                wxString::FromUTF8(err.empty() ? "内核未启动" : err));
            if (ws_status_text_) ws_status_text_->SetLabel(wxEmptyString);
        });
        return;
    }

    // 检测 WS 活跃度：mihomo 正常每秒推送一次 /traffic
    // 若超过 5 秒无推送，可能是 WS 连接尚未建立或已断开
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int64_t last_ms = last_ws_activity_ms_.load(std::memory_order_relaxed);
    const bool ws_alive = (last_ms > 0) && (now_ms - last_ms < 5000);

    auto alive = alive_;
    wxTheApp->CallAfter([this, alive, ws_alive]() {
        if (!alive->load(std::memory_order_acquire)) return;
        if (ws_status_text_) {
            if (!ws_alive) {
                ws_status_text_->SetLabel(
                    wxString::FromUTF8("等待 WebSocket 连接..."));
            } else {
                ws_status_text_->SetLabel(wxEmptyString);
            }
        }
    });

    // 在 detached 线程中执行同步 HTTP 调用，避免阻塞 UI 主线程。
    // 拿到结果后通过 CallAfter 回主线程更新 Text 控件。
    // 若上一次请求仍在进行（/memory 为流式接口，正常情况下 1 秒内返回），
    // 本次跳过，避免后台线程堆积。
    auto busy = memory_busy_;
    bool expected = false;
    if (!busy->compare_exchange_strong(expected, true,
                                       std::memory_order_acq_rel)) {
        return;
    }
    std::thread([this, alive, busy]() {
        auto release = [&busy]() {
            busy->store(false, std::memory_order_release);
        };
        if (!alive->load(std::memory_order_acquire)) { release(); return; }
        if (!clash_yuka::AppContext::Get().IsCoreRunning()) { release(); return; }
        auto mem = clash_yuka::AppContext::Get().api().GetMemory();
        const int64_t inuse = mem.inuse;
        release();
        wxTheApp->CallAfter([this, alive, inuse]() {
            if (!alive->load(std::memory_order_acquire)) return;
            memory_widget_->UpdateMemory(inuse);
        });
    }).detach();
}

} // namespace clash_yuka::ui
