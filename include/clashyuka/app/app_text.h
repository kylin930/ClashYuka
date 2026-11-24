#pragma once

#include <string>

#include <wx/string.h>
#include <wx/uilocale.h>

#include "clashyuka/app/app_state.h"
#include "clashyuka/core/result.h"

namespace clashyuka::app {

inline wxString DesktopText(const char* english, const char* chinese) {
    // The desktop controls follow the primary OS UI language.
    static const bool useChinese = [] {
        const auto languages = wxUILocale::GetPreferredUILanguages();
        return !languages.empty() &&
               wxLocaleIdent::FromTag(languages.front()).GetLanguage() == "zh";
    }();
    return wxString::FromUTF8(useChinese ? chinese : english);
}

inline wxString KernelStateText(KernelState state) {
    switch (state) {
        case KernelState::NotStarted:
            return "Not started";
        case KernelState::Starting:
            return "Starting";
        case KernelState::WaitingForApi:
            return "Waiting for API";
        case KernelState::Running:
            return "Running";
        case KernelState::Stopping:
            return "Stopping";
        case KernelState::Error:
            return "Error";
    }
    return "Unknown";
}

inline wxString ApiStateText(ApiState state) {
    switch (state) {
        case ApiState::Disconnected:
            return "Disconnected";
        case ApiState::Connecting:
            return "Connecting";
        case ApiState::Connected:
            return "Connected";
    }
    return "Unknown";
}

inline wxString ProxyModeText(ProxyMode mode) {
    switch (mode) {
        case ProxyMode::Rule:
            return "Rule";
        case ProxyMode::Global:
            return "Global";
        case ProxyMode::Direct:
            return "Direct";
    }
    return "Rule";
}

inline wxString FormatSpeed(double bytesPerSecond, bool available) {
    if (!available || bytesPerSecond <= 0.0) {
        return wxString::FromUTF8("—");
    }
    if (bytesPerSecond >= 1024.0 * 1024.0) {
        return wxString::Format("%.1f MB/s", bytesPerSecond / (1024.0 * 1024.0));
    }
    return wxString::Format("%.1f KB/s", bytesPerSecond / 1024.0);
}

inline wxString FormatVersion(const std::string& version) {
    return version.empty() ? wxString("Unknown") : wxString::FromUTF8(version);
}

// User-facing explanation for a kernel failure. Raw diagnostics stay in the
// runtime log so the UI never shows stack traces or transport internals.
inline wxString KernelErrorText(core::ErrorKind kind) {
    switch (kind) {
        case core::ErrorKind::KernelNotFound:
            return wxString::FromUTF8(
                "Mihomo executable was not found. Check the kernel path in Settings.");
        case core::ErrorKind::ConfigFailed:
            return wxString::FromUTF8(
                "The runtime configuration could not be prepared. Check the settings and try again.");
        case core::ErrorKind::ProfilePreflightRejected:
            return wxString::FromUTF8(
                "Mihomo rejected the profile configuration. Check the profile or select another one.");
        case core::ErrorKind::AuthFailed:
            return wxString::FromUTF8(
                "The Mihomo API rejected the secret. Update the secret in Settings.");
        case core::ErrorKind::Timeout:
            return wxString::FromUTF8(
                "Mihomo started but its API did not become ready in time.");
        case core::ErrorKind::ConnectionFailed:
            return wxString::FromUTF8(
                "Could not reach the Mihomo API. Check the controller address and port.");
        case core::ErrorKind::StartFailed:
            return wxString::FromUTF8("Mihomo failed to start or exited unexpectedly.");
        case core::ErrorKind::Cancelled:
            return wxString::FromUTF8("The kernel operation was cancelled.");
        case core::ErrorKind::InvalidConfig:
            return wxString::FromUTF8("The Mihomo configuration is invalid.");
        case core::ErrorKind::None:
            return wxString();
        default:
            return wxString::FromUTF8("Mihomo reported an unexpected error.");
    }
}

}  // namespace clashyuka::app
