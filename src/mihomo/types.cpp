#include "clashyuka/mihomo/types.h"

namespace clashyuka::mihomo {

const ProxyInfo* ProxyCollection::Find(std::string_view name) const {
    for (const auto& proxy : proxies) {
        if (proxy.name == name) {
            return &proxy;
        }
    }
    return nullptr;
}

}  // namespace clashyuka::mihomo
