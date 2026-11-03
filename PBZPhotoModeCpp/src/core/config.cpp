#include "core/config.h"

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
        Number("freeze_dilation", "effects", &Config::freeze_dilation, 0.0001, 1.0, "photo_mode::toggle_freeze"),
        Bool("freeze_sequences", "effects", &Config::freeze_sequences, "photo_mode::toggle_freeze"),
        Bool("freeze_dialogue_flow", "effects", &Config::freeze_dialogue_flow, "photo_mode::toggle_freeze"),
        Bool("stop_wwise_on_freeze", "effects", &Config::stop_wwise_on_freeze, "photo_mode::toggle_freeze"),
        Bool("wwise_engine_suspend", "effects", &Config::wwise_engine_suspend, "photo_mode::toggle_freeze"),
        Bool("stop_camera_shakes_on_freeze", "effects", &Config::stop_camera_shakes_on_freeze,
             "photo_mode::toggle_freeze"),
        Bool("freeze_wind", "effects", &Config::freeze_wind, "photo_mode::toggle_freeze"),
        Bool("freeze_mpc", "effects", &Config::freeze_mpc, "photo_mode::toggle_freeze"),
        Bool("freeze_diag", "effects", &Config::freeze_diag, "photo_mode::tick"),
        Bool("restore_fov_on_exit", "effects", &Config::restore_fov_on_exit, "photo_mode::leave"),
        Bool("restore_view_on_exit", "effects", &Config::restore_view_on_exit, "photo_mode::leave"),

        // ---- [ui] ----
        Bool("imgui_tab", "ui", &Config::imgui_tab, "imgui_tab.cpp"),
        Enum("menu_language", "ui", &apply_menu_language, "pbzfm_client_lang"),

        // ---- [misc] ----
        Enum("log_level", "misc", &apply_log_level, "ue4ss_port::log"),
        Bool("dry_run", "misc", &Config::dry_run, "engine_port"),

        // ---- 弃用：接受但忽略，每个键只告警一次 ----
        Deprecated("interval_ms", "已移除并忽略（旧版 ImGui 面板轮询间隔）"),
        Deprecated("ui_scale", "已移除并忽略（旧版 ImGui 面板缩放）"),
        Deprecated("time_offset_compensation", "已在 1.0.66 移除并忽略（冻结渲染线程计时会导致移动视角卡顿）"),
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

ConfigLoadResult parse_config(const std::string& ini_text, const char* source) {
    ConfigLoadResult result;
    result.source = source ? source : "config.ini";

    std::set<std::string> warned;  // 同一键 / 同一 section / 同一行只告警一次，避免刷屏
    auto warn_once = [&result, &warned](const std::string& tag, const std::string& message) {
        if (warned.insert(tag).second) result.warnings.push_back(message);
    };

    // section 白名单：未知 section 报一次，但其中的键仍按已知键名解析（容错，不静默）
    static const std::set<std::string> kKnownSections = {"keys", "control", "gamepad", "camera",
                                                         "effects", "ui", "misc"};
    std::unordered_map<std::string, std::string> seen_keys;  // 键名 -> 首次出现的 section

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
        << "; " << PBZPM_VERSION << " 配置文件（UTF-8，可留空使用默认值）\n"
        << "; 改完存盘后按 F2 重进摄影模式即可生效（或重启游戏）\n"
        << "; ================================================================\n\n"
        << "[keys]\n"
        << "; 进入/退出摄影模式\n"
        << "key_toggle = " << c.key_toggle << "\n"
        << "; 冻结/恢复时间\n"
        << "key_freeze = " << c.key_freeze << "\n"
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
        << "key_vertical_down = " << c.key_vertical_down << "\n\n"
        << "[control]\n"
        << "; 常规移动速度（单位/秒）；按住 Shift 加速、Ctrl 减速\n"
        << "move_speed = " << c.move_speed << "\n"
        << "boost_mult = " << c.boost_mult << "\n"
        << "slow_mult = " << c.slow_mult << "\n"
        << "; 按住 Ctrl（手柄为按下右摇杆 RS）时鼠标/右摇杆视角速度的倍率，越小视角越精细\n"
        << "slow_look_mult = " << c.slow_look_mult << "\n"
        << "; 每单位鼠标位移转多少度\n"
        << "look_sensitivity = " << c.look_sensitivity << "\n"
        << "pitch_limit = " << c.pitch_limit << "\n"
        << "invert_y = " << (c.invert_y ? "true" : "false") << "\n"
        << "; 必须按住 Alt 才能自由转视角\n"
        << "free_look_requires_alt = " << (c.free_look_requires_alt ? "true" : "false") << "\n"
        << "; 进入摄影模式时屏蔽角色移动/视角输入（退出时还原）\n"
        << "block_game_input = " << (c.block_game_input ? "true" : "false") << "\n"
        << "; 升降键行为（具体键位见 [keys] 的 key_vertical_up/down）：translate=相机上下平移，look=俯仰\n"
        << "mouse_vertical_mode = " << mouse_vertical_mode_name(c.mouse_vertical_mode) << "\n"
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
        << "; 右摇杆转视角速度（度/秒，全行程）\n"
        << "pad_look_speed = " << c.pad_look_speed << "\n"
        << "; 左摇杆移动速度占 move_speed 的比例\n"
        << "pad_move_scale = " << c.pad_move_scale << "\n"
        << "; 摇杆死区（0~0.5，摇杆有漂移就调大）\n"
        << "pad_deadzone = " << c.pad_deadzone << "\n\n"
        << "[camera]\n"
        << "fov_default = " << c.fov_default << "\n"
        << "fov_min = " << c.fov_min << "\n"
        << "fov_max = " << c.fov_max << "\n"
        << "fov_step = " << c.fov_step << "\n"
        << "; 超宽屏（21:9/32:9）：进入摄影模式时解除游戏 16:9 画幅锁定（消除两侧黑边），退出还原\n"
        << "ultrawide_unlock = " << (c.ultrawide_unlock ? "true" : "false") << "\n\n"
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
        << "; 冻结诊断（默认关闭）：风源/MPC 参数变化/相机修饰器状态/视角微动；\n"
        << "; 开启后状态翻转时输出，持续期每 10 秒最多一条（诊断需反射扫描，频繁输出会拖帧）\n"
        << "freeze_diag = " << (c.freeze_diag ? "true" : "false") << "\n"
        << "restore_fov_on_exit = " << (c.restore_fov_on_exit ? "true" : "false") << "\n"
        << "restore_view_on_exit = " << (c.restore_view_on_exit ? "true" : "false") << "\n\n"
        << "[ui]\n"
        << "; UE4SS 控制台里的 ImGui 诊断面板（需要 UE4SS 控制台窗口）；游戏内无菜单，纯快捷键操作\n"
        << "imgui_tab = " << (c.imgui_tab ? "true" : "false") << "\n"
        << "; PBZFM 菜单界面语言：auto=跟随 PBZFM 菜单；或词典 id\n"
        << ";（内置 zh-CN/en-US/ja-JP/ko-KR，或 languages/*.json 的文件名）\n"
        << "menu_language = " << c.menu_language << "\n\n"
        << "[misc]\n"
        << "log_level = " << log_level_name(c.log_level) << "\n"
        << "dry_run = " << (c.dry_run ? "true" : "false") << "\n";
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
