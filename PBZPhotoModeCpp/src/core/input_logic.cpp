#include "core/input_logic.h"

#include "core/vk.h"

namespace pbzpm {
namespace {

// 按住连发：首格延迟与连发间隔（秒）。单击仍是一格；按住 0.35 秒后以 12.5 格/秒连发。
// FOV 键 / 对焦键 / 光圈键 / 手柄组合键同规则（FOV 十字键与 A/B 也走这一套）。
constexpr double kRepeatDelay = 0.35;
constexpr double kRepeatInterval = 0.08;

int resolve_key(const std::string& name, int fallback, const char* label, std::vector<std::string>* warnings, bool& all_ok) {
    const int vk = vk_from_name(name);
    if (vk == 0) {
        all_ok = false;
        if (warnings) warnings->push_back(std::string(label) + " 无法识别：" + name + "，已回退 " + vk_to_name(fallback));
        return fallback;
    }
    return vk;
}

// 手柄组合键的「量」：单键 RT/LT 保留模拟量行程（扳机可以半按），其余按 0/1。
// 可配置后用户可能把升降改到任意按钮/组合，这里统一成同一条规则。
double pad_combo_amount(const PadState& s, const std::vector<PadButton>& combo) {
    if (combo.empty()) return 0.0;
    for (const PadButton b : combo) {
        if (!pad_held(s, b)) return 0.0;
    }
    if (combo.size() == 1) {
        if (combo[0] == PadButton::RT) return s.rt;
        if (combo[0] == PadButton::LT) return s.lt;
    }
    return 1.0;
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
    map.roll_left = resolve_key(cfg.key_roll_left, map.roll_left, "key_roll_left", warnings, map.all_resolved);
    map.roll_right = resolve_key(cfg.key_roll_right, map.roll_right, "key_roll_right", warnings, map.all_resolved);
    map.vertical_up = resolve_key(cfg.key_vertical_up, map.vertical_up, "key_vertical_up", warnings, map.all_resolved);
    map.vertical_down = resolve_key(cfg.key_vertical_down, map.vertical_down, "key_vertical_down", warnings, map.all_resolved);
    map.move_forward = resolve_key(cfg.key_move_forward, map.move_forward, "key_move_forward", warnings, map.all_resolved);
    map.move_back = resolve_key(cfg.key_move_back, map.move_back, "key_move_back", warnings, map.all_resolved);
    map.move_left = resolve_key(cfg.key_move_left, map.move_left, "key_move_left", warnings, map.all_resolved);
    map.move_right = resolve_key(cfg.key_move_right, map.move_right, "key_move_right", warnings, map.all_resolved);
    map.boost = resolve_key(cfg.key_boost, map.boost, "key_boost", warnings, map.all_resolved);
    map.slow = resolve_key(cfg.key_slow, map.slow, "key_slow", warnings, map.all_resolved);
    map.free_look = resolve_key(cfg.key_free_look, map.free_look, "key_free_look", warnings, map.all_resolved);
    map.frame_freeze = resolve_key(cfg.key_frame_freeze, map.frame_freeze, "key_frame_freeze", warnings, map.all_resolved);
    map.dof_toggle = resolve_key(cfg.key_dof_toggle, map.dof_toggle, "key_dof_toggle", warnings, map.all_resolved);
    map.focus_near = resolve_key(cfg.key_focus_near, map.focus_near, "key_focus_near", warnings, map.all_resolved);
    map.focus_far = resolve_key(cfg.key_focus_far, map.focus_far, "key_focus_far", warnings, map.all_resolved);
    map.aperture_open = resolve_key(cfg.key_aperture_open, map.aperture_open, "key_aperture_open", warnings, map.all_resolved);
    map.aperture_close = resolve_key(cfg.key_aperture_close, map.aperture_close, "key_aperture_close", warnings, map.all_resolved);

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
    map.pad_roll_left = resolve_combo(cfg.pad_roll_left, map.pad_roll_left, "pad_roll_left");
    map.pad_roll_right = resolve_combo(cfg.pad_roll_right, map.pad_roll_right, "pad_roll_right");
    map.pad_boost = resolve_combo(cfg.pad_boost, map.pad_boost, "pad_boost");
    map.pad_slow = resolve_combo(cfg.pad_slow, map.pad_slow, "pad_slow");
    map.pad_vertical_up = resolve_combo(cfg.pad_vertical_up, map.pad_vertical_up, "pad_vertical_up");
    map.pad_vertical_down = resolve_combo(cfg.pad_vertical_down, map.pad_vertical_down, "pad_vertical_down");
    map.pad_focus_near = resolve_combo(cfg.pad_focus_near, map.pad_focus_near, "pad_focus_near");
    map.pad_focus_far = resolve_combo(cfg.pad_focus_far, map.pad_focus_far, "pad_focus_far");
    map.pad_aperture_open = resolve_combo(cfg.pad_aperture_open, map.pad_aperture_open, "pad_aperture_open");
    map.pad_aperture_close = resolve_combo(cfg.pad_aperture_close, map.pad_aperture_close, "pad_aperture_close");
    map.pad_dof_toggle = resolve_combo(cfg.pad_dof_toggle, map.pad_dof_toggle, "pad_dof_toggle");
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

int InputLogic::axis_nudge(int vk, const std::vector<PadButton>& combo, double dt, RepeatState& st) {
    // 键盘键与手柄组合合成一个方向通道，谁先按下都算同一次按下沿
    const bool down = key_held(vk) || pad_combo_down(pad_, combo);
    int nudge = 0;
    if (down) {
        if (!st.was_down) {
            nudge = 1;  // 按下沿：立即一格
            st.hold = 0.0;
            st.next = kRepeatDelay;
        } else {
            st.hold += dt;
            while (st.hold >= st.next) {  // 大掉帧时一次补多格，节奏不变
                ++nudge;
                st.next += kRepeatInterval;
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

bool InputLogic::modifier_held(int vk) const {
    switch (vk) {
        case kShiftGeneric:
            return key_held(kShiftGeneric) || key_held(kShiftLeft) || key_held(kShiftRight);
        case kCtrlGeneric:
            return key_held(kCtrlGeneric) || key_held(kCtrlLeft) || key_held(kCtrlRight);
        case kAltGeneric:
            return key_held(kAltGeneric) || key_held(kAltLeft) || key_held(kAltRight);
        default:
            return key_held(vk);
    }
}

Intent InputLogic::build(const Config& cfg, const KeyMap& keys, double dt) {
    Intent out;

    // 移动/变速/自由视角门控：v2.0.0-cpp-hotfix9 起全部可配置，默认值与旧写死键一致。
    out.boost = modifier_held(keys.boost);
    out.slow = modifier_held(keys.slow);

    out.move_forward = (key_held(keys.move_forward) ? 1.0 : 0.0) - (key_held(keys.move_back) ? 1.0 : 0.0);
    out.move_right = (key_held(keys.move_right) ? 1.0 : 0.0) - (key_held(keys.move_left) ? 1.0 : 0.0);

    // 方向键左/右转视角（1.0.46 起从左右平移改为偏航，与上/下俯仰组成完整视角旋转簇）；WASD 平移不受影响
    out.yaw_axis = (key_held(keys.yaw_right) ? 1.0 : 0.0) - (key_held(keys.yaw_left) ? 1.0 : 0.0);
    // 滚转（默认 Q/E，可经 [keys] key_roll_left/right 改键）：上下平移只留给升降键与手柄 RT/LT
    out.roll = (key_held(keys.roll_right) ? 1.0 : 0.0) - (key_held(keys.roll_left) ? 1.0 : 0.0);

    // 方向键俯仰：按住持续转（+1 仰视 / -1 俯视），不受 free_look_requires_alt 限制
    out.pitch_axis = (key_held(keys.pitch_up) ? 1.0 : 0.0) - (key_held(keys.pitch_down) ? 1.0 : 0.0);

    // 升降键（默认鼠标左/右键，可在 [keys] 改）：translate 模式下是上下平移，look 模式下是俯仰
    const bool vertical_up = key_held(keys.vertical_up);
    const bool vertical_down = key_held(keys.vertical_down);
    if (cfg.mouse_vertical_mode == MouseVerticalMode::Translate) {
        out.move_up += (vertical_up ? 1.0 : 0.0) - (vertical_down ? 1.0 : 0.0);
    } else {
        out.pitch_up_click = vertical_up;
        out.pitch_down_click = vertical_down;
    }

    // 自由转视角必须按住配置的门控键（默认 Alt，可关），否则鼠标位移一律丢弃
    const bool look_allowed = !cfg.free_look_requires_alt || modifier_held(keys.free_look);
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

    // 步进键统一走「按下沿一格 + 按住连发」：FOV / 对焦 / 光圈共用一个状态机，
    // 键盘键与手柄组合合成同一条方向通道（手柄禁用时快照恒为松开，组合不会触发）。
    out.fov_nudge += axis_nudge(keys.fov_up, keys.pad_fov_up, dt, fov_up_repeat_);
    out.fov_nudge -= axis_nudge(keys.fov_down, keys.pad_fov_down, dt, fov_down_repeat_);
    // 对焦拉近/拉远（+1 = 拉近，距离减小）、光圈开/关（+1 = 开大，F 值减小）
    out.focus_nudge += axis_nudge(keys.focus_near, keys.pad_focus_near, dt, focus_near_repeat_);
    out.focus_nudge -= axis_nudge(keys.focus_far, keys.pad_focus_far, dt, focus_far_repeat_);
    out.aperture_nudge += axis_nudge(keys.aperture_open, keys.pad_aperture_open, dt, aperture_open_repeat_);
    out.aperture_nudge -= axis_nudge(keys.aperture_close, keys.pad_aperture_close, dt, aperture_close_repeat_);

    // 景深/取焦单发键（按下沿）
    out.dof_toggle = key_pressed(keys.dof_toggle);

    // 手柄：enabled=false 时快照全为“松开”，任何组合都不会触发
    if (cfg.pad_enabled) {
        out.toggle_mode = out.toggle_mode || pad_combo_pressed(keys.pad_toggle);
        out.toggle_freeze = out.toggle_freeze || pad_combo_pressed(keys.pad_freeze);
        out.reset_view = out.reset_view || pad_combo_pressed(keys.pad_reset);
        out.reset_roll = out.reset_roll || pad_combo_pressed(keys.pad_roll_reset);
        out.dof_toggle = out.dof_toggle || pad_combo_pressed(keys.pad_dof_toggle);

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

        // 升降：默认 RT 上 / LT 下（单键绑定保留模拟量）；组合键可经 [gamepad] 改。
        out.move_up += pad_combo_amount(pad_, keys.pad_vertical_up) -
                       pad_combo_amount(pad_, keys.pad_vertical_down);
            out.roll += pad_combo_amount(pad_, keys.pad_roll_right) -
                    pad_combo_amount(pad_, keys.pad_roll_left);

        // 十字键上/下俯仰（上 = 仰视 / 下 = 俯视）、左/右偏航（左 = 左转 / 右 = 右转，1.0.46 起从平移改来）；
        // 十字键全部用于视角调节（FOV 已移到 A/B）
        out.pitch_axis += pad_combo_down(pad_, keys.pad_pitch_up) ? 1.0 : 0.0;
        out.pitch_axis -= pad_combo_down(pad_, keys.pad_pitch_down) ? 1.0 : 0.0;
        out.yaw_axis += pad_combo_down(pad_, keys.pad_yaw_right) ? 1.0 : 0.0;
        out.yaw_axis -= pad_combo_down(pad_, keys.pad_yaw_left) ? 1.0 : 0.0;

        // 变速默认 LS 加速 / RS 减速（可经 [gamepad] 改）；
        // 两键同时按住是进出模式组合键，此时不触发变速
        const bool pad_boost = pad_combo_down(pad_, keys.pad_boost);
        const bool pad_slow = pad_combo_down(pad_, keys.pad_slow);
        out.boost = out.boost || (pad_boost && !pad_slow);
        out.slow = out.slow || (pad_slow && !pad_boost);
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
    // 注意：pad_/pad_prev_ 不能清。切换摄影模式时组合键还按着（如 LS+RS），
    // 清掉上一帧状态会让“仍按住”被当成新的按下沿，进出模式死循环。
    // 键盘没有这个问题：win_input 的 key_state_ 在 reset 之外自己记住边沿。
    // 步进键的连发计时清零，但 was_down 保留——仍按住时不算新的按下沿。
    // next 归位到首格延迟而不是 0：按住时进出模式，回来第一帧不应立刻连发。
    for (RepeatState* st : {&fov_up_repeat_, &fov_down_repeat_, &focus_near_repeat_, &focus_far_repeat_,
                            &aperture_open_repeat_, &aperture_close_repeat_}) {
        st->hold = 0.0;
        st->next = kRepeatDelay;
    }
}

}  // namespace pbzpm
