#include "clashyuka/app/app_state.h"

#include <algorithm>
#include <utility>

namespace clashyuka::app {

void AppState::AddObserver(AppStateObserver* observer) {
    if (observer == nullptr) {
        return;
    }
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void AppState::RemoveObserver(AppStateObserver* observer) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                     observers_.end());
}

void AppState::Notify() {
    // Observers may add (new pages) or remove observers while being notified,
    // so iterate over a snapshot instead of the live vector.
    const std::vector<AppStateObserver*> observers = observers_;
    for (AppStateObserver* observer : observers) {
        if (observer == nullptr) {
            continue;
        }
        if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
            continue;  // removed during this notification
        }
        observer->OnAppStateChanged(snapshot_);
    }
}

void AppState::SetKernelState(KernelState state) {
    if (snapshot_.kernel == state) {
        return;
    }
    snapshot_.kernel = state;
    Notify();
}

void AppState::SetApiState(ApiState state) {
    if (snapshot_.api == state) {
        return;
    }
    snapshot_.api = state;
    Notify();
}

void AppState::SetCurrentPage(PageId page) {
    if (snapshot_.currentPage == page) {
        return;
    }
    snapshot_.currentPage = page;
    Notify();
}

void AppState::SetTheme(ui::ColorTheme theme) {
    if (snapshot_.theme == theme) {
        return;
    }
    snapshot_.theme = theme;
    Notify();
}

void AppState::SetDark(bool dark) {
    if (snapshot_.dark == dark) {
        return;
    }
    snapshot_.dark = dark;
    Notify();
}

void AppState::SetAnimationsEnabled(bool enabled) {
    if (snapshot_.animationsEnabled == enabled) {
        return;
    }
    snapshot_.animationsEnabled = enabled;
    Notify();
}

void AppState::SetReducedMotion(bool reduced) {
    if (snapshot_.reducedMotion == reduced) {
        return;
    }
    snapshot_.reducedMotion = reduced;
    Notify();
}

void AppState::SetProxyMode(ProxyMode mode) {
    if (snapshot_.proxyMode == mode) {
        return;
    }
    snapshot_.proxyMode = mode;
    Notify();
}

void AppState::SetProfile(std::string name, bool loaded) {
    if (snapshot_.profileName == name && snapshot_.profileLoaded == loaded) {
        return;
    }
    snapshot_.profileName = std::move(name);
    snapshot_.profileLoaded = loaded;
    Notify();
}

void AppState::SetMihomoVersion(std::string version) {
    if (snapshot_.mihomoVersion == version) {
        return;
    }
    snapshot_.mihomoVersion = std::move(version);
    Notify();
}

void AppState::SetTraffic(double uploadBytesPerSecond, double downloadBytesPerSecond,
                          bool available) {
    if (snapshot_.trafficAvailable == available &&
        snapshot_.uploadBytesPerSecond == uploadBytesPerSecond &&
        snapshot_.downloadBytesPerSecond == downloadBytesPerSecond) {
        return;
    }
    snapshot_.uploadBytesPerSecond = uploadBytesPerSecond;
    snapshot_.downloadBytesPerSecond = downloadBytesPerSecond;
    snapshot_.trafficAvailable = available;
    Notify();
}

void AppState::SetConnectionCount(int count) {
    if (snapshot_.connectionCount == count) {
        return;
    }
    snapshot_.connectionCount = count;
    Notify();
}

void AppState::SetKernelPath(std::string path) {
    if (snapshot_.kernelPath == path) {
        return;
    }
    snapshot_.kernelPath = std::move(path);
    Notify();
}

void AppState::SetApiAddress(std::string address) {
    if (snapshot_.apiAddress == address) {
        return;
    }
    snapshot_.apiAddress = std::move(address);
    Notify();
}

}  // namespace clashyuka::app
