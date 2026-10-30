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
        else if (key == "key_menu") set_string(cfg.key_menu, value, error, key.c_str());
        else if (key == "key_freeze") set_string(cfg.key_freeze, value, error, key.c_str());
        else if (key == "key_reset") set_string(cfg.key_reset, value, error, key.c_str());
        else if (key == "key_fov_up") set_string(cfg.key_fov_up, value, error, key.c_str());
        else if (key == "key_fov_down") set_string(cfg.key_fov_down, value, error, key.c_str());
        else if (key == "move_speed") set_double(cfg.move_speed, value, error, key.c_str());
        else if (key == "boost_mult") set_double(cfg.boost_mult, value, error, key.c_str());
        else if (key == "slow_mult") set_double(cfg.slow_mult, value, error, key.c_str());
        else if (key == "look_sensitivity") set_double(cfg.look_sensitivity, value, error, key.c_str());
        else if (key == "pitch_limit") set_double(cfg.pitch_limit, value, error, key.c_str());
        else if (key == "vertical_speed") set_double(cfg.vertical_speed, value, error, key.c_str());
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
        else if (key == "hide_player") set_bool(cfg.hide_player, value, error, key.c_str());
        else if (key == "hide_game_hud") set_bool(cfg.hide_game_hud, value, error, key.c_str());
        else if (key == "auto_pause_time") set_bool(cfg.auto_pause_time, value, error, key.c_str());
        else if (key == "freeze_dilation") set_double(cfg.freeze_dilation, value, error, key.c_str());
        else if (key == "restore_fov_on_exit") set_bool(cfg.restore_fov_on_exit, value, error, key.c_str());
        else if (key == "restore_view_on_exit") set_bool(cfg.restore_view_on_exit, value, error, key.c_str());
        else if (key == "freeze_mode") {
            const std::string v = lower(value);
            if (v == "dilation" || v == "slow") cfg.freeze_mode = FreezeMode::Dilation;
            else if (v == "pause") cfg.freeze_mode = FreezeMode::Pause;
            else error = "freeze_mode 只能是 dilation 或 pause：" + value;
        } else if (key == "imgui_tab") set_bool(cfg.imgui_tab, value, error, key.c_str());
        else if (key == "hud_fallback") set_bool(cfg.hud_fallback, value, error, key.c_str());
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
    if (result.cfg.freeze_dilation <= 0.0 || result.cfg.freeze_dilation > 1.0) {
        result.warnings.push_back("freeze_dilation 应在 (0, 1]，已回退 0.0001");
        result.cfg.freeze_dilation = 0.0001;
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

std::string default_config_text() {
    Config c;
    std::ostringstream out;
    out << "; ================================================================\n"
        << "; " << PBZPM_VERSION << " 配置文件（UTF-8，可留空使用默认值）\n"
        << "; 改完存盘后按 F2 重进摄影模式即可生效（或重启游戏）\n"
        << "; ================================================================\n\n"
        << "[keys]\n"
        << "; 进入/退出摄影模式\n"
        << "key_toggle = " << c.key_toggle << "\n"
        << "; 显示/隐藏菜单\n"
        << "key_menu = " << c.key_menu << "\n"
        << "; 冻结/恢复时间\n"
        << "key_freeze = " << c.key_freeze << "\n"
        << "; 重置相机到进入摄影模式时位置/朝向/FOV\n"
        << "key_reset = " << c.key_reset << "\n"
        << "; FOV 加减（小键盘 +/-，也可写 OEM_PLUS / OEM_MINUS）\n"
        << "key_fov_up = " << c.key_fov_up << "\n"
        << "key_fov_down = " << c.key_fov_down << "\n\n"
        << "[control]\n"
        << "; 常规移动速度（单位/秒）；按住 Shift 加速、Ctrl 减速\n"
        << "move_speed = " << c.move_speed << "\n"
        << "boost_mult = " << c.boost_mult << "\n"
        << "slow_mult = " << c.slow_mult << "\n"
        << "; 每单位鼠标位移转多少度\n"
        << "look_sensitivity = " << c.look_sensitivity << "\n"
        << "pitch_limit = " << c.pitch_limit << "\n"
        << "invert_y = false\n"
        << "; 必须按住 Alt 才能自由转视角\n"
        << "free_look_requires_alt = true\n"
        << "; 进入摄影模式时屏蔽角色移动/视角输入（退出时还原）\n"
        << "block_game_input = true\n"
        << "; 左键上移/右键下移：translate=相机上下平移，look=俯仰\n"
        << "mouse_vertical_mode = " << mouse_vertical_mode_name(c.mouse_vertical_mode) << "\n"
        << "vertical_speed = " << c.vertical_speed << "\n\n"
        << "[camera]\n"
        << "fov_default = " << c.fov_default << "\n"
        << "fov_min = " << c.fov_min << "\n"
        << "fov_max = " << c.fov_max << "\n"
        << "fov_step = " << c.fov_step << "\n\n"
        << "[effects]\n"
        << "hide_player = false\n"
        << "; 进入摄影模式时隐藏游戏 HUD（退出时恢复原可见性）\n"
        << "hide_game_hud = true\n"
        << "; 进入摄影模式时自动暂停时间（默认开启；false=进入后时间照常流动，可按 F 手动冻结）\n"
        << "auto_pause_time = " << (c.auto_pause_time ? "true" : "false") << "\n"
        << "; dilation=时间几乎停住（画面仍可动）；pause=游戏暂停\n"
        << "freeze_mode = " << (c.freeze_mode == FreezeMode::Pause ? "pause" : "dilation") << "\n"
        << "freeze_dilation = " << c.freeze_dilation << "\n"
        << "restore_fov_on_exit = true\n"
        << "restore_view_on_exit = true\n\n"
        << "[ui]\n"
        << "; UE4SS 控制台里的 ImGui 面板（需要 UE4SS 控制台窗口）\n"
        << "imgui_tab = true\n"
        << "; 没有 ImGui 时用文字菜单兜底\n"
        << "hud_fallback = false\n\n"
        << "[misc]\n"
        << "log_level = " << log_level_name(c.log_level) << "\n"
        << "dry_run = false\n";
    return out.str();
}

}  // namespace pbzpm
