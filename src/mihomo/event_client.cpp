#include "clashyuka/mihomo/event_client.h"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

#include <httplib.h>

#include "clashyuka/mihomo/endpoint.h"
#include "clashyuka/mihomo/json.h"

namespace clashyuka::mihomo {
namespace {

using core::Error;
using core::ErrorKind;
using core::MakeError;

constexpr std::size_t ChannelIndex(EventChannel channel) {
    return static_cast<std::size_t>(channel);
}

void InterruptibleSleep(std::atomic<bool>& stop, int milliseconds) {
    const int step = 50;
    for (int elapsed = 0; elapsed < milliseconds && !stop.load(); elapsed += step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
    }
}

bool IsValidLogLevel(const std::string& level) {
    return level == "info" || level == "warning" || level == "error" || level == "debug";
}

}  // namespace

struct MihomoEventClient::Impl {
    struct Channel {
        EventChannel kind = EventChannel::Traffic;
        std::thread thread;
        std::atomic<bool> stop{false};
        std::atomic<bool> running{false};
        std::atomic<bool> connected{false};
        std::mutex socketMutex;
        std::shared_ptr<httplib::ws::WebSocketClient> socket;
    };

    explicit Impl(MihomoEventConfig value) : config(std::move(value)) {
        for (std::size_t i = 0; i < channels.size(); ++i) {
            channels[i] = std::make_unique<Channel>();
            channels[i]->kind = static_cast<EventChannel>(i);
        }
    }

    ~Impl() { DisconnectAll(); }

    core::Result<void> Start(EventChannel kind, std::string path,
                             std::function<void(const std::string&)> onMessage,
                             ErrorHandler onError) {
        const MihomoApiConfig& api = config.api;
        if (api.host.empty() || IsWildcardAddress(api.host)) {
            return MakeError(ErrorKind::InvalidConfig,
                             "invalid controller host '" + api.host + "'");
        }
        if (api.port <= 0 || api.port > 65535) {
            return MakeError(ErrorKind::InvalidConfig, "invalid controller port");
        }

        Channel& channel = *channels[ChannelIndex(kind)];
        if (channel.thread.joinable()) {
            // Already running: keep the existing connection.
            return {};
        }

        channel.stop.store(false);
        channel.running.store(true);
        channel.thread = std::thread([this, &channel, path = std::move(path),
                                      onMessage = std::move(onMessage),
                                      onError = std::move(onError)] {
            Run(channel, path, onMessage, onError);
        });
        return {};
    }

    void Run(Channel& channel, const std::string& path,
             const std::function<void(const std::string&)>& onMessage,
             const ErrorHandler& onError) {
        const std::string url = BuildWebSocketUrl(config.api.host, config.api.port, path);
        const httplib::Headers headers = MakeHeaders();
        int reconnectDelay = config.reconnectDelayMs;

        while (!channel.stop.load()) {
            auto socket = std::make_shared<httplib::ws::WebSocketClient>(url, headers);
            socket->set_connection_timeout(
                static_cast<time_t>(config.api.connectionTimeoutMs / 1000),
                static_cast<time_t>(config.api.connectionTimeoutMs % 1000) * 1000);
            // A short read timeout lets the loop observe stop requests. Timeouts
            // are reported separately from closed/broken connections.
            socket->set_read_timeout(1, 0);

            const auto connectResult = socket->connect();
            if (!connectResult) {
                channel.connected.store(false);
                Report(onError, ErrorKind::ConnectionFailed,
                       "WebSocket connection failed: " + url);
                if (!config.autoReconnect || channel.stop.load()) {
                    break;
                }
                InterruptibleSleep(channel.stop, reconnectDelay);
                reconnectDelay = NextDelay(reconnectDelay);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(channel.socketMutex);
                channel.socket = socket;
            }
            channel.connected.store(true);
            reconnectDelay = config.reconnectDelayMs;

            std::string message;
            bool closed = false;
            while (!channel.stop.load()) {
                const auto readResult = socket->read(message);
                if (readResult == httplib::ws::ReadResult::Text) {
                    try {
                        onMessage(message);
                    } catch (...) {
                        // A malformed message must never tear down the worker.
                        Report(onError, ErrorKind::InvalidJson,
                               "failed to process WebSocket message");
                    }
                } else if (readResult == httplib::ws::ReadResult::Timeout ||
                           readResult == httplib::ws::ReadResult::Binary) {
                    continue;
                } else {
                    closed = true;
                    break;
                }
            }

            channel.connected.store(false);
            socket->close();
            {
                std::lock_guard<std::mutex> lock(channel.socketMutex);
                if (channel.socket == socket) {
                    channel.socket.reset();
                }
            }

            if (channel.stop.load()) {
                break;
            }

            Report(onError, ErrorKind::ConnectionFailed,
                   closed ? "WebSocket connection closed" : "WebSocket read failed");
            if (!config.autoReconnect) {
                break;
            }
            InterruptibleSleep(channel.stop, reconnectDelay);
            reconnectDelay = NextDelay(reconnectDelay);
        }

        channel.connected.store(false);
        channel.running.store(false);
    }

    int NextDelay(int current) const {
        const int limit = config.maxReconnectDelayMs > 0 ? config.maxReconnectDelayMs
                                                         : config.reconnectDelayMs;
        return current >= limit ? limit : current * 2;
    }

    httplib::Headers MakeHeaders() const {
        httplib::Headers headers;
        if (!config.api.secret.empty()) {
            headers.emplace("Authorization", "Bearer " + config.api.secret);
        }
        return headers;
    }

    static void Report(const ErrorHandler& handler, ErrorKind kind, std::string message) {
        if (handler) {
            handler(Error{kind, std::move(message), 0});
        }
    }

    void Disconnect(EventChannel kind) {
        Channel& channel = *channels[ChannelIndex(kind)];
        if (!channel.thread.joinable()) {
            return;
        }

        channel.stop.store(true);
        {
            std::lock_guard<std::mutex> lock(channel.socketMutex);
            if (channel.socket) {
                channel.socket->close();
            }
        }
        channel.thread.join();
        channel.running.store(false);
        channel.connected.store(false);
    }

    void DisconnectAll() {
        for (auto& channel : channels) {
            Disconnect(channel->kind);
        }
    }

    bool IsConnected(EventChannel kind) const {
        return channels[ChannelIndex(kind)]->connected.load();
    }

    bool IsRunning(EventChannel kind) const {
        return channels[ChannelIndex(kind)]->running.load();
    }

    MihomoEventConfig config;
    std::array<std::unique_ptr<Channel>, 4> channels;
};

MihomoEventClient::MihomoEventClient(MihomoEventConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MihomoEventClient::~MihomoEventClient() = default;

core::Result<void> MihomoEventClient::ConnectTraffic(TrafficHandler handler,
                                                     ErrorHandler onError) {
    return impl_->Start(
        EventChannel::Traffic, "/traffic",
        [handler = std::move(handler), onError](const std::string& message) {
            const auto json = nlohmann::json::parse(message, nullptr, false);
            if (json.is_discarded()) {
                return;
            }
            auto parsed = ParseTraffic(json);
            if (!parsed) {
                return;
            }
            if (handler) {
                handler(parsed.value());
            }
        },
        std::move(onError));
}

core::Result<void> MihomoEventClient::ConnectLogs(LogHandler handler, std::string level,
                                                  ErrorHandler onError) {
    if (!IsValidLogLevel(level)) {
        level = "info";
    }
    return impl_->Start(
        EventChannel::Logs, "/logs?level=" + level,
        [handler = std::move(handler)](const std::string& message) {
            constexpr std::size_t kMaxLogFrameBytes = 64 * 1024;
            if (message.size() > kMaxLogFrameBytes) {
                return;
            }
            const auto json = nlohmann::json::parse(message, nullptr, false);
            if (json.is_discarded()) {
                return;
            }
            auto parsed = ParseLog(json);
            if (!parsed) {
                return;
            }
            if (handler) {
                handler(parsed.value());
            }
        },
        std::move(onError));
}

core::Result<void> MihomoEventClient::ConnectConnections(ConnectionHandler handler,
                                                         int intervalMs,
                                                         ErrorHandler onError) {
    if (intervalMs < 100) {
        intervalMs = 100;
    }
    return impl_->Start(
        EventChannel::Connections, "/connections?interval=" + std::to_string(intervalMs),
        [handler = std::move(handler)](const std::string& message) {
            const auto json = nlohmann::json::parse(message, nullptr, false);
            if (json.is_discarded()) {
                return;
            }
            auto parsed = ParseConnections(json);
            if (!parsed) {
                return;
            }
            if (handler) {
                handler(parsed.value());
            }
        },
        std::move(onError));
}

core::Result<void> MihomoEventClient::ConnectMemory(MemoryHandler handler,
                                                    ErrorHandler onError) {
    return impl_->Start(
        EventChannel::Memory, "/memory",
        [handler = std::move(handler)](const std::string& message) {
            const auto json = nlohmann::json::parse(message, nullptr, false);
            if (json.is_discarded()) {
                return;
            }
            auto parsed = ParseMemory(json);
            if (!parsed) {
                return;
            }
            if (handler) {
                handler(parsed.value());
            }
        },
        std::move(onError));
}

void MihomoEventClient::Disconnect(EventChannel channel) {
    impl_->Disconnect(channel);
}

void MihomoEventClient::Disconnect() {
    impl_->DisconnectAll();
}

bool MihomoEventClient::IsConnected(EventChannel channel) const {
    return impl_->IsConnected(channel);
}

bool MihomoEventClient::IsRunning(EventChannel channel) const {
    return impl_->IsRunning(channel);
}

}  // namespace clashyuka::mihomo
