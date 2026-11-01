#include "core/config.h"

#include "core/version.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <string_view>

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

std::string strip_comment(std::string_view line) {
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if ((c == ';' || c == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
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
    out = value;
    return true;
}

bool parse_int(const std::string& raw, int& out) {
    const std::string s = trim(raw);
    if (s.empty()) return false;
    int value{};
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    const auto res = std::from_chars(begin, end, value);
    if (res.ec != std::errc{} || res.ptr != end) return false;
    out = value;
    return true;
}

void set_double(double& field, const std::string& raw, std::string& error, const char* label) {
    if (!parse_double(raw, field)) error = std::string("值不是合法数字：") + label + " = " + raw;
}

void set_bool(bool& field, const std::string& raw, std::string& error, const char* label) {
    if (!parse_bool(raw, field)) error = std::string("值不是合法布尔：") + label + " = " + raw;
}

void set_string(std::string& field, const std::string& raw, std::string& error, const char* label) {
    if (raw.empty()) {
        error = std::string("值为空：") + label;
        return;
    }
    field = raw;
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

    // 键名 -> 赋值函数。未知键一律 warning，绝不静默吞掉。
    const auto assign = [&result](Config& cfg, const std::string& key, const std::string& value) {
        std::string error;
        if (key == "key_toggle") set_string(cfg.key_toggle, value, error, key.c_str());
        else if (key == "key_freeze") set_string(cfg.key_freeze, value, error, key.c_str());
        else if (key == "key_reset") set_string(cfg.key_reset, value, error, key.c_str());
        else if (key == "key_roll_reset") set_string(cfg.key_roll_reset, value, error, key.c_str());
        else if (key == "key_fov_up") set_string(cfg.key_fov_up, value, error, key.c_str());
        else if (key == "key_fov_down") set_string(cfg.key_fov_down, value, error, key.c_str());
        else if (key == "key_pitch_up") set_string(cfg.key_pitch_up, value, error, key.c_str());
        else if (key == "key_pitch_down") set_string(cfg.key_pitch_down, value, error, key.c_str());
        else if (key == "key_yaw_left") set_string(cfg.key_yaw_left, value, error, key.c_str());
        else if (key == "key_yaw_right") set_string(cfg.key_yaw_right, value, error, key.c_str());
        // 旧键名兼容（≤1.0.45 左右是平移，1.0.46 起改为转向）：老 config.ini 的自定义键位继续生效
        else if (key == "key_pan_left") set_string(cfg.key_yaw_left, value, error, key.c_str());
        else if (key == "key_pan_right") set_string(cfg.key_yaw_right, value, error, key.c_str());
        else if (key == "pad_enabled") set_bool(cfg.pad_enabled, value, error, key.c_str());
        else if (key == "pad_toggle") set_string(cfg.pad_toggle, value, error, key.c_str());
        else if (key == "pad_freeze") set_string(cfg.pad_freeze, value, error, key.c_str());
        else if (key == "pad_reset") set_string(cfg.pad_reset, value, error, key.c_str());
        else if (key == "pad_roll_reset") set_string(cfg.pad_roll_reset, value, error, key.c_str());
        else if (key == "pad_fov_up") set_string(cfg.pad_fov_up, value, error, key.c_str());
        else if (key == "pad_fov_down") set_string(cfg.pad_fov_down, value, error, key.c_str());
        else if (key == "pad_pitch_up") set_string(cfg.pad_pitch_up, value, error, key.c_str());
        else if (key == "pad_pitch_down") set_string(cfg.pad_pitch_down, value, error, key.c_str());
        else if (key == "pad_yaw_left") set_string(cfg.pad_yaw_left, value, error, key.c_str());
        else if (key == "pad_yaw_right") set_string(cfg.pad_yaw_right, value, error, key.c_str());
        else if (key == "pad_pan_left") set_string(cfg.pad_yaw_left, value, error, key.c_str());   // 旧键名兼容
        else if (key == "pad_pan_right") set_string(cfg.pad_yaw_right, value, error, key.c_str());
        else if (key == "pad_look_speed") set_double(cfg.pad_look_speed, value, error, key.c_str());
        else if (key == "pad_move_scale") set_double(cfg.pad_move_scale, value, error, key.c_str());
        else if (key == "pad_deadzone") set_double(cfg.pad_deadzone, value, error, key.c_str());
        else if (key == "move_speed") set_double(cfg.move_speed, value, error, key.c_str());
        else if (key == "boost_mult") set_double(cfg.boost_mult, value, error, key.c_str());
        else if (key == "slow_mult") set_double(cfg.slow_mult, value, error, key.c_str());
        else if (key == "slow_look_mult") set_double(cfg.slow_look_mult, value, error, key.c_str());
        else if (key == "look_sensitivity") set_double(cfg.look_sensitivity, value, error, key.c_str());
        else if (key == "pitch_limit") set_double(cfg.pitch_limit, value, error, key.c_str());
        else if (key == "vertical_speed") set_double(cfg.vertical_speed, value, error, key.c_str());
        else if (key == "roll_speed") set_double(cfg.roll_speed, value, error, key.c_str());
        else if (key == "pitch_speed") set_double(cfg.pitch_speed, value, error, key.c_str());
        else if (key == "yaw_speed") set_double(cfg.yaw_speed, value, error, key.c_str());
        else if (key == "invert_y") set_bool(cfg.invert_y, value, error, key.c_str());
        else if (key == "free_look_requires_alt") set_bool(cfg.free_look_requires_alt, value, error, key.c_str());
        else if (key == "block_game_input") set_bool(cfg.block_game_input, value, error, key.c_str());
        else if (key == "mouse_vertical_mode") {
            const std::string v = lower(value);
            if (v == "translate" || v == "move") cfg.mouse_vertical_mode = MouseVerticalMode::Translate;
            else if (v == "look" || v == "pitch") cfg.mouse_vertical_mode = MouseVerticalMode::Look;
            else error = "mouse_vertical_mode 只能是 translate 或 look：" + value;
        } else if (key == "fov_default") set_double(cfg.fov_default, value, error, key.c_str());
        else if (key == "fov_min") set_double(cfg.fov_min, value, error, key.c_str());
        else if (key == "fov_max") set_double(cfg.fov_max, value, error, key.c_str());
        else if (key == "fov_step") set_double(cfg.fov_step, value, error, key.c_str());
        else if (key == "ultrawide_unlock") set_bool(cfg.ultrawide_unlock, value, error, key.c_str());
        else if (key == "hide_player") set_bool(cfg.hide_player, value, error, key.c_str());
        else if (key == "hide_game_hud") set_bool(cfg.hide_game_hud, value, error, key.c_str());
        else if (key == "hide_interact_prompt") set_bool(cfg.hide_interact_prompt, value, error, key.c_str());
        else if (key == "hide_enemy_health_bars") set_bool(cfg.hide_enemy_health_bars, value, error, key.c_str());
        else if (key == "hide_lockon_marker") set_bool(cfg.hide_lockon_marker, value, error, key.c_str());
        else if (key == "hide_subtitles") set_bool(cfg.hide_subtitles, value, error, key.c_str());
        else if (key == "auto_pause_time") set_bool(cfg.auto_pause_time, value, error, key.c_str());
        else if (key == "freeze_dilation") set_double(cfg.freeze_dilation, value, error, key.c_str());
        else if (key == "freeze_sequences") set_bool(cfg.freeze_sequences, value, error, key.c_str());
        else if (key == "stop_wwise_on_freeze") set_bool(cfg.stop_wwise_on_freeze, value, error, key.c_str());
        else if (key == "stop_camera_shakes_on_freeze") set_bool(cfg.stop_camera_shakes_on_freeze, value, error, key.c_str());
        else if (key == "freeze_wind") set_bool(cfg.freeze_wind, value, error, key.c_str());
        else if (key == "freeze_mpc") set_bool(cfg.freeze_mpc, value, error, key.c_str());
        else if (key == "freeze_diag") set_bool(cfg.freeze_diag, value, error, key.c_str());
        else if (key == "restore_fov_on_exit") set_bool(cfg.restore_fov_on_exit, value, error, key.c_str());
        else if (key == "restore_view_on_exit") set_bool(cfg.restore_view_on_exit, value, error, key.c_str());
        else if (key == "freeze_mode") {
            const std::string v = lower(value);
            if (v == "dilation" || v == "slow") cfg.freeze_mode = FreezeMode::Dilation;
            else if (v == "pause") cfg.freeze_mode = FreezeMode::Pause;
            else error = "freeze_mode 只能是 dilation 或 pause：" + value;
        } else if (key == "imgui_tab") set_bool(cfg.imgui_tab, value, error, key.c_str());
        else if (key == "menu_language") {
            if (value.empty()) error = "menu_language 不能为空（auto 或词典 id）";
            else cfg.menu_language = value;
        }
        else if (key == "log_level") {
            const std::string v = lower(value);
            if (v == "error") cfg.log_level = LogLevel::Error;
            else if (v == "warn" || v == "warning") cfg.log_level = LogLevel::Warn;
            else if (v == "info") cfg.log_level = LogLevel::Info;
            else if (v == "debug") cfg.log_level = LogLevel::Debug;
            else error = "log_level 只能是 error/warn/info/debug：" + value;
        } else if (key == "dry_run") set_bool(cfg.dry_run, value, error, key.c_str());
        else if (key == "interval_ms" || key == "ui_scale") {
            int ignored{};
            if (!parse_int(value, ignored)) error = "值不是合法整数：" + key + " = " + value;
        } else {
            result.warnings.push_back("未知配置项已忽略：" + key);
            return;
        }
        if (!error.empty()) result.warnings.push_back(error);
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
            continue;
        }
        const size_t eq = cleaned.find('=');
        if (eq == std::string::npos) {
            result.warnings.push_back("第 " + std::to_string(line_no) + " 行不是 key=value，已忽略");
            continue;
        }
        const std::string key = lower(trim(std::string_view(cleaned).substr(0, eq)));
        const std::string value = unquote(trim(std::string_view(cleaned).substr(eq + 1)));
        if (key.empty()) {
            result.warnings.push_back("第 " + std::to_string(line_no) + " 行键名为空，已忽略");
            continue;
        }
        (void)section;  // 节名只用于可读性，允许平铺写法
        assign(result.cfg, key, value);
    }

    // 范围保护：写错数值不至于把相机搞坏
    if (result.cfg.fov_min < 1.0) result.cfg.fov_min = 1.0;
    if (result.cfg.fov_max > 179.0) result.cfg.fov_max = 179.0;
    if (result.cfg.fov_max <= result.cfg.fov_min) {
        result.warnings.push_back("fov_max 必须大于 fov_min，已回退默认 15/150");
        result.cfg.fov_min = 15.0;
        result.cfg.fov_max = 150.0;
    }
    if (result.cfg.fov_default < result.cfg.fov_min || result.cfg.fov_default > result.cfg.fov_max) {
        result.warnings.push_back("fov_default 超出 [fov_min, fov_max]，已夹紧");
        result.cfg.fov_default = std::clamp(result.cfg.fov_default, result.cfg.fov_min, result.cfg.fov_max);
    }
    if (result.cfg.pitch_limit <= 0.0 || result.cfg.pitch_limit > 90.0) {
        result.warnings.push_back("pitch_limit 应在 (0, 90]，已回退 89");
        result.cfg.pitch_limit = 89.0;
    }
    if (result.cfg.move_speed <= 0.0) {
        result.warnings.push_back("move_speed 必须为正，已回退 400");
        result.cfg.move_speed = 400.0;
    }
    if (result.cfg.roll_speed <= 0.0) {
        result.warnings.push_back("roll_speed 必须为正，已回退 60");
        result.cfg.roll_speed = 60.0;
    }
    if (result.cfg.pitch_speed <= 0.0) {
        result.warnings.push_back("pitch_speed 必须为正，已回退 30");
        result.cfg.pitch_speed = 30.0;
    }
    if (result.cfg.slow_look_mult <= 0.0 || result.cfg.slow_look_mult > 1.0) {
        result.warnings.push_back("slow_look_mult 应在 (0, 1]，已回退 0.1");
        result.cfg.slow_look_mult = 0.1;
    }
    if (result.cfg.yaw_speed <= 0.0) {
        result.warnings.push_back("yaw_speed 必须为正，已回退 90");
        result.cfg.yaw_speed = 90.0;
    }
    if (result.cfg.freeze_dilation <= 0.0 || result.cfg.freeze_dilation > 1.0) {
        result.warnings.push_back("freeze_dilation 应在 (0, 1]，已回退 0.0001");
        result.cfg.freeze_dilation = 0.0001;
    }
    if (result.cfg.pad_look_speed < 10.0 || result.cfg.pad_look_speed > 720.0) {
        result.warnings.push_back("pad_look_speed 应在 [10, 720]，已回退 60");
        result.cfg.pad_look_speed = 60.0;
    }
    if (result.cfg.pad_move_scale < 0.0 || result.cfg.pad_move_scale > 3.0) {
        result.warnings.push_back("pad_move_scale 应在 [0, 3]，已回退 1");
        result.cfg.pad_move_scale = 1.0;
    }
    if (result.cfg.pad_deadzone < 0.0 || result.cfg.pad_deadzone > 0.5) {
        result.warnings.push_back("pad_deadzone 应在 [0, 0.5]，已回退 0.15");
        result.cfg.pad_deadzone = 0.15;
    }
    return result;
}

ConfigLoadResult load_config_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ConfigLoadResult result;
        result.ok = false;
        result.source = "缺省默认值（未找到 " + path + "）";
        result.warnings.push_back("未找到配置文件，使用内置默认值：" + path);
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
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
        << "key_yaw_right = " << c.key_yaw_right << "\n\n"
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
        << "; 左键上移/右键下移：translate=相机上下平移，look=俯仰\n"
        << "mouse_vertical_mode = " << mouse_vertical_mode_name(c.mouse_vertical_mode) << "\n"
        << "vertical_speed = " << c.vertical_speed << "\n"
        << "; Q/E 滚转相机速度（度/秒）：E 顺时针，Q 逆时针；Shift 加速 / Ctrl 减速同样生效\n"
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
        << "; 进入摄影模式时自动暂停时间（默认开启；false=进入后时间照常流动，可按 F 手动冻结）\n"
        << "auto_pause_time = " << (c.auto_pause_time ? "true" : "false") << "\n"
        << "; dilation=时间几乎停住（画面仍可动）；pause=游戏暂停\n"
        << "freeze_mode = " << (c.freeze_mode == FreezeMode::Pause ? "pause" : "dilation") << "\n"
        << "freeze_dilation = " << c.freeze_dilation << "\n"
        << "; 冻结时同时暂停剧情/过场（Sequencer 播放器），解冻/退出时恢复。\n"
        << "; 剧情系统常绕过世界暂停自顾自播（画面像冻住但动作声音还在走），默认开启\n"
        << "freeze_sequences = " << (c.freeze_sequences ? "true" : "false") << "\n"
        << "; 冻结时把所有 Wwise 音频组件的音量归零（对话/环境/音乐一并静音，可恢复）。\n"
        << "; 声音在冻结期间继续无声播放，解冻恢复音量后当前句从原处继续\n"
        << "stop_wwise_on_freeze = " << (c.stop_wwise_on_freeze ? "true" : "false") << "\n"
        << "; 冻结时停用相机修饰器并立即结束相机 shake。它们用真实时间驱动，世界暂停拦不住，\n"
        << "; 是冻结画面边缘抖动/脸部阴影爬动的主变化源；解冻时按冻结前状态还原\n"
        << "stop_camera_shakes_on_freeze = " << (c.stop_camera_shakes_on_freeze ? "true" : "false") << "\n"
        << "; 冻结时把风源（WindDirectionalSource）的风强/风速归零——材质顶点摆动（布料/植被）\n"
        << "; 的幅度源，世界暂停与时间缩放都拦不住；解冻还原原值\n"
        << "freeze_wind = " << (c.freeze_wind ? "true" : "false") << "\n"
        << "; 冻结时钉住材质参数集合（MPC）当前值，冻结期每帧写回快照，压制实时驱动参数\n"
        << "; 继续推布料/植被摆动；解冻释放\n"
        << "freeze_mpc = " << (c.freeze_mpc ? "true" : "false") << "\n"
        << "; 冻结期每秒输出一条冻结诊断（风源/MPC 参数变化/相机修饰器状态），\n"
        << "; 定位布料与阴影闪烁还剩哪个变化源没压住\n"
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
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "无法打开文件写入：" + path;
        return false;
    }
    const std::string text = config_ini_text(cfg);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        if (error) *error = "写入失败：" + path;
        return false;
    }
    return true;
}

}  // namespace pbzpm
