#include "pbzfm_schema_map.h"

#include "core/tuning.h"

// 本表的 min/max/step 与 src/core/config.cpp 的 schema() 是两份真源（菜单侧与解析侧各一份），
// 改范围必须两边同步（配置解析与菜单共用 core/tuning.h 里的常量，不各写一份）。
namespace pbzpm {
namespace {

// 枚举选项文本（存储值为下标，与枚举定义顺序一致）
const char* const kMouseVertOptions[] = {
    "#mouse_vert_translate",
    "#mouse_vert_look",
};
const char* const kFreezeModeOptions[] = {
    "#freeze_mode_timescale",
    "#freeze_mode_pause",
};

PbzfmField Bool(const char* group, const char* key, const char* label,
                bool (*get)(const Config&), void (*set)(Config&, bool)) {
    PbzfmField f;
    f.kind = PbzfmFieldKind::Bool;
    f.group = group;
    f.key = key;
    f.label = label;
    f.get_bool = get;
    f.set_bool = set;
    return f;
}

PbzfmField Float(const char* group, const char* key, const char* label, double mn, double mx, double step,
                 const char* fmt, double (*get)(const Config&), void (*set)(Config&, double),
                 bool logarithmic = false) {
    PbzfmField f;
    f.kind = PbzfmFieldKind::Float;
    f.group = group;
    f.key = key;
    f.label = label;
    f.mn = mn;
    f.mx = mx;
    f.step = step;
    f.fmt = fmt;
    f.logarithmic = logarithmic;
    f.get_num = get;
    f.set_num = set;
    return f;
}

PbzfmField Enum(const char* group, const char* key, const char* label, const char* const* options, int count,
                int (*get)(const Config&), void (*set)(Config&, int)) {
    PbzfmField f;
    f.kind = PbzfmFieldKind::Enum;
    f.group = group;
    f.key = key;
    f.label = label;
    f.options = options;
    f.option_count = count;
    f.get_enum = get;
    f.set_enum = set;
    return f;
}

PbzfmField Key(const char* group, const char* key, const char* label, std::string Config::* member,
               int (*fallback)(const KeyMap&)) {
    PbzfmField f;
    f.kind = PbzfmFieldKind::Key;
    f.group = group;
    f.key = key;
    f.label = label;
    f.key_name = member;
    f.key_fallback = fallback;
    return f;
}

constexpr const char* kKeysGroup = "#group_keys";
constexpr const char* kBehaviorGroup = "#group_behavior";
constexpr const char* kHideGroup = "#group_hide";
constexpr const char* kFreezeGroup = "#group_freeze";
constexpr const char* kCamGroup = "#group_camera";
constexpr const char* kLookGroup = "#group_look";
constexpr const char* kPadGroup = "#group_pad";

}  // namespace

const std::vector<PbzfmField>& pbzfm_fields() {
    static const std::vector<PbzfmField> table = {
        // ---- 快捷键（键盘；手柄组合键多按钮绑定表达不了，仍留在 config.ini 的 [gamepad]） ----
        Key(kKeysGroup, "key_toggle", "#key_toggle", &Config::key_toggle,
            [](const KeyMap& k) { return k.toggle; }),
        Key(kKeysGroup, "key_freeze", "#key_freeze", &Config::key_freeze,
            [](const KeyMap& k) { return k.freeze; }),
        Key(kKeysGroup, "key_frame_freeze", "#key_frame_freeze", &Config::key_frame_freeze,
            [](const KeyMap& k) { return k.frame_freeze; }),
        Key(kKeysGroup, "key_reset", "#key_reset", &Config::key_reset,
            [](const KeyMap& k) { return k.reset; }),
        Key(kKeysGroup, "key_roll_reset", "#key_roll_reset", &Config::key_roll_reset,
            [](const KeyMap& k) { return k.roll_reset; }),
        Key(kKeysGroup, "key_fov_up", "#key_fov_up", &Config::key_fov_up,
            [](const KeyMap& k) { return k.fov_up; }),
        Key(kKeysGroup, "key_fov_down", "#key_fov_down", &Config::key_fov_down,
            [](const KeyMap& k) { return k.fov_down; }),
        Key(kKeysGroup, "key_pitch_up", "#key_pitch_up", &Config::key_pitch_up,
            [](const KeyMap& k) { return k.pitch_up; }),
        Key(kKeysGroup, "key_pitch_down", "#key_pitch_down", &Config::key_pitch_down,
            [](const KeyMap& k) { return k.pitch_down; }),
        Key(kKeysGroup, "key_yaw_left", "#key_yaw_left", &Config::key_yaw_left,
            [](const KeyMap& k) { return k.yaw_left; }),
        Key(kKeysGroup, "key_yaw_right", "#key_yaw_right", &Config::key_yaw_right,
            [](const KeyMap& k) { return k.yaw_right; }),
        Key(kKeysGroup, "key_roll_left", "#key_roll_left", &Config::key_roll_left,
            [](const KeyMap& k) { return k.roll_left; }),
        Key(kKeysGroup, "key_roll_right", "#key_roll_right", &Config::key_roll_right,
            [](const KeyMap& k) { return k.roll_right; }),
        Key(kKeysGroup, "key_vertical_up", "#key_vertical_up", &Config::key_vertical_up,
            [](const KeyMap& k) { return k.vertical_up; }),
        Key(kKeysGroup, "key_vertical_down", "#key_vertical_down", &Config::key_vertical_down,
            [](const KeyMap& k) { return k.vertical_down; }),
        Key(kKeysGroup, "key_move_forward", "#key_move_forward", &Config::key_move_forward,
            [](const KeyMap& k) { return k.move_forward; }),
        Key(kKeysGroup, "key_move_back", "#key_move_back", &Config::key_move_back,
            [](const KeyMap& k) { return k.move_back; }),
        Key(kKeysGroup, "key_move_left", "#key_move_left", &Config::key_move_left,
            [](const KeyMap& k) { return k.move_left; }),
        Key(kKeysGroup, "key_move_right", "#key_move_right", &Config::key_move_right,
            [](const KeyMap& k) { return k.move_right; }),
        Key(kKeysGroup, "key_boost", "#key_boost", &Config::key_boost,
            [](const KeyMap& k) { return k.boost; }),
        Key(kKeysGroup, "key_slow", "#key_slow", &Config::key_slow,
            [](const KeyMap& k) { return k.slow; }),
        Key(kKeysGroup, "key_free_look", "#key_free_look", &Config::key_free_look,
            [](const KeyMap& k) { return k.free_look; }),

        // ---- 行为 ----
        Bool(kBehaviorGroup, "auto_pause_time", "#auto_pause_time",
             [](const Config& c) { return c.auto_pause_time; },
             [](Config& c, bool v) { c.auto_pause_time = v; }),
        Enum(kBehaviorGroup, "freeze_mode", "#freeze_mode", kFreezeModeOptions, 2,
             [](const Config& c) { return static_cast<int>(c.freeze_mode); },
             [](Config& c, int v) {
                 if (v < 0) v = 0;
                 if (v > 1) v = 1;
                 c.freeze_mode = static_cast<FreezeMode>(v);
             }),
        Bool(kBehaviorGroup, "block_game_input", "#block_game_input",
             [](const Config& c) { return c.block_game_input; },
             [](Config& c, bool v) { c.block_game_input = v; }),
        Bool(kBehaviorGroup, "wheel_fov", "#wheel_fov",
             [](const Config& c) { return c.wheel_fov; },
             [](Config& c, bool v) { c.wheel_fov = v; }),
        Bool(kBehaviorGroup, "wheel_fov_invert", "#wheel_fov_invert",
             [](const Config& c) { return c.wheel_fov_invert; },
             [](Config& c, bool v) { c.wheel_fov_invert = v; }),
        Bool(kBehaviorGroup, "ultrawide_unlock", "#ultrawide_unlock",
             [](const Config& c) { return c.ultrawide_unlock; },
             [](Config& c, bool v) { c.ultrawide_unlock = v; }),
        Bool(kBehaviorGroup, "pad_enabled", "#pad_enabled",
             [](const Config& c) { return c.pad_enabled; },
             [](Config& c, bool v) { c.pad_enabled = v; }),

        // ---- 隐藏（进入摄影模式时；进行中改动部分项到下次进入才生效） ----
        Bool(kHideGroup, "hide_player", "#hide_player",
             [](const Config& c) { return c.hide_player; },
             [](Config& c, bool v) { c.hide_player = v; }),
        Bool(kHideGroup, "hide_game_hud", "#hide_game_hud",
             [](const Config& c) { return c.hide_game_hud; },
             [](Config& c, bool v) { c.hide_game_hud = v; }),
        Bool(kHideGroup, "hide_interact_prompt", "#hide_interact_prompt",
             [](const Config& c) { return c.hide_interact_prompt; },
             [](Config& c, bool v) { c.hide_interact_prompt = v; }),
        Bool(kHideGroup, "hide_enemy_health_bars", "#hide_enemy_health_bars",
             [](const Config& c) { return c.hide_enemy_health_bars; },
             [](Config& c, bool v) { c.hide_enemy_health_bars = v; }),
        Bool(kHideGroup, "hide_lockon_marker", "#hide_lockon_marker",
             [](const Config& c) { return c.hide_lockon_marker; },
             [](Config& c, bool v) { c.hide_lockon_marker = v; }),
        Bool(kHideGroup, "hide_subtitles", "#hide_subtitles",
             [](const Config& c) { return c.hide_subtitles; },
             [](Config& c, bool v) { c.hide_subtitles = v; }),

        // ---- 冻结增强（改动的下一轮冻结开始生效；冻结中改动到解冻后生效） ----
        Bool(kFreezeGroup, "stop_wwise_on_freeze", "#stop_wwise_on_freeze",
             [](const Config& c) { return c.stop_wwise_on_freeze; },
             [](Config& c, bool v) { c.stop_wwise_on_freeze = v; }),
        Bool(kFreezeGroup, "wwise_engine_suspend", "#wwise_engine_suspend",
             [](const Config& c) { return c.wwise_engine_suspend; },
             [](Config& c, bool v) { c.wwise_engine_suspend = v; }),
        Bool(kFreezeGroup, "freeze_sequences", "#freeze_sequences",
             [](const Config& c) { return c.freeze_sequences; },
             [](Config& c, bool v) { c.freeze_sequences = v; }),
        Bool(kFreezeGroup, "freeze_dialogue_flow", "#freeze_dialogue_flow",
             [](const Config& c) { return c.freeze_dialogue_flow; },
             [](Config& c, bool v) { c.freeze_dialogue_flow = v; }),
        Bool(kFreezeGroup, "stop_camera_shakes_on_freeze", "#stop_camera_shakes_on_freeze",
             [](const Config& c) { return c.stop_camera_shakes_on_freeze; },
             [](Config& c, bool v) { c.stop_camera_shakes_on_freeze = v; }),
        Bool(kFreezeGroup, "freeze_wind", "#freeze_wind",
             [](const Config& c) { return c.freeze_wind; },
             [](Config& c, bool v) { c.freeze_wind = v; }),
        Bool(kFreezeGroup, "freeze_mpc", "#freeze_mpc",
             [](const Config& c) { return c.freeze_mpc; },
             [](Config& c, bool v) { c.freeze_mpc = v; }),
        Bool(kFreezeGroup, "freeze_cloth", "#freeze_cloth",
             [](const Config& c) { return c.freeze_cloth; },
             [](Config& c, bool v) { c.freeze_cloth = v; }),
        Bool(kFreezeGroup, "freeze_skeletal_animation", "#freeze_skeletal_animation",
             [](const Config& c) { return c.freeze_skeletal_animation; },
             [](Config& c, bool v) { c.freeze_skeletal_animation = v; }),
        Bool(kFreezeGroup, "freeze_motion_blur_off", "#freeze_motion_blur_off",
             [](const Config& c) { return c.freeze_motion_blur_off; },
             [](Config& c, bool v) { c.freeze_motion_blur_off = v; }),
        Bool(kFreezeGroup, "freeze_diag", "#freeze_diag",
             [](const Config& c) { return c.freeze_diag; },
             [](Config& c, bool v) { c.freeze_diag = v; }),

        // ---- 相机与移动 ----
        Float(kCamGroup, "fov_default", "#fov_default", 1.0, 179.0, 1.0, "%.0f",
              [](const Config& c) { return c.fov_default; },
              [](Config& c, double v) { c.fov_default = v; }),
        Float(kCamGroup, "fov_step", "#fov_step", 0.5, 10.0, 0.5, "%.1f",
              [](const Config& c) { return c.fov_step; },
              [](Config& c, double v) { c.fov_step = v; }),
        Float(kCamGroup, "move_speed", "#move_speed", 50.0, 2000.0, 10.0, "%.0f",
              [](const Config& c) { return c.move_speed; },
              [](Config& c, double v) { c.move_speed = v; }),
        Float(kCamGroup, "boost_mult", "#boost_mult", 1.0, 10.0, 0.5, "%.1f",
              [](const Config& c) { return c.boost_mult; },
              [](Config& c, double v) { c.boost_mult = v; }),
        Float(kCamGroup, "slow_mult", "#slow_mult", 0.05, 1.0, 0.05, "%.2f",
              [](const Config& c) { return c.slow_mult; },
              [](Config& c, double v) { c.slow_mult = v; }),
        Float(kCamGroup, "slow_look_mult", "#slow_look_mult", 0.01, 1.0, 0.01, "%.2f",
              [](const Config& c) { return c.slow_look_mult; },
              [](Config& c, double v) { c.slow_look_mult = v; }),
        Float(kCamGroup, "look_sensitivity", "#look_sensitivity", 0.01, 1.0, 0.01, "%.2f",
              [](const Config& c) { return c.look_sensitivity; },
              [](Config& c, double v) { c.look_sensitivity = v; }),
        Float(kCamGroup, "pitch_limit", "#pitch_limit", 10.0, 89.0, 1.0, "%.0f",
              [](const Config& c) { return c.pitch_limit; },
              [](Config& c, double v) { c.pitch_limit = v; }),
        Float(kCamGroup, "vertical_speed", "#vertical_speed", 10.0, 1000.0, 10.0, "%.0f",
              [](const Config& c) { return c.vertical_speed; },
              [](Config& c, double v) { c.vertical_speed = v; }),
        Float(kCamGroup, "roll_speed", "#roll_speed", 5.0, 360.0, 5.0, "%.0f",
              [](const Config& c) { return c.roll_speed; },
              [](Config& c, double v) { c.roll_speed = v; }),
        Float(kCamGroup, "pitch_speed", "#pitch_speed", 5.0, 180.0, 5.0, "%.0f",
              [](const Config& c) { return c.pitch_speed; },
              [](Config& c, double v) { c.pitch_speed = v; }),
        Float(kCamGroup, "yaw_speed", "#yaw_speed", 5.0, 270.0, 5.0, "%.0f",
              [](const Config& c) { return c.yaw_speed; },
              [](Config& c, double v) { c.yaw_speed = v; }),
        Enum(kCamGroup, "mouse_vertical_mode", "#mouse_vertical_mode", kMouseVertOptions, 2,
             [](const Config& c) { return static_cast<int>(c.mouse_vertical_mode); },
             [](Config& c, int v) {
                 if (v < 0) v = 0;
                 if (v > 1) v = 1;
                 c.mouse_vertical_mode = static_cast<MouseVerticalMode>(v);
             }),

        // ---- 相机后端（可热切换：改这一项立刻拆旧后端、建新后端，不用退出重进）----
        Bool(kCamGroup, "takeover_current_camera", "#takeover_current_camera",
             [](const Config& c) { return c.takeover_current_camera; },
             [](Config& c, bool v) { c.takeover_current_camera = v; }),

        // ---- [look] 暗角：走后处理体积通道（本作 gameplay 视图里实测生效） ----
        Float(kLookGroup, "vignette", "#vignette", tuning::kVignetteMin, tuning::kVignetteMax, 0.1, "%.2f",
              [](const Config& c) { return c.vignette; },
              [](Config& c, double v) { c.vignette = v; }),
        Float(kLookGroup, "saturation", "#saturation", 0.0, 2.0, 0.05, "%.2f",
              [](const Config& c) { return c.saturation; },
              [](Config& c, double v) { c.saturation = v; }),
        Float(kLookGroup, "contrast", "#contrast", 0.0, 2.0, 0.05, "%.2f",
              [](const Config& c) { return c.contrast; },
              [](Config& c, double v) { c.contrast = v; }),
        Float(kLookGroup, "bloom", "#bloom", 0.0, 20.0, 0.5, "%.1f",
              [](const Config& c) { return c.bloom; },
              [](Config& c, double v) { c.bloom = v; }),
        Float(kLookGroup, "temperature", "#temperature", -1.0, 1.0, 0.05, "%.2f",
              [](const Config& c) { return c.temperature; },
              [](Config& c, double v) { c.temperature = v; }),

        // ---- 手柄（组合键绑定仍留在 config.ini 的 [gamepad]） ----
        Float(kPadGroup, "pad_look_speed", "#pad_look_speed", 10.0, 720.0, 5.0, "%.0f",
              [](const Config& c) { return c.pad_look_speed; },
              [](Config& c, double v) { c.pad_look_speed = v; }),
        Float(kPadGroup, "pad_move_scale", "#pad_move_scale", 0.1, 2.0, 0.05, "%.2f",
              [](const Config& c) { return c.pad_move_scale; },
              [](Config& c, double v) { c.pad_move_scale = v; }),
        Float(kPadGroup, "pad_deadzone", "#pad_deadzone", 0.0, 0.5, 0.01, "%.2f",
              [](const Config& c) { return c.pad_deadzone; },
              [](Config& c, double v) { c.pad_deadzone = v; }),
    };
    return table;
}

double pbzfm_read_field(const PbzfmField& f, const Config& cfg, const KeyMap& keys) {
    switch (f.kind) {
        case PbzfmFieldKind::Bool:
            return f.get_bool && f.get_bool(cfg) ? 1.0 : 0.0;
        case PbzfmFieldKind::Float:
            return f.get_num ? f.get_num(cfg) : 0.0;
        case PbzfmFieldKind::Enum:
            return f.get_enum ? f.get_enum(cfg) : 0;
        case PbzfmFieldKind::Key: {
            if (!f.key_name) return 0.0;
            const int vk = vk_from_name(cfg.*f.key_name);
            if (vk != 0) return vk;
            return f.key_fallback ? f.key_fallback(keys) : 0.0;
        }
    }
    return 0.0;
}

bool pbzfm_apply_field(const PbzfmField& f, double value, Config& cfg) {
    switch (f.kind) {
        case PbzfmFieldKind::Bool:
            if (!f.set_bool) return false;
            f.set_bool(cfg, value != 0.0);
            return true;
        case PbzfmFieldKind::Float: {
            if (!f.set_num) return false;
            double v = value;
            if (v < f.mn) v = f.mn;
            if (v > f.mx) v = f.mx;
            f.set_num(cfg, v);
            return true;
        }
        case PbzfmFieldKind::Enum: {
            if (!f.set_enum || f.option_count <= 0) return false;
            int idx = static_cast<int>(value);
            if (idx < 0) idx = 0;
            if (idx > f.option_count - 1) idx = f.option_count - 1;
            f.set_enum(cfg, idx);
            return true;
        }
        case PbzfmFieldKind::Key: {
            if (!f.key_name) return false;
            const int vk = static_cast<int>(value);
            // 键捕获可能给出我们键名表表达不了的键（如鼠标侧键）：vk_to_name 会退化成
            // "0x.." 且解析不回来，直接拒绝，菜单显示值由调用方回滚
            const std::string name = vk_to_name(vk);
            if (vk == 0 || vk_from_name(name) != vk) return false;
            cfg.*f.key_name = name;
            return true;
        }
    }
    return false;
}

}  // namespace pbzpm
