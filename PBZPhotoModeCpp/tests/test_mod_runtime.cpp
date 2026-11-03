// ModRuntime 集成测试：用 FakeEngine 驱动 mod_app 的引擎侧编排。
// 覆盖「进模式 → tick → 改配置 → 退出」完整路径，以及进入被拒绝、实时开关写入失败、
// 退出恢复失败（含退避重试上限）等失败路径。锁顺序本身仍需静态审查/实机验证：
// 这里用外部互斥量模拟 mod_app 的 mutex_，只验证同一临界区内的调用序列与状态一致性。
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "core/mod_runtime.h"
#include "core/photo_mode.h"
#include "tests/fake_engine.h"
#include "tests/test_harness.h"

namespace {

using namespace pbzpm;
using namespace pbzpm::test;  // FakeEngine
using namespace pbzpm_test;   // CHECK / section 等断言夹具

struct Fixture {
    FakeEngine engine;
    Config cfg;
    KeyMap keys;
    PhotoMode mode;
    ModRuntime runtime;

    explicit Fixture(const Config& initial = Config{})
        : cfg(initial), keys(build_keymap(cfg, nullptr)), mode(engine, cfg), runtime(engine, mode, cfg, keys) {}
};

double pose_distance(const CameraPose& a, const CameraPose& b) {
    const double dx = a.loc.x - b.loc.x;
    const double dy = a.loc.y - b.loc.y;
    const double dz = a.loc.z - b.loc.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- 纯函数：实时开关差异 ----
void test_live_toggle_plan() {
    section("ModRuntime 实时开关差异");
    Config base{};
    LiveTogglePlan plan = plan_live_toggles(base, base);
    CHECK(!plan.hide_player);
    CHECK(!plan.block_game_input);
    CHECK(!plan.hide_game_hud);
    CHECK(!plan.any());

    Config next = base;
    next.hide_player = !base.hide_player;
    plan = plan_live_toggles(base, next);
    CHECK(plan.hide_player);
    CHECK(!plan.block_game_input);
    CHECK(!plan.hide_game_hud);
    CHECK(plan.any());

    next.block_game_input = !base.block_game_input;
    next.hide_game_hud = !base.hide_game_hud;
    plan = plan_live_toggles(base, next);
    CHECK(plan.hide_player);
    CHECK(plan.block_game_input);
    CHECK(plan.hide_game_hud);

    // 非实时字段（例如 FOV / 移动速度）不应产生任何即时下发
    Config other = base;
    other.fov_default = 80.0;
    other.move_speed = 800.0;
    other.pad_deadzone = 0.3;
    CHECK(!plan_live_toggles(base, other).any());
}

// ---- 完整集成路径 ----
void test_enter_tick_config_leave() {
    section("ModRuntime 集成：进模式 → tick → 改配置 → 退出");
    Fixture f;

    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    CHECK(f.mode.active());
    CHECK(f.engine.has("spawn_camera"));
    CHECK(f.engine.has("attach_view"));
    CHECK(f.engine.player_hidden == false);  // 默认不隐藏角色

    // tick：前进 + FOV 步进都必须真的作用到引擎
    const CameraPose before = f.mode.pose();
    Intent move{};
    move.move_forward = 1.0;
    move.fov_nudge = 1;
    f.engine.reset_calls();
    f.runtime.dispatch(move, 0.1);
    CHECK(f.engine.has("apply_pose"));
    CHECK(f.engine.has("apply_fov"));
    CHECK(pose_distance(f.mode.pose(), before) > 1.0);
    CHECK_NEAR(f.engine.last_pose.fov, f.mode.fov(), 1e-9);

    // 配置变更：三个实时开关当帧下发，其余字段只更新配置（下一帧生效）
    Config next = f.cfg;
    next.hide_player = true;
    next.block_game_input = false;
    next.hide_game_hud = false;
    next.move_speed = 800.0;
    next.key_toggle = "F3";
    f.engine.reset_calls();
    f.runtime.apply_config(next);
    CHECK_NEAR(f.cfg.move_speed, 800.0, 1e-9);   // cfg 是注入的引用，原地更新
    CHECK_NEAR(f.mode.config().move_speed, 800.0, 1e-9);
    CHECK_INT(f.keys.toggle, 0x72);              // F3
    CHECK_INT(f.engine.count("hide_player_on"), 1);
    CHECK_INT(f.engine.count("ignore_move_off"), 1);
    CHECK_INT(f.engine.count("block_input_off"), 1);
    CHECK_INT(f.engine.count("hud_on"), 1);
    CHECK(f.engine.player_hidden);
    CHECK(!f.engine.ignore_move);
    CHECK(!f.engine.input_blocked);
    CHECK(f.engine.hud_visible);

    // 下一帧：新的 move_speed 生效（800 × 0.1 = 80 单位）
    const CameraPose before_fast = f.mode.pose();
    Intent fast{};
    fast.move_forward = 1.0;
    f.runtime.dispatch(fast, 0.1);
    CHECK(pose_distance(f.mode.pose(), before_fast) > 40.0);

    // 退出：按最终状态逐项还原
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.active());
    CHECK(!f.mode.recovery_pending());
    CHECK(!f.engine.player_hidden);
    CHECK(!f.engine.ignore_move);
    CHECK(!f.engine.input_blocked);
    CHECK(f.engine.hud_visible);
    CHECK(!f.engine.view_attached);
    CHECK(f.engine.logs_contain("退出摄影模式"));
}

// ---- 进入被拒绝 ----
void test_enter_rejected() {
    section("ModRuntime 进入被拒绝");
    Fixture f;
    f.engine.fail_spawn = true;

    CHECK(f.runtime.toggle() == ToggleOutcome::EnterFailed);
    CHECK(!f.mode.active());
    CHECK(!f.mode.recovery_pending());
    CHECK(f.engine.logs_contain("进入摄影模式失败"));
    CHECK(!f.engine.has("restore_view"));  // 失败回滚不应额外还原没做过的事

    // 失败后仍可重试；第二次成功后进入正常
    f.engine.fail_spawn = false;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    CHECK(f.mode.active());
}

// ---- 实时开关写入失败 ----
void test_live_toggle_failures() {
    section("ModRuntime 实时开关写入失败");
    Config initial{};
    initial.hide_player = true;  // 进入时就隐藏角色，退出需要还原
    Fixture f(initial);
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    CHECK(f.engine.player_hidden);

    // 关闭隐藏：恢复失败必须保留标志并告警，等退出时再试
    f.engine.fail_player_restore = true;
    Config off = f.cfg;
    off.hide_player = false;
    f.runtime.apply_config(off);
    CHECK(f.engine.player_hidden);
    CHECK(f.engine.logs_contain("恢复角色显示失败"));

    // 退出仍失败 → 进入恢复待重试状态
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.active());
    CHECK(f.mode.recovery_pending());
    CHECK(f.engine.logs_contain("未能还原"));

    // 环境恢复后，后台重试应完成并清掉待恢复标志
    f.engine.fail_player_restore = false;
    for (int i = 0; i < 20 && f.mode.recovery_pending(); ++i) {
        f.runtime.dispatch(Intent{}, 0.5);
    }
    CHECK(!f.mode.recovery_pending());
    CHECK(!f.engine.player_hidden);
    CHECK(f.engine.logs_contain("后台重试成功"));

    // 输入屏蔽的「部分恢复失败」：移动输入还原失败、键鼠屏蔽还原成功
    Fixture g;
    CHECK(g.runtime.toggle() == ToggleOutcome::Entered);
    g.engine.fail_ignore_move_restore = true;
    Config unblock = g.cfg;
    unblock.block_game_input = false;
    g.runtime.apply_config(unblock);
    CHECK(g.engine.ignore_move);          // 失败项保留
    CHECK(!g.engine.input_blocked);       // 成功项已还原
    CHECK(g.engine.logs_contain("恢复角色移动输入失败"));
    g.engine.fail_ignore_move_restore = false;
    CHECK(g.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!g.mode.recovery_pending());
    CHECK(!g.engine.ignore_move);
}

// ---- 退避重试上限 ----
void test_recovery_retry_limit() {
    section("ModRuntime 退出恢复退避重试上限");
    Config initial{};
    initial.hide_player = true;
    Fixture f(initial);
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    f.engine.fail_player_restore = true;
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(f.mode.recovery_pending());
    const int before = f.engine.count("hide_player_off");

    // dispatch 内部会把 dt 夹到 kMaxFrameDt(0.25)：200 次足够覆盖 1s/2s/4s/8s/8s
    int retries = 0;
    for (int i = 0; i < 200 && f.mode.recovery_pending(); ++i) {
        const int seen = f.engine.count("hide_player_off");
        f.runtime.dispatch(Intent{}, 0.25);
        if (f.engine.count("hide_player_off") > seen) ++retries;
    }
    CHECK(!f.mode.recovery_pending());
    CHECK_INT(retries, 5);  // kMaxRecoveryAttempts：5 次后放弃
    CHECK(f.engine.logs_contain("停止重试"));
    CHECK(f.engine.count("hide_player_off") == before + 5);
}

// ---- 未激活 / 仅恢复待重试 ----
void test_dispatch_gating() {
    section("ModRuntime dispatch 门控");
    Fixture idle;
    idle.engine.reset_calls();
    Intent want{};
    want.reset_view = true;
    want.fov_nudge = 1;
    idle.runtime.dispatch(want, 0.1);
    CHECK(idle.engine.calls.empty());  // 未激活且无待恢复项：绝不碰引擎

    Config initial{};
    initial.hide_player = true;
    Fixture pending(initial);
    CHECK(pending.runtime.toggle() == ToggleOutcome::Entered);
    pending.engine.fail_player_restore = true;
    CHECK(pending.runtime.toggle() == ToggleOutcome::Left);
    CHECK(pending.mode.recovery_pending());
    pending.engine.reset_calls();
    pending.runtime.dispatch(Intent{}, 0.25);
    // 退避 1 秒尚未到点：本帧不应尝试还原
    CHECK(!pending.engine.has("hide_player_off"));
    for (int i = 0; i < 4; ++i) pending.runtime.dispatch(Intent{}, 0.25);
    CHECK(pending.engine.has("hide_player_off"));
}

// ---- 配置热重载与 tick 并发（外部锁序列化，模拟 mod_app 的 mutex_）----
void test_config_tick_concurrency() {
    section("ModRuntime 配置热重载与 tick 并发（外部锁）");
    Fixture f;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);

    std::mutex guard;  // mod_app 的 mutex_：配置写入与 tick 都在这吧临界区内
    std::thread writer([&] {
        for (int i = 0; i < 200; ++i) {
            std::lock_guard<std::mutex> lock(guard);
            Config next = f.cfg;
            next.move_speed = (i % 2 == 0) ? 400.0 : 800.0;
            next.key_toggle = (i % 2 == 0) ? "F2" : "F3";
            f.runtime.apply_config(next);
        }
    });
    std::thread ticker([&] {
        for (int i = 0; i < 200; ++i) {
            std::lock_guard<std::mutex> lock(guard);
            Intent move{};
            move.move_forward = 1.0;
            f.runtime.dispatch(move, 0.01);
        }
    });
    writer.join();
    ticker.join();

    CHECK(f.mode.active());
    CHECK(f.cfg.move_speed == 400.0 || f.cfg.move_speed == 800.0);
    CHECK(f.keys.toggle == 0x71 || f.keys.toggle == 0x72);
    CHECK(f.mode.config().move_speed == f.cfg.move_speed);
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.recovery_pending());
}

}  // namespace

void run_mod_runtime_tests() {
    test_live_toggle_plan();
    test_enter_tick_config_leave();
    test_enter_rejected();
    test_live_toggle_failures();
    test_recovery_retry_limit();
    test_dispatch_gating();
    test_config_tick_concurrency();
}
