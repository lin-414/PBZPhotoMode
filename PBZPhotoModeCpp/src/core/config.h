#pragma once

#include <string>
#include <vector>

namespace pbzpm {

enum class LogLevel { Error, Warn, Info, Debug };

// 鼠标竖向行为：
//   Translate = 相机上/下平移（默认，"左键上移/右键下移"的字面语义）
//   Look      = 夹角俯仰（部分人习惯的"抬头/低头"）
enum class MouseVerticalMode { Translate, Look };

enum class FreezeMode { Dilation, Pause };

struct Config {
    // ---- [keys] 快捷键名，写法和 Lua 版一致：F2 / T / F / ADD / SUBTRACT / OEM_PLUS ----
    std::string key_toggle{"F2"};
    std::string key_menu{"T"};
    std::string key_freeze{"F"};
    std::string key_reset{"R"};
    std::string key_fov_up{"ADD"};
    std::string key_fov_down{"SUBTRACT"};
    std::string key_shutter{"F9"};

    // ---- [control] ----
    double move_speed{400.0};        // 单位/秒
    double boost_mult{4.0};          // Shift
    double slow_mult{0.3};           // Ctrl
    double look_sensitivity{0.10};   // 每单位鼠标位移转多少度
    double pitch_limit{89.0};
    bool invert_y{false};
    bool free_look_requires_alt{true};
    bool block_game_input{true};
    MouseVerticalMode mouse_vertical_mode{MouseVerticalMode::Translate};
    double vertical_speed{200.0};    // 左右键上下平移速度

    // ---- [camera] ----
    double fov_default{90.0};
    double fov_min{15.0};
    double fov_max{150.0};
    double fov_step{2.0};

    // ---- [effects] ----
    bool hide_player{false};
    bool hide_game_hud{false};
    FreezeMode freeze_mode{FreezeMode::Pause};
    double freeze_dilation{0.0001};
    bool restore_fov_on_exit{true};
    bool restore_view_on_exit{true};

    // ---- [ui] ----
    bool imgui_tab{true};        // UE4SS 控制台里的 ImGui 面板
    bool hud_fallback{false};    // 无 ImGui 时用 PrintString 画右下角文字菜单

    // ---- [misc] ----
    std::string screenshot_command{"HighResShot 1"};
    LogLevel log_level{LogLevel::Info};
    bool dry_run{false};         // 只打日志不改游戏状态（排障用）
};

struct ConfigLoadResult {
    Config cfg{};
    std::vector<std::string> warnings{};
    std::string source{"内置默认值"};
    bool ok{true};
};

// 解析 INI 文本。未知键 / 非法值只累积 warning，保留对应默认值。
ConfigLoadResult parse_config(const std::string& ini_text, const char* source = "config.ini");

// 从磁盘读取（不存在时返回默认值，source 标注为默认）
ConfigLoadResult load_config_file(const std::string& path);

// 带中文注释的默认配置文件内容（缺配置文件时可自动写出）
std::string default_config_text();

const char* log_level_name(LogLevel level);
const char* mouse_vertical_mode_name(MouseVerticalMode mode);

}  // namespace pbzpm
