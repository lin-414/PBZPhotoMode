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
                     "hud_off", "save_postfx", "get_time_dilation", "apply_fov", "apply_pose"},
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
    CHECK(!mode.frozen());
    CHECK(mode.menu_visible());

    // 再进一次不应重复 spawn
    fe.reset_calls();
    CHECK(mode.enter());
    CHECK_INT(fe.count("spawn_camera"), 0);

    // 退出：严格逆序还原，且只还原做过的步骤
    fe.reset_calls();
    mode.leave("test");
    expect_sequence(fe, {"restore_postfx", "hud_on", "block_input_off", "ignore_move_off", "restore_view", "destroy_camera"},
                    "退出");
    CHECK(!mode.active());
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
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        Intent freeze;
        freeze.toggle_freeze = true;
        mode.tick(freeze, 0.016);
        CHECK(mode.frozen());
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
    PhotoMode mode(fe, Config{});
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
    test_photo_mode_reassert_and_partial();
    test_photo_mode_input_blocking();
    test_menu_model();

    std::printf("\n----------------------------------------\n");
    std::printf("检查 %d 项，失败 %d 项\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("存在失败项\n");
    return 1;
}
