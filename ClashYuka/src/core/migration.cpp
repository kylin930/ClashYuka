// ClashYuka 配置迁移实现
#include "core/migration.h"

#include <string>

namespace clash_yuka::core {

bool Migration::Migrate(config::IVerge& verge) {
    bool changed = false;
    changed |= MigrateAutoCloseConnection(verge);
    changed |= MigrateAutoLogClean(verge);
    changed |= MigrateWindowSizePosition(verge);
    return changed;
}

bool Migration::MigrateAutoCloseConnection(config::IVerge& verge) {
    // 旧字段有值且新字段无值时迁移
    if (verge.auto_close_connection.has_value() &&
        !verge.break_when_proxy_change.has_value()) {
        verge.break_when_proxy_change =
            *verge.auto_close_connection ? std::string("all") : std::string("none");
        verge.auto_close_connection.reset();
        return true;
    }
    return false;
}

bool Migration::MigrateAutoLogClean(config::IVerge& verge) {
    // 旧字段有值且新字段无值时迁移
    if (verge.auto_log_clean.has_value() &&
        !verge.max_log_files.has_value()) {
        int64_t minutes = *verge.auto_log_clean;
        // 分钟数转天数：days = minutes / 60 / 24
        size_t days = static_cast<size_t>(minutes / 60 / 24);
        if (days == 0) days = 1; // 至少保留 1 天
        verge.max_log_files = days;
        verge.auto_log_clean.reset();
        return true;
    }
    return false;
}

bool Migration::MigrateWindowSizePosition(config::IVerge& verge) {
    // 旧字段有值且新字段无值时迁移
    if (verge.window_size_position.has_value() &&
        !verge.window_size_state.has_value()) {
        const auto& pos = *verge.window_size_position;
        if (pos.size() >= 4) {
            config::WindowState ws;
            // [x, y, width, height]
            ws.x = static_cast<int32_t>(pos[0]);
            ws.y = static_cast<int32_t>(pos[1]);
            ws.width = static_cast<uint32_t>(pos[2]);
            ws.height = static_cast<uint32_t>(pos[3]);
            ws.maximized = false;
            ws.fullscreen = false;
            verge.window_size_state = ws;
            verge.window_size_position.reset();
            return true;
        }
    }
    return false;
}

} // namespace clash_yuka::core
