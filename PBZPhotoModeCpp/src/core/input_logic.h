#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/config.h"
#include "core/gamepad.h"
#include "core/vk.h"

namespace pbzpm {

enum class MouseButton { Left, Right };

// 配置里的键名解析结果。任一键解析失败会退回默认键并记 warning。
struct KeyMap {
    int toggle{0x71};    // F2
    int menu{'T'};
    int freeze{'F'};
    int reset{'R'};
    int fov_up{0x6B};    // ADD
    int fov_down{0x6D};  // SUBTRACT
    // 手柄组合键（由 [gamepad] 配置解析而来，见 parse_pad_combo）
    std::vector<PadButton> pad_toggle{PadButton::LS, PadButton::RS};  // 默认：左右摇杆同时按下
    std::vector<PadButton> pad_menu{PadButton::Start};
    std::vector<PadButton> pad_freeze{PadButton::X};
    std::vector<PadButton> pad_reset{PadButton::Y};
    std::vector<PadButton> pad_fov_up{PadButton::Up};
    std::vector<PadButton> pad_fov_down{PadButton::Down};
    bool all_resolved{true};
};

KeyMap build_keymap(const Config& cfg, std::vector<std::string>* warnings);

// 一帧内玩家想做的事（纯数据，不含引擎调用）
struct Intent {
    double look_dx{};   // 鼠标水平位移（原始计数，乘灵敏度后才是角度）
    double look_dy{};   // 鼠标竖直位移（正值 = 向下）
    double wheel{};     // 滚轮凹槽累积
    double move_forward{};  // W/S  -> +1/-1
    double move_right{};    // D/A  -> +1/-1
    double move_up{};       // E/Q 组合，translate 模式下叠加左右键
    bool pitch_up_click{};  // look 模式：左键
    bool pitch_down_click{};// look 模式：右键
    bool boost{};           // Shift
    bool slow{};            // Ctrl
    bool toggle_mode{};
    bool toggle_menu{};
    bool toggle_freeze{};
    bool reset_view{};  // R：回到进入摄影模式时的位姿/FOV
    int fov_nudge{};    // FOV 步进：+1 增大 / -1 缩小（FOV 键或手柄十字键的按下沿）
    double pad_look_dx{};  // 手柄右摇杆转速（度/秒，正值 = 向右；tick 里乘 dt）
    double pad_look_dy{};  // 手柄右摇杆转速（度/秒，正值 = 向下；invert_y 在 tick 里处理）
};

// 纯状态机：喂 Win32 风格事件，产出 Intent。不依赖引擎，可单测。
class InputLogic {
public:
    void on_key(int vk, bool down);
    void on_mouse_move(double dx, double dy);
    void on_mouse_button(MouseButton button, bool down);
    void on_wheel(double notches);
    // 每帧一次：喂入手柄快照（没接手柄/被禁用/切后台时喂空状态，所有组合视为松开）
    void on_gamepad(const PadState& state);

    bool held(int vk) const;
    bool alt_down() const;
    bool left_down() const { return lmb_; }
    bool right_down() const { return rmb_; }
    // 最近一次 on_gamepad 喂入的快照（图形菜单的手柄导航用）
    const PadState& gamepad() const { return pad_; }

    // 只读：同一帧可以多次调用，只有 end_frame() 会清状态。
    Intent build(const Config& cfg, const KeyMap& keys) const;

    // 每帧末尾调用：清掉按下沿与鼠标累积量
    void end_frame();

    // 退出摄影模式时清空，避免残留按键卡住。
    // 不清手柄快照：组合键仍按住时，清了会把“仍按住”误判成新的按下沿（进出死循环）。
    void reset();

private:
    bool key_held(int vk) const { return vk != 0 && held_.count(vk) != 0; }
    bool key_pressed(int vk) const { return vk != 0 && pressed_.count(vk) != 0; }
    // 组合键：全部按钮按下才算“按住”；本帧按住且上一帧没按住才算一次按下沿
    bool pad_combo_down(const PadState& s, const std::vector<PadButton>& combo) const;
    bool pad_combo_pressed(const std::vector<PadButton>& combo) const;

    std::unordered_set<int> held_;
    std::unordered_set<int> pressed_;
    double dx_{};
    double dy_{};
    double wheel_{};
    bool lmb_{};
    bool rmb_{};
    PadState pad_{};
    PadState pad_prev_{};
};

}  // namespace pbzpm
