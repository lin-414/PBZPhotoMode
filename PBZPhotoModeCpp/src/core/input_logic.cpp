#include "core/input_logic.h"

#include "core/vk.h"

namespace pbzpm {
namespace {

constexpr int kW = 'W';
constexpr int kA = 'A';
constexpr int kS = 'S';
constexpr int kD = 'D';
constexpr int kE = 'E';
constexpr int kQ = 'Q';

// FOV 按住连发：首格延迟与连发间隔（秒）。单击仍是一格；按住 0.35 秒后以 12.5 格/秒连发。
// 键盘 FOV 键与手柄十字键同规则。
constexpr double kFovRepeatDelay = 0.35;
constexpr double kFovRepeatInterval = 0.08;

int resolve_key(const std::string& name, int fallback, const char* label, std::vector<std::string>* warnings, bool& all_ok) {
    const int vk = vk_from_name(name);
    if (vk == 0) {
        all_ok = false;
        if (warnings) warnings->push_back(std::string(label) + " 无法识别：" + name + "，已回退 " + vk_to_name(fallback));
        return fallback;
    }
    return vk;
}

}  // namespace

KeyMap build_keymap(const Config& cfg, std::vector<std::string>* warnings) {
    KeyMap map;
    map.all_resolved = true;
    map.toggle = resolve_key(cfg.key_toggle, map.toggle, "key_toggle", warnings, map.all_resolved);
    map.freeze = resolve_key(cfg.key_freeze, map.freeze, "key_freeze", warnings, map.all_resolved);
    map.reset = resolve_key(cfg.key_reset, map.reset, "key_reset", warnings, map.all_resolved);
    map.roll_reset = resolve_key(cfg.key_roll_reset, map.roll_reset, "key_roll_reset", warnings, map.all_resolved);
    map.fov_up = resolve_key(cfg.key_fov_up, map.fov_up, "key_fov_up", warnings, map.all_resolved);
    map.fov_down = resolve_key(cfg.key_fov_down, map.fov_down, "key_fov_down", warnings, map.all_resolved);
    map.pitch_up = resolve_key(cfg.key_pitch_up, map.pitch_up, "key_pitch_up", warnings, map.all_resolved);
    map.pitch_down = resolve_key(cfg.key_pitch_down, map.pitch_down, "key_pitch_down", warnings, map.all_resolved);
    map.yaw_left = resolve_key(cfg.key_yaw_left, map.yaw_left, "key_yaw_left", warnings, map.all_resolved);
    map.yaw_right = resolve_key(cfg.key_yaw_right, map.yaw_right, "key_yaw_right", warnings, map.all_resolved);

    const auto resolve_combo = [&](const std::string& text, const std::vector<PadButton>& fallback, const char* label) {
        const PadCombo parsed = parse_pad_combo(text, fallback, label, warnings);
        if (!parsed.ok) map.all_resolved = false;
        return parsed.buttons;
    };
    map.pad_toggle = resolve_combo(cfg.pad_toggle, map.pad_toggle, "pad_toggle");
    map.pad_freeze = resolve_combo(cfg.pad_freeze, map.pad_freeze, "pad_freeze");
    map.pad_reset = resolve_combo(cfg.pad_reset, map.pad_reset, "pad_reset");
    map.pad_roll_reset = resolve_combo(cfg.pad_roll_reset, map.pad_roll_reset, "pad_roll_reset");
    map.pad_fov_up = resolve_combo(cfg.pad_fov_up, map.pad_fov_up, "pad_fov_up");
    map.pad_fov_down = resolve_combo(cfg.pad_fov_down, map.pad_fov_down, "pad_fov_down");
    map.pad_pitch_up = resolve_combo(cfg.pad_pitch_up, map.pad_pitch_up, "pad_pitch_up");
    map.pad_pitch_down = resolve_combo(cfg.pad_pitch_down, map.pad_pitch_down, "pad_pitch_down");
    map.pad_yaw_left = resolve_combo(cfg.pad_yaw_left, map.pad_yaw_left, "pad_yaw_left");
    map.pad_yaw_right = resolve_combo(cfg.pad_yaw_right, map.pad_yaw_right, "pad_yaw_right");
    return map;
}

void InputLogic::on_key(int vk, bool down) {
    if (vk == 0) return;
    if (down) {
        if (held_.insert(vk).second) pressed_.insert(vk);  // 只有从松到按时才算一次按下沿
    } else {
        held_.erase(vk);
    }
}

void InputLogic::on_mouse_move(double dx, double dy) {
    dx_ += dx;
    dy_ += dy;
}

void InputLogic::on_mouse_button(MouseButton button, bool down) {
    if (button == MouseButton::Left) {
        lmb_ = down;
    } else {
        rmb_ = down;
    }
}

void InputLogic::on_wheel(double notches) {
    wheel_ += notches;
}

void InputLogic::on_gamepad(const PadState& state) {
    pad_ = state;
}

bool InputLogic::pad_combo_down(const PadState& s, const std::vector<PadButton>& combo) const {
    if (combo.empty()) return false;
    for (const PadButton b : combo) {
        if (!pad_held(s, b)) return false;
    }
    return true;
}

bool InputLogic::pad_combo_pressed(const std::vector<PadButton>& combo) const {
    return pad_combo_down(pad_, combo) && !pad_combo_down(pad_prev_, combo);
}

int InputLogic::fov_axis_nudge(int vk, const std::vector<PadButton>& combo, double dt, FovRepeat& st) {
    // 键盘键与手柄组合合成一个方向通道，谁先按下都算同一次按下沿
    const bool down = key_held(vk) || pad_combo_down(pad_, combo);
    int nudge = 0;
    if (down) {
        if (!st.was_down) {
            nudge = 1;  // 按下沿：立即一格
            st.hold = 0.0;
            st.next = kFovRepeatDelay;
        } else {
            st.hold += dt;
            while (st.hold >= st.next) {  // 大掉帧时一次补多格，节奏不变
                ++nudge;
                st.next += kFovRepeatInterval;
            }
        }
    } else {
        st.hold = 0.0;
        st.next = 0.0;
    }
    st.was_down = down;
    return nudge;
}

bool InputLogic::held(int vk) const {
    return key_held(vk);
}

bool InputLogic::alt_down() const {
    return key_held(kAltGeneric) || key_held(kAltLeft) || key_held(kAltRight);
}

Intent InputLogic::build(const Config& cfg, const KeyMap& keys, double dt) {
    Intent out;

    const bool shift = key_held(kShiftGeneric) || key_held(kShiftLeft) || key_held(kShiftRight);
    const bool ctrl = key_held(kCtrlGeneric) || key_held(kCtrlLeft) || key_held(kCtrlRight);

    out.boost = shift;
    out.slow = ctrl;

    out.move_forward = (key_held(kW) ? 1.0 : 0.0) - (key_held(kS) ? 1.0 : 0.0);
    out.move_right = (key_held(kD) ? 1.0 : 0.0) - (key_held(kA) ? 1.0 : 0.0);

    // 方向键左/右转视角（1.0.46 起从左右平移改为偏航，与上/下俯仰组成完整视角旋转簇）；WASD 平移不受影响
    out.yaw_axis = (key_held(keys.yaw_right) ? 1.0 : 0.0) - (key_held(keys.yaw_left) ? 1.0 : 0.0);
    // Q/E 从升降改为滚转（v1.9.0）：上下平移只留给鼠标左右键与手柄 RT/LT
    out.roll = (key_held(kE) ? 1.0 : 0.0) - (key_held(kQ) ? 1.0 : 0.0);

    // 方向键俯仰：按住持续转（+1 仰视 / -1 俯视），不受 free_look_requires_alt 限制
    out.pitch_axis = (key_held(keys.pitch_up) ? 1.0 : 0.0) - (key_held(keys.pitch_down) ? 1.0 : 0.0);

    // 左右键：translate 模式下是上下平移，look 模式下是俯仰
    if (cfg.mouse_vertical_mode == MouseVerticalMode::Translate) {
        out.move_up += (lmb_ ? 1.0 : 0.0) - (rmb_ ? 1.0 : 0.0);
    } else {
        out.pitch_up_click = lmb_;
        out.pitch_down_click = rmb_;
    }

    // 自由转视角必须按住 Alt（可关），否则鼠标位移一律丢弃
    const bool look_allowed = !cfg.free_look_requires_alt || alt_down();
    if (look_allowed) {
        out.look_dx = dx_;
        out.look_dy = dy_;
    }
    // 滚轮不受 Alt 限制：需求里“滚轮调 FOV”是独立操作
    out.wheel = wheel_;

    out.toggle_mode = key_pressed(keys.toggle);
    out.toggle_freeze = key_pressed(keys.freeze);
    out.reset_view = key_pressed(keys.reset);
    out.reset_roll = key_pressed(keys.roll_reset);

    // FOV 步进：键盘 FOV 键 + 手柄十字键；按下沿立即一格，持续按住先延迟再连发
    out.fov_nudge += fov_axis_nudge(keys.fov_up, keys.pad_fov_up, dt, fov_up_repeat_);
    out.fov_nudge -= fov_axis_nudge(keys.fov_down, keys.pad_fov_down, dt, fov_down_repeat_);

    // 手柄：enabled=false 时快照全为“松开”，任何组合都不会触发
    if (cfg.pad_enabled) {
        out.toggle_mode = out.toggle_mode || pad_combo_pressed(keys.pad_toggle);
        out.toggle_freeze = out.toggle_freeze || pad_combo_pressed(keys.pad_freeze);
        out.reset_view = out.reset_view || pad_combo_pressed(keys.pad_reset);
        out.reset_roll = out.reset_roll || pad_combo_pressed(keys.pad_roll_reset);

        // 左摇杆移动（模拟行程，deadzone 后 0..1）
        double lx = pad_.lx;
        double ly = pad_.ly;
        apply_stick_deadzone(lx, ly, cfg.pad_deadzone);
        out.move_forward += ly * cfg.pad_move_scale;
        out.move_right += lx * cfg.pad_move_scale;

        // 右摇杆视角：折算成度/秒；XInput 正 = 上，Intent 约定正 = 向下，翻转 Y
        double rx = pad_.rx;
        double ry = pad_.ry;
        apply_stick_deadzone(rx, ry, cfg.pad_deadzone);
        out.pad_look_dx = rx * cfg.pad_look_speed;
        out.pad_look_dy = -ry * cfg.pad_look_speed;

        // RT 上 / LT 下（模拟量）；RB/LB 滚转（与 Q/E 同向：RB 顺时针 / LB 逆时针）；两键同按是滚转归零组合键
        out.move_up += pad_.rt - pad_.lt;
        out.roll += (pad_held(pad_, PadButton::RB) ? 1.0 : 0.0) - (pad_held(pad_, PadButton::LB) ? 1.0 : 0.0);

        // 十字键上/下俯仰（上 = 仰视 / 下 = 俯视）、左/右偏航（左 = 左转 / 右 = 右转，1.0.46 起从平移改来）；
        // 十字键全部用于视角调节（FOV 已移到 A/B）
        out.pitch_axis += pad_combo_down(pad_, keys.pad_pitch_up) ? 1.0 : 0.0;
        out.pitch_axis -= pad_combo_down(pad_, keys.pad_pitch_down) ? 1.0 : 0.0;
        out.yaw_axis += pad_combo_down(pad_, keys.pad_yaw_right) ? 1.0 : 0.0;
        out.yaw_axis -= pad_combo_down(pad_, keys.pad_yaw_left) ? 1.0 : 0.0;

        // 变速原在 LB/RB，随滚转让位到摇杆按下：LS 加速 / RS 减速；
        // 两杆同时按住是进出模式组合键，此时不触发变速
        const bool ls = pad_held(pad_, PadButton::LS);
        const bool rs = pad_held(pad_, PadButton::RS);
        out.boost = out.boost || (ls && !rs);
        out.slow = out.slow || (rs && !ls);
    }
    return out;
}

void InputLogic::end_frame() {
    pressed_.clear();
    dx_ = 0.0;
    dy_ = 0.0;
    wheel_ = 0.0;
    pad_prev_ = pad_;
}

void InputLogic::reset() {
    held_.clear();
    pressed_.clear();
    dx_ = 0.0;
    dy_ = 0.0;
    wheel_ = 0.0;
    lmb_ = false;
    rmb_ = false;
    // 注意：pad_/pad_prev_ 不能清。切换摄影模式时组合键还按着（如 LS+RS），
    // 清掉上一帧状态会让“仍按住”被当成新的按下沿，进出模式死循环。
    // 键盘没有这个问题：win_input 的 key_state_ 在 reset 之外自己记住边沿。
    // FOV 连发计时同理清零，但 was_down 保留——十字键仍按住时不算新的按下沿。
    // next 归位到首格延迟而不是 0：按住时进出模式，回来第一帧不应立刻连发。
    fov_up_repeat_.hold = 0.0;
    fov_up_repeat_.next = kFovRepeatDelay;
    fov_down_repeat_.hold = 0.0;
    fov_down_repeat_.next = kFovRepeatDelay;
}

}  // namespace pbzpm
