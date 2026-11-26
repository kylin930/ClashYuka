// ClashYuka 应用单例锁（Phase 10 Task 28）
//
// 确保同时只有一个 ClashYuka 实例运行。
// 通过 TCP 监听本地端口实现（与 clash-nyanpasu 一致）。
// 第二个实例启动时检测到端口被占用，可通过 SendMessage 通知首个实例。
#pragma once

// Windows 头文件（通过 wxWidgets 间接引入）将 SendMessage 宏展开为
// SendMessageW/A，与 Singleton::SendMessage 方法名冲突。统一在此 undef，
// 确保所有包含此头文件的 TU 中 SendMessage 都不会被宏替换。
#ifdef SendMessage
#undef SendMessage
#endif

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace clash_yuka::core {

// 应用单例：确保同时只有一个 ClashYuka 实例运行。
class Singleton {
public:
    // 消息回调类型：收到其他实例发送的消息时在主线程调用
    using MessageHandler = std::function<void(const std::string&)>;

    static Singleton& Get();

    // 不可拷贝/移动
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    // 设置消息处理器（应在 TryLock 之前调用）。
    // 当其他实例通过 SendMessage 发来消息时，处理器会在主线程被调用。
    void SetMessageHandler(MessageHandler handler);

    // 尝试成为单例。返回 true 表示当前是唯一实例（已占用端口）。
    // 返回 false 表示已有其他实例运行（可通过 SendMessage 发送消息）。
    bool TryLock(uint16_t port);

    // 向已运行的实例发送消息（如 "show_window"）。
    // 返回是否发送成功。
    bool SendMessage(const std::string& msg, uint16_t port);

    // 释放单例锁（应用退出时调用）
    void Unlock();

private:
    Singleton() = default;
    ~Singleton();

    // 单例监听线程：接收其他实例的消息并通过 CallAfter 转发到主线程
    void ListenLoop();

    std::atomic<bool> listening_{false};
    std::thread listen_thread_;
    // 监听 socket 的 opaque handle（实际类型为 intptr_t socket fd）
    intptr_t listen_socket_ = -1;

    // 消息处理器（由主线程设置，监听线程通过 CallAfter 在主线程调用）
    MessageHandler message_handler_;
};

} // namespace clash_yuka::core
