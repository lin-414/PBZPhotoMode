#include "core/mod_runtime.h"

namespace pbzpm {

LiveTogglePlan plan_live_toggles(const Config& previous, const Config& next) {
    LiveTogglePlan plan;
    plan.hide_player = previous.hide_player != next.hide_player;
    plan.block_game_input = previous.block_game_input != next.block_game_input;
    plan.hide_game_hud = previous.hide_game_hud != next.hide_game_hud;
    plan.camera_backend = previous.takeover_current_camera != next.takeover_current_camera;
    return plan;
}

ModRuntime::ModRuntime(EnginePort& engine, PhotoMode& mode, Config& cfg, KeyMap& keys)
    : engine_(engine), mode_(mode), cfg_(cfg), keys_(keys) {}

void ModRuntime::apply_config(const Config& incoming) {
    // 三个实时开关先记旧值：改动的就地生效，未改动的绝不重复下发
    const LiveTogglePlan plan = plan_live_toggles(cfg_, incoming);
    cfg_ = incoming;
    keys_ = build_keymap(cfg_, nullptr);
    mode_.set_config(cfg_);

    if (!mode_.active()) return;
    // 相机后端换轨放在最前：它会重建相机对象，其它实时开关不受影响。
    // 未激活时上面已返回 —— 配置已记下，下次 enter() 直接按新后端建立。
    if (plan.camera_backend) mode_.set_camera_backend_live(cfg_.takeover_current_camera);
    // 下发顺序与 mod_app 原有实现一致：角色可见性 → 输入屏蔽 → HUD
    if (plan.hide_player) mode_.set_hide_player_live(cfg_.hide_player);
    if (plan.block_game_input) mode_.set_block_game_input_live(cfg_.block_game_input);
    if (plan.hide_game_hud) mode_.set_hide_game_hud_live(cfg_.hide_game_hud);
}

ToggleOutcome ModRuntime::toggle() {
    if (mode_.active()) {
        mode_.leave("hotkey");
        return ToggleOutcome::Left;
    }
    if (!mode_.enter()) {
        engine_.log(LogLevel::Error, "进入摄影模式失败：" + mode_.last_error());
        return ToggleOutcome::EnterFailed;
    }
    return ToggleOutcome::Entered;
}

void ModRuntime::dispatch(const Intent& intent, double dt) {
    if (!mode_.active() && !mode_.recovery_pending()) return;
    mode_.tick(intent, dt);
}

}  // namespace pbzpm
