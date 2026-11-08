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

    // 相机后端（接管 ↔ 独立相机）必须走热切换：两条后端都是进入时建立的，
    // 只改配置的话得退出重进才生效。
    Config swapped = base;
    swapped.takeover_current_camera = !base.takeover_current_camera;
    const LiveTogglePlan backend_plan = plan_live_toggles(base, swapped);
    CHECK(backend_plan.camera_backend);
    CHECK(backend_plan.any());
    CHECK(!backend_plan.hide_player);
    CHECK(!backend_plan.block_game_input);
    CHECK(!backend_plan.hide_game_hud);
    CHECK(!plan_live_toggles(swapped, swapped).camera_backend);
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


// ---- 景深/胶片（DofPort）：门控不可用拒绝启用 → 重进重新探测 → 生效与退出释放 ----
void test_dof_gate_lifecycle() {
    section("ModRuntime 集成：景深门控生命周期");
    Fixture f;

    // 门控不可用：启用请求被拒绝（每会话探测一次），参数不下发，退出干净无 pending
    f.engine.dof_gate_available = false;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    Config with_dof = f.cfg;
    with_dof.dof_enabled = true;
    f.runtime.apply_config(with_dof);
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, 0);
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.recovery_pending());

    // 重进（enter 会清「本会话不可用」标记重新探测）：门控可用后 ensure → apply
    f.engine.dof_gate_available = true;
    const int ensures_before = static_cast<int>(f.engine.dof_calls.size());
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    f.runtime.dispatch(Intent{}, 0.016);  // dof_enabled 仍在配置里：进入后自动启用
    CHECK(static_cast<int>(f.engine.dof_calls.size()) > ensures_before);
    CHECK(f.engine.dof_calls[ensures_before] == "ensure_dof_gate(dof,-)");
    CHECK(f.engine.dof_apply_calls >= 1);
    CHECK(f.engine.last_dof.dof_enabled);
    // 参数不变不重复下发
    const int applies_after_first = f.engine.dof_apply_calls;
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, applies_after_first);
    // 退出：门控释放，无残留
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.recovery_pending());
    bool released = false;
    for (const std::string& c : f.engine.dof_calls) released |= (c == "release_dof_gate");
    CHECK(released);
}

// ---- 门控释放失败 → recovery_pending，退避重试成功后清账 ----
void test_dof_gate_release_retry() {
    section("ModRuntime 集成：景深门控释放失败走退避重试");
    Fixture f;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    Config with_dof = f.cfg;
    with_dof.dof_enabled = true;
    f.runtime.apply_config(with_dof);
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK(f.engine.dof_apply_calls >= 1);

    f.engine.fail_dof_release = true;
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(f.mode.recovery_pending());  // 门控未释放：保持待恢复，1s/2s/4s/8s 退避重试
    f.engine.fail_dof_release = false;
    // dt 被夹紧到 kMaxFrameDt(0.25s)：按帧推进积累到首个退避周期（1s）
    for (int i = 0; i < 5; ++i) f.runtime.dispatch(Intent{}, 0.25);
    CHECK(!f.mode.recovery_pending());
}

// ---- 胶片闸门延迟补建：首启色差/颗粒全 0 时胶片 cvar 组被跳过，之后调大必须放行补建 ----
// （2026-11-07 实机：ensure_dof_ready 早退挡死补建 → 组件 PP 写入成功但 r.SceneColorFringeQuality
//   等仍是游戏默认 0，pass 不渲染，色差/颗粒滑条动了画面没反应）
void test_dof_film_gate_late_build() {
    section("ModRuntime 集成：胶片闸门延迟补建");
    Fixture f;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);

    // 首启：DOF 开、色差/颗粒 0 → 只建 DOF 闸门（need_film=false）
    Config with_dof = f.cfg;
    with_dof.dof_enabled = true;
    f.runtime.apply_config(with_dof);
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK(f.engine.dof_apply_calls >= 1);
    int film_ensures = 0;
    for (const std::string& c : f.engine.dof_calls) film_ensures += (c == "ensure_dof_gate(dof,film)");
    CHECK_INT(film_ensures, 0);

    // 菜单调大色差：必须再 ensure 一次带 film（早退挡死 = 本测试红）；apply 照常下发
    Config with_ca = with_dof;
    with_ca.chromatic_aberration = 0.5;
    const int applies_before = f.engine.dof_apply_calls;
    f.runtime.apply_config(with_ca);
    f.runtime.dispatch(Intent{}, 0.016);
    film_ensures = 0;
    for (const std::string& c : f.engine.dof_calls) film_ensures += (c == "ensure_dof_gate(dof,film)");
    CHECK_INT(film_ensures, 1);
    CHECK_INT(f.engine.dof_apply_calls, applies_before + 1);
    CHECK(f.engine.last_dof.chromatic_aberration == 0.5);
    // 闸门已建：参数不变不重复 ensure/apply
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, applies_before + 1);
    film_ensures = 0;
    for (const std::string& c : f.engine.dof_calls) film_ensures += (c == "ensure_dof_gate(dof,film)");
    CHECK_INT(film_ensures, 1);

    // 退出释放清账；重进后 CA>0：首启建控即带 film
    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    f.runtime.apply_config(with_ca);
    f.runtime.dispatch(Intent{}, 0.016);
    film_ensures = 0;
    for (const std::string& c : f.engine.dof_calls) film_ensures += (c == "ensure_dof_gate(dof,film)");
    CHECK_INT(film_ensures, 2);
}

// ---- apply_dof 持续失败：按 kDofApplyRetryBackoffSec 退避重试，不每帧重入 SEH 写入 ----
void test_dof_apply_backoff() {
    section("ModRuntime 集成：景深下发失败退避重试");
    Fixture f;
    CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
    Config with_dof = f.cfg;
    with_dof.dof_enabled = true;
    f.runtime.apply_config(with_dof);

    f.engine.fail_dof_apply = true;
    f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, 1);  // 首次尝试即失败，进入退避
    for (int i = 0; i < 20; ++i) f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, 1);  // 退避窗口（0.5s）内不重入写入
    f.runtime.dispatch(Intent{}, 0.25);
    CHECK_INT(f.engine.dof_apply_calls, 2);  // dt 上限 0.25s 两帧跨过窗口：重试（仍失败）
    for (int i = 0; i < 10; ++i) f.runtime.dispatch(Intent{}, 0.016);
    CHECK_INT(f.engine.dof_apply_calls, 2);

    f.engine.fail_dof_apply = false;
    f.runtime.dispatch(Intent{}, 0.25);  // 还在退避窗口内：跳过
    CHECK_INT(f.engine.dof_apply_calls, 2);
    f.runtime.dispatch(Intent{}, 0.25);  // 退避到期：成功下发
    CHECK_INT(f.engine.dof_apply_calls, 3);
    CHECK(f.engine.last_dof.dof_enabled);
    f.runtime.dispatch(Intent{}, 0.25);  // 成功后参数不变：不再下发
    CHECK_INT(f.engine.dof_apply_calls, 3);

    CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    CHECK(!f.mode.recovery_pending());
}

// ---- NLP 过场渲染档严格跟随景深主开关（2026-11-08 实机取证；独立键已删并入总开关）----
// 取证：档位是全局渲染档，游戏原生过场景深随它渲染（游戏自己的对焦逻辑、随镜头移动、本 mod
// 参数管不到），景深总开关关着也压不住（组件通道 Scale=0 只压我们自己写的那一份）；实机同时
// 证伪「仅靠 ShowFlag/画质闸门就能出景深」（gates-only 无画面）——档位遂并入总开关、无独立键。
// 契约：① 引擎端只在 need_dof 时请求 NLP（胶片建控不开档）；② 主开关关掉立即释放档位（只发
// 一次）；③ 主开关再开时补建（早退被 nlp_owed 放行）；④ 开启失败置 exhausted 不再重试；
// ⑤ 释放失败按退避重试，不每帧重入。
void test_dof_nlp_follows_master() {
    section("ModRuntime 集成：NLP 档跟随景深主开关");

    // ① 主开关关 + 色差开（胶片建控，取证复现路径）：不得请求 NLP
    {
        Fixture f;
        Config cfg = f.cfg;
        cfg.chromatic_aberration = 0.5;
        CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(f.engine.dof_apply_calls >= 1);
        CHECK(!f.engine.nlp_active);       // 胶片建控没开档
        int releases = 0;
        for (const std::string& c : f.engine.dof_calls) releases += (c == "release_nlp_profile");
        CHECK_INT(releases, 0);
        CHECK(f.runtime.toggle() == ToggleOutcome::Left);
        CHECK(!f.mode.recovery_pending());
    }

    // ② 主开关开（档位随建控开启）→ 关（立即释放、只发一次）→ 再开（欠账补建）
    {
        Fixture f;
        Config cfg = f.cfg;
        cfg.dof_enabled = true;
        cfg.chromatic_aberration = 0.5;
        CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(f.engine.nlp_active);

        // 关（走 apply_config：与 F8 菜单 live 映射同一条路径）
        cfg.dof_enabled = false;
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(!f.engine.nlp_active);
        int releases = 0;
        for (const std::string& c : f.engine.dof_calls) releases += (c == "release_nlp_profile");
        CHECK_INT(releases, 1);
        f.runtime.dispatch(Intent{}, 0.016);  // 已释放：不重复发
        releases = 0;
        for (const std::string& c : f.engine.dof_calls) releases += (c == "release_nlp_profile");
        CHECK_INT(releases, 1);

        // 再开：胶片闸门已建、早退被 nlp_owed 放行 → 补建请求 NLP
        cfg.dof_enabled = true;
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(f.engine.nlp_active);
        CHECK(f.runtime.toggle() == ToggleOutcome::Left);
        CHECK(!f.mode.recovery_pending());
    }

    // ③ NLP 请求失败置 exhausted：本会话不再重试（后续参数变化也不补建），参数下发不受影响
    {
        Fixture f;
        Config cfg = f.cfg;
        cfg.dof_enabled = true;
        f.engine.fail_nlp_enable = true;
        CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(!f.engine.nlp_active);
        CHECK(f.engine.nlp_exhausted);
        CHECK(f.engine.dof_apply_calls >= 1);  // 门控 cvar 闸门可用：参数照常下发
        const int ensures_after_fail = static_cast<int>(f.engine.dof_calls.size());
        const int applies_after_fail = f.engine.dof_apply_calls;
        cfg.dof_focal_distance = 500.0;  // 参数变化触发 apply 路径：不得再尝试 NLP
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK_INT(static_cast<int>(f.engine.dof_calls.size()), ensures_after_fail);
        CHECK_INT(f.engine.dof_apply_calls, applies_after_fail + 1);
        CHECK(f.runtime.toggle() == ToggleOutcome::Left);
    }

    // ④ 释放失败退避重试（kDofApplyRetryBackoffSec=0.5，dt 夹紧 0.25）：不每帧重入
    {
        Fixture f;
        Config cfg = f.cfg;
        cfg.dof_enabled = true;
        CHECK(f.runtime.toggle() == ToggleOutcome::Entered);
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(f.engine.nlp_active);

        f.engine.fail_nlp_release = true;
        cfg.dof_enabled = false;
        f.runtime.apply_config(cfg);
        f.runtime.dispatch(Intent{}, 0.016);
        CHECK(f.engine.nlp_active);  // 释放失败：档位仍记账，退避开始
        auto release_count = [&] {
            int n = 0;
            for (const std::string& c : f.engine.dof_calls) n += (c == "release_nlp_profile");
            return n;
        };
        CHECK_INT(release_count(), 1);
        for (int i = 0; i < 5; ++i) f.runtime.dispatch(Intent{}, 0.016);
        CHECK_INT(release_count(), 1);       // 退避窗口（累计 0.08s）内不重入
        f.runtime.dispatch(Intent{}, 0.25);
        CHECK_INT(release_count(), 1);       // 窗口未跨过（累计 0.33s）：仍不重入
        f.runtime.dispatch(Intent{}, 0.25);
        CHECK_INT(release_count(), 2);       // 退避到期：重试（仍失败）

        f.engine.fail_nlp_release = false;
        f.runtime.dispatch(Intent{}, 0.25);  // 还在窗口内：跳过
        CHECK(f.engine.nlp_active);
        f.runtime.dispatch(Intent{}, 0.25);  // 窗口到期：释放成功
        CHECK(!f.engine.nlp_active);
        CHECK(f.runtime.toggle() == ToggleOutcome::Left);
        CHECK(!f.mode.recovery_pending());
    }
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
    test_dof_gate_lifecycle();
    test_dof_gate_release_retry();
    test_dof_film_gate_late_build();
    test_dof_apply_backoff();
    test_dof_nlp_follows_master();
    test_enter_rejected();
    test_live_toggle_failures();
    test_recovery_retry_limit();
    test_dispatch_gating();
    test_config_tick_concurrency();
}
