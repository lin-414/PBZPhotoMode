// 纯核心层测试：不加载 UE4SS、不启动游戏，直接用 cl/cmake 编译即可跑。
// 覆盖：配置解析、键名映射、输入状态机、数学约定、摄影模式状态机（含回滚）。
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/math3.h"
#include "core/photo_mode.h"
#include "core/vk.h"
#include "pbzfm_schema_map.h"
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
    CHECK_STR(empty.cfg.key_freeze, "F");
    CHECK_STR(empty.cfg.key_reset, "R");
    CHECK_STR(empty.cfg.key_roll_reset, "Z");  // 滚转归零（1.0.41 并入）
    CHECK_NEAR(empty.cfg.move_speed, 400.0, 1e-9);
    CHECK(empty.cfg.free_look_requires_alt);
    CHECK(empty.cfg.block_game_input);
    CHECK(!empty.cfg.hide_player);
    CHECK(empty.cfg.hide_game_hud);  // 默认进入摄影模式即隐藏游戏 HUD
    CHECK(empty.cfg.hide_interact_prompt);  // 默认隐藏交互提示（E 键图标）
CHECK(empty.cfg.hide_enemy_health_bars);  // 默认隐藏敌人（怪物）血条
CHECK(empty.cfg.hide_subtitles);  // 默认隐藏对话字幕
    CHECK(empty.cfg.auto_pause_time);  // 默认进入摄影模式即自动暂停时间
    CHECK(empty.cfg.freeze_sequences);  // 默认冻结时同时暂停剧情播放器
    CHECK(empty.cfg.stop_wwise_on_freeze);  // 默认冻结时静音 Wwise 组件（对话流程已冻结，可关）
    CHECK(empty.cfg.ultrawide_unlock);  // 默认进入摄影模式即解除 16:9 画幅锁（超宽屏）
    CHECK(empty.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
    CHECK_NEAR(empty.cfg.vertical_speed, 200.0, 1e-9);
    CHECK_NEAR(empty.cfg.roll_speed, 60.0, 1e-9);  // Q/E 滚转默认 60 度/秒
    CHECK_NEAR(empty.cfg.pitch_speed, 30.0, 1e-9);  // 俯仰默认 30 度/秒（方向键与十字键上/下）
    CHECK_NEAR(empty.cfg.slow_look_mult, 0.1, 1e-9);  // Ctrl/RS 按下时视角速度倍率（1.0.37 新增）
    CHECK_NEAR(empty.cfg.yaw_speed, 90.0, 1e-9);  // 方向键/十字键转向速度（1.0.46 起左右键转视角）
    CHECK_STR(empty.cfg.key_pitch_up, "UP");    // 方向键上/下 = 俯仰（1.0.27 起从 Z/X 改来）
    CHECK_STR(empty.cfg.key_pitch_down, "DOWN");
    CHECK_STR(empty.cfg.key_yaw_left, "LEFT");   // 方向键左/右 = 左右转视角（1.0.46 起从平移改来）
    CHECK_STR(empty.cfg.key_yaw_right, "RIGHT");
    CHECK_STR(empty.cfg.pad_pitch_up, "UP");    // 十字键上/下 = 俯仰
    CHECK_STR(empty.cfg.pad_pitch_down, "DOWN");
    CHECK_STR(empty.cfg.pad_roll_reset, "LB+RB");  // LB+RB 同按 = 滚转归零（1.0.41 并入）
    CHECK_STR(empty.cfg.pad_yaw_left, "LEFT");   // 十字键左/右 = 左右转视角（1.0.46 起从平移改来）
    CHECK_STR(empty.cfg.pad_yaw_right, "RIGHT");
    CHECK_STR(empty.cfg.pad_fov_up, "A");    // FOV 移到 A/B（十字键已全部让位给视角调节）
    CHECK_STR(empty.cfg.pad_fov_down, "B");
    CHECK(empty.cfg.freeze_mode == FreezeMode::Pause);

    const std::string ini = R"(
; 注释行
# 另一种注释
[keys]
key_toggle = F9          ; 行尾注释
key_freeze = "G"
key_fov_up = OEM_PLUS
key_pitch_up = C
key_pitch_down = V
key_yaw_left = INSERT
key_yaw_right = DELETE

[control]
move_speed = 250.5
boost_mult = 2
slow_mult = 0.5
slow_look_mult = 0.05
invert_y = yes
free_look_requires_alt = false
mouse_vertical_mode = look
vertical_speed = 350
roll_speed = 120
pitch_speed = 45
yaw_speed = 120

[camera]
fov_default = 75
fov_min = 20
fov_max = 120
ultrawide_unlock = false

[effects]
auto_pause_time = false
freeze_sequences = false
stop_wwise_on_freeze = true

[misc]
log_level = debug
dry_run = on
)";
    const ConfigLoadResult parsed = parse_config(ini, "test.ini");
    CHECK_INT(parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F9");
    CHECK_STR(parsed.cfg.key_freeze, "G");
    CHECK_STR(parsed.cfg.key_fov_up, "OEM_PLUS");
    CHECK_STR(parsed.cfg.key_pitch_up, "C");
    CHECK_STR(parsed.cfg.key_pitch_down, "V");
    CHECK_STR(parsed.cfg.key_yaw_left, "INSERT");
    CHECK_STR(parsed.cfg.key_yaw_right, "DELETE");
    CHECK_NEAR(parsed.cfg.move_speed, 250.5, 1e-9);
    CHECK_NEAR(parsed.cfg.boost_mult, 2.0, 1e-9);
    CHECK(parsed.cfg.invert_y);
    CHECK(!parsed.cfg.free_look_requires_alt);
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Look);
    CHECK_NEAR(parsed.cfg.vertical_speed, 350.0, 1e-9);
    CHECK_NEAR(parsed.cfg.roll_speed, 120.0, 1e-9);
    CHECK_NEAR(parsed.cfg.pitch_speed, 45.0, 1e-9);
    CHECK_NEAR(parsed.cfg.slow_look_mult, 0.05, 1e-9);
    CHECK_NEAR(parsed.cfg.yaw_speed, 120.0, 1e-9);
    CHECK_NEAR(parsed.cfg.fov_default, 75.0, 1e-9);
    CHECK(!parsed.cfg.ultrawide_unlock);
    CHECK(!parsed.cfg.auto_pause_time);
    CHECK(!parsed.cfg.freeze_sequences);
    CHECK(parsed.cfg.stop_wwise_on_freeze);
    CHECK(parsed.cfg.log_level == LogLevel::Debug);
    CHECK(parsed.cfg.dry_run);
    CHECK_STR(parsed.source, "test.ini");

    // 旧键名兼容（≤1.0.45 的 key_pan_*/pad_pan_* 是左右平移，1.0.46 起映射到转向键）
    const ConfigLoadResult legacy = parse_config(
        "key_pan_left = INSERT\nkey_pan_right = DELETE\npad_pan_left = HOME\npad_pan_right = END\n", "legacy.ini");
    CHECK_INT(legacy.warnings.size(), 0);
    CHECK_STR(legacy.cfg.key_yaw_left, "INSERT");
    CHECK_STR(legacy.cfg.key_yaw_right, "DELETE");
    CHECK_STR(legacy.cfg.pad_yaw_left, "HOME");
    CHECK_STR(legacy.cfg.pad_yaw_right, "END");
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
roll_speed = 0
pitch_speed = 0
slow_look_mult = 0
yaw_speed = 0
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
    CHECK_NEAR(parsed.cfg.roll_speed, 60.0, 1e-9);  // 非正值回退默认
    CHECK_NEAR(parsed.cfg.pitch_speed, 30.0, 1e-9);  // 俯仰速度同规则
    CHECK_NEAR(parsed.cfg.slow_look_mult, 0.1, 1e-9);   // 越界回退默认
    CHECK_NEAR(parsed.cfg.yaw_speed, 90.0, 1e-9);   // 非正值回退默认
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
    CHECK(text.find("roll_speed") != std::string::npos);
    CHECK(text.find("key_pitch_up") != std::string::npos);
    CHECK(text.find("pad_pitch_up") != std::string::npos);
    CHECK(text.find("pitch_speed") != std::string::npos);
    CHECK(text.find("slow_look_mult") != std::string::npos);
    CHECK(text.find("yaw_speed") != std::string::npos);
    const ConfigLoadResult parsed = parse_config(text, "generated");
    CHECK_INT(parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F2");
    CHECK_NEAR(parsed.cfg.fov_step, 2.0, 1e-9);
    CHECK(parsed.cfg.ultrawide_unlock);
    CHECK(parsed.cfg.auto_pause_time);
    CHECK(parsed.cfg.freeze_sequences);
    CHECK(parsed.cfg.stop_wwise_on_freeze);  // 默认文本同样开启
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
    CHECK_NEAR(parsed.cfg.roll_speed, 60.0, 1e-9);
    CHECK_NEAR(parsed.cfg.pitch_speed, 30.0, 1e-9);
    CHECK_NEAR(parsed.cfg.slow_look_mult, 0.1, 1e-9);
    CHECK_NEAR(parsed.cfg.yaw_speed, 90.0, 1e-9);
    CHECK_STR(parsed.cfg.key_pitch_up, "UP");  // 默认文本里俯仰/转向键可被原样解析回来
    CHECK_STR(parsed.cfg.key_yaw_left, "LEFT");
    CHECK_STR(parsed.cfg.pad_fov_up, "A");
    CHECK_STR(parsed.cfg.pad_yaw_right, "RIGHT");
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
    CHECK_NEAR(wrap_angle_180(190.0), -170.0, 1e-9);  // 滚转折回对称区间
    CHECK_NEAR(wrap_angle_180(-190.0), 170.0, 1e-9);
    CHECK_NEAR(wrap_angle_180(360.0), 0.0, 1e-9);

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
    CHECK_INT(keys.freeze, 'F');
    CHECK_INT(keys.reset, 'R');
    CHECK_INT(keys.roll_reset, 'Z');
    CHECK_INT(keys.pitch_up, 0x26);    // 默认方向键：上仰视 / 下俯视 / 左右转视角（1.0.46 起）
    CHECK_INT(keys.pitch_down, 0x28);
    CHECK_INT(keys.yaw_left, 0x25);
    CHECK_INT(keys.yaw_right, 0x27);

    InputLogic in;
    in.on_key('W', true);
    Intent intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_forward, 1.0, 1e-9);

    in.on_key('R', true);
    intent = in.build(cfg, keys);
    CHECK(intent.reset_view);
    in.on_key('R', false);
    in.end_frame();

    // Z：滚转归零按下沿（1.0.41 并入）；与 R 互不串扰
    in.on_key('Z', true);
    intent = in.build(cfg, keys);
    CHECK(intent.reset_roll);
    CHECK(!intent.reset_view);
    in.on_key('Z', false);
    in.end_frame();
    intent = in.build(cfg, keys);
    CHECK(!intent.reset_roll);  // 松开后不再触发（按下沿）

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

    // Q/E：v1.9.0 起改为滚转（E=+1 顺时针 / Q=-1 逆时针），不再参与上下平移
    in.on_key('E', true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.roll, 1.0, 1e-9);
    CHECK_NEAR(intent.move_up, 0.0, 1e-9);
    in.on_key('E', false);
    in.on_key('Q', true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.roll, -1.0, 1e-9);
    CHECK_NEAR(intent.move_up, 0.0, 1e-9);
    in.on_key('Q', false);
    in.reset();

    // Q/E 滚转与鼠标左右键升降同帧叠加互不干扰
    in.on_key('Q', true);
    in.on_mouse_button(MouseButton::Left, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.roll, -1.0, 1e-9);
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);
    in.on_key('Q', false);
    in.on_mouse_button(MouseButton::Left, false);
    in.reset();

    // 方向键俯仰（1.0.27 起 Z/X 改方向键）：上 仰视（+1）/ 下 俯视（-1），同按抵消；与滚转互不影响
    in.on_key(keys.pitch_up, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.pitch_axis, 1.0, 1e-9);
    CHECK_NEAR(intent.roll, 0.0, 1e-9);
    in.end_frame();
    in.on_key(keys.pitch_up, false);
    in.on_key(keys.pitch_down, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.pitch_axis, -1.0, 1e-9);
    in.end_frame();
    in.on_key(keys.pitch_up, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.pitch_axis, 0.0, 1e-9);  // 同按抵消
    in.on_key(keys.pitch_up, false);
    in.on_key(keys.pitch_down, false);
    in.reset();

    // 方向键左右转视角（1.0.46 起从平移改为偏航）：电平 +1 右转 / -1 左转，同按抵消；不再并入 move_right
    in.on_key(keys.yaw_left, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.yaw_axis, -1.0, 1e-9);
    CHECK_NEAR(intent.move_right, 0.0, 1e-9);
    in.end_frame();
    in.on_key(keys.yaw_left, false);
    in.on_key(keys.yaw_right, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.yaw_axis, 1.0, 1e-9);
    in.end_frame();
    in.on_key(keys.yaw_left, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.yaw_axis, 0.0, 1e-9);  // 左右同按抵消
    in.on_key(keys.yaw_left, false);
    in.on_key(keys.yaw_right, false);
    in.reset();

    // look 模式：左右键变俯仰，与 Q/E 滚转互不影响
    Config look_cfg = cfg;
    look_cfg.mouse_vertical_mode = MouseVerticalMode::Look;
    in.on_mouse_button(MouseButton::Left, true);
    intent = in.build(look_cfg, keys);
    CHECK(intent.pitch_up_click);
    CHECK(!intent.pitch_down_click);
    CHECK_NEAR(intent.move_up, 0.0, 1e-9);
    CHECK_NEAR(intent.roll, 0.0, 1e-9);
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
                     "hud_off", "prompt_off", "bars_off", "lockon_off", "subs_off", "aspect_unlock", "save_postfx",
                     "get_time_dilation", "apply_fov", "apply_pose", "pause_on", "shakes_freeze", "wind_freeze",
                     "mpc_snapshot", "sequences_pause", "wwise_pause"},
                    "首次进入");

    CHECK(mode.active());
    CHECK(fe.camera_exists);
    CHECK(fe.view_attached);
    CHECK(fe.ignore_move);
    CHECK(fe.input_blocked);
    CHECK(!fe.player_hidden);  // 默认不隐藏角色：摄影模式要能把自己拍进去
    CHECK(!fe.hud_visible);    // 默认隐藏游戏 HUD
    CHECK(!fe.prompts_visible);  // 默认隐藏交互提示
    CHECK(!fe.bars_visible);     // 默认隐藏敌人血条
    CHECK(!fe.subtitles_visible);  // 默认隐藏对话字幕
    CHECK_NEAR(mode.fov(), 88.0, 1e-9);
    CHECK_NEAR(mode.pose().rot.yaw, 45.0, 1e-9);
    CHECK(mode.frozen());  // 默认 auto_pause_time=true：进入即冻结
    CHECK(fe.paused);

    // 再进一次不应重复 spawn
    fe.reset_calls();
    CHECK(mode.enter());
    CHECK_INT(fe.count("spawn_camera"), 0);

    // 退出：严格逆序还原，且只还原做过的步骤
    fe.reset_calls();
    mode.leave("test");
    expect_sequence(fe,
                    {"wind_restore", "shakes_restore", "sequences_resume", "pause_off",
                     "wwise_resume", "restore_postfx", "restore_aspect",
                     "hud_on", "prompt_on", "bars_on", "lockon_on", "subs_on", "block_input_off", "ignore_move_off",
                     "restore_view", "destroy_camera"},
                    "退出");
    CHECK(!mode.active());
    CHECK(!fe.paused);
    CHECK(!fe.camera_exists);
    CHECK(!fe.view_attached);
    CHECK(!fe.ignore_move);
    CHECK(!fe.input_blocked);
    CHECK(!fe.player_hidden);
    CHECK(fe.hud_visible);  // 退出后恢复
    CHECK(fe.prompts_visible);  // 交互提示退出后恢复
    CHECK(fe.bars_visible);     // 敌人血条退出后恢复
    CHECK(fe.subtitles_visible);  // 对话字幕退出后恢复
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
        fe_reset.pov.rot = Rot{5.0, 45.0, 12.0};
        fe_reset.pov.fov = 80.0;
        Config cfg_reset;
        PhotoMode resetter(fe_reset, cfg_reset);
        CHECK(resetter.enter());

        Intent move;
        move.move_forward = 1.0;
        move.look_dy = 10.0;
        move.roll = 1.0;
        move.wheel = 2.0;
        resetter.tick(move, 0.1);
        CHECK(resetter.pose().loc.x > fe_reset.pov.loc.x);  // 先确实移动/转/改 FOV
        CHECK(resetter.pose().rot.pitch < fe_reset.pov.rot.pitch);
        CHECK(resetter.pose().rot.roll > fe_reset.pov.rot.roll);  // 滚转也确实动过
        CHECK(resetter.fov() < fe_reset.pov.fov);

        Intent reset;
        reset.reset_view = true;
        resetter.tick(reset, 0.016);
        CHECK_NEAR(resetter.pose().loc.x, fe_reset.pov.loc.x, 1e-9);
        CHECK_NEAR(resetter.pose().loc.y, fe_reset.pov.loc.y, 1e-9);
        CHECK_NEAR(resetter.pose().loc.z, fe_reset.pov.loc.z, 1e-9);
        CHECK_NEAR(resetter.pose().rot.pitch, fe_reset.pov.rot.pitch, 1e-9);
        CHECK_NEAR(resetter.pose().rot.yaw, fe_reset.pov.rot.yaw, 1e-9);
        CHECK_NEAR(resetter.pose().rot.roll, fe_reset.pov.rot.roll, 1e-9);
        CHECK_NEAR(resetter.fov(), fe_reset.pov.fov, 1e-9);
        CHECK(fe_reset.logs_contain("相机视角已重置"));
        resetter.leave();
    }

    // 滚转归零（1.0.41 并入）：只把 roll 回 0，位置/俯仰/偏航/FOV 不动（与整体重置 R 相对）
    {
        FakeEngine fe_roll0;
        fe_roll0.pov.loc = Vec3{1.0, 2.0, 3.0};
        fe_roll0.pov.rot = Rot{10.0, 20.0, 0.0};
        fe_roll0.pov.fov = 75.0;
        Config cfg_roll0;
        PhotoMode roll_zeroer(fe_roll0, cfg_roll0);
        CHECK(roll_zeroer.enter());

        Intent tilt;
        tilt.roll = 1.0;
        roll_zeroer.tick(tilt, 0.25);  // roll_speed 默认 60 度/秒：roll = 15
        CHECK_NEAR(roll_zeroer.pose().rot.roll, 15.0, 1e-9);

        Intent level;
        level.reset_roll = true;
        roll_zeroer.tick(level, 0.016);
        CHECK_NEAR(roll_zeroer.pose().rot.roll, 0.0, 1e-9);
        CHECK_NEAR(roll_zeroer.pose().loc.x, fe_roll0.pov.loc.x, 1e-9);
        CHECK_NEAR(roll_zeroer.pose().loc.y, fe_roll0.pov.loc.y, 1e-9);
        CHECK_NEAR(roll_zeroer.pose().loc.z, fe_roll0.pov.loc.z, 1e-9);
        CHECK_NEAR(roll_zeroer.pose().rot.pitch, fe_roll0.pov.rot.pitch, 1e-9);
        CHECK_NEAR(roll_zeroer.pose().rot.yaw, fe_roll0.pov.rot.yaw, 1e-9);
        CHECK_NEAR(roll_zeroer.fov(), fe_roll0.pov.fov, 1e-9);
        CHECK(fe_roll0.logs_contain("滚转已归零"));
        roll_zeroer.leave();
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

    // Q/E 滚转：roll_speed 度/秒，正 = 顺时针（roll 增大）；Shift/Ctrl 变速同样生效
    {
        FakeEngine fe_roll;
        Config cfg_roll;
        cfg_roll.roll_speed = 90.0;
        PhotoMode roller(fe_roll, cfg_roll);
        CHECK(roller.enter());

        Intent roll_cw;
        roll_cw.roll = 1.0;
        roller.tick(roll_cw, 0.1);  // 90 * 0.1 = 9 度
        CHECK_NEAR(roller.pose().rot.roll, 9.0, 1e-9);

        Intent roll_ccw;
        roll_ccw.roll = -1.0;
        roller.tick(roll_ccw, 0.1);
        CHECK_NEAR(roller.pose().rot.roll, 0.0, 1e-9);

        roll_ccw.boost = true;
        roller.tick(roll_ccw, 0.1);  // -90 * 4 * 0.1 = -36
        CHECK_NEAR(roller.pose().rot.roll, -36.0, 1e-9);

        roll_ccw.slow = true;  // Shift+Ctrl 同按：4 * 0.3 = 1.2 倍
        roller.tick(roll_ccw, 0.1);  // -10.8 -> -46.8
        CHECK_NEAR(roller.pose().rot.roll, -46.8, 1e-9);

        // dt 超限保护对滚转同样生效
        PhotoMode capped(fe_roll, cfg_roll);
        CHECK(capped.enter());
        capped.tick(roll_cw, 5.0);  // 实际按 0.25s 算：90 * 0.25 = 22.5
        CHECK_NEAR(capped.pose().rot.roll, 22.5, 1e-9);

        // 环绕：滚过 ±180 折回对称区间，不无界增长
        Config cfg_fast = cfg_roll;
        cfg_fast.roll_speed = 3600.0;
        PhotoMode spinner(fe_roll, cfg_fast);
        CHECK(spinner.enter());
        spinner.tick(roll_cw, 0.1);  // +360 -> 0
        CHECK_NEAR(spinner.pose().rot.roll, 0.0, 1e-9);
        Intent roll_left;
        roll_left.roll = -1.0;
        spinner.tick(roll_left, 0.2);  // -720 -> 0
        CHECK_NEAR(spinner.pose().rot.roll, 0.0, 1e-9);
        spinner.tick(roll_left, 0.05);  // -180 -> -180
        CHECK_NEAR(spinner.pose().rot.roll, -180.0, 1e-9);
        spinner.tick(roll_left, 0.05);  // 再 -180：-360 折回 0
        CHECK_NEAR(spinner.pose().rot.roll, 0.0, 1e-9);
        spinner.leave();
    }

    // Z/X 与十字键上/下俯仰（1.0.26）：pitch_speed 度/秒，正 = 仰视；变速/dt 上限/±pitch_limit 夹紧同滚转
    {
        FakeEngine fe_pitch;
        Config cfg_pitch;
        cfg_pitch.pitch_speed = 90.0;
        PhotoMode pitcher(fe_pitch, cfg_pitch);
        CHECK(pitcher.enter());

        Intent pitch_up;
        pitch_up.pitch_axis = 1.0;
        pitcher.tick(pitch_up, 0.1);  // 90 * 0.1 = 9 度仰
        CHECK_NEAR(pitcher.pose().rot.pitch, 9.0, 1e-9);

        Intent pitch_dn;
        pitch_dn.pitch_axis = -1.0;
        pitcher.tick(pitch_dn, 0.1);
        CHECK_NEAR(pitcher.pose().rot.pitch, 0.0, 1e-9);

        pitch_dn.boost = true;
        pitcher.tick(pitch_dn, 0.1);  // -90 * 4 * 0.1 = -36
        CHECK_NEAR(pitcher.pose().rot.pitch, -36.0, 1e-9);

        // invert_y 只管鼠标/摇杆 Y 轴，不影响俯仰按键的方向语义
        Config inv_cfg = cfg_pitch;
        inv_cfg.invert_y = true;
        PhotoMode inverted(fe_pitch, inv_cfg);
        CHECK(inverted.enter());
        inverted.tick(pitch_up, 0.1);
        CHECK_NEAR(inverted.pose().rot.pitch, 9.0, 1e-9);
        inverted.leave();

        // 夹紧：再快也不越过 ±pitch_limit（默认 89）
        Config fast = cfg_pitch;
        fast.pitch_speed = 3600.0;
        PhotoMode clamper(fe_pitch, fast);
        CHECK(clamper.enter());
        clamper.tick(pitch_up, 5.0);  // dt 上限 0.25：+900 度顶到 +89
        CHECK_NEAR(clamper.pose().rot.pitch, 89.0, 1e-9);
        clamper.tick(pitch_dn, 5.0);  // -900 度顶到 -89
        CHECK_NEAR(clamper.pose().rot.pitch, -89.0, 1e-9);
        clamper.leave();
        pitcher.leave();
    }

    // 方向键/十字键左/右偏航（1.0.46 起从左右平移改来）：yaw_speed 度/秒，正 = 右转；变速与俯仰同规则
    {
        FakeEngine fe_yaw;
        Config cfg_yaw;
        cfg_yaw.yaw_speed = 90.0;
        PhotoMode turner(fe_yaw, cfg_yaw);
        CHECK(turner.enter());

        Intent yaw_r;
        yaw_r.yaw_axis = 1.0;
        turner.tick(yaw_r, 0.1);  // 90 * 0.1 = 9 度右转
        CHECK_NEAR(turner.pose().rot.yaw, 9.0, 1e-9);

        Intent yaw_l;
        yaw_l.yaw_axis = -1.0;
        turner.tick(yaw_l, 0.1);
        CHECK_NEAR(turner.pose().rot.yaw, 0.0, 1e-9);

        yaw_l.slow = true;
        turner.tick(yaw_l, 0.1);  // -90 * 0.3 * 0.1 = -2.7，wrap_yaw 回绕成 357.3
        CHECK_NEAR(turner.pose().rot.yaw, 357.3, 1e-9);
        turner.leave();
    }

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

    // 暂停被游戏拒绝（剧情等场景常见）：自动回退时间缩放，冻结照样生效
    {
        FakeEngine fe;
        fe.fail_pause = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, 0.0001, 1e-12);
        CHECK(fe.logs_contain("回退为时间缩放冻结"));
        CHECK(!fe.logs_contain("冻结时间失败"));
        mode.leave("test");
        CHECK(!mode.active());
        CHECK_NEAR(fe.dilation, 1.0, 1e-12);  // 退出还原进入时的倍率
    }

    // 暂停与时间缩放都失败：才真正保持未冻结并告警
    {
        FakeEngine fe;
        fe.fail_pause = true;
        fe.fail_time_dilation = true;
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

void test_photo_mode_cutscene_freeze() {
    section("PhotoMode 剧情冻结（Sequencer 播放器联动 + 冻结期校验）");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // tick 有掉帧保护（单帧最多 0.25s），四步才能跨过 1s 校验阈值
    auto advance = [](PhotoMode& mode, int ticks) {
        Intent none;
        for (int i = 0; i < ticks; ++i) mode.tick(none, 0.25);
    };

    // 冻结时一并暂停剧情播放器，解冻/退出时恢复
    {
        FakeEngine fe;
        fe.sequences_found = 2;
        fe.sequences_playing = 1;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());  // auto_pause_time 默认开：进入即冻结
        CHECK(mode.frozen());
        CHECK_INT(fe.count("sequences_pause"), 1);
        CHECK_INT(fe.count("sequences_resume"), 0);

        Intent toggle;
        toggle.toggle_freeze = true;
        mode.tick(toggle, 0.016);  // F 解冻
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("sequences_resume"), 1);

        mode.tick(toggle, 0.016);  // 再冻结
        CHECK(mode.frozen());
        CHECK_INT(fe.count("sequences_pause"), 2);

        mode.leave("test");  // 冻结状态下退出：也要恢复播放器
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("sequences_resume"), 2);
        CHECK(!fe.paused);
    }

    // freeze_sequences = false：完全不动剧情播放器
    {
        FakeEngine fe;
        fe.sequences_playing = 1;
        Config cfg;
        cfg.freeze_sequences = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.sequence_calls, 0);
        mode.leave("test");
        CHECK_INT(fe.sequence_calls, 0);
    }

    // stop_wwise_on_freeze：冻结时音量归零（可恢复）；解冻/退出恢复音量
    {
        FakeEngine fe;
        fe.wwise_components = 3;
        Config cfg;
        cfg.stop_wwise_on_freeze = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.count("wwise_pause"), 1);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("wwise_resume"), 1);
        CHECK_INT(fe.count("wwise_pause"), 1);
        Intent toggle;
        toggle.toggle_freeze = true;
        mode.tick(toggle, 0.016);
        CHECK(mode.frozen());
        CHECK_INT(fe.count("wwise_pause"), 2);  // 再冻结再静音一次
        mode.leave("test");
        CHECK_INT(fe.count("wwise_resume"), 2);  // 冻结状态下退出也恢复
    }

    // stop_wwise_on_freeze = false：不碰 Wwise 组件
    {
        FakeEngine fe;
        fe.wwise_components = 2;
        Config cfg;
        cfg.stop_wwise_on_freeze = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.count("wwise_pause"), 0);
        mode.leave("test");
        CHECK_INT(fe.count("wwise_pause"), 0);
        CHECK_INT(fe.count("wwise_resume"), 0);
    }

    // 冻结期被游戏解除暂停：每秒校验后重新暂停；连续 3 次压不住则放弃并告警
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());

        fe.reset_calls();
        fe.paused = false;  // 模拟剧情系统解除暂停
        advance(mode, 4);
        CHECK(mode.frozen());
        CHECK(fe.paused);
        CHECK_INT(fe.count("pause_on"), 1);
        CHECK(fe.logs_contain("已重新暂停"));

        for (int i = 0; i < 2; ++i) {  // 连续第 2、3 次压不住：第 3 次后放弃
            fe.paused = false;
            advance(mode, 4);
        }
        CHECK_INT(fe.count("pause_on"), 3);
        CHECK(fe.logs_contain("已停止自动重试"));

        fe.reset_calls();
        fe.paused = false;
        advance(mode, 8);
        CHECK_INT(fe.count("pause_on"), 0);  // 放弃后不再每秒硬写

        fe.paused = true;  // 过场结束、状态恢复正常：重新武装
        advance(mode, 4);
        fe.paused = false;
        fe.reset_calls();
        advance(mode, 4);
        CHECK_INT(fe.count("pause_on"), 1);

        mode.leave("test");
        CHECK(!fe.paused);
    }

    // dilation 模式冻结期被游戏改回 1.0：校验后重新写 freeze_dilation
    {
        FakeEngine fe;
        Config cfg;
        cfg.freeze_mode = FreezeMode::Dilation;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, cfg.freeze_dilation, 1e-12);

        fe.reset_calls();
        fe.dilation = 1.0;  // 模拟游戏改写时间缩放
        advance(mode, 4);
        CHECK_NEAR(fe.dilation, cfg.freeze_dilation, 1e-12);
        CHECK_INT(fe.count("set_time_dilation"), 1);
        CHECK(fe.logs_contain("已重新冻结"));
        mode.leave("test");
        CHECK_NEAR(fe.dilation, 1.0, 1e-12);  // 退出还原进入时的倍率
    }
}


void test_photo_mode_freeze_treatment() {
    section("PhotoMode 冻结期残余微动治理（相机/shake、风源、MPC 钉住、诊断）");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认配置：冻结时依次治理相机 → 风 → MPC；解冻逆序还原
    {
        FakeEngine fe;
        fe.modifiers_found = 2;
        fe.modifiers_disabled = 1;
        fe.wind_sources = 3;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());  // 进入即冻结
        CHECK_INT(fe.count("shakes_freeze"), 1);
        CHECK_INT(fe.count("wind_freeze"), 1);
        CHECK_INT(fe.count("mpc_snapshot"), 1);
        // MPC 快照为空（mpc_params=0）：不进入每帧写回，解冻也不释放
        CHECK_INT(fe.count("mpc_hold"), 0);

        mode.toggle_freeze();  // 解冻
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("shakes_restore"), 1);
        CHECK_INT(fe.count("wind_restore"), 1);
        CHECK_INT(fe.count("mpc_release"), 0);  // 没抓到快照就没有释放

        mode.toggle_freeze();  // 再冻结再来一轮
        CHECK(mode.frozen());
        CHECK_INT(fe.count("shakes_freeze"), 2);
        CHECK_INT(fe.count("wind_freeze"), 2);
        CHECK_INT(fe.count("mpc_snapshot"), 2);
        mode.leave("test");  // 冻结状态退出也要还原
        CHECK_INT(fe.count("shakes_restore"), 2);
        CHECK_INT(fe.count("wind_restore"), 2);
    }

    // MPC 快照非空：冻结期每帧写回，解冻/退出释放
    {
        FakeEngine fe;
        fe.mpc_params = 5;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());
        mode.tick(Intent{}, 0.016);
        mode.tick(Intent{}, 0.016);
        CHECK_INT(fe.count("mpc_hold"), 2);
        CHECK_INT(fe.mpc_hold_calls, 2);
        CHECK_INT(fe.count("pov_watch_tick"), 2);  // 冻结期每帧监测 POV
        CHECK_INT(fe.pov_watch_idle_calls, 2);      // Intent{} 无移动 = idle 帧

        mode.toggle_freeze();  // 解冻释放，之后不再写回
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("mpc_release"), 1);
        const int holds_before = fe.count("mpc_hold");
        mode.tick(Intent{}, 0.016);
        CHECK_INT(fe.count("mpc_hold"), holds_before);
        CHECK_INT(fe.count("pov_watch_tick"), 2);  // 解冻后不再监测

        // 冻结状态下退出同样释放
        mode.toggle_freeze();
        CHECK(mode.frozen());
        mode.leave("test");
        CHECK_INT(fe.count("mpc_release"), 2);
    }

    // 三个开关全关：冻结/解冻/退出都不碰治理链路
    {
        FakeEngine fe;
        fe.modifiers_found = 1;
        fe.wind_sources = 1;
        fe.mpc_params = 2;
        Config cfg;
        cfg.stop_camera_shakes_on_freeze = false;
        cfg.freeze_wind = false;
        cfg.freeze_mpc = false;
        cfg.freeze_diag = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        mode.tick(Intent{}, 0.25);
        mode.leave("test");
        CHECK_INT(fe.count("shakes_freeze"), 0);
        CHECK_INT(fe.count("wind_freeze"), 0);
        CHECK_INT(fe.count("mpc_snapshot"), 0);
        CHECK_INT(fe.count("mpc_hold"), 0);
        CHECK_INT(fe.count("shakes_restore"), 0);
        CHECK_INT(fe.count("wind_restore"), 0);
        CHECK_INT(fe.count("mpc_release"), 0);
        CHECK_INT(fe.count("freeze_diag"), 0);
        CHECK_INT(fe.count("pov_watch_tick"), 0);  // freeze_diag=false 时 POV 监测同样关闭
    }

    // 治理失败不记账：解冻不做对应还原（与 pause 失败回退同一语义）
    {
        FakeEngine fe;
        fe.fail_camera_shakes = true;
        fe.fail_wind = true;
        fe.fail_mpc = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.count("shakes_freeze"), 1);
        CHECK_INT(fe.count("wind_freeze"), 1);
        CHECK_INT(fe.count("mpc_snapshot"), 1);
        CHECK_INT(fe.count("mpc_hold"), 0);  // 快照失败 = 不进入每帧写回
        mode.toggle_freeze();
        CHECK_INT(fe.count("shakes_restore"), 0);
        CHECK_INT(fe.count("wind_restore"), 0);
        CHECK_INT(fe.count("mpc_release"), 0);
        mode.leave("test");
    }

    // 冻结诊断：冻结期每秒一条；freeze_diag=false 时静默
    {
        auto advance = [](PhotoMode& mode, int ticks) {
            Intent none;
            for (int i = 0; i < ticks; ++i) mode.tick(none, 0.25);
        };
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());  // 冻结中
        advance(mode, 4);     // 累计 1 秒
        CHECK_INT(fe.count("freeze_diag"), 1);
        CHECK(fe.logs_contain("冻结诊断"));
        advance(mode, 4);     // 再 1 秒再来一条
        CHECK_INT(fe.count("freeze_diag"), 2);

        FakeEngine fe2;
        Config cfg;
        cfg.freeze_diag = false;
        PhotoMode mode2(fe2, cfg);
        CHECK(mode2.enter());
        advance(mode2, 5);
        CHECK_INT(fe2.count("freeze_diag"), 0);
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

// ---------------------------------------------------------------- 交互提示（E 键图标）

void test_photo_mode_interact_prompt() {
    section("PhotoMode 交互提示隐藏");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认开启：进入隐藏、退出恢复，各只调一次
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.prompts_visible);
        mode.leave();
        CHECK(fe.prompts_visible);
        CHECK_INT(fe.count("prompt_off"), 1);
        CHECK_INT(fe.count("prompt_on"), 1);
    }

    // 配置关闭：进出都不碰交互提示
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_interact_prompt = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.prompts_visible);
        mode.leave();
        CHECK_INT(fe.count("prompt_off"), 0);
        CHECK_INT(fe.count("prompt_on"), 0);
    }

    // 提示组件晚挂出：进入时隐藏失败，tick 每秒重试，成功后退出仍能恢复
    {
        FakeEngine fe;
        fe.fail_prompts = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.prompts_visible);  // 第一次隐藏失败
        CHECK(fe.logs_contain("隐藏交互提示失败"));
        fe.fail_prompts = false;
        Intent none;
        for (int i = 0; i < 5; ++i) mode.tick(none, 0.25);  // 累计 1.25 秒，跨过重试阈值
        CHECK(!fe.prompts_visible);  // 每秒重试后成功
        mode.leave();
        CHECK(fe.prompts_visible);
    }
}

// ---------------------------------------------------------------- 敌人（怪物）血条

void test_photo_mode_enemy_health_bars() {
    section("PhotoMode 敌人血条隐藏");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认开启：进入隐藏、退出恢复，各只调一次
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.bars_visible);
        mode.leave();
        CHECK(fe.bars_visible);
        CHECK_INT(fe.count("bars_off"), 1);
        CHECK_INT(fe.count("bars_on"), 1);
    }

    // 配置关闭：进出都不碰血条
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_enemy_health_bars = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.bars_visible);
        mode.leave();
        CHECK_INT(fe.count("bars_off"), 0);
        CHECK_INT(fe.count("bars_on"), 0);
    }

    // 设置晚就绪：进入时隐藏失败，tick 每秒重试，成功后退出仍能恢复
    {
        FakeEngine fe;
        fe.fail_bars = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.bars_visible);  // 第一次隐藏失败
        CHECK(fe.logs_contain("隐藏敌人血条失败"));
        fe.fail_bars = false;
        Intent none;
        for (int i = 0; i < 5; ++i) mode.tick(none, 0.25);  // 累计 1.25 秒，跨过重试阈值
        CHECK(!fe.bars_visible);  // 每秒重试后成功
        mode.leave();
        CHECK(fe.bars_visible);
    }
}

// ---------------------------------------------------------------- 锁定标记（锁定敌人身上的白点）

void test_photo_mode_lockon_marker() {
    section("PhotoMode 锁定标记隐藏");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认开启：进入隐藏、退出还原，各只调一次
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.lockon_visible);
        mode.leave();
        CHECK(fe.lockon_visible);
        CHECK_INT(fe.count("lockon_off"), 1);
        CHECK_INT(fe.count("lockon_on"), 1);
    }

    // 配置关闭：进出都不碰锁定标记
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_lockon_marker = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.lockon_visible);
        mode.leave();
        CHECK_INT(fe.count("lockon_off"), 0);
        CHECK_INT(fe.count("lockon_on"), 0);
    }

    // 扫描晚就绪/藏不住：进入时隐藏失败，tick 每秒重试，成功后退出仍能恢复
    {
        FakeEngine fe;
        fe.fail_lockon = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.lockon_visible);  // 第一次隐藏失败
        CHECK(fe.logs_contain("隐藏锁定标记失败"));
        fe.fail_lockon = false;
        Intent none;
        for (int i = 0; i < 5; ++i) mode.tick(none, 0.25);  // 累计 1.25 秒，跨过重试阈值
        CHECK(!fe.lockon_visible);  // 每秒重试后成功
        mode.leave();
        CHECK(fe.lockon_visible);
    }
}

// ---------------------------------------------------------------- 对话字幕

void test_photo_mode_subtitles() {
    section("PhotoMode 对话字幕隐藏");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认开启：进入隐藏、退出恢复，各只调一次
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.subtitles_visible);
        mode.leave();
        CHECK(fe.subtitles_visible);
        CHECK_INT(fe.count("subs_off"), 1);
        CHECK_INT(fe.count("subs_on"), 1);
    }

    // 配置关闭：进出都不碰字幕
    {
        FakeEngine fe;
        Config cfg;
        cfg.hide_subtitles = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(fe.subtitles_visible);
        mode.leave();
        CHECK_INT(fe.count("subs_off"), 0);
        CHECK_INT(fe.count("subs_on"), 0);
    }

    // 设置/控件晚就绪：进入时隐藏失败，tick 每秒重试，成功后退出仍能恢复
    {
        FakeEngine fe;
        fe.fail_subtitles = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.subtitles_visible);  // 第一次隐藏失败
        CHECK(fe.logs_contain("隐藏对话字幕失败"));
        fe.fail_subtitles = false;
        Intent none;
        for (int i = 0; i < 5; ++i) mode.tick(none, 0.25);  // 累计 1.25 秒，跨过重试阈值
        CHECK(!fe.subtitles_visible);  // 每秒重试后成功
        mode.leave();
        CHECK(fe.subtitles_visible);
    }
}

// ---------------------------------------------------------------- 超宽屏画幅解锁

void test_photo_mode_ultrawide() {
    section("PhotoMode 超宽屏画幅解锁");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 默认开启：进入解锁、退出还原
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.aspect_unlocked);
        CHECK_INT(fe.count("aspect_unlock"), 1);
        CHECK_INT(fe.count("restore_aspect"), 0);
        mode.leave("test");
        CHECK(!fe.aspect_unlocked);
        CHECK_INT(fe.count("restore_aspect"), 1);
    }

    // 配置关闭：进退都不碰引擎
    {
        FakeEngine fe;
        Config cfg;
        cfg.ultrawide_unlock = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK_INT(fe.count("aspect_unlock"), 0);
        mode.leave("test");
        CHECK_INT(fe.count("restore_aspect"), 0);
    }

    // 解锁失败不阻断进模式：只告警，退出也不再还原（没解锁就没有可还原的）
    {
        FakeEngine fe;
        fe.fail_aspect_unlock = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.active());
        CHECK(!fe.aspect_unlocked);
        CHECK(fe.logs_contain("画幅解锁失败"));
        mode.leave("test");
        CHECK_INT(fe.count("restore_aspect"), 0);
    }

    // 游戏本来就没锁（aspect_lock_active=false 仍返回成功）：进退各调一次
    {
        FakeEngine fe;
        fe.aspect_lock_active = false;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(!fe.aspect_unlocked);
        CHECK_INT(fe.count("aspect_unlock"), 1);
        CHECK(fe.logs_contain("画幅解锁失败") == false);
        mode.leave("test");
        CHECK_INT(fe.count("restore_aspect"), 1);
    }

    // 进入后配置热重载关掉：退出仍按进入时做过的步骤还原
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        Config changed;
        changed.ultrawide_unlock = false;
        mode.set_config(changed);
        mode.leave("test");
        CHECK_INT(fe.count("restore_aspect"), 1);
    }
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

    r = parse_pad_combo(" start ", fb, "pad_freeze", &warnings);
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
    CHECK_NEAR(rt.cfg.pad_look_speed, 60.0, 1e-9);
    CHECK_NEAR(rt.cfg.pad_deadzone, 0.15, 1e-9);
}

void test_dinput_map() {
    section("DInput 映射（PS 手柄）");
    using namespace pbzpm;
    PadButton b{};

    // 按钮 0 基索引：□ ╳ ○ △ L1 R1 L2 R2 CREATE OPTIONS L3 R3 PS（13=触摸板无对应）
    const PadButton expected[] = {
        PadButton::X,    PadButton::A,   PadButton::B,     PadButton::Y,
        PadButton::LB,   PadButton::RB,  PadButton::LT,    PadButton::RT,
        PadButton::Back, PadButton::Start, PadButton::LS, PadButton::RS,
        PadButton::Guide,
    };
    for (int i = 0; i < 13; ++i) {
        CHECK(dinput_button_to_pad(i, b) && b == expected[i]);
    }
    CHECK(!dinput_button_to_pad(13, b));
    CHECK(!dinput_button_to_pad(-1, b));
    CHECK(!dinput_button_to_pad(kDInputButtonCount, b));

    // POV：0=上 9000=右 18000=下 27000=左；斜向同时亮两向；居中全松
    bool d[4];
    dinput_pov_to_buttons(0, d);
    CHECK(d[0] && !d[1] && !d[2] && !d[3]);
    dinput_pov_to_buttons(9000, d);
    CHECK(!d[0] && d[1] && !d[2] && !d[3]);
    dinput_pov_to_buttons(18000, d);
    CHECK(!d[0] && !d[1] && d[2] && !d[3]);
    dinput_pov_to_buttons(27000, d);
    CHECK(!d[0] && !d[1] && !d[2] && d[3]);
    dinput_pov_to_buttons(4500, d);
    CHECK(d[0] && d[1] && !d[2] && !d[3]);
    dinput_pov_to_buttons(31500, d);
    CHECK(d[0] && !d[1] && !d[2] && d[3]);
    dinput_pov_to_buttons(0xFFFF, d);      // 居中（低字 0xFFFF）
    CHECK(!d[0] && !d[1] && !d[2] && !d[3]);
    dinput_pov_to_buttons(0xFFFFFFFF, d);  // 居中（全字 0xFFFFFFFF）
    CHECK(!d[0] && !d[1] && !d[2] && !d[3]);

    // 轴：0..65535；摇杆中心 32767.5 → 0，LY/LRz 取反成「正=上」，扳机 0..1
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LX, 0), -1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LX, 32767), 0.0, 1e-4);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LX, 65535), 1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LY, 0), 1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LY, 65535), -1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LZ, 32767), 0.0, 1e-4);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRz, 0), 1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRx, 0), 0.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRx, 65535), 1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRy, 32767), 0.5, 1e-4);
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
        unbind.pad_freeze = "NONE";
        std::vector<std::string> w2;
        const KeyMap k2 = build_keymap(unbind, &w2);
        CHECK(k2.all_resolved);
        CHECK(k2.pad_freeze.empty());
        PadState s;
        s.held[static_cast<int>(PadButton::X)] = true;
        input.on_gamepad(s);
        CHECK(!input.build(unbind, k2).toggle_freeze);
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

    // RT/LT 升降（模拟量）+ RB/LB 滚转（1.0.22 起：RB 顺时针 / LB 逆时针）
    PadState trig;
    trig.rt = 1.0;
    trig.held[static_cast<int>(PadButton::RB)] = true;
    input.on_gamepad(trig);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.move_up, 1.0, 1e-9);
    CHECK_NEAR(in.roll, 1.0, 1e-9);
    CHECK(!in.boost);
    trig = PadState{};
    trig.lt = 0.5;
    trig.held[static_cast<int>(PadButton::LB)] = true;
    input.on_gamepad(trig);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.move_up, -0.5, 1e-9);
    CHECK_NEAR(in.roll, -1.0, 1e-9);
    input.end_frame();
    input.reset();

    // 变速随滚转让位到摇杆按下：LS 加速 / RS 减速；两杆同按（进出组合）时不触发变速
    PadState sticks;
    sticks.held[static_cast<int>(PadButton::LS)] = true;
    input.on_gamepad(sticks);
    in = input.build(cfg, keys);
    CHECK(in.boost);
    CHECK(!in.slow);
    sticks.held[static_cast<int>(PadButton::RS)] = true;
    input.on_gamepad(sticks);
    in = input.build(cfg, keys);
    CHECK(!in.boost);
    CHECK(!in.slow);
    sticks.held[static_cast<int>(PadButton::LS)] = false;
    input.on_gamepad(sticks);
    in = input.build(cfg, keys);
    CHECK(!in.boost);
    CHECK(in.slow);
    input.end_frame();
    input.reset();

    // 十字键上/下俯仰（1.0.26 起接管上/下）：上 = 仰视 / 下 = 俯视，按住持续（电平语义无按下沿）
    PadState dpad;
    dpad.held[static_cast<int>(PadButton::Up)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.pitch_axis, 1.0, 1e-9);
    CHECK_INT(in.fov_nudge, 0);  // 上键不再驱动 FOV
    input.end_frame();
    dpad = PadState{};
    dpad.held[static_cast<int>(PadButton::Down)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.pitch_axis, -1.0, 1e-9);
    input.end_frame();
    input.on_gamepad(PadState{});
    in = input.build(cfg, keys);
    CHECK_NEAR(in.pitch_axis, 0.0, 1e-9);
    input.end_frame();

    // 十字键左/右转视角（1.0.46 起从平移改为偏航）：左 = 左转 / 右 = 右转；FOV 移到 A/B
    dpad = PadState{};
    dpad.held[static_cast<int>(PadButton::Left)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.yaw_axis, -1.0, 1e-9);
    CHECK_NEAR(in.move_right, 0.0, 1e-9);  // 十字键不再驱动平移
    CHECK_INT(in.fov_nudge, 0);  // 左键不再驱动 FOV
    input.end_frame();
    dpad = PadState{};
    dpad.held[static_cast<int>(PadButton::Right)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_NEAR(in.yaw_axis, 1.0, 1e-9);
    input.end_frame();
    input.on_gamepad(PadState{});
    input.end_frame();

    // FOV 步进：A/B；键盘 FOV 键（此前只解析未生效，1.0.23 起一并接上）
    dpad = PadState{};
    dpad.held[static_cast<int>(PadButton::A)] = true;
    input.on_gamepad(dpad);
    in = input.build(cfg, keys);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    input.on_key(keys.fov_down, true);
    in = input.build(cfg, keys);
    CHECK_INT(in.fov_nudge, -1);
    input.end_frame();
    input.reset();

    // FOV 按住连发（1.0.23）：按下沿立即一格，按住 0.35 秒后按 0.08 秒间隔连发；松开即停。
    // 帧序列 dt=0.10：0.10s 按下沿=1，0.20/0.30s 未到延迟=0，0.40s 过延迟=1，之后每帧约 1~2 格。
    input.on_gamepad(PadState{});  // 先松开并过一帧：上一段把十字键留在按住态，was_down 要在 build 里才会回落
    in = input.build(cfg, keys, 0.0);
    input.end_frame();
    PadState pad_up;
    pad_up.held[static_cast<int>(PadButton::A)] = true;  // 1.0.27 起 FOV 在 A/B
    input.on_gamepad(pad_up);
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 0);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 0);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 0);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 2);
    input.end_frame();
    // 松开：立即停发，且不会把松开误判成反向
    input.on_gamepad(PadState{});
    in = input.build(cfg, keys, 0.10);
    CHECK_INT(in.fov_nudge, 0);
    input.end_frame();
    input.reset();

    // 键盘 FOV 键同样支持按住连发；按住时进出模式（reset）不误发新按下沿
    input.on_key(keys.fov_up, true);
    in = input.build(cfg, keys, 0.05);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    input.reset();
    in = input.build(cfg, keys, 0.0);
    CHECK_INT(in.fov_nudge, 0);
    input.end_frame();
    input.on_key(keys.fov_up, true);  // 重新按下：正常触发新的按下沿
    in = input.build(cfg, keys, 0.35);
    CHECK_INT(in.fov_nudge, 1);
    input.end_frame();
    input.on_key(keys.fov_up, false);
    in = input.build(cfg, keys, 0.05);
    CHECK_INT(in.fov_nudge, 0);
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
    input.end_frame();

    // LB+RB 同按：滚转归零组合键（1.0.41 并入）；同按滚转增量互抵，不会同时产生滚转输入
    input.reset();
    PadState bumpers;
    bumpers.held[static_cast<int>(PadButton::LB)] = true;
    bumpers.held[static_cast<int>(PadButton::RB)] = true;
    input.on_gamepad(bumpers);
    in = input.build(cfg, keys);
    CHECK(in.reset_roll);
    CHECK(!in.reset_view);
    CHECK_NEAR(in.roll, 0.0, 1e-9);
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

// Ctrl（键盘）/按下右摇杆 RS（手柄）时视角精调：鼠标与右摇杆转速一起乘 slow_look_mult（1.0.37）
void test_photo_mode_slow_look() {
    section("PhotoMode Ctrl/RS 视角精调");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 鼠标视角：按住 Ctrl 后同样的位移只转 slow_look_mult 倍
    {
        FakeEngine fe;
        Config cfg;  // look_sensitivity=0.1，slow_look_mult=0.1
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        Intent plain;
        plain.look_dx = 10.0;  // 10 单位 × 0.1 度/单位 = +1.0 度
        mode.tick(plain, 0.1);
        CHECK_NEAR(mode.pose().rot.yaw, 1.0, 1e-9);
        mode.leave();

        FakeEngine fe2;
        PhotoMode fine(fe2, cfg);
        CHECK(fine.enter());
        Intent slow;
        slow.look_dx = 10.0;
        slow.slow = true;  // Ctrl：+1.0 × 0.1 = +0.1 度
        fine.tick(slow, 0.1);
        CHECK_NEAR(fine.pose().rot.yaw, 0.1, 1e-9);
        fine.leave();
    }

    // 右摇杆视角：RS 按下（slow 语义）时转速同样乘 slow_look_mult
    {
        FakeEngine fe;
        Config cfg;
        cfg.pad_look_speed = 100.0;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        Intent plain;
        plain.pad_look_dx = 100.0;  // 100 度/秒 × dt 夹紧 0.25 秒 = +25 度
        mode.tick(plain, 1.0);
        CHECK_NEAR(mode.pose().rot.yaw, 25.0, 1e-9);
        mode.leave();

        FakeEngine fe2;
        PhotoMode fine(fe2, cfg);
        CHECK(fine.enter());
        Intent slow;
        slow.pad_look_dx = 100.0;
        slow.slow = true;  // +25 × 0.1 = +2.5 度
        fine.tick(slow, 1.0);
        CHECK_NEAR(fine.pose().rot.yaw, 2.5, 1e-9);
        fine.leave();
    }
}


// ---------------------------------------------------------------- PBZFM 菜单框架映射

// 字段表完整性 + 每个字段「读 → 改 → 读回 → 还原 → 读回」的往返
void test_pbzfm_fields_roundtrip() {
    section("PBZFM 字段表（读改写回环）");
    using namespace pbzpm;
    const KeyMap keys = build_keymap(Config{}, nullptr);
    const std::vector<PbzfmField>& fields = pbzfm_fields();
    CHECK(fields.size() >= 30);

    // 元数据健全：key 全局唯一、回调齐全
    for (const PbzfmField& f : fields) {
        CHECK(f.key != nullptr);
        CHECK(f.label != nullptr);
        CHECK(f.group != nullptr);
        switch (f.kind) {
            case PbzfmFieldKind::Bool:
                CHECK(f.get_bool != nullptr && f.set_bool != nullptr);
                break;
            case PbzfmFieldKind::Float:
                CHECK(f.get_num != nullptr && f.set_num != nullptr);
                CHECK(f.mx >= f.mn && f.step > 0.0);
                break;
            case PbzfmFieldKind::Enum:
                CHECK(f.get_enum != nullptr && f.set_enum != nullptr);
                CHECK(f.options != nullptr && f.option_count > 0);
                break;
            case PbzfmFieldKind::Key:
                CHECK(f.key_name != nullptr);
                break;
        }
    }
    for (size_t i = 0; i < fields.size(); ++i) {
        for (size_t j = i + 1; j < fields.size(); ++j) {
            CHECK(std::string(fields[i].key) != std::string(fields[j].key));
        }
    }

    // 每个字段：默认值可读；改成新值读回一致；还原默认再读回一致
    for (const PbzfmField& f : fields) {
        Config cfg{};
        const double original = pbzfm_read_field(f, cfg, keys);
        double changed = original;
        switch (f.kind) {
            case PbzfmFieldKind::Bool:
                changed = original != 0.0 ? 0.0 : 1.0;
                break;
            case PbzfmFieldKind::Float:
                changed = f.mx;  // 用上界往返（顺带验证 mx 可写入）
                break;
            case PbzfmFieldKind::Enum:
                changed = f.option_count > 1 ? static_cast<double>(f.option_count - 1) : 0.0;
                break;
            case PbzfmFieldKind::Key:
                changed = 0x74;  // F5：键名表里有名字
                break;
        }
        CHECK(pbzfm_apply_field(f, changed, cfg));
        CHECK_NEAR(pbzfm_read_field(f, cfg, keys), changed, 1e-9);
        CHECK(pbzfm_apply_field(f, original, cfg));
        CHECK_NEAR(pbzfm_read_field(f, cfg, keys), original, 1e-9);
    }

    // 键捕获给出键名表表达不了的键（鼠标侧键 VK 0x06）：拒绝应用、配置不动
    {
        Config cfg{};
        cfg.key_toggle = "F2";
        for (const PbzfmField& f : fields) {
            if (f.kind == PbzfmFieldKind::Key && std::string(f.key) == "key_toggle") {
                CHECK(!pbzfm_apply_field(f, 0x06, cfg));
                CHECK_STR(cfg.key_toggle, "F2");
            }
        }
    }

    // float/enum 越界夹紧
    {
        Config cfg{};
        for (const PbzfmField& f : fields) {
            if (f.kind == PbzfmFieldKind::Float && std::string(f.key) == "move_speed") {
                CHECK(pbzfm_apply_field(f, 99999.0, cfg));
                CHECK_NEAR(cfg.move_speed, f.mx, 1e-9);
            }
            if (f.kind == PbzfmFieldKind::Enum && std::string(f.key) == "freeze_mode") {
                CHECK(pbzfm_apply_field(f, 99.0, cfg));
                CHECK(cfg.freeze_mode == FreezeMode::Pause);  // 下标夹到 option_count-1
            }
        }
    }

    // 默认键名都能解析（KeyMap 回退链不依赖时也成立）
    {
        Config cfg{};
        for (const PbzfmField& f : fields) {
            if (f.kind != PbzfmFieldKind::Key) continue;
            CHECK(vk_from_name(cfg.*f.key_name) != 0);
        }
    }
}

// PBZFM 菜单写回的落盘格式：config_ini_text 全字段序列化 → parse 零告警往返；
// save_config_file 落盘 → load_config_file 读回一致
void test_pbzfm_config_writeback() {
    section("PBZFM 菜单写回（config_ini_text / save_config_file）");
    using namespace pbzpm;
    // 默认序列化与 default_config_text 完全一致（同一函数的两个入口）
    CHECK(config_ini_text(Config{}) == default_config_text());
    CHECK_STR(Config{}.menu_language, "auto");  // 菜单语言默认跟随 PBZFM

    Config custom{};
    custom.key_toggle = "F7";
    custom.key_fov_up = "OEM_PLUS";
    custom.move_speed = 733.0;
    custom.look_sensitivity = 0.22;
    custom.hide_game_hud = false;
    custom.freeze_mode = FreezeMode::Dilation;
    custom.pad_look_speed = 222.0;
    custom.mouse_vertical_mode = MouseVerticalMode::Look;
    custom.menu_language = "ja-JP";

    const std::string text = config_ini_text(custom);
    const ConfigLoadResult parsed = parse_config(text, "menu-writeback");
    CHECK_INT((int)parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F7");
    CHECK_STR(parsed.cfg.key_fov_up, "OEM_PLUS");
    CHECK_NEAR(parsed.cfg.move_speed, 733.0, 1e-9);
    CHECK_NEAR(parsed.cfg.look_sensitivity, 0.22, 1e-9);
    CHECK(!parsed.cfg.hide_game_hud);
    CHECK(parsed.cfg.freeze_mode == FreezeMode::Dilation);
    CHECK_NEAR(parsed.cfg.pad_look_speed, 222.0, 1e-9);
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Look);
    CHECK_STR(parsed.cfg.menu_language, "ja-JP");
    // 未改动的字段保持默认（全字段序列化，不会被吃掉）
    CHECK(parsed.cfg.auto_pause_time);
    CHECK(parsed.cfg.hide_subtitles);
    CHECK(parsed.cfg.pad_enabled);

    const std::string path = "pbzfm_menu_writeback_test.ini";
    std::string err;
    CHECK(save_config_file(path, custom, &err));
    const ConfigLoadResult loaded = load_config_file(path);
    CHECK_INT((int)loaded.warnings.size(), 0);
    CHECK_STR(loaded.cfg.key_toggle, "F7");
    CHECK_NEAR(loaded.cfg.move_speed, 733.0, 1e-9);
    CHECK(!loaded.cfg.hide_game_hud);
    std::remove(path.c_str());
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
    test_photo_mode_cutscene_freeze();
    test_photo_mode_freeze_treatment();
    test_photo_mode_reassert_and_partial();
    test_photo_mode_input_blocking();
    test_photo_mode_live_toggles();
    test_photo_mode_ultrawide();
    test_photo_mode_interact_prompt();
    test_photo_mode_enemy_health_bars();
    test_photo_mode_lockon_marker();
    test_photo_mode_subtitles();
    test_gamepad_names();
    test_gamepad_parse_combo();
    test_dinput_map();
    test_gamepad_deadzone();
    test_gamepad_input_logic();
    test_photo_mode_gamepad();
    test_photo_mode_slow_look();
    test_pbzfm_fields_roundtrip();
    test_pbzfm_config_writeback();

    std::printf("\n----------------------------------------\n");
    std::printf("检查 %d 项，失败 %d 项\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("存在失败项\n");
    return 1;
}
