#pragma once

#include <string>

#include "core/config.h"
#include "core/engine_port.h"
#include "core/input_logic.h"

namespace pbzpm {

// 摄影模式状态机。所有与游戏引擎的交互都经过 EnginePort，
// 因此 enter/leave/tick 的顺序、回滚、幂等性都能在没有游戏的情况下测出来。
class PhotoMode {
public:
    PhotoMode(EnginePort& engine, Config cfg);

    // 进入失败会自动回滚已做的改动，并把原因写进 last_error()
    bool enter();
    void leave(const char* reason = "user");
    void tick(const Intent& intent, double dt);

    bool active() const { return active_; }
    bool frozen() const { return frozen_; }
    bool menu_visible() const { return menu_visible_; }
    void set_menu_visible(bool visible) { menu_visible_ = visible; }
    void toggle_menu() { menu_visible_ = !menu_visible_; }

    const CameraPose& pose() const { return pose_; }
    double fov() const { return fov_; }
    // 夹紧到 [fov_min, fov_max]；返回是否真的改变了
    bool set_fov(double fov);
    void toggle_freeze();
    // 应用当前配置（配置文件热重载后用）
    void set_config(const Config& cfg) { cfg_ = cfg; }
    const Config& config() const { return cfg_; }

    // ---- 图形菜单的实时开关：更新配置 + 立即作用于引擎，退出时按最终状态还原 ----
    // 与 enter() 里的同一套 *_hidden_ 记账联动，保证 leave() 的回滚仍然正确
    void set_hide_player_live(bool visible);
    void set_block_game_input_live(bool blocked);
    void set_hide_game_hud_live(bool hidden);

    const std::string& last_error() const { return last_error_; }
    bool pov_fallback_used() const { return pov_fallback_used_; }
    double reassert_interval() const { return 1.0; }

private:
    bool apply_pose_if_dirty();
    void log(LogLevel level, const std::string& message);

    EnginePort& engine_;
    Config cfg_;

    CameraPose pose_{};
    double fov_{90.0};
    // 进入摄影模式时的原始位姿/FOV，R 键回到这里
    CameraPose reset_pose_{};
    double reset_fov_{90.0};
    bool has_reset_pose_{false};

    bool active_{false};
    bool frozen_{false};
    bool menu_visible_{true};

    // 进入时成功执行了哪些步骤 —— leave 严格按这些标记回滚，不多做也不少做
    bool spawned_{false};
    bool view_attached_{false};
    bool ignore_move_set_{false};
    bool game_input_blocked_{false};
    bool player_hidden_{false};
    bool hud_hidden_{false};
    bool postfx_saved_{false};

    double prev_dilation_{1.0};
    // 冻结时实际使用的模式：配置在冻结中途被热重载改掉时，解冻仍按进入时的模式还原
    bool frozen_with_pause_{false};
    double reassert_accum_{0.0};
    double hud_retry_accum_{0.0};
    bool pose_dirty_{false};
    bool pov_fallback_used_{false};
    std::string last_error_{};
};

}  // namespace pbzpm
