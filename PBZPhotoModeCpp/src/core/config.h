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
    // ---- [keys] 快捷键名，写法与默认键名一致：F2 / F / R / ADD / SUBTRACT / OEM_PLUS ----
    std::string key_toggle{"F2"};
    std::string key_freeze{"F"};
    std::string key_reset{"R"};
    std::string key_fov_up{"ADD"};
    std::string key_fov_down{"SUBTRACT"};

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
    double roll_speed{60.0};         // Q/E 滚转速度（度/秒）：E 顺时针，Q 逆时针

    // ---- [gamepad] 手柄 ----
    bool pad_enabled{true};           // 总开关（false=不轮询手柄）
    std::string pad_toggle{"LS+RS"};  // 进入/退出：默认左右摇杆同时按下（L3+R3）
    std::string pad_freeze{"X"};
    std::string pad_reset{"Y"};
    std::string pad_fov_up{"UP"};
    std::string pad_fov_down{"DOWN"};
    double pad_look_speed{180.0};     // 右摇杆满行程的转速（度/秒）
    double pad_move_scale{1.0};       // 左摇杆移动占 move_speed 的比例
    double pad_deadzone{0.15};        // 摇杆死区（0~0.5）

    // ---- [camera] ----
    double fov_default{90.0};
    double fov_min{15.0};
    double fov_max{150.0};
    double fov_step{2.0};
    bool ultrawide_unlock{true};  // 超宽屏（21:9/32:9）：进入摄影模式时解除 16:9 画幅锁（两侧黑边），退出还原

    // ---- [effects] ----
    bool hide_player{false};
    bool hide_game_hud{true};    // 摄影模式默认隐藏游戏 HUD（退出时恢复）
    bool hide_interact_prompt{true};  // 摄影模式默认隐藏交互提示（可互物体旁的 E 键图标，退出恢复）
    bool auto_pause_time{true};  // 进入摄影模式时自动暂停时间（默认开启）
    FreezeMode freeze_mode{FreezeMode::Pause};
    double freeze_dilation{0.0001};
    bool restore_fov_on_exit{true};
    bool restore_view_on_exit{true};

    // ---- [ui] ----
    bool imgui_tab{true};        // UE4SS 控制台里的 ImGui 诊断面板（游戏内无任何菜单，v1.8.0 起纯快捷键）

    // ---- [misc] ----
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
