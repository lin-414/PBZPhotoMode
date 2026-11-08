#include "core/config.h"

#include "core/tuning.h"
#include "core/version.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pbzpm {
namespace {

std::string trim(std::string_view in) {
    size_t b = 0;
    size_t e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    return std::string(in.substr(b, e - b));
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 注释截断：引号外的第一个 ';' / '#' 即注释起点（不再要求前面有空白），
// 因此 `fov = 50;备注` 也能正确解析；引号内的 ';' / '#' 按字面值保留。
std::string strip_comment(std::string_view line) {
    bool in_double = false;
    bool in_single = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
        } else if (c == '\'' && !in_double) {
            in_single = !in_single;
        } else if ((c == ';' || c == '#') && !in_double && !in_single) {
            return trim(line.substr(0, i));
        }
    }
    return trim(line);
}

std::string unquote(std::string s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool parse_bool(const std::string& raw, bool& out) {
    const std::string v = lower(raw);
    if (v == "true" || v == "1" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "false" || v == "0" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parse_double(const std::string& raw, double& out) {
    const std::string s = trim(raw);
    if (s.empty()) return false;
    double value{};
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto res = std::from_chars(begin, end, value);
    if (res.ec != std::errc{} || res.ptr != end) return false;
    if (!std::isfinite(value)) return false;
    out = value;
    return true;
}

std::string num_to_text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

// ---------------------------------------------------------------- schema 表
// 每个 key 的类型、值域、默认值来源（Config 成员初始化）、弃用状态与消费点集中在这里。
// 解析器不再手写 if/else 链：新增键只需加一行，范围校验/告警去重天然覆盖。
enum class FieldKind { Text, Bool, Number, Enum, Deprecated };

struct ConfigFieldSpec {
    const char* key;  // 规范键名（小写）
    const char* section;  // 期望所在 section；"" = 任意位置（顶层也接受）
    FieldKind kind;
    std::string Config::* text{nullptr};
    bool Config::* boolean{nullptr};
    double Config::* number{nullptr};
    bool (*apply_enum)(Config&, const std::string&){nullptr};
    double min{0.0};
    double max{0.0};
    const char* consumed_at{nullptr};  // 消费点（文档用途）
    const char* note{nullptr};         // 弃用/兼容说明
};

ConfigFieldSpec Text(const char* key, const char* section, std::string Config::* member, const char* consumed_at,
                     const char* note = nullptr) {
    ConfigFieldSpec spec;
    spec.key = key;
    spec.section = section;
    spec.kind = FieldKind::Text;
    spec.text = member;
    spec.consumed_at = consumed_at;
    spec.note = note;
    return spec;
}

ConfigFieldSpec Bool(const char* key, const char* section, bool Config::* member, const char* consumed_at) {
    ConfigFieldSpec spec;
    spec.key = key;
    spec.section = section;
    spec.kind = FieldKind::Bool;
    spec.boolean = member;
    spec.consumed_at = consumed_at;
    return spec;
}

ConfigFieldSpec Number(const char* key, const char* section, double Config::* member, double min, double max,
                       const char* consumed_at) {
    ConfigFieldSpec spec;
    spec.key = key;
    spec.section = section;
    spec.kind = FieldKind::Number;
    spec.number = member;
    spec.min = min;
    spec.max = max;
    spec.consumed_at = consumed_at;
    return spec;
}

ConfigFieldSpec Enum(const char* key, const char* section, bool (*apply)(Config&, const std::string&),
                     const char* consumed_at) {
    ConfigFieldSpec spec;
    spec.key = key;
    spec.section = section;
    spec.kind = FieldKind::Enum;
    spec.apply_enum = apply;
    spec.consumed_at = consumed_at;
    return spec;
}

ConfigFieldSpec Deprecated(const char* key, const char* note) {
    ConfigFieldSpec spec;
    spec.key = key;
    spec.section = "";  // 旧文件里位置不定，任意 section 都接受
    spec.kind = FieldKind::Deprecated;
    spec.note = note;
    return spec;
}

bool apply_mouse_vertical_mode(Config& cfg, const std::string& value) {
    const std::string v = lower(value);
    if (v == "translate" || v == "move") {
        cfg.mouse_vertical_mode = MouseVerticalMode::Translate;
        return true;
    }
    if (v == "look" || v == "pitch") {
        cfg.mouse_vertical_mode = MouseVerticalMode::Look;
        return true;
    }
    return false;
}

bool apply_freeze_mode(Config& cfg, const std::string& value) {
    const std::string v = lower(value);
    if (v == "dilation" || v == "slow") {
        cfg.freeze_mode = FreezeMode::Dilation;
        return true;
    }
    if (v == "pause") {
        cfg.freeze_mode = FreezeMode::Pause;
        return true;
    }
    return false;
}

bool apply_log_level(Config& cfg, const std::string& value) {
    const std::string v = lower(value);
    if (v == "error") {
        cfg.log_level = LogLevel::Error;
        return true;
    }
    if (v == "warn" || v == "warning") {
        cfg.log_level = LogLevel::Warn;
        return true;
    }
    if (v == "info") {
        cfg.log_level = LogLevel::Info;
        return true;
    }
    if (v == "debug") {
        cfg.log_level = LogLevel::Debug;
        return true;
    }
    return false;
}

bool apply_menu_language(Config& cfg, const std::string& value) {
    if (value.empty()) return false;
    cfg.menu_language = value;
    return true;
}

const std::vector<ConfigFieldSpec>& schema() {
    static const std::vector<ConfigFieldSpec> table = {
        // ---- [keys] ----
        Text("key_toggle", "keys", &Config::key_toggle, "mod_app::on_hotkey"),
        Text("key_freeze", "keys", &Config::key_freeze, "photo_mode::tick"),
        Text("key_frame_freeze", "keys", &Config::key_frame_freeze, "mod_app::frame_freeze"),
        Text("key_reset", "keys", &Config::key_reset, "photo_mode::tick"),
        Text("key_roll_reset", "keys", &Config::key_roll_reset, "photo_mode::tick"),
        Text("key_fov_up", "keys", &Config::key_fov_up, "photo_mode::tick"),
        Text("key_fov_down", "keys", &Config::key_fov_down, "photo_mode::tick"),
        Text("key_pitch_up", "keys", &Config::key_pitch_up, "input_logic"),
        Text("key_pitch_down", "keys", &Config::key_pitch_down, "input_logic"),
        Text("key_yaw_left", "keys", &Config::key_yaw_left, "input_logic"),
        Text("key_yaw_right", "keys", &Config::key_yaw_right, "input_logic"),
        Text("key_roll_left", "keys", &Config::key_roll_left, "input_logic"),
        Text("key_roll_right", "keys", &Config::key_roll_right, "input_logic"),
        Text("key_vertical_up", "keys", &Config::key_vertical_up, "input_logic"),
        Text("key_vertical_down", "keys", &Config::key_vertical_down, "input_logic"),
        Text("key_move_forward", "keys", &Config::key_move_forward, "input_logic"),
        Text("key_move_back", "keys", &Config::key_move_back, "input_logic"),
        Text("key_move_left", "keys", &Config::key_move_left, "input_logic"),
        Text("key_move_right", "keys", &Config::key_move_right, "input_logic"),
        Text("key_boost", "keys", &Config::key_boost, "input_logic"),
        Text("key_slow", "keys", &Config::key_slow, "input_logic"),
        Text("key_free_look", "keys", &Config::key_free_look, "input_logic"),
        Text("key_dof_toggle", "keys", &Config::key_dof_toggle, "photo_mode::tick"),
        Text("key_focus_near", "keys", &Config::key_focus_near, "photo_mode::tick"),
        Text("key_focus_far", "keys", &Config::key_focus_far, "photo_mode::tick"),
        Text("key_aperture_open", "keys", &Config::key_aperture_open, "photo_mode::tick"),
        Text("key_aperture_close", "keys", &Config::key_aperture_close, "photo_mode::tick"),
        // 兼容别名（同一字段，不再静默丢弃）
        Text("mouse_vertical_up", "keys", &Config::key_vertical_up, "input_logic", "key_vertical_up 的别名"),
        Text("mouse_vertical_down", "keys", &Config::key_vertical_down, "input_logic", "key_vertical_down 的别名"),
        Text("key_pan_left", "keys", &Config::key_yaw_left, "input_logic", "1.0.46 起改名为 key_yaw_left"),
        Text("key_pan_right", "keys", &Config::key_yaw_right, "input_logic", "1.0.46 起改名为 key_yaw_right"),

        // ---- [control] ----
        Number("move_speed", "control", &Config::move_speed, 50.0, 2000.0, "photo_mode::tick"),
        Number("boost_mult", "control", &Config::boost_mult, 1.0, 10.0, "input_logic"),
        Number("slow_mult", "control", &Config::slow_mult, 0.05, 1.0, "input_logic"),
        Number("slow_look_mult", "control", &Config::slow_look_mult, 0.01, 1.0, "input_logic"),
        Number("look_sensitivity", "control", &Config::look_sensitivity, 0.01, 1.0, "input_logic"),
        Number("pitch_limit", "control", &Config::pitch_limit, 1.0, 90.0, "math3::clamp_pitch"),
        Bool("invert_y", "control", &Config::invert_y, "input_logic"),
        Bool("free_look_requires_alt", "control", &Config::free_look_requires_alt, "input_logic"),
        Bool("block_game_input", "control", &Config::block_game_input, "photo_mode::enter"),
        Enum("mouse_vertical_mode", "control", &apply_mouse_vertical_mode, "input_logic"),
        Bool("wheel_fov", "control", &Config::wheel_fov, "photo_mode::tick"),
        Bool("wheel_fov_invert", "control", &Config::wheel_fov_invert, "photo_mode::tick"),
        Number("vertical_speed", "control", &Config::vertical_speed, 10.0, 1000.0, "photo_mode::tick"),
        Number("roll_speed", "control", &Config::roll_speed, 5.0, 360.0, "photo_mode::tick"),
        Number("pitch_speed", "control", &Config::pitch_speed, 5.0, 180.0, "photo_mode::tick"),
        Number("yaw_speed", "control", &Config::yaw_speed, 5.0, 270.0, "photo_mode::tick"),

        // ---- [gamepad] ----
        Bool("pad_enabled", "gamepad", &Config::pad_enabled, "gamepad::poll"),
        Text("pad_toggle", "gamepad", &Config::pad_toggle, "gamepad::poll"),
        Text("pad_freeze", "gamepad", &Config::pad_freeze, "gamepad::poll"),
        Text("pad_reset", "gamepad", &Config::pad_reset, "gamepad::poll"),
        Text("pad_roll_reset", "gamepad", &Config::pad_roll_reset, "gamepad::poll"),
        Text("pad_fov_up", "gamepad", &Config::pad_fov_up, "gamepad::poll"),
        Text("pad_fov_down", "gamepad", &Config::pad_fov_down, "gamepad::poll"),
        Text("pad_pitch_up", "gamepad", &Config::pad_pitch_up, "input_logic"),
        Text("pad_pitch_down", "gamepad", &Config::pad_pitch_down, "input_logic"),
        Text("pad_yaw_left", "gamepad", &Config::pad_yaw_left, "input_logic"),
        Text("pad_yaw_right", "gamepad", &Config::pad_yaw_right, "input_logic"),
        Text("pad_roll_left", "gamepad", &Config::pad_roll_left, "input_logic"),
        Text("pad_roll_right", "gamepad", &Config::pad_roll_right, "input_logic"),
        Text("pad_boost", "gamepad", &Config::pad_boost, "input_logic"),
        Text("pad_slow", "gamepad", &Config::pad_slow, "input_logic"),
        Text("pad_vertical_up", "gamepad", &Config::pad_vertical_up, "input_logic"),
        Text("pad_vertical_down", "gamepad", &Config::pad_vertical_down, "input_logic"),
        Text("pad_focus_near", "gamepad", &Config::pad_focus_near, "input_logic"),
        Text("pad_focus_far", "gamepad", &Config::pad_focus_far, "input_logic"),
        Text("pad_aperture_open", "gamepad", &Config::pad_aperture_open, "input_logic"),
        Text("pad_aperture_close", "gamepad", &Config::pad_aperture_close, "input_logic"),
        Text("pad_dof_toggle", "gamepad", &Config::pad_dof_toggle, "photo_mode::tick"),
        Text("pad_pan_left", "gamepad", &Config::pad_yaw_left, "input_logic", "1.0.46 起改名为 pad_yaw_left"),
        Text("pad_pan_right", "gamepad", &Config::pad_yaw_right, "input_logic", "1.0.46 起改名为 pad_yaw_right"),
        Number("pad_look_speed", "gamepad", &Config::pad_look_speed, 10.0, 720.0, "input_logic"),
        Number("pad_move_scale", "gamepad", &Config::pad_move_scale, 0.0, 3.0, "input_logic"),
        Number("pad_deadzone", "gamepad", &Config::pad_deadzone, 0.0, 0.5, "gamepad::poll"),

        // ---- [camera] ----
        Number("fov_default", "camera", &Config::fov_default, 1.0, 179.0, "photo_mode::enter"),
        Number("fov_min", "camera", &Config::fov_min, 1.0, 178.0, "photo_mode::set_fov"),
        Number("fov_max", "camera", &Config::fov_max, 2.0, 179.0, "photo_mode::set_fov"),
        Number("fov_step", "camera", &Config::fov_step, 0.5, 10.0, "photo_mode::tick"),
        Bool("ultrawide_unlock", "camera", &Config::ultrawide_unlock, "photo_mode::enter"),
        Bool("takeover_current_camera", "camera", &Config::takeover_current_camera, "photo_mode::enter"),

        // ---- [dof] 景深/对焦（门控 + CineCamera 组件通道；距离 cm，沿用旧版语义）----
        Bool("dof_enabled", "dof", &Config::dof_enabled, "photo_mode::apply_dof_if_needed"),
        Number("dof_focal_distance", "dof", &Config::dof_focal_distance, tuning::kDofFocalMinCm,
               tuning::kDofFocalMaxCm, "photo_mode::apply_dof_if_needed"),
        Number("dof_fstop", "dof", &Config::dof_fstop, tuning::kDofFstopMin, tuning::kDofFstopMax,
               "photo_mode::apply_dof_if_needed"),
        Number("dof_sensor_width", "dof", &Config::dof_sensor_width, tuning::kDofSensorMinMm,
               tuning::kDofSensorMaxMm, "photo_mode::apply_dof_if_needed"),
        Number("dof_focus_step", "dof", &Config::dof_focus_step, tuning::kDofFocusStepMinCm,
               tuning::kDofFocusStepMaxCm, "photo_mode::tick（最小步进）"),
        Number("dof_aperture_step", "dof", &Config::dof_aperture_step, tuning::kDofApertureStepMin,
               tuning::kDofApertureStepMax, "photo_mode::tick"),
        Deprecated("dof_nlp_enable",
                   "已并入景深总开关（2026-11-08：NLP 档随总开关自动请求/释放，取证在 tuning.h）"),
        Bool("dof_bokeh_gate", "dof", &Config::dof_bokeh_gate, "ue4ss_port_dof::ensure_dof_gate"),

        // ---- [look] 画面效果（后处理体积通道：本作 gameplay 视图里暗角是实测生效的）----
        // 每项 = 独立开关 + 数值滑块；每次进入摄影模式时 reset_look_defaults() 会把五项恢复默认。
        Number("vignette", "look", &Config::vignette, tuning::kVignetteMin, tuning::kVignetteMax,
               "photo_mode::enter+tick"),
        Bool("vignette_enabled", "look", &Config::vignette_enabled, "photo_mode::enter+tick"),
        Number("saturation", "look", &Config::saturation, 0.0, 2.0, "photo_mode::enter+tick"),
        Bool("saturation_enabled", "look", &Config::saturation_enabled, "photo_mode::enter+tick"),
        Number("contrast", "look", &Config::contrast, 0.0, 2.0, "photo_mode::enter+tick"),
        Bool("contrast_enabled", "look", &Config::contrast_enabled, "photo_mode::enter+tick"),
        Number("bloom", "look", &Config::bloom, 0.0, 20.0, "photo_mode::enter+tick"),
        Bool("bloom_enabled", "look", &Config::bloom_enabled, "photo_mode::enter+tick"),
        Number("temperature", "look", &Config::temperature, -1.0, 1.0, "photo_mode::enter+tick"),
        Bool("temperature_enabled", "look", &Config::temperature_enabled, "photo_mode::enter+tick"),
        // 调色·进阶三件（2026-11-08 批次）：无独立开关，值回中性（= 默认）即不覆盖；范围
        // 共同真源在 tuning.h（菜单 schema_map 的滑条范围也引用同一批常量）。同批十项判死
        // 移除（取证在 tuning.h 顶部）。
        Number("color_gain", "look", &Config::color_gain, tuning::kColorGainMin, tuning::kColorGainMax,
               "photo_mode::enter+tick"),
        Number("color_gamma", "look", &Config::color_gamma, tuning::kColorGammaMin, tuning::kColorGammaMax,
               "photo_mode::enter+tick"),
        Number("color_offset", "look", &Config::color_offset, tuning::kColorOffsetMin, tuning::kColorOffsetMax,
               "photo_mode::enter+tick"),
        // 色差（2.1 复活）：值走门控 + CineCamera 组件通道（DofPort），不走体积通道
        Number("chromatic_aberration", "look", &Config::chromatic_aberration, 0.0, tuning::kChromaticAberrationMax,
               "photo_mode::apply_dof_if_needed"),

        // ---- [effects] ----
        Bool("hide_player", "effects", &Config::hide_player, "photo_mode"),
        Bool("hide_game_hud", "effects", &Config::hide_game_hud, "photo_mode"),
        Bool("hide_interact_prompt", "effects", &Config::hide_interact_prompt, "photo_mode"),
        Bool("hide_enemy_health_bars", "effects", &Config::hide_enemy_health_bars, "photo_mode"),
        Bool("hide_lockon_marker", "effects", &Config::hide_lockon_marker, "photo_mode"),
        Bool("hide_subtitles", "effects", &Config::hide_subtitles, "photo_mode"),
        Bool("repair_ui_settings_once", "effects", &Config::repair_ui_settings_once, "mod_app::enter"),
        Bool("auto_pause_time", "effects", &Config::auto_pause_time, "photo_mode::enter"),
        Enum("freeze_mode", "effects", &apply_freeze_mode, "photo_mode::toggle_freeze"),
        Number("freeze_dilation", "effects", &Config::freeze_dilation, 1e-9, 1.0, "photo_mode::toggle_freeze"),
        Bool("freeze_sequences", "effects", &Config::freeze_sequences, "photo_mode::toggle_freeze"),
        Bool("freeze_dialogue_flow", "effects", &Config::freeze_dialogue_flow, "photo_mode::toggle_freeze"),
        Bool("stop_wwise_on_freeze", "effects", &Config::stop_wwise_on_freeze, "photo_mode::toggle_freeze"),
        Bool("wwise_engine_suspend", "effects", &Config::wwise_engine_suspend, "photo_mode::toggle_freeze"),
        Bool("stop_camera_shakes_on_freeze", "effects", &Config::stop_camera_shakes_on_freeze,
             "photo_mode::toggle_freeze"),
        Bool("freeze_wind", "effects", &Config::freeze_wind, "photo_mode::toggle_freeze"),
        Bool("freeze_mpc", "effects", &Config::freeze_mpc, "photo_mode::toggle_freeze"),
        Bool("freeze_cloth", "effects", &Config::freeze_cloth, "photo_mode::toggle_freeze"),
        Bool("freeze_custom_cloth", "effects", &Config::freeze_custom_cloth, "photo_mode::toggle_freeze"),
        Bool("freeze_wpo", "effects", &Config::freeze_wpo, "photo_mode::toggle_freeze"),
        Bool("freeze_tonemapper_dither_off", "effects", &Config::freeze_tonemapper_dither_off,
             "photo_mode::toggle_freeze"),
        Bool("freeze_skeletal_animation", "effects", &Config::freeze_skeletal_animation, "photo_mode::toggle_freeze"),
        Bool("freeze_motion_blur_off", "effects", &Config::freeze_motion_blur_off, "photo_mode::toggle_freeze"),
        Bool("freeze_aa_off", "effects", &Config::freeze_aa_off, "photo_mode::toggle_freeze"),
        Bool("freeze_wpo_velocity_off", "effects", &Config::freeze_wpo_velocity_off, "photo_mode::toggle_freeze"),
        Bool("freeze_framegen_off", "effects", &Config::freeze_framegen_off, "photo_mode::toggle_freeze"),
        Bool("freeze_dlss_off", "effects", &Config::freeze_dlss_off, "photo_mode::toggle_freeze"),
        Bool("freeze_lumen_screen_traces_off", "effects", &Config::freeze_lumen_screen_traces_off,
             "photo_mode::toggle_freeze"),
        Bool("freeze_lumen_short_range_ao_off", "effects", &Config::freeze_lumen_short_range_ao_off,
             "photo_mode::toggle_freeze"),
        Bool("freeze_lumen_radiance_cache_off", "effects", &Config::freeze_lumen_radiance_cache_off,
             "photo_mode::toggle_freeze"),
        Bool("freeze_lumen_fixed_jitter", "effects", &Config::freeze_lumen_fixed_jitter, "photo_mode::toggle_freeze"),
        Bool("freeze_lumen_off", "effects", &Config::freeze_lumen_off, "photo_mode::toggle_freeze"),
        Bool("freeze_lighting_ab_cycle", "effects", &Config::freeze_lighting_ab_cycle, "photo_mode::toggle_freeze"),
        Number("freeze_lighting_ab_phase_seconds", "effects", &Config::freeze_lighting_ab_phase_seconds, 1.0, 120.0,
               "photo_mode::tick"),
        Bool("freeze_shadow_ab_cycle", "effects", &Config::freeze_shadow_ab_cycle, "photo_mode::toggle_freeze"),
        Number("freeze_shadow_ab_phase_seconds", "effects", &Config::freeze_shadow_ab_phase_seconds, 1.0, 120.0,
               "photo_mode::tick"),
        Bool("freeze_diag", "effects", &Config::freeze_diag, "photo_mode::tick"),
        // ---- F7 画面定格（全线程冻结）----
        Bool("frame_freeze_time_compensation", "effects", &Config::frame_freeze_time_compensation,
             "mod_app::frame_freeze"),
        Bool("frame_freeze_audio_suspend", "effects", &Config::frame_freeze_audio_suspend,
             "mod_app::frame_freeze"),
        Number("frame_freeze_timeout_seconds", "effects", &Config::frame_freeze_timeout_seconds, 5.0, 300.0,
               "mod_app::frame_freeze"),

        // ---- [ui] ----
        Bool("imgui_tab", "ui", &Config::imgui_tab, "mod_app::on_ui_init"),
        Enum("menu_language", "ui", &apply_menu_language, "pbzfm_client_lang"),

        // ---- [misc] ----
        Enum("log_level", "misc", &apply_log_level, "ue4ss_port::log"),

        // ---- 弃用：接受但忽略，每个键只告警一次 ----
        Deprecated("interval_ms", "已移除并忽略（旧版 ImGui 面板轮询间隔）"),
        Deprecated("ui_scale", "已移除并忽略（旧版 ImGui 面板缩放）"),
        Deprecated("time_offset_compensation", "已在 1.0.66 移除并忽略（冻结渲染线程计时会导致移动视角卡顿）"),
        // 调色·进阶十项判死（2026-11-08 实机探针：字段不存在或写入成功引擎不消费，取证在 tuning.h）
        Deprecated("highlights", "已判死移除（本 build 无此字段），取证在 tuning.h"),
        Deprecated("midtones", "已判死移除（本 build 无此字段），取证在 tuning.h"),
        Deprecated("shadows", "已判死移除（本 build 无此字段），取证在 tuning.h"),
        Deprecated("film_slope", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("film_toe", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("film_shoulder", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("white_temp", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("white_tint", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("bloom_threshold", "已判死移除（写入成功但引擎不消费），取证在 tuning.h"),
        Deprecated("bloom_soft_knee", "已判死移除（本 build 无此字段），取证在 tuning.h"),
        // 锐化/超采样整线判死（2026-11-08 三轮实机：写入通道无效 / 写入有效但画面无增益，取证在 tuning.h）
        Deprecated("sharpness", "已判死移除（写入通道无效），取证在 tuning.h"),
        Deprecated("screen_percentage", "已判死移除（写入有效但画面无增益），取证在 tuning.h"),
        // 胶片颗粒（hotfix33 判死）：色差 2.1 轮复活后它仍是死的——两个 pass 都被 r.Tonemapper.Quality
        // 档位整段跳过，复活只回了色差。判死时期它列在 legacy 清单里物理清除，清单删除后补登记，
        // 否则旧配置里的残留键只打通用的「未知配置项」警告。
        Deprecated("film_grain", "已判死移除（胶片颗粒 pass 被引擎档位跳过，2.1 复活只回了色差），取证在 tuning.h"),
    };
    return table;
}

const ConfigFieldSpec* find_spec(const std::string& key) {
    for (const auto& spec : schema()) {
        if (key == spec.key) return &spec;
    }
    return nullptr;
}

}  // namespace

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "error";
        case LogLevel::Warn: return "warn";
        case LogLevel::Info: return "info";
        case LogLevel::Debug: return "debug";
    }
    return "info";
}

const char* mouse_vertical_mode_name(MouseVerticalMode mode) {
    return mode == MouseVerticalMode::Look ? "look" : "translate";
}

bool reset_look_defaults(Config& cfg) {
    const Config defaults{};
    bool changed = false;
    const auto set_value = [&changed](double& dst, double def) {
        if (dst != def) {
            dst = def;
            changed = true;
        }
    };
    const auto set_flag = [&changed](bool& dst, bool def) {
        if (dst != def) {
            dst = def;
            changed = true;
        }
    };
    set_value(cfg.vignette, defaults.vignette);
    set_value(cfg.saturation, defaults.saturation);
    set_value(cfg.contrast, defaults.contrast);
    set_value(cfg.bloom, defaults.bloom);
    set_value(cfg.temperature, defaults.temperature);
    set_value(cfg.color_gain, defaults.color_gain);
    set_value(cfg.color_gamma, defaults.color_gamma);
    set_value(cfg.color_offset, defaults.color_offset);
    set_value(cfg.chromatic_aberration, defaults.chromatic_aberration);
    set_flag(cfg.vignette_enabled, defaults.vignette_enabled);
    set_flag(cfg.saturation_enabled, defaults.saturation_enabled);
    set_flag(cfg.contrast_enabled, defaults.contrast_enabled);
    set_flag(cfg.bloom_enabled, defaults.bloom_enabled);
    set_flag(cfg.temperature_enabled, defaults.temperature_enabled);
    return changed;
}

ConfigLoadResult parse_config(const std::string& ini_text, const char* source) {
    ConfigLoadResult result;
    result.source = source ? source : "config.ini";

    std::set<std::string> warned;  // 同一键 / 同一 section / 同一行只告警一次，避免刷屏
    auto warn_once = [&result, &warned](const std::string& tag, const std::string& message) {
        if (warned.insert(tag).second) result.warnings.push_back(message);
    };

    // section 白名单：未知 section 报一次，但其中的键仍按已知键名解析（容错，不静默）
    static const std::set<std::string> kKnownSections = {"keys", "control", "gamepad", "camera", "dof", "look",
                                                         "effects", "ui", "misc"};
    std::unordered_map<std::string, std::string> seen_keys;  // 键名 -> 首次出现的 section

    // 旧版判死时期（hotfix32/34）曾把 [dof] 段与色差/颗粒键列进 legacy 清单物理清除；
    // 2.1 轮功能复活后这些键名重新成为正式键，legacy 机制保留（ConfigLoadResult.legacy_keys
    // 与 mod_app 的原子重写路径），当前没有需要清除的键。

    // 按 schema 统一赋值：类型错误保留默认，数值超范围 clamp 到边界，弃用键只告警一次。
    const auto assign = [&result, &warn_once, &seen_keys](const std::string& key, const std::string& value,
                                                          const std::string& section) {
        const ConfigFieldSpec* spec = find_spec(key);
        if (spec == nullptr) {
            warn_once(key, "未知配置项已忽略：" + key);
            return;
        }

        if (!section.empty() && spec->section[0] != '\0' && section != spec->section) {
            warn_once(key, "配置项 " + key + " 位于 [" + section + "]，按 [" + std::string(spec->section) +
                               "] 的键解析（section 可能写错）");
        }

        const auto found = seen_keys.find(key);
        if (found == seen_keys.end()) {
            seen_keys.emplace(key, section);
        } else if (!section.empty() && !found->second.empty() && section != found->second) {
            warn_once(key, "配置项 " + key + " 同时出现在 [" + found->second + "] 与 [" + section +
                               "]，以最后一次为准");
            found->second = section;
        }

        switch (spec->kind) {
            case FieldKind::Deprecated:
                warn_once(key, "配置项 " + key + " 已弃用：" + std::string(spec->note ? spec->note : "已忽略"));
                return;
            case FieldKind::Text:
                if (value.empty()) {
                    warn_once(key, "值为空：" + key + "（保留默认）");
                    return;
                }
                result.cfg.*spec->text = value;
                return;
            case FieldKind::Bool: {
                bool v{};
                if (!parse_bool(value, v)) {
                    warn_once(key, "值不是合法布尔：" + key + " = " + value + "（保留默认）");
                    return;
                }
                result.cfg.*spec->boolean = v;
                return;
            }
            case FieldKind::Enum:
                if (spec->apply_enum == nullptr || !spec->apply_enum(result.cfg, value)) {
                    warn_once(key, "取值非法：" + key + " = " + value + "（保留默认）");
                }
                return;
            case FieldKind::Number: {
                double v{};
                if (!parse_double(value, v)) {
                    warn_once(key, "值不是合法数字：" + key + " = " + value + "（保留默认）");
                    return;
                }
                if (v < spec->min || v > spec->max) {
                    const double clamped = std::clamp(v, spec->min, spec->max);
                    warn_once(key, "值超出范围：" + key + " = " + value + "，已夹紧到 " + num_to_text(clamped) +
                                       "（允许 [" + num_to_text(spec->min) + ", " + num_to_text(spec->max) + "]）");
                    v = clamped;
                }
                result.cfg.*spec->number = v;
                return;
            }
        }
    };

    std::string section;
    std::istringstream stream(ini_text);
    std::string line;
    size_t line_no = 0;
    bool first = true;
    while (std::getline(stream, line)) {
        ++line_no;
        if (first) {
            first = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);  // UTF-8 BOM
            }
        }
        const std::string cleaned = strip_comment(line);
        if (cleaned.empty()) continue;
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = lower(trim(std::string_view(cleaned).substr(1, cleaned.size() - 2)));
            if (!section.empty() && kKnownSections.count(section) == 0) {
                warn_once("section:" + section, "未知 section [" + section + "]，其键仍按已知键名解析");
            }
            continue;
        }
        const size_t eq = cleaned.find('=');
        if (eq == std::string::npos) {
            warn_once("line:" + std::to_string(line_no),
                      "第 " + std::to_string(line_no) + " 行不是 key=value，已忽略");
            continue;
        }
        const std::string key = lower(trim(std::string_view(cleaned).substr(0, eq)));
        const std::string value = unquote(trim(std::string_view(cleaned).substr(eq + 1)));
        if (key.empty()) {
            warn_once("line:" + std::to_string(line_no),
                      "第 " + std::to_string(line_no) + " 行键名为空，已忽略");
            continue;
        }
        assign(key, value, section);
    }

    // 交叉校验：单独 clamp 管不到的键间约束
    if (result.cfg.fov_max <= result.cfg.fov_min) {
        warn_once("fov_max", "fov_max 必须大于 fov_min，已回退默认 15/150");
        result.cfg.fov_min = 15.0;
        result.cfg.fov_max = 150.0;
    }
    if (result.cfg.fov_default < result.cfg.fov_min || result.cfg.fov_default > result.cfg.fov_max) {
        warn_once("fov_default", "fov_default 超出 [fov_min, fov_max]，已夹紧");
        result.cfg.fov_default = std::clamp(result.cfg.fov_default, result.cfg.fov_min, result.cfg.fov_max);
    }
    return result;
}

ConfigLoadResult load_config_file(const std::string& path) {
    std::error_code exists_ec;
    const bool exists = std::filesystem::exists(path, exists_ec);
    if (exists_ec || !exists) {
        ConfigLoadResult result;
        result.ok = false;
        // 文件层失败要能区分：不存在（首次运行，正常）vs 存在但读不了（权限/占用/路径是目录）
        result.file_missing = !exists_ec;
        result.read_error = exists_ec != std::error_code{};
        result.source = exists_ec ? ("读取失败（使用内置默认值）：" + path)
                                  : ("缺省默认值（文件不存在：" + path + "）");
        result.warnings.push_back(exists_ec ? "无法访问配置文件路径，使用内置默认值：" + path
                                            : "未找到配置文件，使用内置默认值：" + path);
        return result;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ConfigLoadResult result;
        result.ok = false;
        result.read_error = true;
        result.source = "读取失败（使用内置默认值）：" + path;
        result.warnings.push_back("配置文件存在但无法读取，使用内置默认值：" + path);
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
        ConfigLoadResult result;
        result.ok = false;
        result.read_error = true;
        result.source = "读取失败（使用内置默认值）：" + path;
        result.warnings.push_back("配置文件读取中断，使用内置默认值：" + path);
        return result;
    }
    return parse_config(buffer.str(), path.c_str());
}

std::string config_ini_text(const Config& c) {
    std::ostringstream out;
    out << "; ================================================================\n"
        << "; " << PBZPM_VERSION_FULL << " 配置文件（UTF-8，可留空使用默认值）\n"
        << "; 改完存盘后按 F2 重进摄影模式即可生效（或重启游戏）\n"
        << "; ================================================================\n\n"
        << "[keys]\n"
        << "; 进入/退出摄影模式\n"
        << "key_toggle = " << c.key_toggle << "\n"
        << "; 冻结/恢复时间\n"
        << "key_freeze = " << c.key_freeze << "\n"
        << "; 画面定格/恢复（挂起本进程除热键线程外的所有线程，屏幕停在最后一帧；再按恢复）\n"
        << "key_frame_freeze = " << c.key_frame_freeze << "\n"
        << "; 重置相机到进入摄影模式时位置/朝向/FOV\n"
        << "key_reset = " << c.key_reset << "\n"
        << "; 滚转归零（水平校正）：只把滚转回 0，位置/俯仰/偏航/FOV 不动\n"
        << "key_roll_reset = " << c.key_roll_reset << "\n"
        << "; FOV 加减（小键盘 +/-，也可写 OEM_PLUS / OEM_MINUS）\n"
        << "key_fov_up = " << c.key_fov_up << "\n"
        << "key_fov_down = " << c.key_fov_down << "\n"
        << "; 俯仰（按住持续转）：方向键上 = 仰视 / 下 = 俯视\n"
        << "key_pitch_up = " << c.key_pitch_up << "\n"
        << "key_pitch_down = " << c.key_pitch_down << "\n"
        << "; 左右转视角（按住持续转，速度 = yaw_speed）：方向键左 = 左转 / 右 = 右转\n"
        << "key_yaw_left = " << c.key_yaw_left << "\n"
        << "key_yaw_right = " << c.key_yaw_right << "\n"
        << "; 滚转（按住持续转，速度 = roll_speed）：默认 Q 逆时针 / E 顺时针\n"
        << "key_roll_left = " << c.key_roll_left << "\n"
        << "key_roll_right = " << c.key_roll_right << "\n"
        << "; 升降（translate=相机上下平移，look=俯仰）：默认鼠标左键上/右键下\n"
        << "; 可写键盘键名（PAGE_UP/PAGE_DOWN 等）或鼠标键 MOUSE_LEFT/MOUSE_RIGHT/MOUSE_MIDDLE/MOUSE_X1/MOUSE_X2\n"
        << "key_vertical_up = " << c.key_vertical_up << "\n"
        << "key_vertical_down = " << c.key_vertical_down << "\n"
        << "; 移动/变速/自由视角门控（2.0 起可配）：默认 W/S/A/D 平移、SHIFT 加速、CONTROL 减速、ALT 转视角门控\n"
        << "key_move_forward = " << c.key_move_forward << "\n"
        << "key_move_back = " << c.key_move_back << "\n"
        << "key_move_left = " << c.key_move_left << "\n"
        << "key_move_right = " << c.key_move_right << "\n"
        << "key_boost = " << c.key_boost << "\n"
        << "key_slow = " << c.key_slow << "\n"
        << "key_free_look = " << c.key_free_look << "\n"
        << "; 景深总开关（与 [dof] dof_enabled 同一状态；门控 + CineCamera 组件通道）\n"
        << "key_dof_toggle = " << c.key_dof_toggle << "\n"
        << "; 对焦拉近/拉远（实际步进 = max(dof_focus_step, 当前距离×10%)，Shift 粗调 / Ctrl 微调）\n"
        << "key_focus_near = " << c.key_focus_near << "\n"
        << "key_focus_far = " << c.key_focus_far << "\n"
        << "; [ 开大光圈（F 值减小、更虚化）/ ] 收小光圈（更实）\n"
        << "key_aperture_open = " << c.key_aperture_open << "\n"
        << "key_aperture_close = " << c.key_aperture_close << "\n"
        << "[control]\n"
        << "; 常规移动速度（单位/秒）；按住 key_boost 加速、key_slow 减速\n"
        << "move_speed = " << c.move_speed << "\n"
        << "boost_mult = " << c.boost_mult << "\n"
        << "slow_mult = " << c.slow_mult << "\n"
        << "; 按住减速键（默认 Ctrl；手柄为 pad_slow，默认 RS）时鼠标/右摇杆视角速度的倍率，越小视角越精细\n"
        << "slow_look_mult = " << c.slow_look_mult << "\n"
        << "; 每单位鼠标位移转多少度\n"
        << "look_sensitivity = " << c.look_sensitivity << "\n"
        << "pitch_limit = " << c.pitch_limit << "\n"
        << "invert_y = " << (c.invert_y ? "true" : "false") << "\n"
        << "; 自由转视角必须按住 key_free_look（默认 ALT）才能动鼠标；false = 不按键也能转\n"
        << "free_look_requires_alt = " << (c.free_look_requires_alt ? "true" : "false") << "\n"
        << "; 进入摄影模式时屏蔽角色移动/视角输入（退出时还原）\n"
        << "block_game_input = " << (c.block_game_input ? "true" : "false") << "\n"
        << "; 升降键行为（具体键位见 [keys] 的 key_vertical_up/down）：translate=相机上下平移，look=俯仰\n"
        << "mouse_vertical_mode = " << mouse_vertical_mode_name(c.mouse_vertical_mode) << "\n"
        << "; 鼠标滚轮调整 FOV（默认向上缩小/拉近、向下放大）；false = 滚轮不参与 FOV\n"
        << "wheel_fov = " << (c.wheel_fov ? "true" : "false") << "\n"
        << "; 滚轮方向反转（true = 向上放大 FOV、向下缩小）\n"
        << "wheel_fov_invert = " << (c.wheel_fov_invert ? "true" : "false") << "\n"
        << "vertical_speed = " << c.vertical_speed << "\n"
        << "; 滚转相机速度（度/秒）：默认 E 顺时针，Q 逆时针（键位见 [keys] key_roll_left/right）；Shift 加速 / Ctrl 减速同样生效\n"
        << "roll_speed = " << c.roll_speed << "\n"
        << "; 方向键上/下（与手柄十字键上/下）俯仰速度（度/秒）；Shift 加速 / Ctrl 减速同样生效\n"
        << "pitch_speed = " << c.pitch_speed << "\n"
        << "; 方向键左/右（与手柄十字键左/右）转向速度（度/秒）；Shift 加速 / Ctrl 减速同样生效\n"
        << "yaw_speed = " << c.yaw_speed << "\n\n"
        << "[gamepad]\n"
        << "; 手柄支持总开关（false=不轮询手柄）\n"
        << "pad_enabled = " << (c.pad_enabled ? "true" : "false") << "\n"
        << "; 进入/退出摄影模式：默认左右摇杆同时按下（LS=左摇杆按下 L3，RS=右摇杆按下 R3）\n"
        << "; 可用按钮：A B X Y LB RB LS RS BACK START GUIDE UP DOWN LEFT RIGHT LT RT\n"
        << "; 也认 PS 叫法（CROSS/CIRCLE/SQUARE/TRIANGLE/L1/R1/L2/R2/L3/R3/OPTIONS/SHARE/PS）\n"
        << "; 多个按钮用 + 连接（同时按住触发）；写 NONE 表示解绑该功能\n"
        << "pad_toggle = " << c.pad_toggle << "\n"
        << "; 冻结/恢复时间\n"
        << "pad_freeze = " << c.pad_freeze << "\n"
        << "; 重置相机到进入摄影模式时位置/朝向/FOV\n"
        << "pad_reset = " << c.pad_reset << "\n"
        << "; 滚转归零（水平校正）：只把滚转回 0；LB+RB 同时按住触发\n"
        << "pad_roll_reset = " << c.pad_roll_reset << "\n"
        << "; FOV 加减（A/B；十字键已全部让位给视角调节；单击一格，按住先延迟约 0.35 秒再连发）\n"
        << "pad_fov_up = " << c.pad_fov_up << "\n"
        << "pad_fov_down = " << c.pad_fov_down << "\n"
        << "; 俯仰（按住持续转）：十字键上 = 仰视 / 下 = 俯视\n"
        << "pad_pitch_up = " << c.pad_pitch_up << "\n"
        << "pad_pitch_down = " << c.pad_pitch_down << "\n"
        << "; 左右转视角（按住持续转）：十字键左 = 左转 / 右 = 右转\n"
        << "pad_yaw_left = " << c.pad_yaw_left << "\n"
        << "pad_yaw_right = " << c.pad_yaw_right << "\n"
        << "; 滚转（按住持续转）：默认 RB 顺时针 / LB 逆时针\n"
        << "pad_roll_left = " << c.pad_roll_left << "\n"
        << "pad_roll_right = " << c.pad_roll_right << "\n"
        << "; 变速：默认 LS（左摇杆按下）加速 / RS（右摇杆按下）减速；两键同按是进出模式组合键，不触发变速\n"
        << "pad_boost = " << c.pad_boost << "\n"
        << "pad_slow = " << c.pad_slow << "\n"
        << "; 升降：默认 RT 上 / LT 下；单键绑定 RT/LT 保留模拟量行程，换其它按钮则为按下/松开\n"
        << "pad_vertical_up = " << c.pad_vertical_up << "\n"
        << "pad_vertical_down = " << c.pad_vertical_down << "\n"
        << "; 景深/对焦（沿用旧版默认；LB/RB 单按默认是滚转，组合期间会连带滚转，可改键避开）：\n"
        << "; LB+BACK 对焦拉近 / LB+START 对焦拉远；RB+BACK 开大光圈 / RB+START 收小光圈\n"
        << "pad_focus_near = " << c.pad_focus_near << "\n"
        << "pad_focus_far = " << c.pad_focus_far << "\n"
        << "pad_aperture_open = " << c.pad_aperture_open << "\n"
        << "pad_aperture_close = " << c.pad_aperture_close << "\n"
        << "pad_dof_toggle = " << c.pad_dof_toggle << "\n"
        << "; 右摇杆转视角速度（度/秒，全行程）\n"
        << "pad_look_speed = " << c.pad_look_speed << "\n"
        << "; 左摇杆移动速度占 move_speed 的比例\n"
        << "pad_move_scale = " << c.pad_move_scale << "\n"
        << "; 摇杆死区（0~0.5，摇杆有漂移就调大）\n"
        << "pad_deadzone = " << c.pad_deadzone << "\n"
        << "; 景深/对焦：LB+BACK 对焦拉近 / LB+START 对焦拉远；RB+BACK 开大光圈 / RB+START 收小光圈\n"
        << "[camera]\n"
        << "fov_default = " << c.fov_default << "\n"
        << "fov_min = " << c.fov_min << "\n"
        << "fov_max = " << c.fov_max << "\n"
        << "fov_step = " << c.fov_step << "\n"
        << "; 超宽屏（21:9/32:9）：进入摄影模式时解除游戏 16:9 画幅锁定（消除两侧黑边），退出还原\n"
        << "ultrawide_unlock = " << (c.ultrawide_unlock ? "true" : "false") << "\n"
        << "; 接管当前相机（UUU 式）：true=不 spawn 独立相机、不切 ViewTarget，每帧写当前相机 POV；\n"
        << "; 进入时接管失败会自动回退到独立相机（spawn + SetViewTargetWithBlend）。false=始终用独立相机\n"
        << "takeover_current_camera = " << (c.takeover_current_camera ? "true" : "false") << "\n\n"
        << "[dof]\n"
        << "; 景深/对焦（2.1 复活）：走「渲染门控（NLP 过场渲染档 + tagged cvar）+ CineCamera 组件通道」。\n"
        << "; 启用任一项（含 [look] 的色差）会自动切到独立相机后端并打开门控；退出全部还原。\n"
        << "; 距离单位 cm。旧 config.ini 里若还留着这些键（判死时期被清除过），会按原值继续生效。\n"
        << "; 景深总开关（也可用 [keys] key_dof_toggle 在模式内随时开关）\n"
        << "dof_enabled = " << (c.dof_enabled ? "true" : "false") << "\n"
        << "; 对焦距离（cm）\n"
        << "dof_focal_distance = " << c.dof_focal_distance << "\n"
        << "; 光圈 F 值（越小越虚化）\n"
        << "dof_fstop = " << c.dof_fstop << "\n"
        << "; 传感器宽度（mm，影响虚化幅度）\n"
        << "dof_sensor_width = " << c.dof_sensor_width << "\n"
        << "; 对焦步进（cm）：最小步进，实际步进 = max(本值, 当前距离×10%)\n"
        << "dof_focus_step = " << c.dof_focus_step << "\n"
        << "; 光圈步进（F 值每次增/减多少）\n"
        << "dof_aperture_step = " << c.dof_aperture_step << "\n"
        << "; bokeh gate cvar 取证开关（默认 false）：true 才写 r.DOF.{Gather,Recombine}.EnableBokehSettings /\n"
        << "; Recombine.Quality——2026-11-07 崩溃二分首位嫌疑（shipping 可能无 bokeh 实现）；\n"
        << "; false 时 DOF 仍走 ShowFlag/画质/kernel 半径闸门+组件通道，散景形状退化为默认。\n"
        << "dof_bokeh_gate = " << (c.dof_bokeh_gate ? "true" : "false") << "\n\n"
        << "[look]\n"
        << "; 画面效果五项：每项 = 独立开关 + 数值滑块。\n"
        << "; 开关 false 只停用这一项、保留滑块值；每次进入摄影模式时五项都会恢复默认参数。\n"
        << "; 暗角强度（0=关闭，1~3 明显；走后处理体积通道，本作 gameplay 视图实测有效）\n"
        << "vignette = " << c.vignette << "\n"
        << "vignette_enabled = " << (c.vignette_enabled ? "true" : "false") << "\n\n"
        << "; 饱和度倍率（1=原样，<1 去色，>1 更艳）\n"
        << "saturation = " << c.saturation << "\n"
        << "saturation_enabled = " << (c.saturation_enabled ? "true" : "false") << "\n\n"
        << "; 对比度倍率（1=原样）\n"
        << "contrast = " << c.contrast << "\n"
        << "contrast_enabled = " << (c.contrast_enabled ? "true" : "false") << "\n\n"
        << "; 泛光强度（0=不覆盖，1~5 明显）\n"
        << "bloom = " << c.bloom << "\n"
        << "bloom_enabled = " << (c.bloom_enabled ? "true" : "false") << "\n\n"
        << "; 冷暖色调（-1 偏冷，0=不覆盖，+1 偏暖）\n"
        << "temperature = " << c.temperature << "\n"
        << "temperature_enabled = " << (c.temperature_enabled ? "true" : "false") << "\n\n"
        << "; 调色·进阶三件（与饱和/对比同一颜色分级通道；菜单在「调色·进阶」组）。\n"
        << "; 中性值 = 不覆盖该项（交还游戏自己的设置）；离开中性才写覆盖，进入摄影模式恢复默认。\n"
        << "; 亮度增益（1=原样，0.5 压暗，2.0 提亮）\n"
        << "color_gain = " << c.color_gain << "\n"
        << "; 伽马（1=原样，<1 提亮中间调，>1 压暗中间调）\n"
        << "color_gamma = " << c.color_gamma << "\n"
        << "; 黑位偏移（0=原样，-压黑更深，+提黑发灰；0.1 已明显）\n"
        << "color_offset = " << c.color_offset << "\n"
        << "; 色差（0=不覆盖，1~3 明显；走门控+CineCamera 组件通道，需独立相机后端）\n"
        << "chromatic_aberration = " << c.chromatic_aberration << "\n\n"
        << "[effects]\n"
        << "hide_player = " << (c.hide_player ? "true" : "false") << "\n"
        << "; 进入摄影模式时隐藏游戏 HUD（退出时恢复原可见性）\n"
        << "hide_game_hud = " << (c.hide_game_hud ? "true" : "false") << "\n"
        << "; 进入摄影模式时隐藏交互提示（可互物体旁的 E 键图标，退出时恢复）\n"
        << "hide_interact_prompt = " << (c.hide_interact_prompt ? "true" : "false") << "\n"
        << "; 进入摄影模式时隐藏敌人（怪物）血条（切到游戏自带\"仅锁定目标\"显示模式，退出时还原原模式）\n"
        << "hide_enemy_health_bars = " << (c.hide_enemy_health_bars ? "true" : "false") << "\n"
        << "; 进入摄影模式时隐藏锁定标记（锁定敌人时其身上出现的白点，退出时还原）\n"
        << "hide_lockon_marker = " << (c.hide_lockon_marker ? "true" : "false") << "\n"
        << "; 进入摄影模式时隐藏对话字幕（关游戏字幕设置 + 藏字幕显示控件，退出时还原）\n"
        << "hide_subtitles = " << (c.hide_subtitles ? "true" : "false") << "\n"
        << "; 1.0.69 一次性修复：首次运行本版时把字幕与场景交互提示两个游戏设置强制打开\n"
        << ";（旧版本可能把它们写坏并让游戏存盘；只执行一次，成功后写 ui_settings_repair_done.flag）。\n"
        << "; 如果你本来就关闭字幕、不要这项修复，设为 false。\n"
        << "repair_ui_settings_once = " << (c.repair_ui_settings_once ? "true" : "false") << "\n"
        << "; 进入摄影模式时自动暂停时间（默认开启；false=进入后时间照常流动，可按 F 手动冻结）\n"
        << "auto_pause_time = " << (c.auto_pause_time ? "true" : "false") << "\n"
        << "; dilation=时间几乎停住（画面仍可动）；pause=游戏暂停\n"
        << "freeze_mode = " << (c.freeze_mode == FreezeMode::Pause ? "pause" : "dilation") << "\n"
        << "freeze_dilation = " << c.freeze_dilation << "\n"
        << "; 冻结时同时暂停剧情/过场（Sequencer 播放器），解冻/退出时恢复。\n"
        << "; 剧情系统常绕过世界暂停自顾自播（画面像冻住但动作声音还在走），默认开启\n"
        << "freeze_sequences = " << (c.freeze_sequences ? "true" : "false") << "\n"
        << "; 冻结时把对话 Flow（WBP_FlowDialog_C）改为 Collapsed 停止 Slate Tick，避免对话 Flow\n"
        << "; 在冻结期继续推进/提前结束当前语音；解冻恢复 Hidden（仍隐藏但继续 Tick）。仅在 hide_subtitles=true 时生效\n"
        << "freeze_dialogue_flow = " << (c.freeze_dialogue_flow ? "true" : "false") << "\n"
        << "; 冻结时处理 Wwise 音频：有逐 PlayingID 时逐语音真暂停（PBZPausePlayingID/PBZResumePlayingID），\n"
        << "; 跳过整引擎 Suspend；没有 ID 时才用整引擎静音兜底，失败回落组件音量\n"
        << "stop_wwise_on_freeze = " << (c.stop_wwise_on_freeze ? "true" : "false") << "\n"
        << "; 仅当没有截获到逐个 PlayingID 时，整引擎 Suspend(true,false) 作兜底：\n"
        << "; render-anyway 只静音、音频继续处理；正常有 ID 时不会调用，避免推进语音时间轴\n"
        << "; false=不做整引擎兜底，只走逐语音/组件音量\n"
        << "wwise_engine_suspend = " << (c.wwise_engine_suspend ? "true" : "false") << "\n"
        << "; 冻结时停用相机修饰器并立即结束相机 shake。它们用真实时间驱动，世界暂停拦不住，\n"
        << "; 是冻结画面边缘抖动/脸部阴影爬动的主变化源；解冻时按冻结前状态还原\n"
        << "stop_camera_shakes_on_freeze = " << (c.stop_camera_shakes_on_freeze ? "true" : "false") << "\n"
        << "; 冻结时把风源（WindDirectionalSource）的风强/风速归零——材质顶点摆动（布料/植被）\n"
        << "; 的幅度源，世界暂停与时间缩放都拦不住；解冻还原原值\n"
        << "freeze_wind = " << (c.freeze_wind ? "true" : "false") << "\n"
        << "; 冻结时钉住材质参数集合（MPC）当前值，冻结期每帧写回快照，压制实时驱动参数\n"
        << "; 继续推布料/植被摆动；解冻释放\n"
        << "freeze_mpc = " << (c.freeze_mpc ? "true" : "false") << "\n"
        << "; 冻结时挂起角色/敌人的布料模拟（SkeletalMeshComponent clothing simulation 与\n"
        << "; ChaosClothComponent 独立模拟），保留最后一次模拟姿态；解冻恢复\n"
        << "freeze_cloth = " << (c.freeze_cloth ? "true" : "false") << "\n"
        << "; 冻结时额外处理 PBZ 自定义布料/摆动 + 毛发/粒子：Sway 组件 tick（按类名全对象扫描找）、\n"
        << "; r.Cloth.Enable 全局开关、Groom 毛发模拟、Niagara 粒子暂停。\n"
        << "; 1.0.76 起不再写 pbz.Sway.SkipSkeletalKinematicBoneUpdate（会让布料件整体换位）\n"
        << "freeze_custom_cloth = " << (c.freeze_custom_cloth ? "true" : "false") << "\n"
        << "; 冻结时关闭 WPO 评估（SkeletalMesh + StaticMesh/植被/地形图元），排查材质顶点动画闪烁\n"
        << "freeze_wpo = " << (c.freeze_wpo ? "true" : "false") << "\n"
        << "; 冻结时把 r.Tonemapper.Quality 从 5 降到 4（去掉 GrainJitter 逐帧抖动）并关 Backbuffer 量化抖动\n"
        << "freeze_tonemapper_dither_off = " << (c.freeze_tonemapper_dither_off ? "true" : "false") << "\n"
        << "; 冻结时暂停全部 SkeletalMesh 的动画/骨骼 pose（bPauseAnims/bNoSkeletonUpdate），\n"
        << "; 防止骨骼驱动的衣摆/披风在暂停下继续流动；解冻恢复\n"
        << "freeze_skeletal_animation = " << (c.freeze_skeletal_animation ? "true" : "false") << "\n"
        << "; 冻结时关闭动态模糊（r.MotionBlurQuality=0），消除残留速度缓冲造成的流动拖影；解冻还原\n"
        << "freeze_motion_blur_off = " << (c.freeze_motion_blur_off ? "true" : "false") << "\n"
        << "; 冻结时关 AA：r.AntiAliasingMethod=0，并直接改 PPV/相机 POV 的 FPostProcessSettings\n"
        << "; （AA/动态模糊/Lumen/反射），防止 PPV override 让 cvar 失效；解冻/退出按原值还原\n"
        << "freeze_aa_off = " << (c.freeze_aa_off ? "true" : "false") << "\n"
        << "; 冻结时临时关闭 WPO/Nanite WPO/植被/毛发的速度输出（r.Velocity.EnableVertexDeformation 等）：\n"
        << "; 用于 A/B 判断暂停期假运动向量；解冻/退出按原值还原。默认 false\n"
        << "freeze_wpo_velocity_off = " << (c.freeze_wpo_velocity_off ? "true" : "false") << "\n"
        << "; 冻结时抑制帧生成（r.PBZ.SuppressFrameGen=1 + 显式关 DLSSG/FSR-FI），解冻/退出还原。默认 true：\n"
        << "; 静止拍照不需要插帧，FG 还可能放大/制造闪烁；关 false 可保留插帧\n"
        << "freeze_framegen_off = " << (c.freeze_framegen_off ? "true" : "false") << "\n"
        << "; 冻结时临时关闭 DLSS 超分（r.NGX.DLSS.Enable=0），解冻/退出还原。默认 false；\n"
        << "; 若日志显示 DLSS 开启且闪烁仍在，可临时设 true 做 A/B\n"
        << "freeze_dlss_off = " << (c.freeze_dlss_off ? "true" : "false") << "\n"
        << "; ---- 光照 A/B（Lumen / 屏幕空间光照）：冻结时临时改光照 CVar，解冻/退出按原值还原 ----\n"
        << "; 关 Lumen 屏幕空间追踪（GI/反射的 ScreenTraces 归零，保留距离场追踪）\n"
        << "freeze_lumen_screen_traces_off = " << (c.freeze_lumen_screen_traces_off ? "true" : "false") << "\n"
        << "; 关 Lumen 短距屏幕空间 AO（ShortRangeAO / DiffuseIndirect.SSAO / r.AmbientOcclusionLevels）\n"
        << "freeze_lumen_short_range_ao_off = " << (c.freeze_lumen_short_range_ao_off ? "true" : "false") << "\n"
        << "; 关 Lumen 屏幕探针辐射缓存（逐帧更新、易在细几何上闪）\n"
        << "freeze_lumen_radiance_cache_off = " << (c.freeze_lumen_radiance_cache_off ? "true" : "false") << "\n"
        << "; 固定 Lumen 抖动/状态帧（FixedJitterIndex/FixedStateFrameIndex=0），判断是否逐帧随机采样\n"
        << "freeze_lumen_fixed_jitter = " << (c.freeze_lumen_fixed_jitter ? "true" : "false") << "\n"
        << "; 关 Lumen GI/反射总开关（DiffuseIndirect.Allow / Reflections.Allow / 方法 CVar）\n"
        << "freeze_lumen_off = " << (c.freeze_lumen_off ? "true" : "false") << "\n"
        << "; 冻结期自动轮换光照阶段：原始→屏幕追踪→短距AO→辐射缓存→固定抖动→Lumen全关（诊断用）\n"
        << "freeze_lighting_ab_cycle = " << (c.freeze_lighting_ab_cycle ? "true" : "false") << "\n"
        << "; 自动轮换每阶段持续秒数（1..120）\n"
        << "freeze_lighting_ab_phase_seconds = " << c.freeze_lighting_ab_phase_seconds << "\n"
        << "; ---- 阴影/后处理 A/B：Lumen 之外的第二层（SMRT→VSM 缓存→VSM→全部阴影→接触阴影→体积雾→后处理） ----\n"
        << "; 冻结期自动轮换阴影/后处理阶段（诊断用；与 freeze_lighting_ab_cycle 二选一）\n"
        << "freeze_shadow_ab_cycle = " << (c.freeze_shadow_ab_cycle ? "true" : "false") << "\n"
        << "; 阴影/后处理轮换每阶段持续秒数（1..120）\n"
        << "freeze_shadow_ab_phase_seconds = " << c.freeze_shadow_ab_phase_seconds << "\n"
        << "; 冻结诊断（默认关闭）：风源/MPC 参数变化/相机修饰器状态/视角微动；\n"
        << "; 开启后状态翻转时输出，持续期每 10 秒最多一条（诊断需反射扫描，频繁输出会拖帧）\n"
        << "freeze_diag = " << (c.freeze_diag ? "true" : "false") << "\n"
        << "; ---- F7 画面定格（全线程冻结）：参考 PBZ Pause 的“进程级硬冻结 + 时间 API 补偿” ----\n"
        << "; 定格期间冻结 QueryPerformanceCounter/GetTickCount/GetTickCount64/timeGetTime，\n"
        << "; 恢复时把定格时长从返回值里扣掉，避免动画/物理/计时器把整段定格当成一次巨大 delta。\n"
        << "; 注意：只适用于 F7 这种连渲染线程一起停的定格；摄影模式的引擎暂停不会用这套钩子。\n"
        << "frame_freeze_time_compensation = " << (c.frame_freeze_time_compensation ? "true" : "false") << "\n"
        << "; F7 定格前若摄影模式没挂 Wwise，则由 F7 调用 AK::SoundEngine::Suspend(true,false) 静音，恢复时唤醒\n"
        << "frame_freeze_audio_suspend = " << (c.frame_freeze_audio_suspend ? "true" : "false") << "\n"
        << "; F7 定格安全阀（秒）：即使恢复热键收不到，也会在超时后自动恢复，避免只能结束进程（5~300）\n"
        << "frame_freeze_timeout_seconds = " << c.frame_freeze_timeout_seconds << "\n\n"
        << "[ui]\n"
        << "; UE4SS 控制台里的只读诊断面板（需要 UE4SS 控制台窗口）；false=不注册该 tab\n"
        << "; 游戏内无菜单，纯快捷键操作；可写设置的唯一入口是 PBZFM 菜单页\n"
        << "imgui_tab = " << (c.imgui_tab ? "true" : "false") << "\n"
        << "; PBZFM 菜单界面语言：auto=跟随 PBZFM 菜单；或词典 id\n"
        << ";（内置 zh-CN/en-US/ja-JP/ko-KR，或 languages/*.json 的文件名）\n"
        << "menu_language = " << c.menu_language << "\n\n"
        << "[misc]\n"
        << "log_level = " << log_level_name(c.log_level) << "\n";
    return out.str();
}

std::string default_config_text() {
    return config_ini_text(Config{});
}

bool save_config_file(const std::string& path, const Config& cfg, std::string* error) {
    if (path.empty()) {
        if (error) *error = "配置文件路径为空";
        return false;
    }
    // 原子写：先写 <path>.tmp，成功后再 rename 覆盖目标。引擎 tick / 菜单两个线程读盘时
    // 要么读到旧文件、要么读到完整的新文件，不会读到半写的中间态。
    const std::string tmp = path + ".tmp";
    const std::string text = config_ini_text(cfg);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "无法创建临时文件：" + tmp;
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code remove_ec;
            std::filesystem::remove(tmp, remove_ec);
            if (error) *error = "写入临时文件失败：" + tmp;
            return false;
        }
    }

    std::error_code rename_ec;
    std::filesystem::rename(tmp, path, rename_ec);
    if (rename_ec) {
        std::error_code remove_ec;
        std::filesystem::remove(tmp, remove_ec);
        if (error) *error = "原子替换失败：" + path + "（" + rename_ec.message() + "）";
        return false;
    }
    return true;
}

}  // namespace pbzpm
