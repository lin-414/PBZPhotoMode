// 纯核心层测试：不加载 UE4SS、不启动游戏，直接用 cl/cmake 编译即可跑。
// 覆盖：配置解析、键名映射、输入状态机、数学约定、摄影模式状态机（含回滚）。
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/hook_registry.h"
#include "core/i18n_logic.h"
#include "core/input_logic.h"
#include "core/look_preset.h"
#include "core/math3.h"
#include "core/photo_mode.h"
#include "core/retry_policy.h"
#include "core/tuning.h"
#include "core/vk.h"
#include "core/world_snapshot.h"
#include "pbzfm_client_lang.h"
#include "pbzfm_schema_map.h"
#include "tests/fake_engine.h"
#include "tests/test_harness.h"

using namespace pbzpm_test;

// 按子系统拆分的测试 TU（各自定义 run_*，共用 test_harness.h 的 CHECK 宏与计数）
void run_mod_runtime_tests();
void run_input_pure_tests();
void run_pbzfm_validation_tests();

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
    // 鼠标键名（key_vertical_up/down 的默认值与可选键）
    CHECK_INT(vk_from_name("MOUSE_LEFT"), 0x01);
    CHECK_INT(vk_from_name("mouse_right"), 0x02);
    CHECK_INT(vk_from_name("MOUSE_MIDDLE"), 0x04);
    CHECK_INT(vk_from_name("MOUSE_X1"), 0x05);
    CHECK_INT(vk_from_name("MOUSE_X2"), 0x06);
    CHECK_INT(vk_from_name("LMB"), 0x01);
    CHECK_INT(vk_from_name("RMB"), 0x02);
    CHECK_INT(vk_from_name("MOUSE_BACK"), 0x05);
    CHECK_INT(vk_from_name("MOUSE_FORWARD"), 0x06);
    CHECK_STR(pbzpm::vk_to_name(0x01), "MOUSE_LEFT");  // 规范名优先，别名不参与输出
    CHECK_STR(pbzpm::vk_to_name(0x02), "MOUSE_RIGHT");
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
    CHECK(empty.cfg.freeze_dialogue_flow);  // 默认冻结对话 Flow（仅在 hide_subtitles=true 时生效）
    CHECK(empty.cfg.stop_wwise_on_freeze);  // 默认冻结时处理 Wwise 音频（对话流程已冻结，可关）
    CHECK(empty.cfg.wwise_engine_suspend);  // 默认允许整引擎在无逐语音 ID 时作静音兜底
    CHECK(empty.cfg.ultrawide_unlock);  // 默认进入摄影模式即解除 16:9 画幅锁（超宽屏）
    CHECK(empty.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
    CHECK_NEAR(empty.cfg.vertical_speed, 200.0, 1e-9);
    CHECK_NEAR(empty.cfg.roll_speed, 60.0, 1e-9);  // Q/E 滚转默认 60 度/秒
    CHECK_NEAR(empty.cfg.pitch_speed, 30.0, 1e-9);  // 俯仰默认 30 度/秒（方向键与十字键上/下）
    CHECK_NEAR(empty.cfg.slow_look_mult, 0.1, 1e-9);  // Ctrl/RS 按下时视角速度倍率（1.0.37 新增）
    CHECK_NEAR(empty.cfg.yaw_speed, 90.0, 1e-9);  // 方向键/十字键转向速度（1.0.46 起左右键转视角）
    // [look] 五项画面效果的默认参数（每次进入摄影模式都会恢复到这里）与独立开关
    CHECK_NEAR(empty.cfg.vignette, 0.0, 1e-9);
    CHECK_NEAR(empty.cfg.saturation, 1.0, 1e-9);
    CHECK_NEAR(empty.cfg.contrast, 1.0, 1e-9);
    CHECK_NEAR(empty.cfg.bloom, 0.0, 1e-9);
    CHECK_NEAR(empty.cfg.temperature, 0.0, 1e-9);
    CHECK(empty.cfg.vignette_enabled);
    CHECK(empty.cfg.saturation_enabled);
    CHECK(empty.cfg.contrast_enabled);
    CHECK(empty.cfg.bloom_enabled);
    CHECK(empty.cfg.temperature_enabled);
    // [look] 调色·进阶三件默认=中性值（中性=不覆盖该项；见 tuning.h。同批十项判死移除）
    CHECK_NEAR(empty.cfg.color_gain, tuning::kColorGainDefault, 1e-9);
    CHECK_NEAR(empty.cfg.color_gamma, tuning::kColorGammaDefault, 1e-9);
    CHECK_NEAR(empty.cfg.color_offset, 0.0, 1e-9);
    CHECK_STR(empty.cfg.key_pitch_up, "UP");    // 方向键上/下 = 俯仰（1.0.27 起从 Z/X 改来）
    CHECK_STR(empty.cfg.key_pitch_down, "DOWN");
    CHECK_STR(empty.cfg.key_yaw_left, "LEFT");   // 方向键左/右 = 左右转视角（1.0.46 起从平移改来）
    CHECK_STR(empty.cfg.key_yaw_right, "RIGHT");
    CHECK_STR(empty.cfg.key_roll_left, "Q");      // 滚转默认 Q/E（1.0.75 起可配置）
    CHECK_STR(empty.cfg.key_roll_right, "E");
    CHECK_STR(empty.cfg.key_vertical_up, "MOUSE_LEFT");    // 升降默认鼠标左/右键（1.0.63 起可配置）
    CHECK_STR(empty.cfg.key_vertical_down, "MOUSE_RIGHT");
    // 移动/变速/自由视角门控（2.0.0-cpp-hotfix9 起可配置，默认值与旧写死键一致）
    CHECK_STR(empty.cfg.key_move_forward, "W");
    CHECK_STR(empty.cfg.key_move_back, "S");
    CHECK_STR(empty.cfg.key_move_left, "A");
    CHECK_STR(empty.cfg.key_move_right, "D");
    CHECK_STR(empty.cfg.key_boost, "SHIFT");
    CHECK_STR(empty.cfg.key_slow, "CONTROL");
    CHECK_STR(empty.cfg.key_free_look, "ALT");
    CHECK(empty.cfg.wheel_fov);            // 滚轮默认参与 FOV
    CHECK(!empty.cfg.wheel_fov_invert);    // 默认向上缩小 FOV
    CHECK_STR(empty.cfg.pad_roll_left, "LB");
    CHECK_STR(empty.cfg.pad_roll_right, "RB");
    CHECK_STR(empty.cfg.pad_boost, "LS");
    CHECK_STR(empty.cfg.pad_slow, "RS");
    CHECK_STR(empty.cfg.pad_vertical_up, "RT");
    CHECK_STR(empty.cfg.pad_vertical_down, "LT");
    CHECK_STR(empty.cfg.pad_pitch_up, "UP");    // 十字键上/下 = 俯仰
    CHECK_STR(empty.cfg.pad_pitch_down, "DOWN");
    CHECK_STR(empty.cfg.pad_roll_reset, "LB+RB");  // LB+RB 同按 = 滚转归零（1.0.41 并入）
    CHECK_STR(empty.cfg.pad_yaw_left, "LEFT");   // 十字键左/右 = 左右转视角（1.0.46 起从平移改来）
    CHECK_STR(empty.cfg.pad_yaw_right, "RIGHT");
    CHECK_STR(empty.cfg.pad_fov_up, "A");    // FOV 移到 A/B（十字键已全部让位给视角调节）
    CHECK_STR(empty.cfg.pad_fov_down, "B");
    CHECK(empty.cfg.freeze_mode == FreezeMode::Pause);
    CHECK(!empty.cfg.freeze_aa_off);           // 渲染对照默认关闭，不影响正常画质
    CHECK(!empty.cfg.freeze_wpo_velocity_off); // 渲染对照默认关闭，不影响正常画质
    CHECK(empty.cfg.freeze_framegen_off);      // 摄影模式默认抑制帧生成
    CHECK(empty.cfg.freeze_cloth);             // 布料模拟默认冻结，避免裙摆/披风在暂停下继续摆动
    CHECK(empty.cfg.freeze_skeletal_animation); // 骨骼动画默认冻结，防止 AnimBP 在暂停下继续驱动布料
    CHECK(empty.cfg.freeze_motion_blur_off);    // 动态模糊默认关闭，避免残留速度缓冲拖影
    CHECK(!empty.cfg.freeze_dlss_off);         // DLSS 关闭默认关闭，保留画质
    CHECK(!empty.cfg.freeze_custom_cloth);     // 自定义 Sway/Cloth 默认关闭，保持原有行为
    CHECK(!empty.cfg.freeze_wpo);              // WPO 评估冻结默认关闭
    CHECK(!empty.cfg.freeze_tonemapper_dither_off);  // Tonemapper 抖动关闭默认关闭
    CHECK(!empty.cfg.freeze_lumen_screen_traces_off);  // 光照 A/B 默认全关，不影响正常画质
    CHECK(!empty.cfg.freeze_lumen_short_range_ao_off);
    CHECK(!empty.cfg.freeze_lumen_radiance_cache_off);
    CHECK(!empty.cfg.freeze_lumen_fixed_jitter);
    CHECK(!empty.cfg.freeze_lumen_off);
    CHECK(!empty.cfg.freeze_lighting_ab_cycle);
    CHECK_NEAR(empty.cfg.freeze_lighting_ab_phase_seconds, 8.0, 1e-9);
    CHECK(!empty.cfg.freeze_shadow_ab_cycle);  // 阴影/后处理 A/B 默认关闭
    CHECK_NEAR(empty.cfg.freeze_shadow_ab_phase_seconds, 8.0, 1e-9);
    // F7 画面定格：默认时间补偿/音频挂起开启，安全阀 20 秒
    CHECK(empty.cfg.frame_freeze_time_compensation);
    CHECK(empty.cfg.frame_freeze_audio_suspend);
    CHECK_NEAR(empty.cfg.frame_freeze_timeout_seconds, 60.0, 1e-9);

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
key_roll_left = HOME
key_roll_right = END
key_vertical_up = MOUSE_MIDDLE
key_vertical_down = PAGE_DOWN

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

[look]
vignette = 2.5
vignette_enabled = false
saturation = 0.4
saturation_enabled = false
contrast = 1.6
bloom = 6
temperature = -0.5
temperature_enabled = false
color_gain = 1.8
color_gamma = 0.8
color_offset = -0.15

[effects]
auto_pause_time = false
freeze_sequences = false
freeze_dialogue_flow = false
stop_wwise_on_freeze = true
wwise_engine_suspend = false
freeze_aa_off = true
freeze_wpo_velocity_off = true
freeze_framegen_off = false
freeze_cloth = false
freeze_custom_cloth = true
freeze_wpo = true
freeze_tonemapper_dither_off = true
freeze_skeletal_animation = false
freeze_motion_blur_off = false
freeze_dlss_off = true
freeze_lumen_screen_traces_off = true
freeze_lumen_short_range_ao_off = true
freeze_lumen_radiance_cache_off = true
freeze_lumen_fixed_jitter = true
freeze_lumen_off = true
freeze_lighting_ab_cycle = true
freeze_lighting_ab_phase_seconds = 4.5
freeze_shadow_ab_cycle = true
freeze_shadow_ab_phase_seconds = 5.5
frame_freeze_time_compensation = false
frame_freeze_audio_suspend = false
frame_freeze_timeout_seconds = 45
time_offset_compensation = false

[misc]
log_level = debug
)";
    const ConfigLoadResult parsed = parse_config(ini, "test.ini");
    // time_offset_compensation 已弃用：无论值是什么都只告警一次（其余键无告警）
    CHECK_INT(parsed.warnings.size(), 1);
    CHECK_STR(parsed.cfg.key_toggle, "F9");
    CHECK_STR(parsed.cfg.key_freeze, "G");
    CHECK_STR(parsed.cfg.key_fov_up, "OEM_PLUS");
    CHECK_STR(parsed.cfg.key_pitch_up, "C");
    CHECK_STR(parsed.cfg.key_pitch_down, "V");
    CHECK_STR(parsed.cfg.key_yaw_left, "INSERT");
    CHECK_STR(parsed.cfg.key_yaw_right, "DELETE");
    CHECK_STR(parsed.cfg.key_roll_left, "HOME");
    CHECK_STR(parsed.cfg.key_roll_right, "END");
    CHECK_STR(parsed.cfg.key_vertical_up, "MOUSE_MIDDLE");
    CHECK_STR(parsed.cfg.key_vertical_down, "PAGE_DOWN");
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
    CHECK_NEAR(parsed.cfg.vignette, 2.5, 1e-9);
    CHECK(!parsed.cfg.vignette_enabled);
    CHECK_NEAR(parsed.cfg.saturation, 0.4, 1e-9);
    CHECK(!parsed.cfg.saturation_enabled);
    CHECK_NEAR(parsed.cfg.contrast, 1.6, 1e-9);
    CHECK(parsed.cfg.contrast_enabled);  // 未写的开关保持默认开启
    CHECK_NEAR(parsed.cfg.bloom, 6.0, 1e-9);
    CHECK_NEAR(parsed.cfg.temperature, -0.5, 1e-9);
    CHECK(!parsed.cfg.temperature_enabled);
    // 调色·进阶三件解析（越界夹紧在 test_config_color_pro_keys 覆盖）
    CHECK_NEAR(parsed.cfg.color_gain, 1.8, 1e-9);
    CHECK_NEAR(parsed.cfg.color_gamma, 0.8, 1e-9);
    CHECK_NEAR(parsed.cfg.color_offset, -0.15, 1e-9);
    CHECK(!parsed.cfg.auto_pause_time);
    CHECK(!parsed.cfg.freeze_sequences);
    CHECK(!parsed.cfg.freeze_dialogue_flow);
    CHECK(parsed.cfg.stop_wwise_on_freeze);
    CHECK(!parsed.cfg.wwise_engine_suspend);
    CHECK(parsed.cfg.freeze_aa_off);
    CHECK(parsed.cfg.freeze_wpo_velocity_off);
    CHECK(!parsed.cfg.freeze_framegen_off);
    CHECK(!parsed.cfg.freeze_cloth);
    CHECK(parsed.cfg.freeze_custom_cloth);
    CHECK(parsed.cfg.freeze_wpo);
    CHECK(parsed.cfg.freeze_tonemapper_dither_off);
    CHECK(!parsed.cfg.freeze_skeletal_animation);
    CHECK(!parsed.cfg.freeze_motion_blur_off);
    CHECK(parsed.cfg.freeze_dlss_off);
    CHECK(parsed.cfg.freeze_lumen_screen_traces_off);
    CHECK(parsed.cfg.freeze_lumen_short_range_ao_off);
    CHECK(parsed.cfg.freeze_lumen_radiance_cache_off);
    CHECK(parsed.cfg.freeze_lumen_fixed_jitter);
    CHECK(parsed.cfg.freeze_lumen_off);
    CHECK(parsed.cfg.freeze_lighting_ab_cycle);
    CHECK_NEAR(parsed.cfg.freeze_lighting_ab_phase_seconds, 4.5, 1e-9);
    CHECK(parsed.cfg.freeze_shadow_ab_cycle);
    CHECK_NEAR(parsed.cfg.freeze_shadow_ab_phase_seconds, 5.5, 1e-9);
    CHECK(!parsed.cfg.frame_freeze_time_compensation);
    CHECK(!parsed.cfg.frame_freeze_audio_suspend);
    CHECK_NEAR(parsed.cfg.frame_freeze_timeout_seconds, 45.0, 1e-9);
    CHECK(parsed.cfg.log_level == LogLevel::Debug);
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
    CHECK_NEAR(parsed.cfg.pitch_limit, 90.0, 1e-9);  // 超上限夹紧到边界（不再整键回默认）
    CHECK_NEAR(parsed.cfg.roll_speed, 5.0, 1e-9);   // 非正值夹紧到下限
    CHECK_NEAR(parsed.cfg.pitch_speed, 5.0, 1e-9);  // 俯仰速度同规则
    CHECK_NEAR(parsed.cfg.slow_look_mult, 0.01, 1e-9);   // 越界夹紧到下限
    CHECK_NEAR(parsed.cfg.yaw_speed, 5.0, 1e-9);    // 非正值夹紧到下限
    CHECK_NEAR(parsed.cfg.freeze_dilation, 1e-9, 1e-15);  // 下限本身

    // fov_default 落在范围外要被夹紧并告警
    const ConfigLoadResult clamp = parse_config("fov_min = 20\nfov_max = 60\nfov_default = 90\n");
    CHECK(clamp.warnings.size() >= 1);
    CHECK_NEAR(clamp.cfg.fov_default, 60.0, 1e-9);

    // 缺文件：ok=false，但区分「文件不存在」（首次运行，正常）与「读不了」
    const ConfigLoadResult missing = load_config_file("definitely_not_here.ini");
    CHECK(!missing.ok);
    CHECK(missing.file_missing);
    CHECK(!missing.read_error);
    CHECK(missing.warnings.size() >= 1);
    CHECK_STR(missing.cfg.key_toggle, "F2");
}

// 景深/色差/颗粒键名复活（2.1）：判死时期（hotfix32/34）曾进 legacy 清单的键重新成为正式键。
// 旧 config.ini 里残留的这些键按原值继续生效，不再收集/清理；legacy 机制保留但当前为空。
void test_config_revived_dof_keys() {
    section("parse_config 景深/胶片键复活");
    using namespace pbzpm;

    const ConfigLoadResult revived = parse_config(
        "[dof]\ndof_enabled = true\ndof_fstop = 1.4\ndof_focal_distance = 450\nkey_focus_near = COMMA\n"
        "pad_focus_near = LB+BACK\n"
        "[look]\nvignette = 2\nchromatic_aberration = 3\n",
        "revived.ini");
    // 全部按正式键解析进 Config（不再有 legacy 收集）
    CHECK_INT(static_cast<int>(revived.legacy_keys.size()), 0);
    CHECK(revived.cfg.dof_enabled);
    CHECK_NEAR(revived.cfg.dof_fstop, 1.4, 1e-9);
    CHECK_NEAR(revived.cfg.dof_focal_distance, 450.0, 1e-9);
    CHECK_STR(revived.cfg.key_focus_near, "COMMA");
    CHECK_STR(revived.cfg.pad_focus_near, "LB+BACK");
    CHECK_NEAR(revived.cfg.vignette, 2.0, 1e-9);
    CHECK_NEAR(revived.cfg.chromatic_aberration, 3.0, 1e-9);
    // 越界夹紧：对焦距离/光圈按 tuning 范围收口
    const ConfigLoadResult clamped = parse_config("dof_focal_distance = 9999999\ndof_fstop = 0.01\n", "clamp.ini");
    CHECK(clamped.cfg.dof_focal_distance >= tuning::kDofFocalMinCm &&
          clamped.cfg.dof_focal_distance <= tuning::kDofFocalMaxCm);
    CHECK_NEAR(clamped.cfg.dof_fstop, tuning::kDofFstopMin, 1e-9);
    // 序列化往返：复活键出现在规范输出里，再解析值不变
    const std::string text = config_ini_text(revived.cfg);
    CHECK(text.find("[dof]") != std::string::npos);
    CHECK(text.find("dof_fstop = 1.4") != std::string::npos);
    CHECK(text.find("chromatic_aberration = 3") != std::string::npos);
    const ConfigLoadResult roundtrip = parse_config(text, "roundtrip.ini");
    CHECK_INT(static_cast<int>(roundtrip.warnings.size()), 0);
    CHECK(roundtrip.cfg.dof_enabled);
    CHECK_NEAR(roundtrip.cfg.dof_focal_distance, 450.0, 1e-9);
    // reset_look_defaults 把色差/颗粒连同五项一起归默认（每次进入摄影模式恢复）
    Config reset_cfg = revived.cfg;
    (void)reset_look_defaults(reset_cfg);
    CHECK_NEAR(reset_cfg.chromatic_aberration, 0.0, 1e-9);
    CHECK_NEAR(reset_cfg.vignette, 0.0, 1e-9);
    CHECK(reset_cfg.dof_enabled);  // [dof] 参数不随进入重置（用户配置），与 [look] 不同
}

// 调色·进阶三件（2026-11-08 批次；实机探针后由 13 项收缩而来）：解析/越界夹紧/往返/reset +
// 中性值语义。中性值=恒等值（gain/gamma 1、offset 0），中性=不覆盖该项（bOverride 不下发）。
// 判死十项的取证结论在 tuning.h 顶部（此处只钉存活项）。
void test_config_color_pro_keys() {
    section("parse_config 调色·进阶三件");
    using namespace pbzpm;

    const ConfigLoadResult pro = parse_config(
        "[look]\ncolor_gain = 1.8\ncolor_gamma = 0.8\ncolor_offset = -0.15\n", "pro.ini");
    CHECK(pro.warnings.empty());
    CHECK_NEAR(pro.cfg.color_gain, 1.8, 1e-9);
    CHECK_NEAR(pro.cfg.color_gamma, 0.8, 1e-9);
    CHECK_NEAR(pro.cfg.color_offset, -0.15, 1e-9);

    // 中性值语义：全默认的 LookOptions 不活跃；离中性即活跃，且进 ColorGrading LUT 家族
    LookOptions neutral{};
    CHECK(!neutral.color_pro_active());
    CHECK(!neutral.color_grading_active());
    LookOptions touched{};
    touched.color_gain = 1.5;
    touched.color_offset = -0.1;
    CHECK(touched.color_pro_active());
    CHECK(touched.color_grading_active());
    // 相等比较覆盖新字段（值变了必须能看出来）
    CHECK(!look_options_equal(neutral, touched));

    // 越界夹紧：下限/上限各取一头
    const ConfigLoadResult clamped = parse_config(
        "[look]\ncolor_gain = -5\ncolor_gamma = 99\ncolor_offset = 9\n", "clamp.ini");
    CHECK(clamped.warnings.size() >= 3);
    CHECK_NEAR(clamped.cfg.color_gain, tuning::kColorGainMin, 1e-9);
    CHECK_NEAR(clamped.cfg.color_gamma, tuning::kColorGammaMax, 1e-9);
    CHECK_NEAR(clamped.cfg.color_offset, tuning::kColorOffsetMax, 1e-9);

    // 非法文本保留默认
    const ConfigLoadResult bad = parse_config("[look]\ncolor_gain = abc\n", "bad.ini");
    CHECK(bad.warnings.size() >= 1);
    CHECK_NEAR(bad.cfg.color_gain, tuning::kColorGainDefault, 1e-9);

    // 序列化往返：新键出现在规范输出里，再解析值不变、零告警
    const std::string text = config_ini_text(pro.cfg);
    CHECK(text.find("color_gain = 1.8") != std::string::npos);
    CHECK(text.find("color_offset = -0.15") != std::string::npos);
    const ConfigLoadResult roundtrip = parse_config(text, "roundtrip.ini");
    CHECK(roundtrip.warnings.empty());
    CHECK_NEAR(roundtrip.cfg.color_gain, 1.8, 1e-9);
    CHECK_NEAR(roundtrip.cfg.color_offset, -0.15, 1e-9);

    // reset_look_defaults 把三件连同老五项一起归中性（每次进入摄影模式恢复）
    Config reset_cfg = pro.cfg;
    CHECK(reset_look_defaults(reset_cfg));
    CHECK_NEAR(reset_cfg.color_gain, tuning::kColorGainDefault, 1e-9);
    CHECK_NEAR(reset_cfg.color_offset, 0.0, 1e-9);
    CHECK(!reset_look_defaults(reset_cfg));  // 已是默认，再 reset 无变化
}

// schema 表驱动：每个数值键的 0 / 负值 / 极大 / 非法文本边界；section 归属；告警去重
void test_config_schema_and_sections() {
    section("parse_config schema 边界 / section 归属 / 告警去重");
    using namespace pbzpm;

    struct NumCase {
        const char* key;
        const char* section;
        double Config::* member;
        double lo;
        double hi;
        const char* extra;  // 同 section 的伴随键，避免 fov 交叉校验干扰单键边界
    };
    static const NumCase kNumCases[] = {
        {"move_speed", "control", &Config::move_speed, 50.0, 2000.0, ""},
        {"boost_mult", "control", &Config::boost_mult, 1.0, 10.0, ""},
        {"slow_mult", "control", &Config::slow_mult, 0.05, 1.0, ""},
        {"slow_look_mult", "control", &Config::slow_look_mult, 0.01, 1.0, ""},
        {"look_sensitivity", "control", &Config::look_sensitivity, 0.01, 1.0, ""},
        {"pitch_limit", "control", &Config::pitch_limit, 1.0, 90.0, ""},
        {"vertical_speed", "control", &Config::vertical_speed, 10.0, 1000.0, ""},
        {"roll_speed", "control", &Config::roll_speed, 5.0, 360.0, ""},
        {"pitch_speed", "control", &Config::pitch_speed, 5.0, 180.0, ""},
        {"yaw_speed", "control", &Config::yaw_speed, 5.0, 270.0, ""},
        {"pad_look_speed", "gamepad", &Config::pad_look_speed, 10.0, 720.0, ""},
        {"pad_move_scale", "gamepad", &Config::pad_move_scale, 0.0, 3.0, ""},
        {"pad_deadzone", "gamepad", &Config::pad_deadzone, 0.0, 0.5, ""},
        {"fov_default", "camera", &Config::fov_default, 1.0, 179.0, "fov_min = 1\nfov_max = 179\n"},
        {"fov_min", "camera", &Config::fov_min, 1.0, 178.0, "fov_max = 179\n"},
        {"fov_max", "camera", &Config::fov_max, 2.0, 179.0, "fov_min = 1\n"},
        {"fov_step", "camera", &Config::fov_step, 0.5, 10.0, ""},
        {"freeze_dilation", "effects", &Config::freeze_dilation, 1e-9, 1.0, ""},
        {"freeze_lighting_ab_phase_seconds", "effects", &Config::freeze_lighting_ab_phase_seconds, 1.0, 120.0, ""},
        {"freeze_shadow_ab_phase_seconds", "effects", &Config::freeze_shadow_ab_phase_seconds, 1.0, 120.0, ""},
    };

    auto parse_num = [](const NumCase& c, const char* value) {
        std::string ini = std::string("[") + c.section + "]\n" + c.key + " = " + value + "\n" + c.extra;
        return parse_config(ini, "case");
    };

    const Config defaults{};
    for (const NumCase& c : kNumCases) {
        // 0：下限 > 0 时夹紧到下限；下限本身为 0 的键（pad_move_scale/pad_deadzone）0 合法
        const ConfigLoadResult zero = parse_num(c, "0");
        CHECK_NEAR(zero.cfg.*c.member, c.lo > 0.0 ? c.lo : 0.0, 1e-9);
        CHECK(zero.warnings.size() >= 1 || c.lo <= 0.0);

        // 负值：一律夹紧到下限
        const ConfigLoadResult neg = parse_num(c, "-1");
        CHECK_NEAR(neg.cfg.*c.member, c.lo, 1e-9);
        CHECK(neg.warnings.size() >= 1);

        // 极大：夹紧到上限
        const ConfigLoadResult huge = parse_num(c, "1e12");
        CHECK_NEAR(huge.cfg.*c.member, c.hi, 1e-9);
        CHECK(huge.warnings.size() >= 1);

        // 非法文本：保留默认并告警
        const ConfigLoadResult text = parse_num(c, "abc");
        CHECK_NEAR(text.cfg.*c.member, defaults.*c.member, 1e-9);
        CHECK(text.warnings.size() >= 1);
    }

    // 行尾注释：数值键后无空白直接跟 ;/# 也能截断（不再回默认）
    const ConfigLoadResult trailing = parse_config("[camera]\nfov_default = 50;备注\nfov_step = 1#x\n", "trailing");
    CHECK_INT((int)trailing.warnings.size(), 0);
    CHECK_NEAR(trailing.cfg.fov_default, 50.0, 1e-9);
    CHECK_NEAR(trailing.cfg.fov_step, 1.0, 1e-9);

    // 引号内的 ; / # 按字面值保留
    const ConfigLoadResult quoted = parse_config("[keys]\nkey_toggle = \"F9;备注\"\n", "quoted");
    CHECK_INT((int)quoted.warnings.size(), 0);
    CHECK_STR(quoted.cfg.key_toggle, "F9;备注");

    // 平铺（无 section）兼容旧配置：不误报 section 错位
    const ConfigLoadResult flat = parse_config("key_toggle = F8\nmove_speed = 600\n", "flat");
    CHECK_INT((int)flat.warnings.size(), 0);
    CHECK_STR(flat.cfg.key_toggle, "F8");
    CHECK_NEAR(flat.cfg.move_speed, 600.0, 1e-9);

    // section 与键期望不符：告警一次，值仍按键解析（不静默串键）
    const ConfigLoadResult wrong_section = parse_config("[camera]\nmove_speed = 600\n", "wrong");
    CHECK_INT((int)wrong_section.warnings.size(), 1);
    CHECK(wrong_section.warnings[0].find("section") != std::string::npos);
    CHECK_NEAR(wrong_section.cfg.move_speed, 600.0, 1e-9);

    // 同一键跨 section 重复：按「同一键只报一次」去重，以最后一次为准
    const ConfigLoadResult dup_section =
        parse_config("[keys]\nkey_freeze = F\n[control]\nkey_freeze = G\n", "dup");
    CHECK_INT((int)dup_section.warnings.size(), 1);
    CHECK_STR(dup_section.cfg.key_freeze, "G");

    // 告警去重：同一键多次越界只报一次（最后一次的值生效）
    const ConfigLoadResult dup_key = parse_config("[control]\nmove_speed = 0\nmove_speed = 99999\n", "dupkey");
    CHECK_INT((int)dup_key.warnings.size(), 1);
    CHECK_NEAR(dup_key.cfg.move_speed, 2000.0, 1e-9);

    // 未知 section 每段一次、未知键只报一次；非法枚举保留默认
    const ConfigLoadResult misc = parse_config(
        "[control]\nmouse_vertical_mode = diagonal\n[unknown]\nfoo = 1\nfoo = 2\n", "misc");
    CHECK_INT((int)misc.warnings.size(), 3);
    CHECK(misc.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);

    // 弃用键：接受但忽略，三个键各告警一次
    const ConfigLoadResult dep = parse_config(
        "[misc]\ninterval_ms = 16\nui_scale = 1.25\ntime_offset_compensation = true\n", "dep");
    CHECK_INT((int)dep.warnings.size(), 3);
    CHECK(dep.warnings[0].find("interval_ms") != std::string::npos);
    CHECK(dep.warnings[1].find("ui_scale") != std::string::npos);
    CHECK(dep.warnings[2].find("time_offset_compensation") != std::string::npos);

    // 非法行 / 空键 / 空文本值：逐行告警
    const ConfigLoadResult rows = parse_config("this line has no equals\n = 3\n", "rows");
    CHECK_INT((int)rows.warnings.size(), 2);
    const ConfigLoadResult empty_text = parse_config("[keys]\nkey_toggle =\n", "empty");
    CHECK_INT((int)empty_text.warnings.size(), 1);
    CHECK_STR(empty_text.cfg.key_toggle, "F2");

    // 路径存在但是目录：read_error，不是 file_missing
    namespace fs = std::filesystem;
    const fs::path dir = "pbzpm_config_read_error_dir";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directory(dir, ec);
    const ConfigLoadResult read_err = load_config_file(dir.string());
    CHECK(!read_err.ok);
    CHECK(read_err.read_error);
    CHECK(!read_err.file_missing);
    fs::remove_all(dir, ec);
}

void test_config_default_text_roundtrip() {
    section("default_config_text 往返");
    using namespace pbzpm;
    const std::string text = default_config_text();
    CHECK(text.find("key_toggle") != std::string::npos);
    CHECK(text.find("roll_speed") != std::string::npos);
    CHECK(text.find("key_pitch_up") != std::string::npos);
    CHECK(text.find("key_roll_left") != std::string::npos);
    CHECK(text.find("key_roll_right") != std::string::npos);
    CHECK(text.find("key_vertical_up") != std::string::npos);
    CHECK(text.find("pad_pitch_up") != std::string::npos);
    CHECK(text.find("pitch_speed") != std::string::npos);
    CHECK(text.find("slow_look_mult") != std::string::npos);
    CHECK(text.find("yaw_speed") != std::string::npos);
    CHECK(text.find("wwise_engine_suspend") != std::string::npos);
    CHECK(text.find("freeze_dialogue_flow") != std::string::npos);
    CHECK(text.find("takeover_current_camera") != std::string::npos);
    // 景深/对焦复活（2.1）：默认配置里有 [dof] 段与对焦/光圈键位（默认关闭）
    CHECK(text.find("key_focus_near") != std::string::npos);
    CHECK(text.find("pad_focus_near") != std::string::npos);
    CHECK(text.find("[dof]") != std::string::npos);
    CHECK(text.find("dof_enabled = false") != std::string::npos);
    CHECK(text.find("chromatic_aberration = 0") != std::string::npos);
    CHECK(text.find("key_dof_toggle") != std::string::npos);
    CHECK(text.find("key_move_forward") != std::string::npos);  // 2.0.0-cpp-hotfix9
    CHECK(text.find("key_boost") != std::string::npos);
    CHECK(text.find("key_free_look") != std::string::npos);
    CHECK(text.find("wheel_fov") != std::string::npos);
    CHECK(text.find("pad_boost") != std::string::npos);
    CHECK(text.find("pad_vertical_up") != std::string::npos);
    CHECK(text.find("vignette_enabled") != std::string::npos);  // 画面效果五项独立开关随配置序列化
    CHECK(text.find("temperature_enabled") != std::string::npos);
    // 调色·进阶三件随配置序列化（中性默认：增益/伽马 1、黑位 0）
    CHECK(text.find("color_gain") != std::string::npos);
    CHECK(text.find("color_gamma") != std::string::npos);
    CHECK(text.find("color_offset") != std::string::npos);
    const ConfigLoadResult parsed = parse_config(text, "generated");
    CHECK_INT(parsed.warnings.size(), 0);
    CHECK_STR(parsed.cfg.key_toggle, "F2");
    CHECK_NEAR(parsed.cfg.fov_step, 2.0, 1e-9);
    CHECK(parsed.cfg.ultrawide_unlock);
    CHECK(parsed.cfg.auto_pause_time);
    CHECK(parsed.cfg.freeze_sequences);
    CHECK(parsed.cfg.freeze_dialogue_flow);  // 默认文本同样开启
    CHECK(parsed.cfg.stop_wwise_on_freeze);  // 默认文本同样开启
    CHECK(parsed.cfg.wwise_engine_suspend);
    CHECK(parsed.cfg.mouse_vertical_mode == MouseVerticalMode::Translate);
    CHECK_NEAR(parsed.cfg.roll_speed, 60.0, 1e-9);
    CHECK_NEAR(parsed.cfg.pitch_speed, 30.0, 1e-9);
    CHECK_NEAR(parsed.cfg.slow_look_mult, 0.1, 1e-9);
    CHECK_NEAR(parsed.cfg.yaw_speed, 90.0, 1e-9);
    CHECK_STR(parsed.cfg.key_pitch_up, "UP");  // 默认文本里俯仰/转向键可被原样解析回来
    CHECK_STR(parsed.cfg.key_yaw_left, "LEFT");
    CHECK_STR(parsed.cfg.key_roll_left, "Q");
    CHECK_STR(parsed.cfg.key_roll_right, "E");
    CHECK_STR(parsed.cfg.key_vertical_up, "MOUSE_LEFT");
    CHECK_STR(parsed.cfg.key_vertical_down, "MOUSE_RIGHT");
    CHECK_STR(parsed.cfg.pad_fov_up, "A");
    CHECK_STR(parsed.cfg.pad_yaw_right, "RIGHT");
    CHECK_STR(parsed.cfg.key_move_forward, "W");
    CHECK_STR(parsed.cfg.key_boost, "SHIFT");
    CHECK_STR(parsed.cfg.key_slow, "CONTROL");
    CHECK_STR(parsed.cfg.key_free_look, "ALT");
    CHECK(parsed.cfg.wheel_fov);
    CHECK(!parsed.cfg.wheel_fov_invert);
    CHECK_STR(parsed.cfg.pad_roll_left, "LB");
    CHECK_STR(parsed.cfg.pad_boost, "LS");
    CHECK_STR(parsed.cfg.pad_vertical_up, "RT");
    CHECK_STR(parsed.cfg.pad_vertical_down, "LT");
    CHECK(parsed.cfg.takeover_current_camera);  // 默认启用接管当前相机（失败自动回退）
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

// ---------------------------------------------------------------- 引擎端口纯判定（M-1/M-6/M-7）

void test_engine_port_pure_logic() {
    section("EnginePort 纯判定（日志级别/参数名/ViewTarget/位姿）");
    using namespace pbzpm;

    // M-6：阈值越高输出越少；Error 在任何阈值下都必须保留
    CHECK(log_level_enabled(LogLevel::Error, LogLevel::Error));
    CHECK(!log_level_enabled(LogLevel::Error, LogLevel::Warn));
    CHECK(!log_level_enabled(LogLevel::Error, LogLevel::Info));
    CHECK(!log_level_enabled(LogLevel::Error, LogLevel::Debug));
    CHECK(log_level_enabled(LogLevel::Warn, LogLevel::Error));
    CHECK(log_level_enabled(LogLevel::Warn, LogLevel::Warn));
    CHECK(!log_level_enabled(LogLevel::Warn, LogLevel::Info));
    CHECK(log_level_enabled(LogLevel::Info, LogLevel::Info));
    CHECK(!log_level_enabled(LogLevel::Info, LogLevel::Debug));
    CHECK(log_level_enabled(LogLevel::Debug, LogLevel::Debug));
    for (LogLevel threshold : {LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug}) {
        CHECK(log_level_enabled(threshold, LogLevel::Error));
    }

    // M-7：参数名只按名字命中；全不中返回 -1（禁止按位置退化）
    const std::vector<std::string> present = {"DeltaX", "ReturnValue", "InString"};
    auto exists = [&](const char* name) {
        return std::find(present.begin(), present.end(), name) != present.end();
    };
    CHECK_INT(resolve_param_name({"DeltaX", "X"}, exists), 0);
    CHECK_INT(resolve_param_name({"X", "ReturnValue"}, exists), 1);
    CHECK_INT(resolve_param_name({"Foo", "Bar"}, exists), -1);
    CHECK_INT(resolve_param_name({}, exists), -1);              // 空候选集不属于任何位置
    CHECK_INT(resolve_param_name({nullptr, "ReturnValue"}, exists), 1);

    // M-1：ViewTarget 三态；读不出来不能被当成成功
    CHECK(classify_view_target(false, false) == ViewTargetState::Unreadable);
    CHECK(classify_view_target(false, true) == ViewTargetState::Unreadable);
    CHECK(classify_view_target(true, true) == ViewTargetState::Match);
    CHECK(classify_view_target(true, false) == ViewTargetState::Mismatch);

    // M-1 收口：回读 null / 不可读不是“确凿失效”，不能据此阻断进入或退出；
    // 只有读到明确的其他 Actor，或摄影相机位姿被搬走，才算接管真的失效。
    CHECK(!view_loss_is_conclusive(false, false, true, false));  // 读不出，Target 也未知
    CHECK(!view_loss_is_conclusive(true, false, true, false));   // 读到了但 Target=null（同帧未生效）
    CHECK(view_loss_is_conclusive(true, true, true, false));     // 读到明确的其他 Actor
    CHECK(view_loss_is_conclusive(true, false, true, true));     // 目标是摄影相机但位姿被搬走
    // 首帧误报回归点：刚 spawn、apply_pose 还没写入时位姿不可比，
    // 此时位姿偏差不能算确凿失效（真机 782/953/1019 行每次进入都误报一次）。
    CHECK(!view_loss_is_conclusive(true, false, false, true));
    CHECK(!view_loss_is_conclusive(false, false, false, true));

    // M-1：相机位姿容差（位置 10 / 角度 2°），角度要按 ±180 归一化
    CameraPose expected{};
    CameraPose actual{};
    CHECK(camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    actual.loc.x = 5.0;
    CHECK(camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    actual.loc.x = 20.0;
    CHECK(!camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    actual = expected;
    actual.rot.yaw = 1.0;
    CHECK(camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    actual.rot.yaw = 5.0;
    CHECK(!camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    expected.rot.yaw = 359.0;
    actual = expected;
    actual.rot.yaw = 1.0;  // 差 2°，不应误判成 358°
    CHECK(camera_pose_within_tolerance(expected, actual, 10.0, 2.0));
    actual.rot.roll = 4.0;
    CHECK(!camera_pose_within_tolerance(expected, actual, 10.0, 2.0));

    // 焦距 ↔ FOV 换算：36mm 传感器、90° 水平视野 ≈ 18mm 焦距；往返一致；非法输入返回 0
    CHECK_NEAR(focal_length_from_fov(36.0, 90.0), 18.0, 1e-9);
    CHECK_NEAR(fov_from_focal_length(36.0, 18.0), 90.0, 1e-9);
    CHECK_NEAR(fov_from_focal_length(36.0, focal_length_from_fov(36.0, 50.0)), 50.0, 1e-6);
    CHECK_NEAR(focal_length_from_fov(0.0, 90.0), 0.0, 1e-12);
    CHECK_NEAR(focal_length_from_fov(36.0, 0.0), 0.0, 1e-12);
    CHECK_NEAR(focal_length_from_fov(36.0, 180.0), 0.0, 1e-12);
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
    CHECK_INT(keys.roll_left, 'Q');             // 滚转默认 Q/E（1.0.75 起可配置）
    CHECK_INT(keys.roll_right, 'E');
    CHECK_INT(keys.vertical_up, kMouseLeft);    // 升降默认鼠标左/右键（1.0.63 起可配置）
    CHECK_INT(keys.vertical_down, kMouseRight);
    CHECK_INT(keys.move_forward, 'W');    // 移动/变速/自由视角门控默认值（2.0.0-cpp-hotfix9 起可配）
    CHECK_INT(keys.move_back, 'S');
    CHECK_INT(keys.move_left, 'A');
    CHECK_INT(keys.move_right, 'D');
    CHECK_INT(keys.boost, kShiftGeneric);
    CHECK_INT(keys.slow, kCtrlGeneric);
    CHECK_INT(keys.free_look, kAltGeneric);
    CHECK_INT(keys.frame_freeze, 0x76);   // F7
    CHECK_INT((int)keys.pad_roll_left.size(), 1);       // LB
    CHECK_INT((int)keys.pad_roll_right.size(), 1);      // RB
    CHECK_INT((int)keys.pad_boost.size(), 1);           // LS
    CHECK_INT((int)keys.pad_slow.size(), 1);            // RS
    CHECK_INT((int)keys.pad_vertical_up.size(), 1);     // RT
    CHECK_INT((int)keys.pad_vertical_down.size(), 1);   // LT

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
    in.on_key(kMouseLeft, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);
    CHECK(!intent.pitch_up_click);
    in.on_key(kMouseLeft, false);
    in.on_key(kMouseRight, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.move_up, -1.0, 1e-9);
    in.reset();

    // 升降键可换：鼠标右键上 / 左键下，或改成键盘键
    Config swapped = cfg;
    swapped.key_vertical_up = "MOUSE_RIGHT";
    swapped.key_vertical_down = "MOUSE_LEFT";
    std::vector<std::string> swap_warnings;
    const KeyMap swap_keys = build_keymap(swapped, &swap_warnings);
    CHECK_INT(swap_warnings.size(), 0);
    in.on_key(kMouseRight, true);
    intent = in.build(swapped, swap_keys);
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);
    in.on_key(kMouseRight, false);
    in.on_key(kMouseLeft, true);
    intent = in.build(swapped, swap_keys);
    CHECK_NEAR(intent.move_up, -1.0, 1e-9);
    in.reset();

    Config keyboard_vertical = cfg;
    keyboard_vertical.key_vertical_up = "PAGE_UP";
    keyboard_vertical.key_vertical_down = "PAGE_DOWN";
    const KeyMap kb_keys = build_keymap(keyboard_vertical, nullptr);
    in.on_key(0x21, true);
    CHECK_NEAR(in.build(keyboard_vertical, kb_keys).move_up, 1.0, 1e-9);
    in.on_key(0x21, false);
    in.on_key(0x22, true);
    CHECK_NEAR(in.build(keyboard_vertical, kb_keys).move_up, -1.0, 1e-9);
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

    // 滚转键可改（1.0.75）：key_roll_left/right 换任意键名后按新键生效、旧 Q/E 失效
    Config roll_cfg = cfg;
    roll_cfg.key_roll_left = "HOME";
    roll_cfg.key_roll_right = "END";
    std::vector<std::string> roll_warnings;
    const KeyMap roll_keys = build_keymap(roll_cfg, &roll_warnings);
    CHECK_INT(roll_warnings.size(), 0);
    CHECK_INT(roll_keys.roll_left, 0x24);   // HOME
    CHECK_INT(roll_keys.roll_right, 0x23);  // END
    in.on_key(0x24, true);
    CHECK_NEAR(in.build(roll_cfg, roll_keys).roll, -1.0, 1e-9);
    in.on_key(0x24, false);
    in.on_key(0x23, true);
    CHECK_NEAR(in.build(roll_cfg, roll_keys).roll, 1.0, 1e-9);
    in.on_key(0x23, false);
    in.on_key('Q', true);
    CHECK_NEAR(in.build(roll_cfg, roll_keys).roll, 0.0, 1e-9);  // 旧默认键不再触发
    in.on_key('Q', false);
    in.reset();

    // Q/E 滚转与鼠标左右键升降同帧叠加互不干扰
    in.on_key('Q', true);
    in.on_key(kMouseLeft, true);
    intent = in.build(cfg, keys);
    CHECK_NEAR(intent.roll, -1.0, 1e-9);
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);
    in.on_key('Q', false);
    in.on_key(kMouseLeft, false);
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

    // look 模式：升降键变俯仰，与 Q/E 滚转互不影响
    Config look_cfg = cfg;
    look_cfg.mouse_vertical_mode = MouseVerticalMode::Look;
    in.on_key(kMouseLeft, true);
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

    // ---- 2.0.0-cpp-hotfix9：移动 / 变速 / 自由视角门控改键 ----
    Config rb = cfg;
    rb.key_move_forward = "I";
    rb.key_move_back = "K";
    rb.key_move_left = "J";
    rb.key_move_right = "L";
    rb.key_boost = "G";
    rb.key_slow = "H";
    rb.key_free_look = "V";
    std::vector<std::string> rb_warnings;
    const KeyMap rb_keys = build_keymap(rb, &rb_warnings);
    CHECK_INT(rb_warnings.size(), 0);
    CHECK_INT(rb_keys.move_forward, 'I');
    CHECK_INT(rb_keys.move_back, 'K');
    CHECK_INT(rb_keys.move_left, 'J');
    CHECK_INT(rb_keys.move_right, 'L');
    CHECK_INT(rb_keys.boost, 'G');
    CHECK_INT(rb_keys.slow, 'H');
    CHECK_INT(rb_keys.free_look, 'V');

    in.on_key('I', true);
    in.on_key('J', true);
    intent = in.build(rb, rb_keys);
    CHECK_NEAR(intent.move_forward, 1.0, 1e-9);
    CHECK_NEAR(intent.move_right, -1.0, 1e-9);
    in.on_key('W', true);  // 旧默认键不再触发（W 不在 rb 的任何绑定上）
    CHECK_NEAR(in.build(rb, rb_keys).move_forward, 1.0, 1e-9);
    in.reset();

    in.on_key('G', true);
    in.on_key('H', true);
    intent = in.build(rb, rb_keys);
    CHECK(intent.boost);
    CHECK(intent.slow);
    in.reset();
    in.on_key(kShiftLeft, true);  // 旧默认 SHIFT 不再触发变速
    CHECK(!in.build(rb, rb_keys).boost);
    in.reset();

    // 自由视角门控：默认 ALT 换成 V 后，按 Alt 不再放行、按 V 放行
    in.on_mouse_move(5.0, 5.0);
    CHECK_NEAR(in.build(rb, rb_keys).look_dx, 0.0, 1e-9);
    in.reset();
    in.on_mouse_move(5.0, 5.0);
    in.on_key(kAltLeft, true);
    CHECK_NEAR(in.build(rb, rb_keys).look_dx, 0.0, 1e-9);
    in.reset();
    in.on_mouse_move(5.0, 5.0);
    in.on_key('V', true);
    CHECK_NEAR(in.build(rb, rb_keys).look_dx, 5.0, 1e-9);
    in.reset();

    // 手柄：滚转/变速/升降改绑后按新按钮生效
    Config pad_cfg = cfg;
    pad_cfg.pad_roll_left = "X";
    pad_cfg.pad_roll_right = "Y";
    pad_cfg.pad_boost = "A";
    pad_cfg.pad_slow = "B";
    pad_cfg.pad_vertical_up = "Y";
    pad_cfg.pad_vertical_down = "X";
    std::vector<std::string> pad_warnings;
    const KeyMap pad_keys = build_keymap(pad_cfg, &pad_warnings);
    CHECK_INT(pad_warnings.size(), 0);

    PadState pad_y;
    pad_y.held[static_cast<int>(PadButton::Y)] = true;
    in.on_gamepad(pad_y);
    intent = in.build(pad_cfg, pad_keys);
    CHECK_NEAR(intent.roll, 1.0, 1e-9);      // Y = pad_roll_right
    CHECK_NEAR(intent.move_up, 1.0, 1e-9);   // Y = pad_vertical_up（数字量）
    in.on_gamepad(PadState{});
    in.reset();

    PadState pad_a;
    pad_a.held[static_cast<int>(PadButton::A)] = true;
    in.on_gamepad(pad_a);
    CHECK(in.build(pad_cfg, pad_keys).boost);  // A = pad_boost
    in.on_gamepad(PadState{});
    in.reset();

    // 默认 RT/LT 单键绑定保留模拟量：半按 = 0.5
    PadState pad_half;
    pad_half.rt = 0.5;
    pad_half.held[static_cast<int>(PadButton::RT)] = true;
    in.on_gamepad(pad_half);
    CHECK_NEAR(in.build(cfg, keys).move_up, 0.5, 1e-9);
    in.on_gamepad(PadState{});
    in.reset();
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

// 顺序断言：a 必须出现在 b 之前（两者都必须在调用记录里存在）
bool appears_before(const pbzpm::test::FakeEngine& fe, const char* a, const char* b) {
    const int ia = fe.index_of(a);
    const int ib = fe.index_of(b);
    return ia >= 0 && ib >= 0 && ia < ib;
}

void test_photo_mode_happy_path() {
    section("PhotoMode 进入/退出");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    fe.pov.loc = Vec3{100.0, 200.0, 300.0};
    fe.pov.rot = Rot{10.0, 45.0, 0.0};
    fe.pov.fov = 88.0;
    fe.wwise_playing_ids = 3;  // 默认游戏路径：钩子已截获逐语音 PlayingID
    fe.flow_dialog_widgets = 1;  // 默认路径：场上有 1 个对话 Flow 控件

    Config cfg;
    PhotoMode mode(fe, cfg);
    // M-6：构造时把 log_level 同步给引擎侧；进入时清除函数查找缓存（前批收尾）
    CHECK(fe.configured_log_level == LogLevel::Info);
    CHECK(!mode.active());
    CHECK(mode.enter());
    CHECK(fe.refresh_cache_calls >= 1);

    expect_sequence(fe,
                    {"world_valid", "read_pov", "spawn_camera", "attach_view", "ignore_move_on", "block_input_on",
                     "hud_off", "prompt_off", "bars_off", "lockon_off", "subs_off", "aspect_unlock",
                     "get_time_dilation", "apply_fov", "apply_pose", "pause_on", "shakes_freeze",
                     "wind_freeze", "mpc_snapshot", "cloth_freeze", "anim_freeze", "motion_blur_freeze",
                     "sequences_pause", "flow_freeze_on", "wwise_pause"},
                    "首次进入");
    // 有逐语音 PlayingID：只做逐语音真暂停，不得再调用整引擎 Suspend
    CHECK_INT(fe.count("wwise_engine_suspend"), 0);

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
                    {"motion_blur_restore", "anim_restore", "cloth_restore", "wind_restore", "shakes_restore",
                     "sequences_resume", "pause_off",
                     "wwise_resume", "flow_freeze_off", "restore_aspect",
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

// [look] 五项画面效果：每次进入摄影模式都从默认参数开始（数值归中性、五个开关归默认），
// 且首次 tick 下发的 LookOptions 必须已经是默认值，不能把上一轮拖出来的效果带进新会话。
void test_photo_mode_look_defaults_on_enter() {
    section("画面效果五项进入时恢复默认");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    Config cfg;
    cfg.vignette = 3.2;
    cfg.saturation = 0.35;
    cfg.contrast = 1.7;
    cfg.bloom = 8.5;
    cfg.temperature = -0.75;
    cfg.vignette_enabled = false;
    cfg.saturation_enabled = false;
    cfg.contrast_enabled = false;
    cfg.bloom_enabled = false;
    cfg.temperature_enabled = false;
    // 调色·进阶（2026-11-08）：同样随进入恢复中性（无开关，中性=不覆盖）
    cfg.color_gain = 2.0;
    cfg.color_offset = -0.1;

    PhotoMode mode(fe, cfg);
    CHECK(mode.enter());
    const Config& active_cfg = mode.config();
    CHECK_NEAR(active_cfg.vignette, 0.0, 1e-9);
    CHECK_NEAR(active_cfg.saturation, 1.0, 1e-9);
    CHECK_NEAR(active_cfg.contrast, 1.0, 1e-9);
    CHECK_NEAR(active_cfg.bloom, 0.0, 1e-9);
    CHECK_NEAR(active_cfg.temperature, 0.0, 1e-9);
    CHECK(active_cfg.vignette_enabled);
    CHECK(active_cfg.saturation_enabled);
    CHECK(active_cfg.contrast_enabled);
    CHECK(active_cfg.bloom_enabled);
    CHECK(active_cfg.temperature_enabled);
    CHECK_NEAR(active_cfg.color_gain, tuning::kColorGainDefault, 1e-9);
    CHECK_NEAR(active_cfg.color_offset, tuning::kColorOffsetDefault, 1e-9);

    Intent none;
    mode.tick(none, 0.016);
    CHECK_INT(fe.look_calls, 1);
    CHECK(!fe.last_look.any_active());
    CHECK(!fe.last_look.color_pro_active());
    CHECK_NEAR(fe.last_look.color_gain, tuning::kColorGainDefault, 1e-9);
    CHECK(fe.last_look.vignette_enabled);
    CHECK(fe.last_look.saturation_enabled);
    CHECK(fe.last_look.contrast_enabled);
    CHECK(fe.last_look.bloom_enabled);
    CHECK(fe.last_look.temperature_enabled);

    // 独立开关：关掉只停用，滑块值仍原样保存在 LookOptions 里；重新打开立即恢复效果。
    Config live = mode.config();
    live.vignette = 2.5;
    live.vignette_enabled = false;
    mode.set_config(live);
    mode.tick(none, 0.016);
    CHECK_INT(fe.look_calls, 2);
    CHECK_NEAR(fe.last_look.vignette, 2.5, 1e-9);
    CHECK(!fe.last_look.vignette_active());
    live.vignette_enabled = true;
    mode.set_config(live);
    mode.tick(none, 0.016);
    CHECK_INT(fe.look_calls, 3);
    CHECK(fe.last_look.vignette_active());

    // 调色·进阶（2026-11-08）：无开关——值离中性即活跃，回中性即停用（同一 set_look 通道）。
    Config pro = mode.config();
    pro.color_offset = -0.1;
    mode.set_config(pro);
    mode.tick(none, 0.016);
    CHECK_INT(fe.look_calls, 4);
    CHECK(fe.last_look.color_offset_active());
    CHECK(fe.last_look.color_pro_active());
    CHECK_NEAR(fe.last_look.color_offset, -0.1, 1e-9);
    pro.color_offset = tuning::kColorOffsetDefault;
    mode.set_config(pro);
    mode.tick(none, 0.016);
    CHECK_INT(fe.look_calls, 5);
    CHECK(!fe.last_look.color_offset_active());
    CHECK(!fe.last_look.color_pro_active());
    mode.leave("test");

    // 重置辅助函数：有变化返回 true，全默认时返回 false（避免无谓写盘）
    Config reset_cfg;
    reset_cfg.vignette = 1.5;
    reset_cfg.saturation_enabled = false;
    CHECK(reset_look_defaults(reset_cfg));
    CHECK_NEAR(reset_cfg.vignette, 0.0, 1e-9);
    CHECK(reset_cfg.saturation_enabled);
    CHECK(!reset_look_defaults(reset_cfg));
}

void test_photo_mode_failures_rollback() {
    section("PhotoMode 失败回滚");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 1) 关闭接管时 spawn 失败：不留下任何状态
    {
        FakeEngine fe;
        fe.fail_spawn = true;
        Config cfg;
        cfg.takeover_current_camera = false;
        PhotoMode mode(fe, cfg);
        CHECK(!mode.enter());
        CHECK(!mode.active());
        CHECK_STR(mode.last_error(), "创建 CameraActor 失败");
        CHECK(!fe.camera_exists);
        CHECK(!fe.view_attached);
        CHECK(!fe.ignore_move);
        CHECK(!fe.player_hidden);
    }

    // 1b) 接管失败 + 独立相机也失败：错误文案必须区分两条路径
    {
        FakeEngine fe;
        fe.takeover_supported = false;
        fe.fail_spawn = true;
        PhotoMode mode(fe, Config{});
        CHECK(!mode.enter());
        CHECK_INT(fe.takeover_attempts, 1);
        CHECK_STR(mode.last_error(), "接管当前相机失败，且独立 CameraActor 创建也失败");
        CHECK(!mode.takeover_active());
        CHECK(!fe.takeover_active);
    }

    // 1c) 接管成功：不 spawn、不换 ViewTarget，退出只退出接管
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.record_camera_extras = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.takeover_active());
        CHECK(fe.takeover_active);
        CHECK(!fe.camera_exists);
        CHECK(!fe.view_attached);
        CHECK_INT(fe.count("spawn_camera"), 0);
        CHECK_INT(fe.count("attach_view"), 0);
        fe.reset_calls();
        mode.leave("takeover");
        CHECK(!mode.takeover_active());
        CHECK(!fe.takeover_active);
        CHECK_INT(fe.count("spawn_camera"), 0);
        CHECK_INT(fe.count("attach_view"), 0);
        CHECK_INT(fe.count("leave_camera_takeover"), 1);
        CHECK_INT(fe.count("destroy_camera"), 0);
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

void test_photo_mode_takeover_input() {
    section("PhotoMode 接管模式：输入必须写进 POV / 失效必须自动回退");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 1.0.75 回归：接管模式下输入→相机位姿正常，但写点在渲染之后，镜头完全不动。
    // 这里锁死两件事：① 每帧输入位移都必须反映到写出的 POV；② 引擎判定失效时必须回退独立相机。
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.pov.loc = Vec3{0.0, 0.0, 0.0};
        fe.pov.rot = Rot{0.0, 0.0, 0.0};
        fe.pov.fov = 90.0;
        Config cfg;
        cfg.auto_pause_time = false;  // 只测输入→位姿，不掺冻结路径
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.takeover_active());

        const size_t before = fe.pose_history.size();
        Intent forward;
        forward.move_forward = 1.0;
        for (int i = 0; i < 6; ++i) mode.tick(forward, 0.016);
        CHECK(fe.pose_history.size() >= before + 6);  // 6 帧全都真的写了位姿
        CHECK(mode.pose().loc.x > 0.0);
        CHECK_NEAR(fe.last_pose.loc.x, mode.pose().loc.x, 1e-9);  // 写出的 POV == 内部状态
        CHECK(fe.takeover_active);
        CHECK_INT(fe.count("spawn_camera"), 0);  // 正常接管不能被误判成失效

        // 视角输入同样必须写进 POV（先按住 Alt 的鼠标 look 路径）
        Intent look;
        look.look_dx = 10.0;
        look.look_dy = 5.0;
        mode.tick(look, 0.016);
        CHECK_NEAR(fe.last_pose.rot.yaw, mode.pose().rot.yaw, 1e-9);
        CHECK(fe.pose_history.back().rot.yaw > 0.0);

        // 接管正常时连续多帧移动仍不回退
        for (int i = 0; i < 20; ++i) mode.tick(forward, 0.016);
        CHECK(mode.takeover_active());
        CHECK_INT(fe.count("spawn_camera"), 0);
        mode.leave();
    }

    // 引擎在渲染后回读确认接管失效：必须自动回退独立相机，不能停在「能进模式但动不了」
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.takeover_view_lost_value = true;
        fe.record_camera_extras = true;
        Config cfg;
        cfg.auto_pause_time = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.takeover_active());

        Intent forward;
        forward.move_forward = 1.0;
        fe.reset_calls();
        mode.tick(forward, 0.016);

        CHECK(!mode.takeover_active());
        CHECK(!fe.takeover_active);
        CHECK(mode.active());  // 摄影模式本身保留，只是换成独立相机
        CHECK_INT(fe.count("leave_camera_takeover"), 1);
        CHECK_INT(fe.count("spawn_camera"), 1);
        CHECK_INT(fe.count("attach_view"), 1);
        CHECK(fe.camera_exists);
        CHECK(fe.view_attached);
        CHECK(fe.has("apply_pose"));  // 位姿已写到新相机
        CHECK(fe.logs_contain("已自动回退到独立相机"));

        // 回退后继续移动：位姿仍要写出去
        fe.reset_calls();
        mode.tick(forward, 0.016);
        CHECK(fe.has("apply_pose"));
        mode.leave();
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

    // 滚轮可关 / 可反向（2.0.0-cpp-hotfix9）
    {
        Config no_wheel = cfg;
        no_wheel.wheel_fov = false;
        PhotoMode wheel_off(fe, no_wheel);
        CHECK(wheel_off.enter());
        const double f0 = wheel_off.fov();
        Intent w;
        w.wheel = 2.0;
        wheel_off.tick(w, 0.016);
        CHECK_NEAR(wheel_off.fov(), f0, 1e-9);
        wheel_off.leave();

        Config inv_wheel = cfg;
        inv_wheel.wheel_fov_invert = true;
        PhotoMode wheel_inv(fe, inv_wheel);
        CHECK(wheel_inv.enter());
        const double f1 = wheel_inv.fov();
        wheel_inv.tick(w, 0.016);
        CHECK_NEAR(wheel_inv.fov(), f1 + 4.0, 1e-9);  // 反转：向上 = 放大 FOV
        wheel_inv.leave();
    }

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

    // 光照 A/B：单项开关冻结时应用、解冻时还原
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        cfg.freeze_lumen_off = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK_INT(fe.count("lighting_freeze"), 1);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("lighting_restore"), 1);
    }

    // 光照 A/B 自动轮换：冻结期每帧 tick 驱动阶段切换
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        cfg.freeze_lighting_ab_cycle = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.toggle_freeze();
        CHECK(mode.frozen());
        Intent none;
        mode.tick(none, 0.5);
        CHECK(fe.lighting_ab_ticks > 0);
        mode.leave("test");
        CHECK_INT(fe.count("lighting_restore"), 1);
    }

    // 自定义布料/摆动：冻结时挂起、解冻时恢复
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        cfg.freeze_custom_cloth = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK_INT(fe.count("custom_cloth_freeze"), 1);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("custom_cloth_restore"), 1);
    }

    // WPO 评估冻结：冻结时关闭、解冻时恢复
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        cfg.freeze_wpo = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK_INT(fe.count("wpo_freeze"), 1);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("wpo_restore"), 1);
    }

    // 阴影/后处理 A/B：冻结时进入、每帧 tick、解冻还原
    {
        FakeEngine fe;
        Config cfg;
        cfg.auto_pause_time = false;
        cfg.freeze_shadow_ab_cycle = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.toggle_freeze();
        CHECK(mode.frozen());
        CHECK_INT(fe.count("shadow_freeze"), 1);
        Intent none;
        mode.tick(none, 0.5);
        CHECK(fe.shadow_ab_ticks > 0);
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("shadow_restore"), 1);
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

    // stop_wwise_on_freeze：冻结时逐语音真暂停（组件音量兜底，可恢复）；解冻/退出恢复
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 3;
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
        // 放弃后如实标记冻结失效：对外 frozen() 变 false，freeze_lost() 供 UI/日志区分
        CHECK(!mode.frozen());
        CHECK(mode.freeze_lost());
        CHECK_INT(fe.count_logs_contain("冻结已失效"), 1);  // 一次性 Error，不刷屏

        fe.reset_calls();
        fe.paused = false;
        advance(mode, 8);
        CHECK_INT(fe.count("pause_on"), 0);  // 放弃后不再每秒硬写
        CHECK_INT(fe.count_logs_contain("冻结已失效"), 1);  // 持续失效期不重复告警

        fe.paused = true;  // 过场结束、状态恢复正常：重新武装
        advance(mode, 4);
        CHECK(mode.frozen());
        CHECK(!mode.freeze_lost());
        CHECK_INT(fe.count_logs_contain("冻结状态已恢复"), 1);  // 恢复也只打一次
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


void test_photo_mode_wwise_engine() {
    section("PhotoMode Wwise：逐语音优先，整引擎仅无 ID 兜底");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // A) 有逐语音 PlayingID：只做逐语音真暂停，绝不调用整引擎 Suspend
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 3;
        fe.wwise_engine_suspend_ok = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.count("wwise_pause"), 1);
        CHECK_INT(fe.count("wwise_engine_suspend"), 0);
        CHECK_INT(fe.count("wwise_engine_wake"), 0);
        CHECK(!fe.wwise_engine_suspended);

        fe.reset_calls();
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("wwise_resume"), 1);
        CHECK_INT(fe.count("wwise_engine_wake"), 0);  // 从未 Suspend，解冻无需 Wake
        mode.leave("test");
    }

    // B) 无逐语音 PlayingID：整引擎 Suspend 作为静音兜底；解冻先 wake 再 resume
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 0;
        fe.wwise_engine_suspend_ok = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.count("wwise_pause"), 1);
        CHECK_INT(fe.count("wwise_engine_suspend"), 1);
        CHECK(fe.wwise_engine_suspended);
        CHECK(fe.logs_contain("无逐语音 PlayingID，整引擎 Suspend 作为静音兜底"));

        fe.reset_calls();
        mode.toggle_freeze();
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("wwise_engine_wake"), 1);
        CHECK_INT(fe.count("wwise_resume"), 1);
        CHECK(appears_before(fe, "wwise_engine_wake", "wwise_resume"));
        CHECK(!fe.wwise_engine_suspended);
        mode.leave("test");
    }

    // C) 无逐语音 ID 且配置关闭整引擎：不尝试整引擎，只走逐语音/组件兜底
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 0;
        fe.wwise_engine_suspend_ok = true;
        Config cfg;
        cfg.wwise_engine_suspend = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK_INT(fe.count("wwise_engine_suspend"), 0);
        CHECK_INT(fe.count("wwise_pause"), 1);
        mode.leave("test");
    }

    // D) 无逐语音 ID + 整引擎成功 + 冻结中直接退出：wake 必须先于 resume
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 0;
        fe.wwise_engine_suspend_ok = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.wwise_engine_suspended);
        fe.reset_calls();
        mode.leave("exit while frozen");
        CHECK_INT(fe.count("wwise_engine_wake"), 1);
        CHECK_INT(fe.count("wwise_resume"), 1);
        CHECK(appears_before(fe, "wwise_engine_wake", "wwise_resume"));
        CHECK(!fe.wwise_engine_suspended);
    }

    // E) 有逐语音 ID 时日志必须记录「跳过整引擎 Suspend」（证明没走 render-anyway 路径）
    {
        FakeEngine fe;
        fe.wwise_playing_ids = 5;
        fe.wwise_engine_suspend_ok = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.logs_contain("跳过整引擎 Suspend"));
        CHECK(fe.logs_contain("逐语音真暂停已执行 5 个 PlayingID"));
        CHECK_INT(fe.count("wwise_engine_suspend"), 0);
        mode.leave("test");
    }
}

void test_photo_mode_dialogue_flow() {
    section("PhotoMode 对话 Flow 冻结（Collapsed 停 Tick）");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // A) 默认 hide_subtitles=true、freeze_dialogue_flow=true：冻结改 Collapsed，解冻/退出恢复 Hidden
    {
        FakeEngine fe;
        fe.flow_dialog_widgets = 2;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK(fe.flow_freeze_calls >= 1);
        CHECK(fe.flow_dialog_paused);
        CHECK_INT(fe.count("flow_freeze_on"), 1);
        CHECK(fe.logs_contain("对话 Flow 冻结"));

        mode.toggle_freeze();  // 解冻
        CHECK(!mode.frozen());
        CHECK_INT(fe.count("flow_freeze_off"), 1);
        CHECK(!fe.flow_dialog_paused);

        mode.toggle_freeze();  // 再冻结
        CHECK(mode.frozen());
        CHECK_INT(fe.count("flow_freeze_on"), 2);
        CHECK(fe.flow_dialog_paused);

        mode.leave("test");  // 冻结中退出
        CHECK_INT(fe.count("flow_freeze_off"), 2);
        CHECK(!fe.flow_dialog_paused);
    }

    // B) freeze_dialogue_flow=false：完全不调用
    {
        FakeEngine fe;
        fe.flow_dialog_widgets = 2;
        Config cfg;
        cfg.freeze_dialogue_flow = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.flow_freeze_calls, 0);
        CHECK_INT(fe.count("flow_freeze_on"), 0);
        mode.leave("test");
        CHECK_INT(fe.count("flow_freeze_off"), 0);
        CHECK_INT(fe.flow_freeze_calls, 0);
    }

    // C) hide_subtitles=false：即使 freeze_dialogue_flow=true 也不调用
    {
        FakeEngine fe;
        fe.flow_dialog_widgets = 2;
        Config cfg;
        cfg.hide_subtitles = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_INT(fe.flow_freeze_calls, 0);
        mode.leave("test");
        CHECK_INT(fe.flow_freeze_calls, 0);
    }

    // D) 场上没有 FlowDialog（返回 0）：不算冻结成功、无调用序列、无冻结日志，退出不重复调用
    {
        FakeEngine fe;
        fe.flow_dialog_widgets = 0;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.flow_freeze_calls, 1);
        CHECK(!fe.flow_dialog_paused);
        CHECK_INT(fe.count("flow_freeze_on"), 0);
        CHECK(!fe.logs_contain("对话 Flow 冻结"));
        mode.leave("test");
        CHECK_INT(fe.count("flow_freeze_off"), 0);
        CHECK_INT(fe.flow_freeze_calls, 1);
    }

    // E) 冻结中直接 leave：Flow 恢复必须先于字幕恢复（subs_on）
    {
        FakeEngine fe;
        fe.flow_dialog_widgets = 1;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        fe.reset_calls();
        mode.leave("exit while frozen");
        CHECK_INT(fe.count("flow_freeze_off"), 1);
        CHECK(appears_before(fe, "flow_freeze_off", "subs_on"));
        CHECK(!fe.flow_dialog_paused);
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
        Config cfg;
        cfg.freeze_diag = true;  // POV 微动监测随诊断开关
        PhotoMode mode(fe, cfg);
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

    // 布料模拟：默认冻结/恢复；关闭开关不碰；失败不记账
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.count("cloth_freeze"), 1);
        mode.toggle_freeze();
        CHECK_INT(fe.count("cloth_restore"), 1);
        mode.toggle_freeze();
        CHECK_INT(fe.count("cloth_freeze"), 2);
        mode.leave("test");
        CHECK_INT(fe.count("cloth_restore"), 2);
    }
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        Config cfg;
        cfg.freeze_cloth = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.leave("test");
        CHECK_INT(fe.count("cloth_freeze"), 0);
        CHECK_INT(fe.count("cloth_restore"), 0);
    }
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        fe.fail_cloth = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.count("cloth_freeze"), 1);
        mode.toggle_freeze();
        CHECK_INT(fe.count("cloth_restore"), 0);  // 冻结未成功，不假装还原
        mode.leave("test");
    }
    {
        FakeEngine fe;
        fe.cloth_components = 1;
        fe.fail_cloth_restore = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        mode.leave("test");
        CHECK_INT(fe.count("cloth_restore"), 1);  // 失败保留标志，退出重试
        mode.leave("test");
        CHECK_INT(fe.count("cloth_restore"), 2);
    }

    // 布料闸门：冻结期每秒复核一次；解冻/关闭布料冻结后不跑；闸门节流按 dt 累计
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        fe.cloth_gate_caught = 1;  // 注入：本次闸门发现 1 个组件仍处于模拟态
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());       // 进入即冻结（auto_pause_time 默认 true）
        CHECK(mode.frozen());
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 0);  // 累计 0.75s 不足 1s
        mode.tick(Intent{}, 0.25);                  // 累计 1.0s：闸门跑第一次
        CHECK_INT(fe.count("cloth_gate_tick"), 1);
        mode.tick(Intent{}, 0.25);                  // dt 钳制在 kMaxFrameDt=0.25，用 0.25 步进累计
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 2);
        CHECK_INT(fe.cloth_gate_calls, 2);
        mode.toggle_freeze();                        // 解冻：闸门停
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 2);
        mode.toggle_freeze();                        // 再冻结：闸门恢复
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 3);
        mode.leave("test");                          // 退出：闸门停
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 3);
    }
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        Config cfg;
        cfg.freeze_cloth = false;  // 关闭布料冻结：闸门同样不跑（没有可复核的对象）
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 0);
        CHECK_INT(fe.count("cloth_freeze"), 0);
        mode.leave("test");
    }
    {
        FakeEngine fe;
        fe.cloth_components = 2;
        Config cfg;
        cfg.auto_pause_time = false;  // 进入不自动冻结：解冻状态下闸门不跑
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!mode.frozen());
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("cloth_gate_tick"), 0);
        CHECK_INT(fe.count("cloth_freeze"), 0);
        mode.leave("test");
    }

    // 骨骼动画 + 动态模糊：默认冻结/恢复；关闭开关不碰
    {
        FakeEngine fe;
        fe.anim_components = 3;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.count("anim_freeze"), 1);
        CHECK_INT(fe.count("motion_blur_freeze"), 1);
        mode.toggle_freeze();
        CHECK_INT(fe.count("anim_restore"), 1);
        CHECK_INT(fe.count("motion_blur_restore"), 1);
        mode.toggle_freeze();
        CHECK_INT(fe.count("anim_freeze"), 2);
        mode.leave("test");
        CHECK_INT(fe.count("anim_restore"), 2);
    }
    {
        FakeEngine fe;
        fe.anim_components = 3;
        Config cfg;
        cfg.freeze_skeletal_animation = false;
        cfg.freeze_motion_blur_off = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        mode.leave("test");
        CHECK_INT(fe.count("anim_freeze"), 0);
        CHECK_INT(fe.count("motion_blur_freeze"), 0);
        CHECK_INT(fe.count("anim_restore"), 0);
        CHECK_INT(fe.count("motion_blur_restore"), 0);
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

    // 冻结诊断：默认关闭；显式开启后状态翻转立即输出，持续期每 10 秒最多一条
    {
        auto advance = [](PhotoMode& mode, int ticks) {
            Intent none;
            for (int i = 0; i < ticks; ++i) mode.tick(none, 0.25);
        };
        FakeEngine fe;
        Config cfg;
        cfg.freeze_diag = true;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());  // 冻结中
        advance(mode, 4);     // 累计 1 秒：首次采样（状态翻转）立即输出
        CHECK_INT(fe.count("freeze_diag"), 1);
        CHECK(fe.logs_contain("冻结诊断"));
        advance(mode, 4);     // 第 2 秒状态未变：限流，不再输出
        CHECK_INT(fe.count("freeze_diag"), 1);
        advance(mode, 36);  // 累计到第 11 秒：持续期每 10 秒最多一条
        CHECK_INT(fe.count("freeze_diag"), 2);

        FakeEngine fe2;
        Config cfg_off;
        cfg_off.freeze_diag = false;
        PhotoMode mode2(fe2, cfg_off);
        CHECK(mode2.enter());
        advance(mode2, 5);
        CHECK_INT(fe2.count("freeze_diag"), 0);
    }
}

void test_photo_mode_reassert_and_partial() {
    section("PhotoMode 守护与部分失败");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 每 1 秒重设 ViewTarget；M-1：连续 3 次回读确认失败才退出，单次失败只告警
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

        // 部分成功：请求已发出，但回读确凿显示视角被抢。第一次不能退出
        fe.attach_reports_mismatch = true;
        for (int i = 0; i < 61; ++i) mode.tick(Intent{}, 0.016);
        mode.tick(Intent{}, 0.05);
        CHECK(mode.active());
        CHECK_INT(fe.attach_verify_fail_streak, 1);
        CHECK(!fe.logs_contain("相机接管已失效"));

        // 第二、三次确认失败 → 达到阈值，明确错误并 leave
        for (int round = 0; round < 2; ++round) {
            for (int i = 0; i < 61; ++i) mode.tick(Intent{}, 0.016);
            mode.tick(Intent{}, 0.05);
        }
        CHECK(!mode.active());
        CHECK(fe.logs_contain("相机接管已失效"));
        CHECK_INT(fe.attach_verify_fail_streak, 3);
    }

    // 1.0.75 hotfix1 回归点：同帧回读 Target=null / 属性不可读（真机实测）只降级告警，
    // 既不阻断进入、也不累计失效、不触发 leave
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());  // attach_view 返回 true，enter 不能因“回读暂未确认”失败
        fe.reset_calls();
        fe.attach_reports_unconfirmed = true;
        for (int round = 0; round < 5; ++round) {
            for (int i = 0; i < 61; ++i) mode.tick(Intent{}, 0.016);
            mode.tick(Intent{}, 0.05);
        }
        CHECK(mode.active());
        CHECK_INT(fe.attach_verify_fail_streak, 0);
        CHECK_INT(fe.count("attach_view"), 5);
        CHECK(!fe.logs_contain("相机接管已失效"));
    }

    // fail_attach（函数调用本身失败）同样累计，连续 3 次后退出
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        fe.fail_attach = true;
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 61; ++i) mode.tick(Intent{}, 0.016);
            mode.tick(Intent{}, 0.05);
        }
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

// ---------------------------------------------------------------- 相机后端热切换

// PBZFM 菜单里改「接管当前相机」必须当场生效：拆旧后端 → 建新后端 → 重新下发 FOV/景深；
// 任何一步失败都要回滚到原后端，绝不能让用户点了开关之后没有相机可用。
void test_camera_backend_switch() {
    section("相机后端热切换（接管 ↔ 独立相机）");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 1) 接管中 → 切独立相机：退出接管、spawn、attach
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.record_camera_extras = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.takeover_active());
        CHECK(!fe.camera_exists);
        fe.reset_calls();

        CHECK(mode.set_camera_backend_live(false));
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);
        CHECK(fe.view_attached);
        CHECK_INT(fe.count("leave_camera_takeover"), 1);
        CHECK_INT(fe.count("spawn_camera"), 1);
        CHECK_INT(fe.count("attach_view"), 1);
        CHECK(!mode.config().takeover_current_camera);

        mode.leave("switch");
        CHECK(!fe.camera_exists);
    }

    // 2) 独立相机中 → 切接管：还视角、销毁相机、接管 POV
    {
        FakeEngine fe;
        fe.takeover_supported = true;   // 假件默认不支持接管，这里必须显式打开
        fe.record_camera_extras = true;
        Config cfg;
        cfg.takeover_current_camera = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);
        fe.reset_calls();

        CHECK(mode.set_camera_backend_live(true));
        CHECK(mode.takeover_active());
        CHECK(fe.takeover_active);
        CHECK(!fe.camera_exists);   // 独立相机已销毁
        CHECK(!fe.view_attached);   // 视角已交还游戏
        CHECK_INT(fe.count("takeover_camera"), 1);
        CHECK_INT(fe.count("destroy_camera"), 1);
        CHECK(mode.config().takeover_current_camera);

        mode.leave("switch");
        CHECK(!fe.takeover_active);
    }

    // 3) 切独立相机但 spawn 失败 → 回滚到接管，用户仍有相机
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.fail_spawn = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.takeover_active());

        CHECK(!mode.set_camera_backend_live(false));
        CHECK(mode.takeover_active());
        CHECK(fe.takeover_active);
        CHECK(!fe.camera_exists);
        CHECK(mode.config().takeover_current_camera);  // 失败回滚：配置要跟实际后端一致
    }

    // 4) 切接管但接管不可用 → 回滚到独立相机，用户仍有相机
    {
        FakeEngine fe;
        fe.takeover_supported = false;
        Config cfg;
        cfg.takeover_current_camera = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!mode.takeover_active());

        CHECK(!mode.set_camera_backend_live(true));
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);
        CHECK(fe.view_attached);
        CHECK(!mode.config().takeover_current_camera);  // 失败回滚：配置要跟实际后端一致
    }

    // 5) 未激活时只记配置，绝不碰引擎（下次 enter() 按新后端建立）
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        fe.reset_calls();
        CHECK(mode.set_camera_backend_live(false));
        CHECK_INT(fe.count("spawn_camera"), 0);
        CHECK_INT(fe.count("takeover_camera"), 0);
        CHECK(!mode.config().takeover_current_camera);
        CHECK(mode.enter());
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);
    }

    // 8) 切接管但「先还视角」失败：保持独立相机，配置随实际后端回滚
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.fail_view_restore = true;
        Config cfg;
        cfg.takeover_current_camera = false;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);

        CHECK(!mode.set_camera_backend_live(true));
        CHECK(!mode.takeover_active());
        CHECK(fe.camera_exists);
        CHECK(!mode.config().takeover_current_camera);  // 失败回滚：配置要跟实际后端一致
        fe.fail_view_restore = false;                    // 别让退出还原也跟着失败
        mode.leave("switch-restore-view-fail");
    }

    // 9) 切独立相机但设置 ViewTarget 失败：回滚到接管，配置随实际后端回滚
    {
        FakeEngine fe;
        fe.takeover_supported = true;
        fe.fail_attach = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.takeover_active());

        CHECK(!mode.set_camera_backend_live(false));
        CHECK(mode.takeover_active());
        CHECK(mode.config().takeover_current_camera);  // 失败回滚：配置要跟实际后端一致
        fe.fail_attach = false;
        mode.leave("switch-attach-fail");
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


void test_photo_mode_aspect_reassert() {
    section("PhotoMode 超宽屏画幅每帧重申");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    FakeEngine fe;
    PhotoMode mode(fe, Config{});
    CHECK(mode.enter());
    CHECK_INT(fe.aspect_reassert_count, 0);
    mode.reassert_aspect_ratio_if_needed();
    CHECK_INT(fe.aspect_reassert_count, 1);
    // 未进入 / 退出后不再重申
    mode.leave("test");
    mode.reassert_aspect_ratio_if_needed();
    CHECK_INT(fe.aspect_reassert_count, 1);

    // 配置关闭画幅解锁：从不重申
    FakeEngine fe_off;
    Config cfg;
    cfg.ultrawide_unlock = false;
    PhotoMode off(fe_off, cfg);
    CHECK(off.enter());
    off.reassert_aspect_ratio_if_needed();
    CHECK_INT(fe_off.aspect_reassert_count, 0);
    off.leave("test");
}

void test_photo_mode_cinematic_cleanup() {
    section("PhotoMode 处决/过场演出残留恢复");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 暂停模式：退出时必须调用一次玩家角色清理（引擎侧负责清 CustomTimeDilation /
    // SkeletalMesh bPauseAnims / CinematicTimeDilation，core 只负责在正确时机触发）
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK_INT(fe.recover_character_calls, 0);
        mode.leave("test");
        CHECK_INT(fe.recover_character_calls, 1);
    }

    // 进入时读到接近 0 的全局倍率（处决/命中停顿的瞬时值）：不能把它当原值写回，
    // 否则退出后角色会永久停在慢动作/定格。
    {
        FakeEngine fe;
        fe.dilation = 0.0005;
        Config cfg;
        cfg.freeze_mode = FreezeMode::Dilation;
        PhotoMode mode(fe, cfg);
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, cfg.freeze_dilation, 1e-12);
        mode.leave("test");
        CHECK_NEAR(fe.dilation, 1.0, 1e-12);  // 不是 0.0005
    }

    // 暂停被演出拒绝、回退时间缩放时同样不能把 0 写回
    {
        FakeEngine fe;
        fe.fail_pause = true;
        fe.dilation = 0.0;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(mode.frozen());
        CHECK_NEAR(fe.dilation, 0.0001, 1e-12);
        mode.leave("test");
        CHECK_NEAR(fe.dilation, 1.0, 1e-12);
    }

    // 恢复失败后重试也会重复触发清理（幂等），不会因为一次失败就丢失这条兜底
    {
        FakeEngine fe;
        fe.fail_pause_restore = true;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        mode.leave("test");
        CHECK(mode.recovery_pending());
        CHECK_INT(fe.recover_character_calls, 1);
        Intent none;
        // tick_recovery 有单帧 0.25s 上限：四拍才到 1s 的首次重试
        for (int i = 0; i < 4; ++i) mode.tick(none, 0.25);
        CHECK_INT(fe.recover_character_calls, 2);
        fe.fail_pause_restore = false;
        // 第二次重试要再等 2s（再八拍）
        for (int i = 0; i < 8 && mode.recovery_pending(); ++i) mode.tick(none, 0.25);
        CHECK(!mode.recovery_pending());
    }
}

void test_photo_mode_input_blocking() {
    section("PhotoMode 输入屏蔽与角色可见性");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

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
    section("PhotoMode 实时开关（输入屏蔽/角色/HUD）");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 屏蔽输入：关→开 循环；退出只按内部标记还原，不多调
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.input_blocked);
        // 默认配置下进入模式不得隐藏角色（hide_player 默认 false）。
        // 这条断言原先只在 test_photo_mode_input_blocking 里出现，去重时把它挪到这里保住覆盖。
        CHECK(!fe.player_hidden);
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

    // 键捕获给出键名表表达不了的键（VK_CANCEL 0x03）：拒绝应用、配置不动
    {
        Config cfg{};
        cfg.key_toggle = "F2";
        for (const PbzfmField& f : fields) {
            if (f.kind == PbzfmFieldKind::Key && std::string(f.key) == "key_toggle") {
                CHECK(!pbzfm_apply_field(f, 0x03, cfg));
                CHECK_STR(cfg.key_toggle, "F2");
            }
        }
    }

    // 鼠标键现在能通过菜单应用（key_vertical_up 默认值就是 MOUSE_LEFT）
    {
        Config cfg{};
        for (const PbzfmField& f : fields) {
            if (f.kind == PbzfmFieldKind::Key && std::string(f.key) == "key_vertical_up") {
                CHECK(pbzfm_apply_field(f, 0x05, cfg));  // MOUSE_X1
                CHECK_STR(cfg.key_vertical_up, "MOUSE_X1");
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
    custom.freeze_dialogue_flow = false;
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
    CHECK(!parsed.cfg.freeze_dialogue_flow);
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

// 配置原子写：写 <path>.tmp 后 rename 覆盖目标，任何时刻读到的都是完整文件
void test_config_atomic_write() {
    section("save_config_file 原子写（临时文件 + rename）");
    using namespace pbzpm;
    namespace fs = std::filesystem;

    const fs::path path = "pbzpm_atomic_write_test.ini";
    const fs::path tmp = path.string() + ".tmp";
    std::error_code ec;
    fs::remove(path, ec);
    fs::remove(tmp, ec);

    Config first{};
    first.key_toggle = "F6";
    first.move_speed = 512.0;
    std::string err;
    CHECK(save_config_file(path.string(), first, &err));
    CHECK(!fs::exists(tmp));  // 临时文件已 rename，无残留
    const ConfigLoadResult read1 = load_config_file(path.string());
    CHECK(read1.ok);
    CHECK(!read1.file_missing);
    CHECK(!read1.read_error);
    CHECK_INT((int)read1.warnings.size(), 0);
    CHECK_STR(read1.cfg.key_toggle, "F6");
    CHECK_NEAR(read1.cfg.move_speed, 512.0, 1e-9);

    // 覆盖已存在目标：rename 原子替换，整体内容更新，不残留半写
    Config second = first;
    second.key_toggle = "F7";
    second.move_speed = 640.0;
    CHECK(save_config_file(path.string(), second, &err));
    CHECK(!fs::exists(tmp));
    const ConfigLoadResult read2 = load_config_file(path.string());
    CHECK_INT((int)read2.warnings.size(), 0);
    CHECK_STR(read2.cfg.key_toggle, "F7");
    CHECK_NEAR(read2.cfg.move_speed, 640.0, 1e-9);

    // 空路径：明确失败并给出原因
    CHECK(!save_config_file("", second, &err));
    CHECK(!err.empty());

    // 父目录不存在：失败且不残留 .tmp / 目标文件
    const fs::path missing_dir = "pbzpm_no_such_dir_for_save";
    fs::remove_all(missing_dir, ec);
    const fs::path missing_path = missing_dir / "config.ini";
    CHECK(!save_config_file(missing_path.string(), second, &err));
    CHECK(!fs::exists(missing_path));
    CHECK(!fs::exists(missing_path.string() + ".tmp"));

    fs::remove(path, ec);
    fs::remove(tmp, ec);
}


// ---------------------------------------------------------------- PBZFM 词典快照（H-4）

// 不依赖 UE4SS/ImGui 的纯判定：展示顺序与「当前语言 → en-US → 键名」查译链
void test_i18n_pure_logic() {
    section("PBZFM 词典纯逻辑（展示顺序 / 查译链）");
    using namespace pbzpm;
    CHECK(language_display_less("en-US", "zh-CN"));
    CHECK(language_display_less("zh-CN", "ja-JP"));
    CHECK(language_display_less("ja-JP", "ko-KR"));
    CHECK(language_display_less("ko-KR", "aa-AA"));  // 已知语言排在未知语言之前
    CHECK(language_display_less("aa-AA", "ab-AA"));  // 未知语言按 id 字典序
    CHECK(!language_display_less("zh-CN", "en-US"));

    const std::map<std::string, std::string> cur{{"a", "A"}, {"b", "B"}};
    const std::map<std::string, std::string> base{{"b", "BE"}, {"c", "CE"}};
    CHECK_STR(resolve_translation(cur, &base, "a"), "A");
    CHECK_STR(resolve_translation(cur, &base, "b"), "B");
    CHECK_STR(resolve_translation(cur, &base, "c"), "CE");  // 当前词典缺键 → 基准兜底
    CHECK_STR(resolve_translation(cur, &base, "d"), "d");   // 两边都缺 → 原样返回键名
    CHECK_STR(resolve_translation(cur, nullptr, "c"), "c");
}

// 词典快照：T 返回值脱离快照后仍有效；重扫替换快照时并发读取不失效。
void test_i18n_snapshot_concurrency() {
    section("PBZFM 词典快照（返回值生命周期 / 并发重扫）");
    using namespace pbzpm;
    namespace fs = std::filesystem;

    // 相对路径：测试可能在项目根或构建目录运行，两处都可写；全程用 error_code 避免抛异常
    const fs::path root = "pbzpm_i18n_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path dir = root / "languages";
    // create_directories 在目录已存在时返回 false，不能直接当失败断言
    fs::create_directories(dir, ec);
    CHECK(!ec && fs::is_directory(dir));

    auto write_lang = [](const fs::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return out.good();
    };
    CHECK(write_lang(dir / "en-US.json", R"({"__display":"English","#greet":"Hello","#only_en":"Base only"})"));
    CHECK(write_lang(dir / "zh-CN.json", R"({"__display":"简体中文","#greet":"你好"})"));

    PbzmI18n lang;
    lang.RescanLanguages(root.string(), true);
    CHECK_INT(lang.LanguageCount(), 2);
    CHECK_STR(lang.CurrentId(), "en-US");
    lang.SetLanguageById("zh-CN");
    CHECK_STR(lang.CurrentId(), "zh-CN");
    CHECK_STR(lang.T("#greet"), "你好");
    CHECK_STR(lang.T("#only_en"), "Base only");  // 当前语言缺键 → en-US 基准兜底

    // T 返回拷贝：拿到后立刻重扫替换词典，旧返回值仍然有效
    const std::string held = lang.T("#greet");
    CHECK(write_lang(dir / "zh-CN.json", R"({"__display":"简体中文","#greet":"您好"})"));
    CHECK(write_lang(dir / "en-US.json", R"({"__display":"English","#greet":"Hello again"})"));
    lang.RescanLanguages(root.string(), true);
    CHECK_STR(held, "你好");
    CHECK_STR(lang.T("#greet"), "您好");

    // 一个线程反复重扫并替换词典，另一个线程持续读取：读到的永远是完整字符串
    std::atomic<int> bad{0};
    std::thread writer([&] {
        for (int i = 0; i < 300; ++i) {
            write_lang(dir / "zh-CN.json",
                       i % 2 == 0 ? R"({"__display":"简体中文","#greet":"你好"})"
                                  : R"({"__display":"简体中文","#greet":"您好"})");
            lang.RescanLanguages(root.string(), true);
        }
    });
    std::thread reader([&] {
        for (int i = 0; i < 5000; ++i) {
            const std::string v = lang.T("#greet");
            if (v != "你好" && v != "您好" && v != "Hello again" && v != "Hello") ++bad;
            const std::vector<PbzmI18n::LanguageInfo> list = lang.Languages();
            if (list.size() < 2 || list[0].id != "en-US") ++bad;
            const std::string cur = lang.CurrentId();
            if (cur != "zh-CN" && cur != "en-US") ++bad;
        }
    });
    writer.join();
    reader.join();
    CHECK_INT(bad.load(), 0);

    fs::remove_all(root, ec);
}

// ---------------------------------------------------------------- 世界令牌与快照（H-2）

void test_world_snapshot() {
    section("WorldIdentity / 时间与暂停快照（H-2）");
    using namespace pbzpm;

    int world_a = 0;
    int world_b = 0;
    int settings_a = 0;
    int settings_b = 0;
    const void* wa = &world_a;
    const void* wb = &world_b;
    const void* sa = &settings_a;
    const void* sb = &settings_b;

    // 空指针（关卡切换窗口期）不换代，否则同一世界的原始值会被误丢
    WorldIdentity identity;
    CHECK_INT((int)identity.generation(), 0);
    CHECK(!identity.observe(nullptr, nullptr));
    CHECK(!identity.observe(nullptr, nullptr));
    CHECK_INT((int)identity.generation(), 0);
    CHECK(!identity.observe(wa, nullptr));
    CHECK_INT((int)identity.generation(), 0);
    CHECK(!identity.observe(wa, sa));  // 首次拿到世界：建立基线，不算换代
    CHECK_INT((int)identity.generation(), 0);
    CHECK(!identity.observe(wa, sa));
    CHECK_INT((int)identity.generation(), 0);
    CHECK(!identity.observe(nullptr, nullptr));  // 卸载窗口期
    CHECK_INT((int)identity.generation(), 0);
    CHECK(identity.observe(wb, nullptr));  // UWorld 变化 → 换代
    CHECK_INT((int)identity.generation(), 1);
    CHECK(identity.observe(wb, sb));  // WorldSettings 变化 → 换代
    CHECK_INT((int)identity.generation(), 2);
    identity.invalidate();
    CHECK_INT((int)identity.generation(), 3);

    // 时间字段位图：读失败就不写
    TimeSettingsSnapshot time;
    CHECK(!time.belongs_to(7));
    time.adopt(7);
    CHECK(time.belongs_to(7));
    CHECK(!time.was_read(TimeField::Dilation));
    CHECK(!time.any_written());
    // 只读到 TimeDilation → 只允许写 TimeDilation
    time.mark_read(TimeField::Dilation);
    time.dilation = 1.0;
    CHECK_INT(plan_time_freeze_fields(time, 0.0), (int)time_field_bit(TimeField::Dilation));
    CHECK_INT(plan_time_freeze_fields(time, 100.0), (int)time_field_bit(TimeField::Dilation));
    // 读到 Allow/Min/Max 后，越界才附带解锁与放宽上下限
    time.mark_read(TimeField::Allow);
    time.mark_read(TimeField::Min);
    time.mark_read(TimeField::Max);
    const int with_all = (int)(time_field_bit(TimeField::Dilation) | time_field_bit(TimeField::Allow) |
                               time_field_bit(TimeField::Min));
    CHECK_INT(plan_time_freeze_fields(time, 0.0), with_all);  // 0 < min → 需要放宽 Min
    const int with_max = (int)(time_field_bit(TimeField::Dilation) | time_field_bit(TimeField::Allow) |
                               time_field_bit(TimeField::Max));
    CHECK_INT(plan_time_freeze_fields(time, 100.0), with_max);  // 100 > max → 需要放宽 Max
    // 未读到 Allow 时即使越界也不解锁
    TimeSettingsSnapshot no_allow;
    no_allow.mark_read(TimeField::Dilation);
    no_allow.mark_read(TimeField::Min);
    CHECK_INT(plan_time_freeze_fields(no_allow, 0.0),
              (int)(time_field_bit(TimeField::Dilation) | time_field_bit(TimeField::Min)));
    // 完全没读到 → 放弃冻结
    TimeSettingsSnapshot empty;
    CHECK_INT(plan_time_freeze_fields(empty, 0.0), 0);
    // 写入记账独立于读取记账
    time.mark_written(TimeField::Dilation);
    time.mark_written(TimeField::Allow);
    CHECK(time.was_written(TimeField::Dilation));
    CHECK(time.was_written(TimeField::Allow));
    CHECK(!time.was_written(TimeField::Min));
    CHECK(time.any_written());
    time.reset();
    CHECK(time.read_mask == 0 && time.written_mask == 0);
    CHECK(!time.any_written());
    CHECK(!time.belongs_to(7));

    // 暂停快照：三项独立记账，读不到就完全不写
    PauseSnapshot pause;
    CHECK(!pause.any_written());
    CHECK(!pause.full_tick_read && !pause.game_mode_read && !pause.camera_moveable_read);
    pause.adopt(11);
    CHECK(pause.belongs_to(11));
    pause.full_tick_read = true;
    pause.full_tick_written = true;
    CHECK(pause.any_written());
    pause.reset();
    CHECK(!pause.any_written());
    CHECK(!pause.belongs_to(11));
    CHECK(!pause.game_mode_written);
    CHECK(!pause.camera_moveable_written);
    // 只有相机标志写过也必须算 any_written（解冻/退出要能触发还原）
    pause.camera_moveable_read = true;
    pause.camera_moveable_original = false;
    pause.camera_moveable_written = true;
    CHECK(pause.any_written());
    pause.reset();
    CHECK(!pause.camera_moveable_read && !pause.camera_moveable_written);
    CHECK(!pause.any_written());
}

// ---------------------------------------------------------------- 退出恢复失败重试（H-1）

void test_photo_mode_recovery_retry() {
    section("PhotoMode 退出恢复失败重试");
    using namespace pbzpm;
    using pbzpm::test::FakeEngine;

    // 失败项保留标志 + Error 日志；1 秒退避后重试，恢复成功后清干净
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.paused);
        CHECK(mode.frozen());

        fe.fail_pause_restore = true;
        mode.leave("测试恢复失败");
        CHECK(!mode.active());
        CHECK(mode.recovery_pending());  // 状态如实反映「未完成恢复」
        CHECK(mode.frozen());            // 未恢复就不清冻结标志
        CHECK(fe.paused);
        CHECK(fe.logs_contain("仍有 1 项未能还原"));
        CHECK(fe.logs_contain("游戏暂停未解除"));

        // 退避 1 秒内不重试（不能每帧重压）
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        mode.tick(Intent{}, 0.25);
        CHECK_INT(fe.count("pause_off"), 1);  // 只有 leave 时那一次
        mode.tick(Intent{}, 0.25);            // 累积 1.0 秒 → 第一次重试，仍失败
        CHECK_INT(fe.count("pause_off"), 2);
        CHECK(mode.recovery_pending());
        CHECK(mode.frozen());

        // 第二次重试要等 2 秒；这次让引擎恢复正常
        fe.fail_pause_restore = false;
        for (int i = 0; i < 12 && mode.recovery_pending(); ++i) mode.tick(Intent{}, 0.25);
        CHECK(!mode.recovery_pending());
        CHECK(!mode.frozen());
        CHECK(!fe.paused);
        CHECK(fe.logs_contain("后台重试成功"));
        // 恢复完成后再次 leave 是幂等的
        const int pause_off_calls = fe.count("pause_off");
        mode.leave("测试");
        CHECK_INT(fe.count("pause_off"), pause_off_calls);
    }

    // 重试次数到上限后停止：不再反复重压，但仍如实保留未还原状态
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        fe.fail_pause_restore = true;
        mode.leave("测试");
        CHECK(mode.recovery_pending());
        for (int i = 0; i < 200 && mode.recovery_pending(); ++i) mode.tick(Intent{}, 0.25);
        CHECK(!mode.recovery_pending());
        CHECK(mode.frozen());  // 没恢复成功就不谎报已恢复
        CHECK(fe.paused);
        CHECK(fe.logs_contain("停止重试"));

        // 仍有残留：再次进入应先补还原，失败则拒绝进入
        CHECK(!mode.enter());
        CHECK(fe.logs_contain("本次进入已取消"));
    }

    // Wwise 整引擎唤醒失败：保留挂起标志，后续 tick 重试成功后清除
    {
        FakeEngine fe;
        fe.wwise_engine_suspend_ok = true;
        fe.wwise_engine_wake_ok = false;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        CHECK(fe.wwise_engine_suspended);  // 自动冻结时无逐语音 ID → 整引擎挂起
        CHECK(fe.logs_contain("整引擎 Suspend"));

        mode.leave("测试唤醒失败");
        CHECK(mode.recovery_pending());
        CHECK(fe.wwise_engine_suspended);  // 唤醒失败不虚假清除
        CHECK(fe.logs_contain("Wwise 音频引擎仍处于挂起"));

        fe.wwise_engine_wake_ok = true;
        for (int i = 0; i < 8 && mode.recovery_pending(); ++i) mode.tick(Intent{}, 0.25);
        CHECK(!mode.recovery_pending());
        CHECK(!fe.wwise_engine_suspended);
        CHECK(fe.logs_contain("后台重试成功"));
    }

    // 多失败项逐项累积：HUD 只失败一项时，其他项照常还原，不因一项失败而重做全部
    {
        FakeEngine fe;
        PhotoMode mode(fe, Config{});
        CHECK(mode.enter());
        fe.fail_hud_restore = true;
        mode.leave("测试 HUD 恢复失败");
        CHECK(mode.recovery_pending());
        CHECK(fe.logs_contain("HUD 仍处于隐藏"));
        CHECK(fe.hud_visible == false);  // 引擎侧确实仍是隐藏
        CHECK(fe.input_blocked == false); // 其他项已正常还原
        CHECK(fe.view_attached == false);

        fe.fail_hud_restore = false;
        for (int i = 0; i < 8 && mode.recovery_pending(); ++i) mode.tick(Intent{}, 0.25);
        CHECK(!mode.recovery_pending());
        CHECK(fe.hud_visible);
    }
}

// ---------------------------------------------------------------- 重试退避（M-11）

// 引擎输入钩子重试的纯逻辑：指数退避序列、封顶、溢出保护、到点判定。
// 真实计时（steady_clock）与 UE4SS 钩子安装只能实机验证，这里只覆盖可纯化的判定。
void test_retry_backoff() {
    section("重试退避（M-11）");
    using namespace pbzpm;

    RetryBackoff backoff{};
    // 默认节奏：0.5 / 1 / 2 / 4 / 8 封顶，之后一直 8 秒
    CHECK_NEAR(backoff.delay_seconds(1), 0.5, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(2), 1.0, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(3), 2.0, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(4), 4.0, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(5), 8.0, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(6), 8.0, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(30), 8.0, 1e-9);
    // 极大失败次数不能溢出、也不能让循环次数线性增长
    CHECK_NEAR(backoff.delay_seconds(1000000), 8.0, 1e-9);
    // 0 / 负数按第 1 次处理（防御性边界）
    CHECK_NEAR(backoff.delay_seconds(0), 0.5, 1e-9);
    CHECK_NEAR(backoff.delay_seconds(-5), 0.5, 1e-9);

    // 单调不减
    bool monotonic = true;
    double previous = 0.0;
    for (int failures = 0; failures <= 20; ++failures) {
        const double delay = backoff.delay_seconds(failures);
        if (delay < previous) monotonic = false;
        previous = delay;
    }
    CHECK(monotonic);

    // 到点判定：相等即到点（浮点边界不卡住），未到点不重试
    CHECK(retry_due(1.0, 0.5));
    CHECK(retry_due(0.5, 0.5));
    CHECK(!retry_due(0.499999, 0.5));
    CHECK(retry_due(10.0, 8.0));

    // 自定义参数（base=1.0 / 封顶 4.0）
    RetryBackoff custom{1.0, 4.0};
    CHECK_NEAR(custom.delay_seconds(1), 1.0, 1e-9);
    CHECK_NEAR(custom.delay_seconds(2), 2.0, 1e-9);
    CHECK_NEAR(custom.delay_seconds(3), 4.0, 1e-9);
    CHECK_NEAR(custom.delay_seconds(9), 4.0, 1e-9);

    // 模拟 tick 线程的真实时间序列：0.5→1→2→4→8，第 5 次失败后固定 8 秒一次
    double now = 0.0;
    double next_retry_at = 0.0;
    int failures = 0;
    CHECK(retry_due(now, next_retry_at));  // 首次立即尝试
    const double first_time = 0.0;
    CHECK(retry_due(first_time, next_retry_at));
    for (int attempt = 1; attempt <= 6; ++attempt) {
        ++failures;
        next_retry_at = now + backoff.delay_seconds(failures);
        // 未到点：本帧必须跳过（不能每帧都重试）
        CHECK(!retry_due(next_retry_at - 0.001, next_retry_at));
        now = next_retry_at;  // 到点
        CHECK(retry_due(now, next_retry_at));
        if (attempt == 6) CHECK_NEAR(now, 0.5 + 1 + 2 + 4 + 8 + 8, 1e-9);
    }
}

// ---------------------------------------------------------------- 钩子记账（H-5）

// 纯记账结构：幂等判定、成功/失败/部分成功、待注销快照、注销结果回写。
// 真实 RegisterHook/UnregisterHook 只能实机验证，这里只覆盖判定逻辑本身。
void test_hook_registry() {
    section("HookRegistry 记账与幂等");
    using namespace pbzpm;

    int dummy_function = 0;
    void* function = &dummy_function;

    {
        HookRegistry registry;
        CHECK(!registry.installed("PBZSoundFXSubsystem.PBZPostEventOnActor"));
        CHECK_INT(registry.installed_count(), 0);
        CHECK_INT(registry.failed_count(), 0);
        CHECK_INT(registry.unregister_failed_count(), 0);
        CHECK_INT((int)registry.records().size(), 0);
        CHECK_INT((int)registry.take_pending_unregister().size(), 0);
    }

    // 成功安装：两个 id 有效 → Installed
    {
        HookRegistry registry;
        CHECK(registry.record_install("PBZSoundFXSubsystem.PBZPostEventOnActor", HookKind::Script, function, 3, 4));
        CHECK(registry.installed("PBZSoundFXSubsystem.PBZPostEventOnActor"));
        CHECK_INT(registry.installed_count(), 1);
        const std::vector<HookRecord> all = registry.records();
        CHECK_INT((int)all.size(), 1);
        CHECK_INT(all[0].pre_id, 3);
        CHECK_INT(all[0].post_id, 4);
        CHECK(all[0].function == function);
        CHECK_INT(all[0].attempts, 1);
    }

    // 幂等：已 Installed 的 target 再次安装返回 false，且旧 id 不被覆盖（否则旧钩子失去注销线索）
    {
        HookRegistry registry;
        CHECK(registry.record_install("AkComponent.PostAkEvent", HookKind::Script, function, 11, 12));
        CHECK(!registry.record_install("AkComponent.PostAkEvent", HookKind::Script, function, 21, 22));
        CHECK(registry.installed("AkComponent.PostAkEvent"));
        CHECK_INT(registry.installed_count(), 1);
        const std::vector<HookRecord> all = registry.records();
        CHECK_INT((int)all.size(), 1);
        CHECK_INT(all[0].pre_id, 11);
        CHECK_INT(all[0].post_id, 12);
        CHECK_INT(all[0].attempts, 1);  // 未覆盖也未累加 attempts
    }

    // 部分成功：脚本钩子只拿到一个 id 也算装上；两个都无效才算失败
    {
        HookRegistry registry;
        CHECK(registry.record_install("A", HookKind::Script, function, 5, -1));
        CHECK(registry.installed("A"));
        CHECK(!registry.record_install("B", HookKind::Script, function, -1, -1));
        CHECK(!registry.installed("B"));
        CHECK_INT(registry.failed_count(), 1);
        CHECK_INT(registry.installed_count(), 1);
        const std::vector<HookRecord> all = registry.records();
        CHECK_INT((int)all.size(), 2);
        CHECK(all[1].state == HookState::Failed);
        CHECK_INT(all[1].pre_id, -1);
        CHECK_INT(all[1].post_id, -1);
    }

    // 失败记录可重试：Failed → record_install 成功后转 Installed，attempts 累加
    {
        HookRegistry registry;
        registry.record_failure("PlayAudio_C.ForceFinishAudio", HookKind::Script);
        CHECK(!registry.installed("PlayAudio_C.ForceFinishAudio"));
        CHECK_INT(registry.failed_count(), 1);
        CHECK_INT(registry.records()[0].attempts, 1);
        CHECK(registry.record_install("PlayAudio_C.ForceFinishAudio", HookKind::Script, function, 7, 8));
        CHECK(registry.installed("PlayAudio_C.ForceFinishAudio"));
        CHECK_INT(registry.failed_count(), 0);
        CHECK_INT(registry.installed_count(), 1);
        CHECK_INT(registry.records()[0].attempts, 2);
    }

    // 全局回调：单 id，记录在 pre_id；post_id 恒为 -1
    {
        HookRegistry registry;
        CHECK(registry.record_install("UE4SS.EngineTickPre", HookKind::Global, nullptr, 42, -1));
        CHECK(registry.installed("UE4SS.EngineTickPre"));
        const std::vector<HookRecord> all = registry.records();
        CHECK_INT(all[0].pre_id, 42);
        CHECK_INT(all[0].post_id, -1);
        CHECK(all[0].kind == HookKind::Global);
        CHECK(all[0].function == nullptr);
    }

    // 待注销快照只含 Installed / UnregisterFailed；注销成功与失败的回报各自如实更新
    {
        HookRegistry registry;
        CHECK(registry.record_install("A", HookKind::Script, function, 1, 2));
        CHECK(registry.record_install("B", HookKind::Global, nullptr, 9, -1));
        registry.record_failure("C", HookKind::Script);

        std::vector<HookRecord> pending = registry.take_pending_unregister();
        CHECK_INT((int)pending.size(), 2);

        registry.record_unregistered("A");
        CHECK(!registry.installed("A"));
        CHECK_INT(registry.installed_count(), 1);
        registry.record_unregister_failure("B");
        CHECK_INT(registry.unregister_failed_count(), 1);
        CHECK(!registry.installed("B"));

        // 注销失败的条目重新出现在待注销列表里，允许下次重试；成功的与失败安装不再出现
        pending = registry.take_pending_unregister();
        CHECK_INT((int)pending.size(), 1);
        CHECK_STR(pending[0].target, "B");

        // 重试成功后清除失败计数
        registry.record_unregistered("B");
        CHECK_INT(registry.unregister_failed_count(), 0);
        CHECK_INT((int)registry.take_pending_unregister().size(), 0);
    }

    // 注销后重新安装（热重载场景）可以复用同一 target，attempts 累加
    {
        HookRegistry registry;
        CHECK(registry.record_install("D", HookKind::Script, function, 10, 11));
        registry.record_unregistered("D");
        CHECK(registry.record_install("D", HookKind::Script, function, 20, 21));
        CHECK(registry.installed("D"));
        const std::vector<HookRecord> all = registry.records();
        CHECK_INT(all[0].pre_id, 20);
        CHECK_INT(all[0].post_id, 21);
        CHECK_INT(all[0].attempts, 2);
    }
}


// 风格槽位（core/look_preset.h）：导出文本往返/坏文件拒绝（写死初值的风格预设按钮已下线）
void test_look_preset_slots() {
    section("风格槽位三槽位导入导出");
    using namespace pbzpm;
    const double eps = 1e-9;

    // 导出 → 导入严格往返（9 风格字段 + 5 开关）；渲染管线键不进槽位文件
    Config exported{};
    exported.vignette = 1.5;
    exported.saturation = 0.85;
    exported.contrast = 1.2;
    exported.bloom = 3.0;
    exported.temperature = 0.25;
    exported.chromatic_aberration = 0.8;
    exported.color_gain = 1.4;
    exported.color_gamma = 0.9;
    exported.color_offset = 0.02;
    exported.vignette_enabled = false;
    const std::string text = look_preset_ini_text(exported);
    CHECK(text.find("[look]") != std::string::npos);
    CHECK(text.find("vignette = 1.5") != std::string::npos);
    CHECK(text.find("sharpness") == std::string::npos);
    CHECK(text.find("screen_percentage") == std::string::npos);
    Config imported{};
    CHECK(parse_look_preset_ini(text, imported));
    CHECK_NEAR(imported.vignette, 1.5, eps);
    CHECK_NEAR(imported.saturation, 0.85, eps);
    CHECK_NEAR(imported.contrast, 1.2, eps);
    CHECK_NEAR(imported.bloom, 3.0, eps);
    CHECK_NEAR(imported.temperature, 0.25, eps);
    CHECK_NEAR(imported.chromatic_aberration, 0.8, eps);
    CHECK_NEAR(imported.color_gain, 1.4, eps);
    CHECK_NEAR(imported.color_gamma, 0.9, eps);
    CHECK_NEAR(imported.color_offset, 0.02, eps);
    CHECK(!imported.vignette_enabled);
    CHECK(imported.saturation_enabled);

    // 坏文件拒绝：空文本 / 无 [look] 段 / 乱码（防「静默套默认值」路径）
    Config guard{};
    guard.vignette = 2.0;
    CHECK(!parse_look_preset_ini("", guard));
    CHECK(!parse_look_preset_ini("[effects]\nhide_player = true\n", guard));
    CHECK(!parse_look_preset_ini("look_preset", guard));
    CHECK_NEAR(guard.vignette, 2.0, eps);  // 失败时 out 不动
}

int main() {
    std::printf("PBZ Photo Mode (C++) 核心层测试\n");

    test_vk_names();
    test_config_defaults_and_parse();
    test_config_warnings_and_clamps();
    test_config_revived_dof_keys();
    test_config_color_pro_keys();
    test_config_schema_and_sections();
    test_config_default_text_roundtrip();
    test_math();
    test_engine_port_pure_logic();
    test_input_logic();
    test_photo_mode_happy_path();
    test_photo_mode_look_defaults_on_enter();
    test_photo_mode_failures_rollback();
    test_photo_mode_takeover_input();
    test_photo_mode_tick();
    test_photo_mode_freeze();
    test_photo_mode_auto_pause();
    test_photo_mode_cutscene_freeze();
    test_photo_mode_wwise_engine();
    test_photo_mode_dialogue_flow();
    test_photo_mode_freeze_treatment();
    test_photo_mode_reassert_and_partial();
    test_photo_mode_input_blocking();
    test_photo_mode_live_toggles();
    test_camera_backend_switch();
    test_photo_mode_ultrawide();
    test_photo_mode_aspect_reassert();
    test_photo_mode_cinematic_cleanup();
    test_photo_mode_interact_prompt();
    test_photo_mode_enemy_health_bars();
    test_photo_mode_lockon_marker();
    test_photo_mode_subtitles();
    test_world_snapshot();
    test_photo_mode_recovery_retry();
    test_gamepad_names();
    test_gamepad_parse_combo();
    test_dinput_map();
    test_gamepad_deadzone();
    test_gamepad_input_logic();
    test_photo_mode_gamepad();
    test_photo_mode_slow_look();
    test_pbzfm_fields_roundtrip();
    test_pbzfm_config_writeback();
    test_look_preset_slots();
    test_config_atomic_write();
    test_i18n_pure_logic();
    test_i18n_snapshot_concurrency();
    test_retry_backoff();
    test_hook_registry();
    run_mod_runtime_tests();
    run_input_pure_tests();
    run_pbzfm_validation_tests();

    std::printf("\n----------------------------------------\n");
    // 机器可读行：tools\update_test_count.ps1 只解析这一行（不依赖中文编码）
    std::printf("TESTS checks=%d failures=%d\n", g_checks, g_failures);
    std::printf("检查 %d 项，失败 %d 项\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("存在失败项\n");
    return 1;
}
