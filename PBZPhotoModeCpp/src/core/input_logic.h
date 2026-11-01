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
    int freeze{'F'};
    int reset{'R'};
    int roll_reset{'Z'};  // 滚转归零（水平校正）
    int fov_up{0x6B};    // ADD
    int fov_down{0x6D};  // SUBTRACT
    int pitch_up{0x26};    // 仰视（抬头）：方向键上
    int pitch_down{0x28};  // 俯视（低头）：方向键下
    int yaw_left{0x25};    // 左转视角：方向键左（1.0.46 起从左右平移改来）
    int yaw_right{0x27};   // 右转视角：方向键右
    // 手柄组合键（由 [gamepad] 配置解析而来，见 parse_pad_combo）
    std::vector<PadButton> pad_toggle{PadButton::LS, PadButton::RS};  // 默认：左右摇杆同时按下
    std::vector<PadButton> pad_freeze{PadButton::X};
    std::vector<PadButton> pad_reset{PadButton::Y};
    std::vector<PadButton> pad_roll_reset{PadButton::LB, PadButton::RB};  // 同按：滚转归零（同按滚转本来就净零）
    std::vector<PadButton> pad_fov_up{PadButton::A};         // A = 增大 FOV（十字键已全部让位给视角调节）
    std::vector<PadButton> pad_fov_down{PadButton::B};       // B = 缩小 FOV
    std::vector<PadButton> pad_pitch_up{PadButton::Up};      // 十字键上 = 仰视
    std::vector<PadButton> pad_pitch_down{PadButton::Down};  // 十字键下 = 俯视
    std::vector<PadButton> pad_yaw_left{PadButton::Left};    // 十字键左 = 左转视角（1.0.46 起从平移改来）
    std::vector<PadButton> pad_yaw_right{PadButton::Right};  // 十字键右 = 右转视角
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
    double move_up{};       // 左右键（translate 模式）+ 手柄 RT/LT
    double roll{};          // Q/E 滚转：E=+1 顺时针 / Q=-1 逆时针
    double pitch_axis{};    // 俯仰：+1 仰视 / -1 俯视（方向键或十字键上/下按住；tick 里乘 pitch_speed × dt）
    double yaw_axis{};      // 偏航：+1 右转 / -1 左转（方向键或十字键左/右按住；tick 里乘 yaw_speed × dt）
    bool pitch_up_click{};  // look 模式：左键
    bool pitch_down_click{};// look 模式：右键
    bool boost{};           // Shift
    bool slow{};            // Ctrl
    bool toggle_mode{};
    bool toggle_freeze{};
    bool reset_view{};  // R：回到进入摄影模式时的位姿/FOV
    bool reset_roll{};  // 滚转归零（水平校正）：roll 置 0，位置/俯仰/偏航/FOV 不动
    int fov_nudge{};    // FOV 步进：+1 增大 / -1 缩小（FOV 键或手柄十字键的按下沿；按住时连发折算成多格）
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

    // dt 用于 FOV 按住连发计时；默认 0 表示不推进计时（旧调用/单测兼容）。每帧只调用一次。
    Intent build(const Config& cfg, const KeyMap& keys, double dt = 0.0);

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

    // FOV 单个方向的按住连发：按下沿立即 1 格，按住超过延迟后按固定间隔补格
    struct FovRepeat {
        bool was_down{};  // 上一帧该方向是否按住（键盘键或手柄组合任一）
        double hold{};    // 已连续按住的时长（秒）
        double next{};    // 下一次连发步进的时间点（秒，从按下起算）
    };
    int fov_axis_nudge(int vk, const std::vector<PadButton>& combo, double dt, FovRepeat& st);

    std::unordered_set<int> held_;
    std::unordered_set<int> pressed_;
    double dx_{};
    double dy_{};
    double wheel_{};
    bool lmb_{};
    bool rmb_{};
    PadState pad_{};
    PadState pad_prev_{};
    FovRepeat fov_up_repeat_{};
    FovRepeat fov_down_repeat_{};
};

}  // namespace pbzpm
