#pragma once

#include <functional>
#include <memory>
#include <string>

#include "clashyuka/core/result.h"
#include "clashyuka/mihomo/api_client.h"
#include "clashyuka/mihomo/types.h"

namespace clashyuka::mihomo {

enum class EventChannel { Traffic, Logs, Connections, Memory };

struct MihomoEventConfig {
    MihomoApiConfig api;
    bool autoReconnect = true;
    int reconnectDelayMs = 1000;
    int maxReconnectDelayMs = 10000;
};

// Streaming client for the Mihomo WebSocket endpoints.
//
// Connections are created on demand: nothing is connected until Connect*() is
// called. Callbacks always run on the channel's worker thread; GUI code must
// marshal them onto the wxWidgets main thread (wxTheApp->CallAfter / wxQueueEvent).
// The destructor disconnects and joins every worker thread.
class MihomoEventClient {
public:
    using TrafficHandler = std::function<void(const TrafficStatistics&)>;
    using LogHandler = std::function<void(const MihomoLogEntry&)>;
    using ConnectionHandler = std::function<void(const ConnectionsSnapshot&)>;
    using MemoryHandler = std::function<void(const MihomoMemoryInfo&)>;
    using ErrorHandler = std::function<void(const core::Error&)>;

    explicit MihomoEventClient(MihomoEventConfig config);
    ~MihomoEventClient();

    MihomoEventClient(const MihomoEventClient&) = delete;
    MihomoEventClient& operator=(const MihomoEventClient&) = delete;

    core::Result<void> ConnectTraffic(TrafficHandler handler, ErrorHandler onError = {});
    core::Result<void> ConnectLogs(LogHandler handler, std::string level = "info",
                                   ErrorHandler onError = {});
    core::Result<void> ConnectConnections(ConnectionHandler handler, int intervalMs = 1000,
                                          ErrorHandler onError = {});
    core::Result<void> ConnectMemory(MemoryHandler handler, ErrorHandler onError = {});

    void Disconnect(EventChannel channel);
    void Disconnect();

    bool IsConnected(EventChannel channel) const;
    bool IsRunning(EventChannel channel) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clashyuka::mihomo
