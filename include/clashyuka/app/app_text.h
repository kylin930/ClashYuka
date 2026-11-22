#pragma once

#include <string>

#include <wx/string.h>

#include "clashyuka/app/app_state.h"

namespace clashyuka::app {

inline wxString KernelStateText(KernelState state) {
    switch (state) {
        case KernelState::NotStarted:
            return "Not started";
        case KernelState::Starting:
            return "Starting";
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

}  // namespace clashyuka::app
