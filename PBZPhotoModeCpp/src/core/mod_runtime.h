// mod_app 的引擎侧编排（进/退、每帧 tick 分工、配置应用与实时开关差异）抽到这里，
// 依赖全部通过构造注入（EnginePort / PhotoMode / Config / KeyMap 均为引用），
// 因此可以脱离 UE4SS、用 FakeEngine 驱动完整的「进模式 → tick → 改配置 → 退出」路径。
// 锁与真实输入层仍留在 mod_app：本类只在调用方已持有互斥量时使用。
#pragma once

#include "core/config.h"
#include "core/engine_port.h"
#include "core/input_logic.h"
#include "core/photo_mode.h"

namespace pbzpm {

// 切换结果：调用方（mod_app）据此决定是否清空输入状态、重置退避、转储引擎状态。
enum class ToggleOutcome { Entered, Left, EnterFailed };

// PBZFM 菜单三个「有实时通道」的开关差异。其余字段只更新配置，等下一次
// 进入 / 下一次冻结 / 每秒重试路径按新值执行（即「下一帧生效」）。
struct LiveTogglePlan {
    bool hide_player{false};
    bool block_game_input{false};
    bool hide_game_hud{false};

    bool any() const { return hide_player || block_game_input || hide_game_hud; }
};

// 纯函数：只比较三个实时开关，非实时字段的变化不产生任何动作。
LiveTogglePlan plan_live_toggles(const Config& previous, const Config& next);

class ModRuntime {
public:
    // 所有参数都按引用注入，调用方保证生命周期长于本对象：
    //   engine  —— 引擎端口（测试用 FakeEngine）
    //   mode    —— 摄影模式状态机
    //   cfg     —— mod_app 持有的当前配置（apply_config 会原地更新）
    //   keys    —— mod_app 持有的键位映射（apply_config 会重建）
    ModRuntime(EnginePort& engine, PhotoMode& mode, Config& cfg, KeyMap& keys);

    // 用 cfg_ 重新同步 PhotoMode（配置文件热重载后调用）。
    void sync_mode_config();

    // 应用一份新配置：原地更新 cfg_/keys_、同步 PhotoMode；若当前处于摄影模式，
    // 按 plan_live_toggles 的差异当帧下发三个实时开关。
    void apply_config(const Config& incoming);

    // 快捷键/菜单切换。进入失败时由本类打 Error 日志（与 mod_app 原行为一致）。
    ToggleOutcome toggle();

    // 每帧分发的「非 toggle」部分：未激活且无待恢复项时完全不碰引擎；
    // 退出后仍有未还原项时由 PhotoMode::tick 驱动退避重试。
    void dispatch(const Intent& intent, double dt);

    PhotoMode& mode() { return mode_; }
    const PhotoMode& mode() const { return mode_; }
    const Config& config() const { return cfg_; }
    const KeyMap& keys() const { return keys_; }

private:
    EnginePort& engine_;
    PhotoMode& mode_;
    Config& cfg_;
    KeyMap& keys_;
};

}  // namespace pbzpm
