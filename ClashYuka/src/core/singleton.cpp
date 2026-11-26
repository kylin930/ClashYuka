// ClashYuka 应用单例锁实现
#include "core/singleton.h"

// Winsock2 必须在 wx 头文件（可能间接包含 windows.h）之前引入
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// windows.h 将 SendMessage 宏展开为 SendMessageW/A，与 Singleton::SendMessage 方法冲突
#ifdef SendMessage
#undef SendMessage
#endif
#endif

#include <wx/app.h>

#include <iostream>
#include <utility>

namespace clash_yuka::core {

Singleton& Singleton::Get() {
    static Singleton instance;
    return instance;
}

Singleton::~Singleton() {
    Unlock();
}

void Singleton::SetMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

bool Singleton::TryLock(uint16_t port) {
#ifdef _WIN32
    if (listening_) return true; // 已锁定

    // 初始化 Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[Singleton] WSAStartup failed" << std::endl;
        return false;
    }

    // 创建 TCP 监听 socket
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        std::cerr << "[Singleton] socket() failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    // 允许地址重用（便于重启时快速绑定）
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // 绑定到 127.0.0.1:port
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        // 端口已被占用，说明已有实例运行
        closesocket(s);
        WSACleanup();
        return false;
    }

    if (listen(s, 5) == SOCKET_ERROR) {
        std::cerr << "[Singleton] listen() failed: " << WSAGetLastError() << std::endl;
        closesocket(s);
        WSACleanup();
        return false;
    }

    listen_socket_ = static_cast<intptr_t>(s);
    listening_ = true;
    listen_thread_ = std::thread(&Singleton::ListenLoop, this);
    return true;
#else
    // 非 Windows 平台暂未实现
    (void)port;
    return false;
#endif
}

void Singleton::ListenLoop() {
#ifdef _WIN32
    SOCKET srv = static_cast<SOCKET>(listen_socket_);

    while (listening_) {
        // 使用 select 带超时轮询，便于及时响应 listening_ = false
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(0, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) {
            // 超时或出错，继续循环检查 listening_
            continue;
        }

        SOCKET client = accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }

        // 读取消息（单次 recv，消息应较短）
        char buf[1024] = {0};
        int received = recv(client, buf, sizeof(buf) - 1, 0);
        closesocket(client);

        if (received > 0) {
            std::string msg(buf, static_cast<size_t>(received));
            // 通过 CallAfter 在主线程调用消息处理器
            if (wxTheApp) {
                wxTheApp->CallAfter([this, msg]() {
                    if (message_handler_) {
                        message_handler_(msg);
                    }
                });
            }
        }
    }
#endif
}

bool Singleton::SendMessage(const std::string& msg, uint16_t port) {
#ifdef _WIN32
    if (msg.empty()) return false;

    // 初始化 Winsock（与 TryLock 中的配对，引用计数安全）
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // 连接到 127.0.0.1:port
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return false;
    }

    int sent = send(s, msg.data(), static_cast<int>(msg.size()), 0);
    closesocket(s);
    WSACleanup();

    return sent > 0;
#else
    (void)msg;
    (void)port;
    return false;
#endif
}

void Singleton::Unlock() {
#ifdef _WIN32
    if (!listening_) return;

    listening_ = false;

    if (listen_socket_ != -1) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = -1;
    }

    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    WSACleanup();
#endif
}

} // namespace clash_yuka::core
