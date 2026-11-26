// ClashYuka 配置迁移（Phase 10 Task 27）
//
// 处理 verge.yaml 弃用字段到新字段的运行时迁移。
// 在 AppContext::Init 加载 verge.yaml 后调用。
#pragma once

#include "core/config/verge_config.h"

namespace clash_yuka::core {

// 配置迁移：处理 verge.yaml 弃用字段到新字段的迁移。
class Migration {
public:
    // 执行所有迁移。返回是否有字段被修改（需要回写）。
    static bool Migrate(config::IVerge& verge);

private:
    Migration() = default;

    // auto_close_connection (bool) → break_when_proxy_change (string)
    // true → "all", false → "none"
    static bool MigrateAutoCloseConnection(config::IVerge& verge);

    // auto_log_clean (int64 minutes) → max_log_files (size_t days)
    // 旧值转天数：days = minutes / 60 / 24
    static bool MigrateAutoLogClean(config::IVerge& verge);

    // window_size_position (vector<double>) → window_size_state (WindowState)
    // [x, y, width, height] → WindowState{x, y, width, height, false, false}
    static bool MigrateWindowSizePosition(config::IVerge& verge);
};

} // namespace clash_yuka::core
