// Mods Menu 149（SLEEP「Mods Menu - In-Game Mod Settings」）UE4SS Bridge 客户端。
//
// 配置架构（3.0 起）：
//   - PBZPhotoModeCpp.settings.json      全量配置，唯一真源（core/config_json 读写）
//   - PBZPhotoModeCpp.config.json        Mods Menu 菜单子集（68 项），由本客户端与 Bridge 维护
//   - PBZModsMenuBridge/events-<name>    菜单保存事件（250ms 轮询，End=1 才读）
//
// 本类只负责菜单子集：检测 Bridge、启动时/配置变化时把子集写回 config.json、
// 轮询 events 并把菜单值合并进调用方给的 Config。全量 settings.json 的读写由
// mod_app 的 reload_config / apply_config_from_mods_menu 负责。
#pragma once

#ifdef PBZPM_WITH_UE4SS

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"

namespace pbzpm {

class ModsMenuClient {
public:
    ModsMenuClient() = default;
    ~ModsMenuClient() = default;
    ModsMenuClient(const ModsMenuClient&) = delete;
    ModsMenuClient& operator=(const ModsMenuClient&) = delete;

    // 检测并初始化。返回 false = 未安装 Mods Menu/Bridge（或 descriptor 缺失），
    // 此时本客户端完全静默，不影响模组原行为。config_changed=true 表示启动时
    // 菜单子集 JSON 比 settings.json 新，调用方需用 cfg 写回 settings.json。
    bool start(const std::string& mod_dir, Config& cfg, bool* config_changed = nullptr,
               bool settings_new = false);

    bool active() const { return active_; }
    const std::string& status() const { return status_; }

    // 引擎 Tick 每帧调用（内部 250ms 节流）。返回 true 时 out 是合并菜单值后的完整配置。
    bool poll(const Config& current, Config* out, std::vector<std::string>* warnings = nullptr);

    // 把当前 Config 的 68 项菜单子集写进 PBZPhotoModeCpp.config.json（保留未知键）。
    void write_config_json(const Config& cfg);

    // 最近一次错误（取走后清空，避免刷日志）。
    std::string take_error();

private:
    bool read_event_number(std::uint64_t* out) const;
    std::filesystem::path mod_dir_;
    std::filesystem::path menu_config_path_;
    std::filesystem::path settings_path_;
    std::filesystem::path descriptor_path_;
    std::filesystem::path bridge_dir_;
    std::filesystem::path events_path_;
    bool active_{false};
    std::uint64_t last_event_{0};
    std::chrono::steady_clock::time_point last_poll_{};
    std::string status_;
    std::string last_error_;
    bool error_reported_{false};
    mutable std::mutex write_mutex_;
    mutable std::mutex state_mutex_;
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
