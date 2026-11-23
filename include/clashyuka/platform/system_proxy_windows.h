#pragma once

#include <memory>
#include <string>

#include "clashyuka/platform/system_proxy.h"

namespace clashyuka::platform {

// Windows 下系统代理保存在当前用户的 Internet Settings 注册表项中。
// 该测试入口允许使用独立子键，避免自动化测试改动真实系统代理。
std::unique_ptr<SystemProxyController> CreateWindowsSystemProxyController(
    const std::wstring& subKey = {});

// "http=host:port;https=host:port"
std::string BuildWindowsProxyServer(const std::string& host, int port);

}  // namespace clashyuka::platform
