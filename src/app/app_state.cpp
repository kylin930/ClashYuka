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

wxString ProfilesSnapshot::NameFor(const std::string& id, const wxString& fallback) const {
    const auto row = std::ranges::find(rows, id, &ProfileRow::id);
    return row == rows.end() ? fallback : wxString::FromUTF8(row->name);
}

void AppState::SetProfiles(ProfilesSnapshot profiles) {
    if (snapshot_.profiles && *snapshot_.profiles == profiles) {
        return;
    }
    snapshot_.profiles = std::make_shared<const ProfilesSnapshot>(std::move(profiles));
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

bool LiveStreamsSnapshot::CanClose(std::uint64_t expectedGeneration, std::string_view id) const {
    return running && !switching && generation == expectedGeneration &&
           connectionsStatus == runtime::StreamAvailability::Available && connectionsFresh &&
           !pendingClose && connections && !id.empty() &&
           std::ranges::find(connections->connections, id, &mihomo::ConnectionInfo::id) !=
               connections->connections.end();
}

void AppState::UpdateLiveStreams(const runtime::KernelSnapshot& kernel) {
    auto& live = snapshot_.live;
    if (kernel.generation < live.generation) {
        return;
    }
    const bool running = kernel.phase == runtime::KernelPhase::Running;
    const bool newGeneration = kernel.generation != live.generation;
    if (!newGeneration && live.running == running && live.switching == kernel.profileSwitchInProgress &&
        live.logsStatus == kernel.logStream.availability &&
        live.connectionsStatus == kernel.connectionStream.availability) {
        return;
    }
    if (newGeneration) {
        live.logs.reset();
    }
    if (newGeneration || !running) {
        live.connections.reset();
        live.pendingClose.reset();
        snapshot_.connectionCount = 0;
    }
    if (newGeneration || !running ||
        kernel.connectionStream.availability != runtime::StreamAvailability::Available) {
        live.connectionsFresh = false;
    }
    live.generation = kernel.generation;
    live.running = running;
    live.switching = kernel.profileSwitchInProgress;
    live.logsStatus = kernel.logStream.availability;
    live.connectionsStatus = kernel.connectionStream.availability;
    Notify();
}

void AppState::ResetLiveStreams() {
    snapshot_.live = {};
    snapshot_.connectionCount = 0;
    redactions_.clear();
    Notify();
}

void AppState::PublishConnections(std::uint64_t generation,
                                  const mihomo::ConnectionsSnapshot& snapshot) {
    auto& live = snapshot_.live;
    if (!live.running || live.generation != generation ||
        live.connectionsStatus != runtime::StreamAvailability::Available) {
        return;
    }
    if (live.pendingClose && live.pendingClose->awaitingSnapshot) {
        live.pendingClose.reset();
    }
    ScrubConnections(snapshot, true);
}

void AppState::ScrubConnections(const mihomo::ConnectionsSnapshot& snapshot, bool fresh) {
    auto& live = snapshot_.live;
    auto next = std::make_shared<mihomo::ConnectionsSnapshot>(snapshot);
    for (auto& row : next->connections) {
        auto& m = row.metadata;
        for (auto* field : {&m.network, &m.type, &m.sourceIP, &m.destinationIP, &m.host,
                            &m.dnsMode, &m.processPath, &row.rule, &row.rulePayload}) {
            *field = Redact(std::move(*field));
        }
        for (auto* chain : {&row.chains, &row.providerChains}) {
            for (auto& hop : *chain) {
                hop = Redact(std::move(hop));
            }
        }
    }
    live.connections = std::move(next);
    live.connectionsFresh = fresh;
    snapshot_.connectionCount = static_cast<int>(
        std::min<std::size_t>(live.connections->connections.size(), 1000000));
    Notify();
}

namespace {
constexpr std::size_t kLogLimit = 500;
constexpr std::size_t kLogTypeBytes = 32;
constexpr std::size_t kLogPayloadBytes = 4096;

std::string LimitLogField(std::string field, std::size_t bytes) {
    if (field.size() > bytes) {
        while ((static_cast<unsigned char>(field[bytes]) & 0xC0) == 0x80) {
            --bytes;
        }
        field.resize(bytes);
    }
    return field;
}
}  // namespace

void AppState::AppendLogs(std::uint64_t generation, const std::vector<mihomo::MihomoLogEntry>& batch) {
    auto& live = snapshot_.live;
    if (!live.running || live.generation != generation || batch.empty()) {
        return;
    }
    auto next = std::make_shared<std::vector<mihomo::MihomoLogEntry>>();
    const auto incoming = std::min(batch.size(), kLogLimit);
    const auto retained = live.logs ? std::min(live.logs->size(), kLogLimit - incoming) : 0;
    next->reserve(retained + incoming);
    if (retained != 0) {
        next->insert(next->end(), live.logs->end() - retained, live.logs->end());
    }
    for (auto it = batch.end() - incoming; it != batch.end(); ++it) {
        next->push_back({LimitLogField(Redact(it->type), kLogTypeBytes),
                         LimitLogField(Redact(it->payload), kLogPayloadBytes)});
    }
    live.logs = std::move(next);
    Notify();
}

void AppState::AppendAppLog(std::string type, std::string payload) {
    auto& live = snapshot_.live;
    auto next = std::make_shared<std::vector<mihomo::MihomoLogEntry>>();
    const auto retained = live.logs ? std::min(live.logs->size(), kLogLimit - 1) : 0;
    next->reserve(retained + 1);
    if (retained != 0) {
        next->insert(next->end(), live.logs->end() - retained, live.logs->end());
    }
    next->push_back({LimitLogField(Redact(std::move(type)), kLogTypeBytes),
                     LimitLogField(Redact(std::move(payload)), kLogPayloadBytes)});
    live.logs = std::move(next);
    Notify();
}

void AppState::ClearLogs() {
    if (snapshot_.live.logs) {
        snapshot_.live.logs.reset();
        Notify();
    }
}

bool AppState::RememberRedaction(std::string value) {
    if (value.empty() || std::ranges::find(redactions_, value) != redactions_.end()) {
        return false;
    }
    const auto where = std::ranges::find_if(redactions_, [&](const auto& known) {
        return known.size() < value.size();
    });
    redactions_.insert(where, std::move(value));

    auto& live = snapshot_.live;
    bool changed = false;
    if (live.logs) {
        auto logs = std::make_shared<std::vector<mihomo::MihomoLogEntry>>(*live.logs);
        // A field clipped by either layer may contain an incomplete match for
        // the newly known value. UTF-8 clipping can back up by up to three bytes.
        changed = std::erase_if(*logs, [](const auto& entry) {
            return entry.type.size() >= kLogTypeBytes - 3 ||
                   entry.payload.size() >= kLogPayloadBytes - 3;
        }) != 0;
        for (auto& entry : *logs) {
            auto type = LimitLogField(Redact(entry.type), kLogTypeBytes);
            auto payload = LimitLogField(Redact(entry.payload), kLogPayloadBytes);
            changed |= type != entry.type || payload != entry.payload;
            entry = {std::move(type), std::move(payload)};
        }
        if (changed) {
            live.logs = logs->empty() ? nullptr : std::move(logs);
        }
    }
    if (live.connections) {
        const bool fresh = live.connectionsFresh;
        // Re-scrub the latest snapshot without treating retained rows as fresh.
        const auto previous = live.connections;
        ScrubConnections(*previous, fresh);
    } else if (changed) {
        Notify();
    }
    return true;
}

std::string AppState::Redact(std::string text) const {
    // Find matches in the original text so overlapping values are covered too,
    // and replacement text itself is never matched again.
    std::string clean;
    std::size_t copied = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        std::size_t start = std::string::npos;
        std::size_t end = 0;
        for (const auto& value : redactions_) {
            const auto match = text.find(value, pos);
            if (match < start) {
                start = match;
                end = match + value.size();
            }
        }
        if (start == std::string::npos) {
            break;
        }
        if (start >= copied) {
            clean.append(text, copied, start - copied);
            clean += "[redacted]";
        }
        copied = std::max(copied, end);
        pos = start + 1;
    }
    if (copied == 0) {
        return text;
    }
    clean.append(text, copied);
    return clean;
}

bool AppState::BeginConnectionClose(PendingConnectionClose request) {
    auto& live = snapshot_.live;
    if (!live.CanClose(request.generation, request.connectionId)) {
        return false;
    }
    live.pendingClose = std::move(request);
    Notify();
    return true;
}

bool AppState::FinishConnectionClose(const runtime::ConnectionCloseResult& result) {
    auto& live = snapshot_.live;
    if (!live.running || live.generation != result.generation || !live.pendingClose ||
        live.pendingClose->requestId != result.requestId ||
        live.pendingClose->generation != result.generation ||
        live.pendingClose->connectionId != result.connectionId || live.pendingClose->awaitingSnapshot) {
        return false;
    }
    if (result.error) {
        live.pendingClose.reset();
    } else {
        live.pendingClose->awaitingSnapshot = true;
    }
    Notify();
    return true;
}

void AppState::SetRules(bool available, runtime::RulesSnapshot rules) {
    if (!available) {
        rules = {};
    }
    if (snapshot_.rulesAvailable == available && snapshot_.rules == rules) {
        return;
    }
    snapshot_.rulesAvailable = available;
    snapshot_.rules = std::move(rules);
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

void AppState::SetKernelError(std::string error) {
    if (snapshot_.kernelError == error) {
        return;
    }
    snapshot_.kernelError = std::move(error);
    Notify();
}

void AppState::SetProxyCatalog(std::shared_ptr<const runtime::ProxyCatalog> catalog) {
    if (snapshot_.proxyCatalog == catalog) {
        return;
    }
    snapshot_.proxyCatalog = std::move(catalog);
    Notify();
}

void AppState::SetKernelUpdate(std::shared_ptr<const runtime::KernelUpdateSnapshot> update) {
    if (snapshot_.kernelUpdate == update) {
        return;
    }
    snapshot_.kernelUpdate = std::move(update);
    Notify();
}

void AppState::SetAppUpdate(std::shared_ptr<const runtime::AppUpdateSnapshot> update) {
    snapshot_.appUpdate = std::move(update);
    Notify();
}

void AppState::SetSystemProxy(bool enabled, bool active, std::string address,
                              std::string status, bool retryAvailable) {
    if (snapshot_.systemProxyEnabled == enabled && snapshot_.systemProxyActive == active &&
        snapshot_.systemProxyAddress == address && snapshot_.systemProxyStatus == status &&
        snapshot_.systemProxyRetryAvailable == retryAvailable) {
        return;
    }
    snapshot_.systemProxyEnabled = enabled;
    snapshot_.systemProxyActive = active;
    snapshot_.systemProxyAddress = std::move(address);
    snapshot_.systemProxyStatus = std::move(status);
    snapshot_.systemProxyRetryAvailable = retryAvailable;
    Notify();
}

}  // namespace clashyuka::app
