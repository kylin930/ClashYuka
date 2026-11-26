// ClashYuka Dashboard 页面（Phase 4 Task 10）
//
// 仪表盘作为应用启动后的首屏，集中展示核心运行状态：
//   - 实时上传/下载流量（数值 + sparkline 折线图，订阅 ws /traffic 流）
//   - 累计上传/下载总量（基于实时流量累加）
//   - mihomo 内存占用（定时拉取 GET /memory）
//   - 当前活跃连接数（订阅 ws /connections 流的 total 字段）
//
// 数据来源与线程模型：
//   - 流量 / 连接数：WsClient 后台线程回调 -> wxTheApp->CallAfter -> UI 主线程更新
//   - 内存：wxTimer 触发 -> detached std::thread 调用 ApiClient::GetMemory ->
//     CallAfter 回主线程
//   - 所有 UI 访问均发生在主线程；alive_ 标志位避免页面析构后访问悬空指针
//
// 布局：固定 2 列 wxGridBagSizer，本阶段不实现编辑模式（接口预留）。
#pragma once

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace clash_yuka::ui {

// 流量曲线绘制面板：自绘最近 N 秒的上传/下载折线。
// 线程安全：PushSample 可在 ws 后台线程调用，OnPaint 在主线程。
class TrafficSparkline : public wxPanel {
public:
    TrafficSparkline(wxWindow* parent);

    // 追加一个采样点（线程安全）。deque 自动截断到 kMaxSamples。
    void PushSample(int64_t up, int64_t down);

private:
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    // 在 dc 上绘制一条折线，data 已按时间顺序排列。
    void DrawCurve(wxPaintDC& dc, const std::deque<int64_t>& data,
                   const wxColour& color, int64_t max_value);

    std::mutex mutex_;
    std::deque<int64_t> up_samples_;
    std::deque<int64_t> down_samples_;
    static constexpr size_t kMaxSamples = 60;

    wxDECLARE_EVENT_TABLE();
};

// 流量 widget：实时上传/下载速率 + sparkline + 累计总量。
// UpdateTraffic 可在 ws 后台线程调用（内部通过 atomic 累加总量，
// sparkline 自身线程安全）。
class TrafficWidget : public wxPanel {
public:
    explicit TrafficWidget(wxWindow* parent);

    void UpdateTraffic(int64_t up, int64_t down);

private:
    wxStaticText* up_value_;
    wxStaticText* down_value_;
    wxStaticText* total_up_value_;
    wxStaticText* total_down_value_;
    TrafficSparkline* sparkline_;
    int64_t total_up_ = 0;
    int64_t total_down_ = 0;
};

class MemoryWidget : public wxPanel {
public:
    explicit MemoryWidget(wxWindow* parent);

    void UpdateMemory(int64_t inuse_bytes);
    void UpdateError(const wxString& error_msg);

    // 设置「释放内存」按钮回调（点击后由 DashboardPage 重启内核）
    void SetReleaseHandler(std::function<void()> handler);
    // 请求进行中：禁用按钮并显示处理中状态
    void SetReleaseBusy(bool busy);
    // 更新底部提示文本
    void SetHint(const wxString& text);

private:
    wxStaticText* value_;
    wxStaticText* status_hint_;
    wxButton* release_btn_ = nullptr;
    std::function<void()> release_handler_;
};

class ConnectionsWidget : public wxPanel {
public:
    explicit ConnectionsWidget(wxWindow* parent);

    void UpdateConnections(int total);

private:
    wxStaticText* value_ = nullptr;
};

// Dashboard 页面容器：组合上述 widgets，管理 wxTimer 与 ws 回调注册/注销。
class DashboardPage : public wxPanel {
public:
    explicit DashboardPage(wxWindow* parent);
    ~DashboardPage() override;

private:
    // 注册 ws /traffic 与 /connections 回调；构造时调用。
    void RegisterCallbacks();
    // 清除 ws 回调并标记 alive_=false；析构时调用，避免悬空回调。
    void UnregisterCallbacks();

    // 内存轮询定时器：触发 detached 线程调 GetMemory。
    void OnMemoryTimer(wxTimerEvent& event);
    // 释放内存：重启 mihomo 内核（Go 运行时不会主动归还堆内存）
    void OnReleaseMemory();

    TrafficWidget* traffic_widget_ = nullptr;
    MemoryWidget* memory_widget_ = nullptr;
    ConnectionsWidget* connections_widget_ = nullptr;

    wxTimer memory_timer_;

    // 跨线程存活标志：析构时置 false，CallAfter lambda 据此跳过 UI 更新。
    // shared_ptr 让捕获此标志的 lambda 在页面析构后仍能安全读取。
    std::shared_ptr<std::atomic<bool>> alive_;

    // 内存请求在途标志：/memory 为流式接口，避免定时器重叠发起多个请求
    std::shared_ptr<std::atomic<bool>> memory_busy_;

    // 「释放内存」重启流程进行中：暂停内存轮询，避免覆盖界面提示
    std::atomic<bool> releasing_{false};

    // WsClient 订阅句柄（RegisterCallbacks 时获取，UnregisterCallbacks 时注销）。
    // -1 表示未订阅。
    int traffic_handle_ = -1;
    int connections_handle_ = -1;

    // WS 活跃度检测：记录上次收到 traffic 推送的时间（毫秒时间戳）
    std::atomic<int64_t> last_ws_activity_ms_{0};

    // WS 连接状态提示文本
    wxStaticText* ws_status_text_ = nullptr;

    wxDECLARE_EVENT_TABLE();
};

} // namespace clash_yuka::ui
