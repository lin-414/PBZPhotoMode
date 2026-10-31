// 纯核心层测试：不加载 UE4SS、不启动游戏，直接用 cl/cmake 编译即可跑。
// 覆盖：配置解析、键名映射、输入状态机、数学约定、摄影模式状态机（含回滚）、菜单模型。
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/math3.h"
#include "core/menu_bridge.h"
#include "core/menu_model.h"
#include "core/photo_mode.h"
#include "core/vk.h"
#include "tests/fake_engine.h"

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_section = "";

void report_fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::printf("  [FAIL] %s:%d  %s\n", file, line, what.c_str());
}

void check(bool cond, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (!cond) report_fail(file, line, what);
}

void check_eq_int(long long got, long long want, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (got != want) {
        std::printf("  [FAIL] %s:%d  %s（期望 %lld，实际 %lld）\n", file, line, what.c_str(), want, got);
        ++g_failures;
    }
}

void check_near(double got, double want, double eps, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (std::fabs(got - want) > eps) {
        std::printf("  [FAIL] %s:%d  %s（期望 %.6f，实际 %.6f）\n", file, line, what.c_str(), want, got);
        ++g_failures;
    }
}

void check_str(const std::string& got, const std::string& want, const char* file, int line, const std::string& what) {
    ++g_checks;
    if (got != want) {
        std::printf("  [FAIL] %s:%d  %s（期望 \"%s\"，实际 \"%s\"）\n", file, line, what.c_str(), want.c_str(),
                    got.c_str());
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), __FILE__, __LINE__, #cond)
#define CHECK_INT(got, want) check_eq_int((got), (want), __FILE__, __LINE__, #got)
#define CHECK_NEAR(got, want, eps) check_near((got), (want), (eps), __FILE__, __LINE__, #got)
#define CHECK_STR(got, want) check_str((got), (want), __FILE__, __LINE__, #got)

void section(const char* name) {
    g_section = name;
    std::printf("\n== %s ==\n", name);
}

// ---------------------------------------------------------------- 键名映射

void test_vk_names() {
    section("vk_from_name");
    using pbzpm::vk_from_name;
    CHECK_INT(vk_from_name("F2"), 0x71);
    CHECK_INT(vk_from_name("f2"), 0x71);
    CHECK_INT(vk_from_name(" F9 "), 0x78);
    CHECK_INT(vk_from_name("T"), 'T');
    CHECK_INT(vk_from_name("ADD"), 0x6B);
    CHECK_INT(vk_from_name("SUBTRACT"), 0x6D);
    CHECK_INT(vk_from_name("OEM_PLUS"), 0xBB);
    CHECK_INT(vk_from_name("OEM_MINUS"), 0xBD);
    CHECK_INT(vk_from_name("PAGE_UP"), 0x21);
    CHECK_INT(vk_from_name("LEFT_ALT"), 0xA4);
    CHECK_INT(vk_from_name("LEFT_CONTROL"), 0xA2);
    CHECK_INT(vk_from_name("z"), 'Z');
    CHECK_INT(vk_from_name("7"), '7');
    CHECK_INT(vk_from_name(""), 0);
    CHECK_INT(vk_from_name("NOT_A_KEY"), 0);
    CHECK_INT(vk_from_name("~"), 0);  // 波浪键没做别名，按文档用 TILDE
    CHECK_INT(vk_from_name("TILDE"), 0xC0);
}

// ---------------------------------------------------------------- 配置解析

void test_config_defaults_and_parse() {
    section("parse_config");
    using namespace pbzpm;

    const ConfigLoadResult empty = parse_config("");
    CHECK_INT(empty.warnings.size(), 0);
    CHECK_STR(empty.cfg.key_toggle, "F2");
    CHECK_STR(empty.cfg.key_menu, "T");
    CHECK_STR(empty.cfg.key_freeze, "F");
    CHECK_STR(empty.cfg.key_reset, "R");
    CHECK_NEAR(empty.cfg.move_speed, 400.0, 1e-9);
    CHECK(empty.cfg.free_look_requires_alt);
    CHECK(empty.cfg.block_game_input);
    CHECK(!empty.cfg.hide_player);
    CHECK(empty.cfg.hide_game_hud);  // 默认进入摄影模式即隐藏游戏 HUD
    CHECK(empty.cfg.auto_pause_time);  // 默认进入摄影模式即自动暂停时间
    CHECK(empty.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
    CHECK(empty.cfg.freeze_mode == FreezeMode::Pause);

    const std::string ini = R"(
; 注释行
# 另一种注释
[keys]
key_toggle = F9          ; 行尾注释
key_freeze = "G"
key_fov_up = OEM_PLUS

[control]
move_speed = 250.5
boost_mult = 2
slow_mult = 0.5
invert_y = yes
free_look_requires_alt = false
mouse_vertical_mode = look

[camera]
fov_default = 75
fov_min = 20
fov_max = 120

[effects]
auto_pause_time = false

[misc]
log_level = debug
dry_run = on
)";
    const ConfigLoadResult parsed = parse_config(ini, "test.ini");
    CHECK_INT(parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F9");
    CHECK_STR(parsed.cfg.key_freeze, "G");
    CHECK_STR(parsed.cfg.key_fov_up, "OEM_PLUS");
    CHECK_NEAR(parsed.cfg.move_speed, 250.5, 1e-9);
    CHECK_NEAR(parsed.cfg.boost_mult, 2.0, 1e-9);
    CHECK(parsed.cfg.invert_y);
    CHECK(!parsed.cfg.free_look_requires_alt);
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Look);
    CHECK_NEAR(parsed.cfg.fov_default, 75.0, 1e-9);
    CHECK(!parsed.cfg.auto_pause_time);
    CHECK(parsed.cfg.log_level == LogLevel::Debug);
    CHECK(parsed.cfg.dry_run);
    CHECK_STR(parsed.source, "test.ini");
}

void test_config_warnings_and_clamps() {
    section("parse_config 容错");
    using namespace pbzpm;

    const std::string bad = R"(
nonsense_key = 1
move_speed = abc
invert_y = maybe
fov_min = 30
fov_max = 10
pitch_limit = 120
freeze_dilation = 0
this line has no equals
)";
    const ConfigLoadResult parsed = parse_config(bad);
    CHECK(parsed.warnings.size() >= 6);
    CHECK_NEAR(parsed.cfg.move_speed, 400.0, 1e-9);   // 非法值保留默认
    CHECK(!parsed.cfg.invert_y);
    CHECK_NEAR(parsed.cfg.fov_min, 15.0, 1e-9);      // 越界组合回退默认
    CHECK_NEAR(parsed.cfg.fov_max, 150.0, 1e-9);
    CHECK_NEAR(parsed.cfg.pitch_limit, 89.0, 1e-9);
    CHECK_NEAR(parsed.cfg.freeze_dilation, 0.0001, 1e-12);

    // fov_default 落在范围外要被夹紧并告警
    const ConfigLoadResult clamp = parse_config("fov_min = 20\nfov_max = 60\nfov_default = 90\n");
    CHECK(clamp.warnings.size() >= 1);
    CHECK_NEAR(clamp.cfg.fov_default, 60.0, 1e-9);

    // 缺文件：ok=false 但仍是可用的默认值
    const ConfigLoadResult missing = load_config_file("definitely_not_here.ini");
    CHECK(!missing.ok);
    CHECK(missing.warnings.size() >= 1);
    CHECK_STR(missing.cfg.key_toggle, "F2");
}

void test_config_default_text_roundtrip() {
    section("default_config_text 往返");
    using namespace pbzpm;
    const std::string text = default_config_text();
    CHECK(text.find("key_toggle") != std::string::npos);
    const ConfigLoadResult parsed = parse_config(text, "generated");
    CHECK_INT(parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F2");
    CHECK_NEAR(parsed.cfg.fov_step, 2.0, 1e-9);
    CHECK(parsed.cfg.auto_pause_time);
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
}

// ---------------------------------------------------------------- 数学

void test_math() {
    section("math3");
    using namespace pbzpm;

    const Vec3 fwd0 = forward_from_angles(0.0, 0.0);
    CHECK_NEAR(fwd0.x, 1.0, 1e-9);
    CHECK_NEAR(fwd0.y, 0.0, 1e-9);
    CHECK_NEAR(fwd0.z, 0.0, 1e-9);

    const Vec3 fwd_yaw90 = forward_from_angles(0.0, 90.0);
    CHECK_NEAR(fwd_yaw90.x, 0.0, 1e-9);
    CHECK_NEAR(fwd_yaw90.y, 1.0, 1e-9);

    const Vec3 fwd_up = forward_from_angles(90.0, 0.0);
    CHECK_NEAR(fwd_up.z, 1.0, 1e-9);

    const Vec3 right0 = right_from_yaw(0.0);
    CHECK_NEAR(right0.x, 0.0, 1e-9);
    CHECK_NEAR(right0.y, 1.0, 1e-9);  // UE 左手法则：yaw=0 面朝 +X，右手边是 +Y

    const Vec3 right90 = right_from_yaw(90.0);
    CHECK_NEAR(right90.x, -1.0, 1e-9);
    CHECK_NEAR(right90.y, 0.0, 1e-9);

    CHECK_NEAR(clamp_pitch(120.0, 89.0), 89.0, 1e-9);
    CHECK_NEAR(clamp_pitch(-120.0, 89.0), -89.0, 1e-9);
    CHECK_NEAR(wrap_yaw(370.0), 10.0, 1e-9);
    CHECK_NEAR(wrap_yaw(-10.0), 350.0, 1e-9);

    const Vec3 moved = offset_by_local(Vec3{}, Rot{}, Vec3{1.0, 0.0, 0.0}, 100.0);
    CHECK_NEAR(moved.x, 100.0, 1e-9);
    const Vec3 moved_right = offset_by_local(Vec3{}, Rot{}, Vec3{0.0, 1.0, 0.0}, 50.0);
    CHECK_NEAR(moved_right.y, 50.0, 1e-9);
}

// ---------------------------------------------------------------- 输入

void test_input_logic() {
    section("InputLogic");
    using namespace pbzpm;

    Config cfg;
    std::vector<std::string> warnings;
    const KeyMap keys = build_keymap(cfg, &warnings);
    CHECK_INT(warnings.size(), 0);
    CHECK(keys.all_resolved);
    CHECK_INT(keys.toggle, 0x71);
    CHECK_INT(keys.menu, 'T');
    CHECK_INT(keys.freeze, 'F');
    CHECK_INT(keys.reset, 'R');

    InputLogic in;
    in.on_key('W', true);
    Intent intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_forward, 1.0, 1e-9);

    in.on_key('R', true);
    intent = in.build(cfg, keys);
    CHECK(intent.reset_view);
    in.on_key('R', false);
    in.end_frame();

    in.on_key('W', false);
    in.on_key('S', true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_forward, -1.0, 1e-9);

    // 斜向同时按 = 两个轴各 1，长度由 PhotoMode 归一化
    in.on_key('D', true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_forward, -1.0, 1e-9);
    CHECK_NEAR(intent.move_right, 1.0, 1e-9);
    in.reset();

    // 鼠标位移必须按住 Alt（free_look_requires_alt 默认 true）
    in.on_mouse_move(10.0, -5.0);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.look_dx, 0.0, 1e-9);
    CHECK_NEAR(intent.look_dy, 0.0, 1e-9);

    in.on_key(kAltLeft, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.look_dx, 10.0, 1e-9);
    CHECK_NEAR(intent.look_dy, -5.0, 1e-9);
    in.on_key(kAltLeft, false);

    // end_frame 清累积，但按键仍 held
    in.on_key(kAltGeneric, true);
    in.on_mouse_move(3.0, 4.0);
    (void)in.build(cfg, keys);
    in.end_frame();
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.look_dx, 0.0, 1e-9);
    in.reset();

    // Alt 要求可关闭
    Config no_alt = cfg;
    no_alt.free_look_requires_alt = false;
    in.on_mouse_move(7.0, 7.0);
    intent = in.build(no_alt, keys);
    CHECK_NEAR(intent.look_dx, 7.0, 1e-9);
    in.reset();

    // 滚轮不受 Alt 门控（需求里“滚轮调 FOV”是独立操作），且累积
    in.on_wheel(1.0);
    in.on_wheel(1.0);
    CHECK_NEAR(in.build(cfg, keys).wheel, 2.0, 1e-9);
    CHECK_NEAR(in.build(cfg, keys).look_dx, 0.0, 1e-9);  // 没按 Alt 时位移仍然不算视角
    in.end_frame();
    CHECK_NEAR(in.build(cfg, keys).wheel, 0.0, 1e-9);
    in.reset();

    // 左键/右键：默认 translate 模式 -> 上下平移
    in.on_mouse_button(MouseButton::Left, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);
    CHECK(!intent.pitch_up_click);
    in.on_mouse_button(MouseButton::Left, false);
    in.on_mouse_button(MouseButton::Right, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_up, -1.0, 1e-9);
    in.reset();

    // look 模式：左右键变俯仰，且与 E/Q 升降不混
    Config look_cfg = cfg;
    look_cfg.mouse_vertical_mode = MouseVerticalMode::Look;
    in.on_mouse_button(MouseButton::Left, true);
    intent = in.build(look_cfg, keys);
    CHECK(intent.pitch_up_click);
    CHECK(!intent.pitch_down_click);
    CHECK_NEAR(intent.move_up, 0.0, 1e-9);
    in.reset();

    // 按下沿只触发一次；按住不放不会重复触发
    in.on_key(keys.freeze, true);
    CHECK(in.build(cfg, keys).toggle_freeze);
    in.end_frame();
    CHECK(!in.build(cfg, keys).toggle_freeze);
    in.on_key(keys.freeze, false);
    in.on_key(keys.freeze, true);
    CHECK(in.build(cfg, keys).toggle_freeze);
    in.reset();

    // Shift / Ctrl
    in.on_key(kShiftLeft, true);
    in.on_key(kCtrlLeft, true);
    intent = in.build(cfg, keys);
    CHECK(intent.boost);
    CHECK(intent.slow);
    in.reset();
    CHECK(!in.build(cfg, keys).boost);
}

// ---------------------------------------------------------------- 摄影模式状态机

std::vector<std::string> call_list(const pbzpm::test::FakeEngine& fe) { return fe.calls; }

void expect_sequence(const pbzpm::test::FakeEngine& fe, std::initializer_list<const char*> expected,
                     const char* label) {
    const std::vector<std::string> want(expected.begin(), expected.end());
    ++g_checks;
    if (fe.calls != want) {
        std::printf("  [FAIL] 调用序列不符（%s）\n", label);
        std::printf("    期望：");
        for (const std::string& w : want) std::printf("%s ", w.c_str());
        std::printf("\n    实际：");
        for (const std::string& a : fe.calls) std::printf("%s ", a.c_str());
        std::printf("\n");
        ++g_failures;
    }
}

void test_photo_mode_happy_path() {
    section("PhotoMode 进入/退出");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    fe.pov.loc = Vec3{100.0, 200.0, 300.0};
    fe.pov.rot = Rot{10.0, 45.0, 0.0};
    fe.pov.fov = 88.0;

    Config cfg;
    PhotoMode mode(fe, cfg);
    CHECK(!mode.active());
    CHECK(mode.enter());

    expect_sequence(fe,
                    {"world_valid", "read_pov", "spawn_camera", "attach_view", "ignore_move_on", "block_input_on",
                     "hud_off", "save_postfx", "get_time_dilation", "apply_fov", "apply_pose", "pause_on"},
                    "首次进入");

    CHECK(mode.active());
    CHECK(fe.camera_exists);
    CHECK(fe.view_attached);
    CHECK(fe.ignore_move);
    CHECK(fe.input_blocked);
    CHECK(!fe.player_hidden);  // 默认不隐藏角色：摄影模式要能把自己拍进去
    CHECK(!fe.hud_visible);    // 默认隐藏游戏 HUD
    CHECK_NEAR(mode.fov(), 88.0, 1e-9);
    CHECK_NEAR(mode.pose().rot.yaw, 45.0, 1e-9);
    CHECK(mode.frozen());  // 默认 auto_pause_time=true：进入即冻结
    CHECK(fe.paused);
    CHECK(mode.menu_visible());

    // 再进一次不应重复 spawn
    fe.reset_calls();
    CHECK(mode.enter());
    CHECK_INT(fe.count("spawn_camera"), 0);

    // 退出：严格逆序还原，且只还原做过的步骤
    fe.reset_calls();
    mode.leave("test");
    expect_sequence(fe,
                    {"pause_off", "restore_postfx", "hud_on", "block_input_off", "ignore_move_off", "restore_view",
                     "destroy_camera"},
                    "退出");
    CHECK(!mode.active());
    CHECK(!fe.paused);
    CHECK(!fe.camera_exists);
    CHECK(!fe.view_attached);
    CHECK(!fe.ignore_move);
    CHECK(!fe.input_blocked);
    CHECK(!fe.player_hidden);
    CHECK(fe.hud_visible);  // 退出后恢复
    CHECK_NEAR(fe.dilation, 1.0, 1e-12);

    // 幂等：再退一次不应有任何调用
    fe.reset_calls();
    mode.leave("again");
    CHECK_INT(fe.calls.size(), 0);
}

void test_photo_mode_failures_rollback() {
    section("PhotoMode 失败回滚");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 1) spawn 失败：不留下任何状态
    {
        FakeEngine fe;
        fe.fail_spawn = true;
        PhotoMode mode(fe, Config{});
        CHECK(!mode.enter());
        CHECK(!mode.active());
        CHECK_STR(mode.last_error(), "创建 CameraActor 失败");
        CHECK(!fe.camera_exists);
        CHECK(!fe.view_attached);
        CHECK(!fe.ignore_move);
        CHECK(!fe.player_hidden);
    }

    // 2) attach 失败：必须销毁已创建的相机，且不碰输入/隐藏/后处理
    {
        FakeEngine fe;
        fe.fail_attach = true;
        PhotoMode mode(fe, Config{});
        CHECK(!mode.enter());
        CHECK(!mode.active());
        CHECK(!fe.camera_exists);
        CHECK_INT(fe.count("destroy_camera"), 1);
        CHECK_INT(fe.count("ignore_move_on"), 0);
        CHECK_INT(fe.count("hide_player_on"), 0);
        CHECK_INT(fe.count("save_postfx"), 0);
        // 失败后仍可重试成功（fail_attach 关掉）
        fe.fail_attach = false;
        fe.reset_calls();
        CHECK(mode.enter());
        CHECK(mode.active());
    }

    // 3) 世界不可用
    {
        FakeEngine fe;
        fe.world_ok = false;
        PhotoMode mode(fe, Config{});
        CHECK(!mode.enter());
        CHECK_STR(mode.last_error(), "世界未加载（UWorld 不可用）");
        CHECK_INT(fe.count("spawn_camera"), 0);
    }

    // 4) 读 POV 失败 -> 用默认位姿继续
    {
        FakeEngine fe;
        fe.read_pov_ok = false;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.pov_fallback_used());
        CHECK_NEAR(mode.fov(), 90.0, 1e-9);
        CHECK(fe.logs_contain("读不到当前视角"));
    }
}

void test_photo_mode_tick() {
    section("PhotoMode tick");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    fe.pov.loc = Vec3{0.0, 0.0, 0.0};
    fe.pov.rot = Rot{0.0, 0.0, 0.0};
    fe.pov.fov = 90.0;
    Config cfg;
    cfg.look_sensitivity = 0.1;
    PhotoMode mode(fe, cfg);
    CHECK(mode.enter());

    // 鼠标右移 -> yaw 增大；鼠标下移 -> pitch 减小
    Intent intent;
    intent.look_dx = 10.0;
    intent.look_dy = 5.0;
    fe.reset_calls();
    mode.tick(intent, 0.016);
    CHECK_NEAR(mode.pose().rot.yaw, 1.0, 1e-9);
    CHECK_NEAR(mode.pose().rot.pitch, -0.5, 1e-9);
    CHECK(fe.has("apply_pose"));

    // invert_y 反转竖直方向
    Config inv = cfg;
    inv.invert_y = true;
    PhotoMode mode2(fe, inv);
    CHECK(mode2.enter());
    Intent look_down;
    look_down.look_dy = 10.0;
    mode2.tick(look_down, 0.016);
    CHECK(mode2.pose().rot.pitch > 0.0);
    mode2.leave();

    // pitch 夹紧
    Intent big;
    big.look_dy = -100000.0;
    mode.tick(big, 0.016);
    CHECK_NEAR(mode.pose().rot.pitch, cfg.pitch_limit, 1e-9);

    // 滚轮改 FOV：向上（正 wheel）缩小，向下（负 wheel）放大，且夹紧
    Intent wheel;
    wheel.wheel = 2.0;  // 向上 2 格 -> -4 度
    const double before = mode.fov();
    mode.tick(wheel, 0.016);
    CHECK_NEAR(mode.fov(), before - 4.0, 1e-9);
    wheel.wheel = -1000.0;  // 向下大量 -> 上限
    mode.tick(wheel, 0.016);
    CHECK_NEAR(mode.fov(), cfg.fov_max, 1e-9);
    CHECK_NEAR(fe.last_fov, cfg.fov_max, 1e-9);
    wheel.wheel = 1000.0;  // 向上大量 -> 下限
    mode.tick(wheel, 0.016);
    CHECK_NEAR(mode.fov(), cfg.fov_min, 1e-9);
    CHECK_NEAR(fe.last_fov, cfg.fov_min, 1e-9);

    // R 重置相机视角：回到进入摄影模式时的位置/朝向/FOV
    {
        FakeEngine fe_reset;
        fe_reset.pov.loc = Vec3{10.0, 20.0, 30.0};
        fe_reset.pov.rot = Rot{5.0, 45.0, 0.0};
        fe_reset.pov.fov = 80.0;
        Config cfg_reset;
        PhotoMode resetter(fe_reset, cfg_reset);
        CHECK(resetter.enter());

        Intent move;
        move.move_forward = 1.0;
        move.look_dy = 10.0;
        move.wheel = 2.0;
        resetter.tick(move, 0.1);
        CHECK(resetter.pose().loc.x > fe_reset.pov.loc.x);  // 先确实移动/转/改 FOV
        CHECK(resetter.pose().rot.pitch < fe_reset.pov.rot.pitch);
        CHECK(resetter.fov() < fe_reset.pov.fov);

        Intent reset;
        reset.reset_view = true;
        resetter.tick(reset, 0.016);
        CHECK_NEAR(resetter.pose().loc.x, fe_reset.pov.loc.x, 1e-9);
        CHECK_NEAR(resetter.pose().loc.y, fe_reset.pov.loc.y, 1e-9);
        CHECK_NEAR(resetter.pose().loc.z, fe_reset.pov.loc.z, 1e-9);
        CHECK_NEAR(resetter.pose().rot.pitch, fe_reset.pov.rot.pitch, 1e-9);
        CHECK_NEAR(resetter.pose().rot.yaw, fe_reset.pov.rot.yaw, 1e-9);
        CHECK_NEAR(resetter.fov(), fe_reset.pov.fov, 1e-9);
        CHECK(fe_reset.logs_contain("相机视角已重置"));
        resetter.leave();
    }

    // 前移：yaw=0,pitch=0 时应该只动 +X
    // （上面把 pitch 夹到了 89 度，位移测试用一套新的干净实例）
    FakeEngine fe_move;
    Config cfg_move = cfg;
    cfg_move.look_sensitivity = 0.1;
    PhotoMode mover(fe_move, cfg_move);
    CHECK(mover.enter());

    mover.set_fov(90.0);
    const Vec3 start = mover.pose().loc;
    Intent fwd;
    fwd.move_forward = 1.0;
    mover.tick(fwd, 0.1);  // 400 * 0.1 = 40 单位
    CHECK_NEAR(mover.pose().loc.x, start.x + 40.0, 1e-6);
    CHECK_NEAR(mover.pose().loc.y, start.y, 1e-6);

    // 斜向不加速：长度仍是 40
    const Vec3 diag_start = mover.pose().loc;
    Intent diag;
    diag.move_forward = 1.0;
    diag.move_right = 1.0;
    mover.tick(diag, 0.1);
    const double dx = mover.pose().loc.x - diag_start.x;
    const double dy = mover.pose().loc.y - diag_start.y;
    CHECK_NEAR(std::sqrt(dx * dx + dy * dy), 40.0, 1e-6);

    // boost / slow 只乘速度
    const Vec3 boost_start = mover.pose().loc;
    Intent boost;
    boost.move_forward = 1.0;
    boost.boost = true;
    mover.tick(boost, 0.1);
    CHECK_NEAR(mover.pose().loc.x - boost_start.x, 160.0, 1e-6);  // 400*4*0.1

    Intent slow;
    slow.move_forward = 1.0;
    slow.slow = true;
    const Vec3 slow_start = mover.pose().loc;
    mover.tick(slow, 0.1);
    CHECK_NEAR(std::fabs(mover.pose().loc.x - slow_start.x), 12.0, 1e-6);  // 400*0.3*0.1

    // dt 上限保护：一帧掉到 5 秒不该瞬移 2000 单位
    const Vec3 lag_start = mover.pose().loc;
    Intent lag;
    lag.move_forward = 1.0;
    mover.tick(lag, 5.0);
    CHECK_NEAR(std::fabs(mover.pose().loc.x - lag_start.x), 400.0 * 0.25, 1e-6);


    // 世界失效 -> 自动退出并还原
    fe.world_ok = false;
    fe.reset_calls();
    mode.tick(Intent{}, 0.016);
    CHECK(!mode.active());
    CHECK(fe.has("restore_view"));
    CHECK(fe.has("destroy_camera"));
}

void test_photo_mode_freeze() {
    section("PhotoMode 冻结/解冻");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // dilation 模式：记住进入前的值并还原
    {
        FakeEngine fe;
        fe.dilation = 0.8;  // 模拟游戏本来就不是 1.0（慢动作状态）
        Config cfg;
        cfg.auto_pause_time = false;  // 本组测手动 F 键
        cfg.freeze_mode = FreezeMode::Dilation;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK_NEAR(fe.dilation, 0.8, 1e-12);

        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, 0.0001, 1e-12);

        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_NEAR(fe.dilation, 0.8, 1e-12);  // 还原到进入前的 0.8，不是 1.0

        // 冻结状态直接退出，也必须还原
        mode.toggle_freeze();
        CHECK(mode.frozen());
        mode.leave("exit while frozen");
        CHECK_NEAR(fe.dilation, 0.8, 1e-12);
    }

    // pause 模式
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;  // 本组测手动 F 键
        cfg.freeze_mode = FreezeMode::Pause;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!fe.paused);
        mode.toggle_freeze();
        CHECK(fe.paused);
        mode.toggle_freeze();
        CHECK(!fe.paused);
        mode.leave();
        CHECK(!fe.paused);
    }

    // 冻结键走 intent 通道也能生效
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        Intent freeze;
        freeze.toggle_freeze = true;
        mode.tick(freeze, 0.016);
        CHECK(mode.frozen());
    }
}

void test_photo_mode_auto_pause() {
    section("PhotoMode 自动暂停时间");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认配置：进入即自动暂停（freeze_mode=pause）
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(mode.frozen());
        CHECK(fe.paused);
        CHECK(fe.logs_contain("自动暂停时间"));
        CHECK(fe.logs_contain("时间已冻结"));

        // F 键仍可恢复时间，再按一次重新冻结
        Intent toggle;
        toggle.toggle_freeze = true;
        mode.tick(toggle, 0.016);
        CHECK(!mode.frozen());
        CHECK(!fe.paused);
        mode.tick(toggle, 0.016);
        CHECK(mode.frozen());
        CHECK(fe.paused);

        mode.leave("test");
        CHECK(!fe.paused);
    }

    // 冻结期间配置热重载改了 freeze_mode：解冻仍按实际使用的 pause 模式还原
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        Config changed;
        changed.freeze_mode = FreezeMode::Dilation;
        mode.set_config(changed);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK(!fe.paused);
        CHECK_INT(fe.count("pause_off"), 1);
        CHECK_INT(fe.count("set_time_dilation"), 0);
    }

    // auto_pause_time = false：进入后时间照常流动，F 仍可手动冻结
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!mode.frozen());
        CHECK(!fe.paused);
        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK(fe.paused);
        mode.leave("test");
        CHECK(!fe.paused);
    }

    // dilation 模式也遵守 auto_pause_time：进入即切到 freeze_dilation，退出还原原倍率
    {
        FakeEngine fe;
        fe.dilation = 0.8;
        Config cfg;
        cfg.freeze_mode = FreezeMode::Dilation;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, cfg.freeze_dilation, 1e-12);
        mode.leave("test");
        CHECK_NEAR(fe.dilation, 0.8, 1e-12);
    }

    // 自动暂停失败不阻止进模式：只告警，状态保持未冻结
    {
        FakeEngine fe;
        fe.fail_pause = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(!mode.frozen());
        CHECK(!fe.paused);
        CHECK(fe.logs_contain("冻结时间失败"));
        mode.leave("test");
        CHECK(!mode.active());
    }

    // dilation 自动冻结失败同样不阻止进模式
    {
        FakeEngine fe;
        fe.fail_time_dilation = true;
        Config cfg;
        cfg.freeze_mode = FreezeMode::Dilation;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(!mode.frozen());
        CHECK(fe.logs_contain("冻结时间失败"));
        mode.leave("test");
    }
}

void test_photo_mode_reassert_and_partial() {
    section("PhotoMode 守护与部分失败");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 每 1 秒重设 ViewTarget；attach 失败则退出
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        fe.reset_calls();
        for (int i = 0; i < 61; ++i) {  // 61 * 0.016 ≈ 0.98s，还没到
            mode.tick(Intent{}, 0.016);
        }
        CHECK_INT(fe.count("attach_view"), 0);
        mode.tick(Intent{}, 0.05);  // 越过 1s
        CHECK_INT(fe.count("attach_view"), 1);

        fe.fail_attach = true;
        for (int i = 0; i < 61; ++i) mode.tick(Intent{}, 0.016);
        mode.tick(Intent{}, 0.05);
        CHECK(!mode.active());
    }

    // 屏蔽移动输入失败不算致命，但要告警
    {
        FakeEngine fe;
        fe.fail_ignore_move = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(fe.logs_contain("屏蔽角色移动输入失败"));
        CHECK(!fe.ignore_move);
        // 没设置成功的步骤，退出时不应该去"还原"
        fe.reset_calls();
        mode.leave();
        CHECK_INT(fe.count("ignore_move_off"), 0);
    }

    // 后处理快照失败：退出时不调用 restore
    {
        FakeEngine fe;
        fe.fail_postfx = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.logs_contain("PostProcess 快照失败"));
        fe.reset_calls();
        mode.leave();
        CHECK_INT(fe.count("restore_postfx"), 0);
    }

    // hide_game_hud = true 时才动 HUD，并在退出时恢复
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_game_hud = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!fe.hud_visible);
        mode.leave();
        CHECK(fe.hud_visible);
    }

    // HUD 比摄影模式晚建好：进入时隐藏失败，tick 每秒重试，成功后退出仍能恢复
    {
        FakeEngine fe;
        fe.fail_hud = true;
        Config cfg;
        cfg.hide_game_hud = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.hud_visible);  // 第一次隐藏失败
        CHECK(fe.logs_contain("隐藏 HUD 失败"));
        fe.fail_hud = false;
        Intent none;
        for (int i = 0; i < 5; ++i) mode.tick(none, 0.25);  // 累计 1.25 秒，跨过重试阈值
        CHECK(!fe.hud_visible);  // 每秒重试后成功
        mode.leave();
        CHECK(fe.hud_visible);
    }
}

// ---------------------------------------------------------------- 菜单

void test_menu_model() {
    section("menu_model");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    Config menu_cfg;
    menu_cfg.auto_pause_time = false;  // 本组测菜单状态，手动控制冻结
    PhotoMode mode(fe, menu_cfg);
    const KeyMap keys = build_keymap(mode.config(), nullptr);

    MenuModel off = build_menu_model(mode, keys);
    CHECK_STR(off.status, "OFF");

    CHECK(mode.enter());
    MenuModel live = build_menu_model(mode, keys);
    CHECK_STR(live.status, "LIVE");
    CHECK(!live.status_frozen);
    CHECK(live.rows.size() >= 5);
    CHECK_STR(live.rows[0].label, "FOV");
    CHECK_STR(live.rows[0].value, "90.0");

    mode.toggle_freeze();
    MenuModel frozen = build_menu_model(mode, keys);
    CHECK_STR(frozen.status, "FROZEN");
    CHECK(frozen.status_frozen);

    const std::string text = render_menu_text(frozen);
    CHECK(text.find("FROZEN") != std::string::npos);
    CHECK(text.find("FOV") != std::string::npos);
    CHECK(text.find("ALT") != std::string::npos);
    CHECK(text.find("F2") != std::string::npos);   // 退出键提示
    CHECK(text.find("F") != std::string::npos);    // 冻结键提示

    // 改了键位，提示也要跟着变
    Config cfg;
    cfg.key_toggle = "F10";
    cfg.key_freeze = "G";
    PhotoMode mode2(fe, cfg);
    const KeyMap keys2 = build_keymap(mode2.config(), nullptr);
    const std::string text2 = render_menu_text(build_menu_model(mode2, keys2));
    CHECK(text2.find("F10") != std::string::npos);
    CHECK(text2.find("G") != std::string::npos);
}

}  // namespace


void test_photo_mode_input_blocking() {
    section("PhotoMode 输入屏蔽与角色可见性");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认配置：不隐藏角色（要能拍到自己的角色），但必须屏蔽游戏键鼠
    {
        FakeEngine fe;
        Config cfg;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!fe.player_hidden);
        CHECK(fe.input_blocked);
        CHECK_INT(fe.count("block_input_on"), 1);
        mode.leave("test");
        CHECK_INT(fe.count("block_input_off"), 1);
        CHECK(!fe.input_blocked);
    }

    // 打开 hide_player 时才隐藏角色，退出必须还原
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_player = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.player_hidden);
        mode.leave("test");
        CHECK(!fe.player_hidden);
    }

    // 关掉 block_game_input 就一步都不该做
    {
        FakeEngine fe;
        Config cfg;
        cfg.block_game_input = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK_INT(fe.count("block_input_on"), 0);
        CHECK_INT(fe.count("ignore_move_on"), 0);
        CHECK(!fe.input_blocked);
    }

    // 屏蔽失败只警告，不阻止进入模式（否则玩家彻底进不去）
    {
        FakeEngine fe;
        fe.fail_block_input = true;
        Config cfg;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(!fe.input_blocked);
        mode.leave("test");
        CHECK(!mode.active());
    }
}

void test_photo_mode_live_toggles() {
    section("PhotoMode 图形菜单实时开关");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 屏蔽输入：关→开 循环；退出只按内部标记还原，不多调
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.input_blocked);
        mode.set_block_game_input_live(false);
        CHECK(!fe.input_blocked);
        CHECK_INT(fe.count("block_input_off"), 1);
        CHECK_INT(fe.count("ignore_move_off"), 1);
        mode.set_block_game_input_live(true);
        CHECK(fe.input_blocked);
        CHECK_INT(fe.count("block_input_on"), 2);
        mode.leave("test");
        CHECK(!fe.input_blocked);
        CHECK_INT(fe.count("block_input_off"), 2);
    }

    // 隐藏角色：未进入模式时只改配置不碰引擎；进入后开/关即时生效
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        mode.set_hide_player_live(true);
        CHECK_INT(fe.count("hide_player_on"), 0);
        CHECK(mode.enter());
        CHECK(fe.player_hidden);
        mode.set_hide_player_live(true);
        CHECK_INT(fe.count("hide_player_on"), 1);
        mode.set_hide_player_live(false);
        CHECK(!fe.player_hidden);
        mode.leave("test");
        CHECK_INT(fe.count("hide_player_off"), 1);
        CHECK(!fe.player_hidden);
    }

    // HUD：进入时默认隐藏一次；开关来回切换即时生效
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.hud_visible);
        mode.set_hide_game_hud_live(false);
        CHECK(fe.hud_visible);
        mode.set_hide_game_hud_live(false);
        CHECK_INT(fe.count("hud_on"), 1);
        mode.set_hide_game_hud_live(true);
        CHECK(!fe.hud_visible);
        mode.leave("test");
        CHECK(fe.hud_visible);
    }
}

void test_gamepad_names() {
    section("pad_button 名字表");
    using namespace pbzpm;
    PadButton b{};
    CHECK(pad_button_from_name("LS", b) && b == PadButton::LS);
    CHECK(pad_button_from_name("l3", b) && b == PadButton::LS);
    CHECK(pad_button_from_name("R3", b) && b == PadButton::RS);
    CHECK(pad_button_from_name("cross", b) && b == PadButton::A);
    CHECK(pad_button_from_name("Triangle", b) && b == PadButton::Y);
    CHECK(pad_button_from_name("L1", b) && b == PadButton::LB);
    CHECK(pad_button_from_name("L2", b) && b == PadButton::LT);
    CHECK(pad_button_from_name("options", b) && b == PadButton::Start);
    CHECK(pad_button_from_name("share", b) && b == PadButton::Back);
    CHECK(pad_button_from_name("ps", b) && b == PadButton::Guide);
    CHECK(pad_button_from_name("DPAD_UP", b) && b == PadButton::Up);
    CHECK(pad_button_from_name("RT", b) && b == PadButton::RT);
    CHECK(!pad_button_from_name("NOPE", b));
    CHECK(!pad_button_from_name("", b));
    CHECK_STR(pad_button_name(PadButton::LS), "LS");
    CHECK_STR(pad_button_name(PadButton::Start), "START");
    CHECK_STR(pad_button_name(PadButton::Guide), "GUIDE");
}

void test_gamepad_parse_combo() {
    section("parse_pad_combo");
    using namespace pbzpm;
    std::vector<std::string> warnings;
    const std::vector<PadButton> fb{PadButton::LS, PadButton::RS};

    PadCombo r = parse_pad_combo("LS+RS", fb, "pad_toggle", &warnings);
    CHECK(r.ok);
    CHECK_INT((int)r.buttons.size(), 2);
    CHECK(r.buttons[0] == PadButton::LS && r.buttons[1] == PadButton::RS);

    r = parse_pad_combo(" start ", fb, "pad_menu", &warnings);
    CHECK(r.ok && r.buttons.size() == 1 && r.buttons[0] == PadButton::Start);

    r = parse_pad_combo("ls + rs", fb, "pad_toggle", &warnings);
    CHECK(r.ok && r.buttons.size() == 2);

    r = parse_pad_combo("NONE", fb, "pad_freeze", &warnings);
    CHECK(r.ok && r.buttons.empty());

    warnings.clear();
    r = parse_pad_combo("A+FOO", fb, "pad_toggle", &warnings);
    CHECK(!r.ok && r.buttons.size() == 2);
    CHECK_INT((int)warnings.size(), 1);

    warnings.clear();
    r = parse_pad_combo("", fb, "pad_toggle", &warnings);
    CHECK(!r.ok && warnings.size() == 1);

    warnings.clear();
    r = parse_pad_combo("NONE+A", fb, "pad_toggle", &warnings);
    CHECK(!r.ok && warnings.size() == 1);

    CHECK_STR(pad_combo_name(fb), "LS+RS");
    CHECK_STR(pad_combo_name({}), "NONE");

    // 默认配置文本包含手柄节，且能无损往返解析
    const std::string defaults = default_config_text();
    CHECK(defaults.find("[gamepad]") != std::string::npos);
    CHECK(defaults.find("pad_toggle = LS+RS") != std::string::npos);
    const ConfigLoadResult rt = parse_config(defaults);
    CHECK_INT((int)rt.warnings.size(), 0);
    CHECK_STR(rt.cfg.pad_toggle, "LS+RS");
    CHECK(rt.cfg.pad_enabled);
    CHECK_NEAR(rt.cfg.pad_look_speed, 180.0, 1e-9);
    CHECK_NEAR(rt.cfg.pad_deadzone, 0.15, 1e-9);
}

void test_gamepad_deadzone() {
    section("apply_stick_deadzone");
    using namespace pbzpm;
    double x = 0.1, y = 0.0;
    apply_stick_deadzone(x, y, 0.15);
    CHECK_NEAR(x, 0.0, 1e-9);

    x = 1.0;
    apply_stick_deadzone(x, y, 0.15);
    CHECK_NEAR(x, 1.0, 1e-9);

    x = 0.5;
    apply_stick_deadzone(x, y, 0.25);
    CHECK_NEAR(x, 1.0 / 3.0, 1e-9);  // (0.5-0.25)/(1-0.25)

    x = 0.0, y = -1.0;
    apply_stick_deadzone(x, y, 0.15);
    CHECK_NEAR(y, -1.0, 1e-9);

    x = 0.05, y = 0.05;  // 幅度 0.0707 < 0.15
    apply_stick_deadzone(x, y, 0.15);
    CHECK_NEAR(x, 0.0, 1e-9);
    CHECK_NEAR(y, 0.0, 1e-9);

    x = 0.7, y = 0.0;
    apply_stick_deadzone(x, y, 0.0);
    CHECK_NEAR(x, 0.7, 1e-9);  // 死区 0 不缩放

    x = 5.0;
    apply_stick_deadzone(x, y, 0.0);
    CHECK_NEAR(x, 1.0, 1e-9);  // 分量夹紧
}

void test_gamepad_input_logic() {
    section("InputLogic 手柄");
    using namespace pbzpm;

    Config cfg;
    std::vector<std::string> warnings;
    const KeyMap keys = build_keymap(cfg, &warnings);
    CHECK(keys.all_resolved);
    CHECK_INT((int)warnings.size(), 0);
    CHECK_INT((int)keys.pad_toggle.size(), 2);

    InputLogic input;
    Intent in;

    // 组合键边沿：按住只触发一次，松开再按才再触发
    PadState combo;
    combo.held[static_cast<int>(PadButton::LS)] = true;
    combo.held[static_cast<int>(PadButton::RS)] = true;
    input.on_gamepad(combo);
    in = input.build(cfg, keys);
    CHECK(in.toggle_mode);
    input.end_frame();
    in = input.build(cfg, keys);  // 仍按住
    CHECK(!in.toggle_mode);
    input.end_frame();
    input.on_gamepad(PadState{});  // 松开
    input.end_frame();
    input.on_gamepad(combo);  // 再按
    in = input.build(cfg, keys);
    CHECK(in.toggle_mode);
    input.end_frame();
    input.reset();

    // enabled=false 时不触发
    Config off = cfg;
    off.pad_enabled = false;
    input.on_gamepad(combo);
    in = input.build(off, keys);
    CHECK(!in.toggle_mode);
    input.end_frame();
    input.reset();

    // handle_toggle 会调 reset()：此时组合键仍按住，不能立刻再次触发（否则进出死循环）
    {
        InputLogic fresh;  // 全新状态机，不带上文
        fresh.on_gamepad(combo);
        in = fresh.build(cfg, keys);
        CHECK(in.toggle_mode);
        fresh.end_frame();  // prev = combo
        fresh.reset();      // 模拟退出模式时的 input_.reset()
        in = fresh.build(cfg, keys);
        CHECK(!in.toggle_mode);  // 仍按住：没有新的按下沿
        fresh.end_frame();
        fresh.on_gamepad(PadState{});  // 松开
        fresh.end_frame();
        fresh.on_gamepad(combo);  // 再按
        in = fresh.build(cfg, keys);
        CHECK(in.toggle_mode);
        fresh.end_frame();
    }
    input.reset();

    // NONE 解绑后不触发，且不算解析失败
    {
        Config unbind = cfg;
        unbind.pad_menu = "NONE";
        std::vector<std::string> w2;
        const KeyMap k2 = build_keymap(unbind, &w2);
        CHECK(k2.all_resolved);
        CHECK(k2.pad_menu.empty());
        PadState s;
        s.held[static_cast<int>(PadButton::Start)] = true;
        input.on_gamepad(s);
        CHECK(!input.build(unbind, k2).toggle_menu);
        input.end_frame();
        input.reset();
    }

    // 非法按钮名：回退默认组合并记 warning
    {
        Config bad = cfg;
        bad.pad_toggle = "FOO";
        std::vector<std::string> w3;
        const KeyMap k3 = build_keymap(bad, &w3);
        CHECK(!k3.all_resolved);
        CHECK_INT((int)w3.size(), 1);
        CHECK_INT((int)k3.pad_toggle.size(), 2);
    }

    // 自定义组合 + 扳机当按钮（rt >= 0.3 视为按下）
    {
        Config custom = cfg;
        custom.pad_freeze = "RB + RT";
        std::vector<std::string> w4;
        const KeyMap k4 = build_keymap(custom, &w4);
        CHECK(k4.all_resolved);
        PadState s;
        s.rt = 1.0;
        s.held[static_cast<int>(PadButton::RB)] = true;
        input.on_gamepad(s);
        CHECK(input.build(custom, k4).toggle_freeze);
        input.end_frame();
        PadState s2 = s;
        s2.rt = 0.29;  // 阈值以下
        input.on_gamepad(s2);
        CHECK(!input.build(custom, k4).toggle_freeze);
        input.end_frame();
        input.reset();
    }

    // 左摇杆移动：满行程 ±1；半行程按死区缩放
    PadState stick;
    stick.ly = 1.0;
    input.on_gamepad(stick);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.move_forward, 1.0, 1e-9);
    CHECK_NEAR(in.move_right, 0.0, 1e-9);
    PadState stick_half;
    stick_half.ly = 0.5;
    stick_half.lx = -0.5;  // 幅度 0.7071：径向死区后整体缩放，方向不变
    input.on_gamepad(stick_half);
    in = input.build(cfg, keys);
    const double mag = std::sqrt(0.5);
    const double scaled = (mag - 0.15) / 0.85;
    CHECK_NEAR(in.move_forward, scaled * (0.5 / mag), 1e-9);
    CHECK_NEAR(in.move_right, -scaled * (0.5 / mag), 1e-9);
    input.end_frame();
    input.reset();

    // 右摇杆视角：折算成度/秒；摇杆上推（ry 正）= 向上 = dy 负
    Config look_cfg = cfg;
    look_cfg.pad_look_speed = 100.0;
    PadState rstick;
    rstick.rx = 1.0;
    input.on_gamepad(rstick);
    in = input.build(look_cfg, keys);
    CHECK_NEAR(in.pad_look_dx, 100.0, 1e-9);
    CHECK_NEAR(in.pad_look_dy, 0.0, 1e-9);
    input.end_frame();
    PadState rstick_up;
    rstick_up.ry = 1.0;
    input.on_gamepad(rstick_up);
    in = input.build(look_cfg, keys);
    CHECK_NEAR(in.pad_look_dx, 0.0, 1e-9);
    CHECK_NEAR(in.pad_look_dy, -100.0, 1e-9);
    input.end_frame();
    input.reset();

    // 手柄视角不受 free_look_requires_alt 限制；鼠标增量仍被挡
    Config alt_cfg = cfg;
    alt_cfg.free_look_requires_alt = true;
    InputLogic logic2;
    logic2.on_mouse_move(10.0, 0.0);
    PadState r2;
    r2.rx = 1.0;
    logic2.on_gamepad(r2);
    in = logic2.build(alt_cfg, keys);
    CHECK_NEAR(in.look_dx, 0.0, 1e-9);
    CHECK_NEAR(in.pad_look_dx, cfg.pad_look_speed, 1e-9);
    logic2.end_frame();

    // RT/LT 升降（模拟量）+ LB/RB 变速
    PadState trig;
    trig.rt = 1.0;
    trig.lt = 0.5;
    trig.held[static_cast<int>(PadButton::LB)] = true;
    trig.held[static_cast<int>(PadButton::RB)] = true;
    input.on_gamepad(trig);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.move_up, 0.5, 1e-9);
    CHECK(in.boost);
    CHECK(in.slow);
    input.end_frame();
    input.reset();

    // FOV 步进：手柄十字键与键盘 FOV 键（此前键盘 FOV 键只解析未生效，现在一并接上）
    PadState dpad;
    dpad.held[static_cast<int>(PadButton::Up)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    input.on_key(keys.fov_down, true);
    in = input.build(cfg, keys);
    CHECK_INT(in.fov_nudge, -1);
    input.end_frame();
    input.reset();

    // 手柄冻结/重置键
    PadState btns;
    btns.held[static_cast<int>(PadButton::X)] = true;
    btns.held[static_cast<int>(PadButton::Y)] = true;
    input.on_gamepad(btns);
    in = input.build(cfg, keys);
    CHECK(in.toggle_freeze);
    CHECK(in.reset_view);
    CHECK(!in.toggle_menu);
    input.end_frame();
}

void test_photo_mode_gamepad() {
    section("PhotoMode 手柄");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    fe.pov.loc = Vec3{0.0, 0.0, 0.0};
    fe.pov.rot = Rot{0.0, 0.0, 0.0};
    fe.pov.fov = 90.0;
    Config cfg;
    cfg.pad_look_speed = 100.0;
    PhotoMode mode(fe, cfg);
    CHECK(mode.enter());

    // 右摇杆右推到底：100 度/秒 * 0.25 秒 = yaw +25（tick 内部把 dt 夹到 0.25）
    Intent look;
    look.pad_look_dx = 100.0;
    fe.reset_calls();
    mode.tick(look, 0.25);
    CHECK_NEAR(mode.pose().rot.yaw, 25.0, 1e-9);
    CHECK(fe.has("apply_pose"));

    // 摇杆上推（pad_look_dy 负）-> pitch 增大
    Intent look_up;
    look_up.pad_look_dy = -40.0;
    mode.tick(look_up, 0.25);
    CHECK_NEAR(mode.pose().rot.pitch, 10.0, 1e-9);

    // invert_y 同时作用于手柄
    Config inv = cfg;
    inv.invert_y = true;
    PhotoMode mode2(fe, inv);
    CHECK(mode2.enter());
    Intent stick_up;
    stick_up.pad_look_dy = -20.0;  // 摇杆上推
    mode2.tick(stick_up, 0.25);    // 反转后 pitch -5
    CHECK_NEAR(mode2.pose().rot.pitch, -5.0, 1e-9);
    mode2.leave();

    // FOV 步进：+1 格 / -1 格
    const double fov0 = mode.fov();
    Intent fov_up;
    fov_up.fov_nudge = 1;
    mode.tick(fov_up, 0.016);
    CHECK_NEAR(mode.fov(), fov0 + cfg.fov_step, 1e-9);
    Intent fov_dn;
    fov_dn.fov_nudge = -1;
    mode.tick(fov_dn, 0.016);
    CHECK_NEAR(mode.fov(), fov0, 1e-9);

    // 左摇杆半程 = 半速；满行程 = 全速
    {
        FakeEngine fe2;
        Config c2;
        c2.move_speed = 100.0;
        PhotoMode m2(fe2, c2);
        CHECK(m2.enter());
        const Vec3 before = m2.pose().loc;
        Intent half;
        half.move_forward = 0.5;
        m2.tick(half, 0.25);
        const double dist_half = std::sqrt(std::pow(m2.pose().loc.x - before.x, 2) +
                                           std::pow(m2.pose().loc.y - before.y, 2) +
                                           std::pow(m2.pose().loc.z - before.z, 2));
        CHECK_NEAR(dist_half, 12.5, 1e-9);

        const Vec3 before2 = m2.pose().loc;
        Intent full;
        full.move_forward = 1.0;
        m2.tick(full, 0.25);
        const double dist_full = std::sqrt(std::pow(m2.pose().loc.x - before2.x, 2) +
                                           std::pow(m2.pose().loc.y - before2.y, 2) +
                                           std::pow(m2.pose().loc.z - before2.z, 2));
        CHECK_NEAR(dist_full, 25.0, 1e-9);

        // 键盘对角仍归一化到全速（不因改动变快/变慢）
        const Vec3 before3 = m2.pose().loc;
        Intent diag;
        diag.move_forward = 1.0;
        diag.move_right = 1.0;
        m2.tick(diag, 0.1);
        const double dist_diag = std::sqrt(std::pow(m2.pose().loc.x - before3.x, 2) +
                                           std::pow(m2.pose().loc.y - before3.y, 2) +
                                           std::pow(m2.pose().loc.z - before3.z, 2));
        CHECK_NEAR(dist_diag, 10.0, 1e-9);

        // RT 升 / LT 降（vertical_speed 默认 200）
        Intent up;
        up.move_up = 1.0;
        m2.tick(up, 0.25);
        CHECK_NEAR(m2.pose().loc.z, 50.0, 1e-9);
        Intent down;
        down.move_up = -1.0;
        m2.tick(down, 0.25);
        CHECK_NEAR(m2.pose().loc.z, 0.0, 1e-9);
    }
}

// ---------------------------------------------------------------- WBP 资产菜单变量桥

void test_wbp_bridge() {
    using namespace pbzpm;
    section("WbpBridge");
    {
        // 无效快照：既无命令也无写回（控件不可用时绝不能往别处写属性）
        WbpBridge b;
        WbpWriteBack out;
        WbpVars v;
        v.valid = false;
        v.fov = 999;
        v.command = 3;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed && !c.speed_changed && !c.sens_changed);
        CHECK(!c.toggle_freeze && !c.reset_view && !c.exit_mode);
        CHECK(!out.write_fov && !out.write_speed && !out.write_sens);
        CHECK(!out.write_frozen && !out.write_status && !out.write_command);
    }
    {
        // 未 on_opened 直接 step：以外部真值为基线自举，并把真值推给 BP
        WbpBridge b;
        WbpWriteBack out;
        WbpVars v;
        v.valid = true;
        v.fov = 90;
        v.speed = 400;
        v.sens = 0.1f;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed && !c.speed_changed && !c.sens_changed);
        CHECK(out.write_fov);
        CHECK_NEAR(out.fov, 90.0, 1e-6);
        CHECK(out.write_speed);
        CHECK_NEAR(out.speed, 400.0, 1e-6);
        CHECK(out.write_sens);
        CHECK_NEAR(out.sens, 0.1, 1e-6);
        CHECK(out.write_frozen && !out.frozen);
        CHECK(out.write_status && out.status == 1);
        CHECK(!out.write_command);
    }
    {
        // on_opened 之后的正常帧：同值无命令，状态写回仍在
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpWriteBack out;
        WbpVars v;
        v.valid = true;
        v.fov = 90;
        v.speed = 400;
        v.sens = 0.1f;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed && !c.speed_changed && !c.sens_changed);
        CHECK(out.write_fov && out.write_speed && out.write_sens);  // 首帧 push_pending：三个真值全推
        CHECK_NEAR(out.fov, 90.0, 1e-6);
        out = WbpWriteBack{};
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!out.write_fov && !out.write_speed && !out.write_sens);  // 真值没变 → 不再推
        CHECK(out.write_frozen && out.write_status);                   // 状态仍然每帧推
    }
    {
        // 滑条变化 → 命令；同值不重复；微抖动（< eps）不触发
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        v.fov = 90;
        v.speed = 400;
        v.sens = 0.1f;
        WbpWriteBack out;
        b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);  // warmup：消耗 push_pending
        v.fov = 90.03f;  // < kFovEps(0.05)
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed);
        v.fov = 100.0f;
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.fov_changed);
        CHECK_NEAR(c.fov, 100.0, 1e-9);
        CHECK(!out.write_fov);  // 未夹紧：不回写
        // 外部真值追上来后（相机已应用 100）：命令不重复，真值回显推一次给 BP
        c = b.step(v, 100, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed);
        CHECK(out.write_fov);
        CHECK_NEAR(out.fov, 100.0, 1e-6);
        out = WbpWriteBack{};
        c = b.step(v, 100, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!out.write_fov);  // 回显也只推一次
    }
    {
        // 超上限：命令夹紧；拖动中不回写；松手后回写夹紧值
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        v.fov = 90;
        v.speed = 400;
        v.sens = 0.1f;
        WbpWriteBack out;
        b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);  // warmup：消耗 push_pending
        v.fov = 160.0f;  // 轨道渲染上限 150 之外（理论上不会出现，防资产轨道配错）
        v.dragging = true;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.fov_changed);
        CHECK_NEAR(c.fov, 150.0, 1e-9);
        CHECK(!out.write_fov);  // 拖动中：不回写，避免和拖拽打架
        v.dragging = false;
        c = b.step(v, 150, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.fov_changed);   // 残余差仍在 → 仍下发（夹紧后同值，无副作用）
        CHECK_NEAR(c.fov, 150.0, 1e-9);
        CHECK(out.write_fov);   // 松手：回写夹紧值，滑条弹回
        CHECK_NEAR(out.fov, 150.0, 1e-6);
    }
    {
        // 灵敏度低于下限：命令夹紧
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        v.sens = 0.001f;
        WbpWriteBack out;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.sens_changed);
        CHECK_NEAR(c.sens, 0.01, 1e-9);
    }
    {
        // 速度：变化下发 + 夹紧到 [0, 2000]
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        v.speed = 3000.0f;
        WbpWriteBack out;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.speed_changed);
        CHECK_NEAR(c.speed, 2000.0, 1e-9);
    }
    {
        // 冻结状态每帧同步
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        WbpWriteBack out;
        b.step(v, 90, 400, 0.1, true, 15, 150, 0.01, 0.5, out);
        CHECK(out.write_frozen && out.frozen);
        CHECK(out.write_status && out.status == 2);
    }
    {
        // 命令沿：执行 + 写回 0；写回未生效前不重复；归零后可再次触发
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        WbpWriteBack out;
        v.command = 1;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.toggle_freeze);
        CHECK(!c.reset_view && !c.exit_mode);
        CHECK(out.write_command);
        CHECK_INT(out.command, 0);
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.toggle_freeze);  // 同值（写回还没被 BP 看到）不双触发
        v.command = 0;
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!out.write_command || out.command == 0);
        v.command = 1;
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.toggle_freeze);  // 归零后可再次触发
    }
    {
        // 三种命令各有出口；未知命令忽略但清零
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        WbpVars v;
        v.valid = true;
        WbpWriteBack out;
        v.command = 2;
        auto c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.reset_view && !c.toggle_freeze);
        v.command = 0;
        b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        v.command = 3;
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(c.exit_mode);
        v.command = 0;
        b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        v.command = 7;
        c = b.step(v, 90, 400, 0.1, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.toggle_freeze && !c.reset_view && !c.exit_mode);
        CHECK(out.write_command);  // 仍要清零，免得卡在未知值上
    }
    {
        // reset + 重新 on_opened：新基线，不产生伪命令
        WbpBridge b;
        b.on_opened(90, 400, 0.1);
        b.reset();
        b.on_opened(60, 300, 0.05);
        WbpVars v;
        v.valid = true;
        v.fov = 60;
        v.speed = 300;
        v.sens = 0.05f;
        WbpWriteBack out;
        auto c = b.step(v, 60, 300, 0.05, false, 15, 150, 0.01, 0.5, out);
        CHECK(!c.fov_changed && !c.speed_changed && !c.sens_changed);
        CHECK(out.write_fov);
        CHECK_NEAR(out.fov, 60.0, 1e-6);
    }
}

int main() {
    std::printf("PBZ Photo Mode (C++) 核心层测试\n");

    test_vk_names();
    test_config_defaults_and_parse();
    test_config_warnings_and_clamps();
    test_config_default_text_roundtrip();
    test_math();
    test_input_logic();
    test_photo_mode_happy_path();
    test_photo_mode_failures_rollback();
    test_photo_mode_tick();
    test_photo_mode_freeze();
    test_photo_mode_auto_pause();
    test_photo_mode_reassert_and_partial();
    test_photo_mode_input_blocking();
    test_photo_mode_live_toggles();
    test_menu_model();
    test_gamepad_names();
    test_gamepad_parse_combo();
    test_gamepad_deadzone();
    test_gamepad_input_logic();
    test_photo_mode_gamepad();
    test_wbp_bridge();

    std::printf("\n----------------------------------------\n");
    std::printf("检查 %d 项，失败 %d 项\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("存在失败项\n");
    return 1;
}
