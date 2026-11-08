#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/config.h"
#include "core/gamepad.h"
#include "core/vk.h"

namespace pbzpm {

// AController 的 IgnoreLookInput / IgnoreMoveInput 是「堆叠锁」：SetIgnore*Input(true/false)
// 每次只 +1/-1，不是布尔开关。进入摄影模式前若游戏自己已经持有同类锁（比如暂停、过场、
// 菜单把视角锁住），再配对一次 true/false 会把计数留在 1，表现为退出后视角/移动被永久吞掉。
// 恢复时用 Is*InputIgnored() 的布尔结果比对进入前状态，决定是否需要 Reset*（清掉全部残留）
// 或补回一把锁（进入前本来就是锁定的情况）。
enum class IgnoreLockRestore { None, Reset, Relock };

inline IgnoreLockRestore plan_ignore_lock_restore(bool was_ignored, bool now_ignored) {
    if (!was_ignored && now_ignored) return IgnoreLockRestore::Reset;
    if (was_ignored && !now_ignored) return IgnoreLockRestore::Relock;
    return IgnoreLockRestore::None;
}

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
    int roll_left{'Q'};    // 滚转：逆时针（默认 Q）
    int roll_right{'E'};   // 滚转：顺时针（默认 E）
    int vertical_up{kMouseLeft};    // 升降上：默认鼠标左键（可改键盘键/其它鼠标键）
    int vertical_down{kMouseRight}; // 升降下：默认鼠标右键
    // 移动/变速/自由视角门控（2.0 起可配）
    int move_forward{'W'};
    int move_back{'S'};
    int move_left{'A'};
    int move_right{'D'};
    int boost{kShiftGeneric};   // 加速（默认 SHIFT，兼容左右 Shift）
    int slow{kCtrlGeneric};     // 减速（默认 CONTROL）
    int free_look{kAltGeneric}; // 自由转视角门控（默认 ALT；free_look_requires_alt=false 时不使用）
    // 景深/对焦（2.1 复活，键位见 config.h [keys] 注释）
    int dof_toggle{'G'};
    int focus_near{0xBC};   // COMMA：对焦拉近
    int focus_far{0xBE};    // PERIOD：对焦拉远
    int aperture_open{0xDB};  // OEM_4 = '['：开大光圈（F 值减小）
    int aperture_close{0xDD}; // OEM_6 = ']'：收小光圈
    // 画面定格键：只给 PBZFM 菜单显示/回退用。InputLogic 不消费（定格走独立 watcher 线程）。
    int frame_freeze{0x76};
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
    // 原写死键（2.0 起可配）：单键 RT/LT 绑定仍按模拟量行程给量
    std::vector<PadButton> pad_roll_left{PadButton::LB};    // 滚转逆时针（默认 LB）
    std::vector<PadButton> pad_roll_right{PadButton::RB};   // 滚转顺时针（默认 RB）
    std::vector<PadButton> pad_boost{PadButton::LS};        // 加速（默认 L3）
    std::vector<PadButton> pad_slow{PadButton::RS};         // 减速（默认 R3）
    std::vector<PadButton> pad_vertical_up{PadButton::RT};    // 升降上（默认 RT，模拟量）
    std::vector<PadButton> pad_vertical_down{PadButton::LT};  // 升降下（默认 LT，模拟量）
    // 景深/对焦组合键（默认值必须与 Config::pad_* 的字符串默认值逐项一致，
    // 否则 ini 写错时手柄会静默换一套键位；NONE 解绑 = 空组合）。
    // 历史缺陷：旧版曾有单按 BACK/START 与 LB↔RB 互换的漂移。
    std::vector<PadButton> pad_focus_near{PadButton::LB, PadButton::Back};
    std::vector<PadButton> pad_focus_far{PadButton::LB, PadButton::Start};
    std::vector<PadButton> pad_aperture_open{PadButton::RB, PadButton::Back};
    std::vector<PadButton> pad_aperture_close{PadButton::RB, PadButton::Start};
    std::vector<PadButton> pad_dof_toggle{};      // 默认 NONE（解绑）
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
    double move_up{};       // 升降键（translate 模式）+ 手柄 RT/LT
    double roll{};          // 滚转（默认 Q/E）：+1 顺时针 / -1 逆时针
    double pitch_axis{};    // 俯仰：+1 仰视 / -1 俯视（方向键或十字键上/下按住；tick 里乘 pitch_speed × dt）
    double yaw_axis{};      // 偏航：+1 右转 / -1 左转（方向键或十字键左/右按住；tick 里乘 yaw_speed × dt）
    bool pitch_up_click{};  // look 模式：升降上键
    bool pitch_down_click{};// look 模式：升降下键
    bool boost{};           // 加速键（默认 Shift）
    bool slow{};            // 减速键（默认 Ctrl）
    bool toggle_mode{};
    bool toggle_freeze{};
    bool reset_view{};  // R：回到进入摄影模式时的位姿/FOV
    bool reset_roll{};  // 滚转归零（水平校正）：roll 置 0，位置/俯仰/偏航/FOV 不动
    int fov_nudge{};    // FOV 步进：+1 增大 / -1 缩小（FOV 键或手柄十字键的按下沿；按住时连发折算成多格）
    int focus_nudge{};     // 对焦步进：+1 拉近（距离减小）/ -1 拉远（按住连发同 FOV）
    int aperture_nudge{};  // 光圈步进：+1 开大（F 值减小、更虚化）/ -1 收小
    bool dof_toggle{};        // 景深总开关
    double pad_look_dx{};  // 手柄右摇杆转速（度/秒，正值 = 向右；tick 里乘 dt）
    double pad_look_dy{};  // 手柄右摇杆转速（度/秒，正值 = 向下；invert_y 在 tick 里处理）
};

// 纯状态机：喂 Win32 风格事件，产出 Intent。不依赖引擎，可单测。
class InputLogic {
public:
    void on_key(int vk, bool down);
    void on_mouse_move(double dx, double dy);
    void on_wheel(double notches);
    // 每帧一次：喂入手柄快照（没接手柄/被禁用/切后台时喂空状态，所有组合视为松开）
    void on_gamepad(const PadState& state);

    bool held(int vk) const;

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
    // 配置到通用修饰键（SHIFT/CONTROL/ALT）时，左右两侧的具体键也算按住：
    // 保持旧版「Shift 左右都行」的手感；用户显式绑 LEFT_SHIFT 时则只认左 Shift。
    bool modifier_held(int vk) const;
    // 组合键：全部按钮按下才算“按住”；本帧按住且上一帧没按住才算一次按下沿
    bool pad_combo_down(const PadState& s, const std::vector<PadButton>& combo) const;
    bool pad_combo_pressed(const std::vector<PadButton>& combo) const;

    // 单个方向的按住连发（FOV / 对焦 / 光圈共用）：按下沿立即 1 格，按住超过延迟后按固定间隔补格
    struct RepeatState {
        bool was_down{};  // 上一帧该方向是否按住（键盘键或手柄组合任一）
        double hold{};    // 已连续按住的时长（秒）
        double next{};    // 下一次连发步进的时间点（秒，从按下起算）
    };
    int axis_nudge(int vk, const std::vector<PadButton>& combo, double dt, RepeatState& st);

    std::unordered_set<int> held_;
    std::unordered_set<int> pressed_;
    double dx_{};
    double dy_{};
    double wheel_{};
    PadState pad_{};
    PadState pad_prev_{};
    RepeatState fov_up_repeat_{};
    RepeatState fov_down_repeat_{};
    RepeatState focus_near_repeat_{};   // 对焦拉近（对焦/光圈与 FOV 共用同一套连发状态机）
    RepeatState focus_far_repeat_{};
    RepeatState aperture_open_repeat_{};
    RepeatState aperture_close_repeat_{};
};

}  // namespace pbzpm
