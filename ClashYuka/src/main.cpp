#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "core/app_context.h"
#include "core/autostart.h"
#include "core/hotkey.h"
#include "core/i18n.h"
#include "core/migration.h"
#include "core/resources.h"
#include "core/singleton.h"
#include "core/sysopt.h"
#include "core/tray.h"
#include "core/updater.h"
#include "core/utils/dirs.h"
#include "ui/main_frame.h"

class ClashYukaApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }

        auto verge_tmp = clash_yuka::config::IVerge::Load(
            clash_yuka::utils::verge_config_path());
        const uint16_t singleton_port =
            verge_tmp.app_singleton_port.value_or(33333);
        if (!clash_yuka::core::Singleton::Get().TryLock(singleton_port)) {
            clash_yuka::core::Singleton::Get().SendMessage("show_window",
                                                              singleton_port);
            return false;
        }

        clash_yuka::AppContext::Get().Init();

        auto& verge = clash_yuka::AppContext::Get().verge();
        clash_yuka::core::I18n::Get().SetLanguage(
            verge.language.value_or("zh-cn"));

        clash_yuka::core::Resources::Get().EnsureResourcesDir();

        clash_yuka::core::Singleton::Get().SetMessageHandler(
            [](const std::string& msg) {
                if (msg == "show_window") {
                    auto* top = wxTheApp->GetTopWindow();
                    if (top) {
                        auto* tlw = static_cast<wxTopLevelWindow*>(top);
                        top->Show(true);
                        if (tlw->IsIconized()) tlw->Iconize(false);
                        tlw->Raise();
                        tlw->SetFocus();
                    }
                }
            });

        auto frame = new clash_yuka::ui::MainFrame();
        // 静默启动：不显示主窗口，仅托盘运行
        if (!verge.enable_silent_start.value_or(false)) {
            frame->Show(true);
        }
        // 恢复「窗口置顶」设置
        if (verge.always_on_top.value_or(false)) {
            frame->SetAlwaysOnTop(true);
        }

        clash_yuka::core::TrayIcon::Get().Create();
        clash_yuka::core::HotkeyManager::Get().LoadFromConfig(verge);

        // 仅在内核启动成功后才启用系统代理，避免内核未启动导致断网
        if (verge.enable_system_proxy.value_or(false) &&
            clash_yuka::AppContext::Get().IsCoreRunning()) {
            const uint16_t port = clash_yuka::AppContext::Get().GetMixedPort();
            const std::string bypass = verge.system_proxy_bypass.value_or("");
            clash_yuka::core::SysProxy::Get().Enable(port, bypass);
            if (verge.enable_proxy_guard.value_or(false)) {
                const uint32_t interval =
                    static_cast<uint32_t>(verge.proxy_guard_interval.value_or(10));
                clash_yuka::core::SysProxy::Get().StartGuard(port, bypass, interval);
            }
        }

        return true;
    }

    int OnExit() override {
        clash_yuka::core::HotkeyManager::Get().UnregisterAll();
        clash_yuka::core::SysProxy::Get().StopGuard();
        // 退出时禁用系统代理，避免遗留失效代理导致无法上网
        if (clash_yuka::AppContext::Get().IsInitialized() &&
            clash_yuka::AppContext::Get().verge().enable_system_proxy.value_or(false)) {
            clash_yuka::core::SysProxy::Get().Disable();
        }
        // 停止 mihomo 内核进程
        // 服务模式下不停止服务（服务的设计目的就是后台持续运行）
        if (clash_yuka::AppContext::Get().IsInitialized()) {
            auto& ctx = clash_yuka::AppContext::Get();
            if (!ctx.verge().enable_service_mode.value_or(false)) {
                ctx.StopCore();
            }
        }
        clash_yuka::core::TrayIcon::Get().Destroy();
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(ClashYukaApp);
