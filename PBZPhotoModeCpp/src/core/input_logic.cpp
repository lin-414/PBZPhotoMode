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
    map.menu = resolve_key(cfg.key_menu, map.menu, "key_menu", warnings, map.all_resolved);
    map.freeze = resolve_key(cfg.key_freeze, map.freeze, "key_freeze", warnings, map.all_resolved);
    map.reset = resolve_key(cfg.key_reset, map.reset, "key_reset", warnings, map.all_resolved);
    map.fov_up = resolve_key(cfg.key_fov_up, map.fov_up, "key_fov_up", warnings, map.all_resolved);
    map.fov_down = resolve_key(cfg.key_fov_down, map.fov_down, "key_fov_down", warnings, map.all_resolved);

    const auto resolve_combo = [&](const std::string& text, const std::vector<PadButton>& fallback, const char* label) {
        const PadCombo parsed = parse_pad_combo(text, fallback, label, warnings);
        if (!parsed.ok) map.all_resolved = false;
        return parsed.buttons;
    };
    map.pad_toggle = resolve_combo(cfg.pad_toggle, map.pad_toggle, "pad_toggle");
    map.pad_menu = resolve_combo(cfg.pad_menu, map.pad_menu, "pad_menu");
    map.pad_freeze = resolve_combo(cfg.pad_freeze, map.pad_freeze, "pad_freeze");
    map.pad_reset = resolve_combo(cfg.pad_reset, map.pad_reset, "pad_reset");
    map.pad_fov_up = resolve_combo(cfg.pad_fov_up, map.pad_fov_up, "pad_fov_up");
    map.pad_fov_down = resolve_combo(cfg.pad_fov_down, map.pad_fov_down, "pad_fov_down");
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

bool InputLogic::held(int vk) const {
    return key_held(vk);
}

bool InputLogic::alt_down() const {
    return key_held(kAltGeneric) || key_held(kAltLeft) || key_held(kAltRight);
}

Intent InputLogic::build(const Config& cfg, const KeyMap& keys) const {
    Intent out;

    const bool shift = key_held(kShiftGeneric) || key_held(kShiftLeft) || key_held(kShiftRight);
    const bool ctrl = key_held(kCtrlGeneric) || key_held(kCtrlLeft) || key_held(kCtrlRight);

    out.boost = shift;
    out.slow = ctrl;

    out.move_forward = (key_held(kW) ? 1.0 : 0.0) - (key_held(kS) ? 1.0 : 0.0);
    out.move_right = (key_held(kD) ? 1.0 : 0.0) - (key_held(kA) ? 1.0 : 0.0);
    out.move_up = (key_held(kE) ? 1.0 : 0.0) - (key_held(kQ) ? 1.0 : 0.0);

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
    out.toggle_menu = key_pressed(keys.menu);
    out.toggle_freeze = key_pressed(keys.freeze);
    out.reset_view = key_pressed(keys.reset);

    // FOV 步进：键盘 FOV 键 + 手柄十字键
    if (key_pressed(keys.fov_up) || pad_combo_pressed(keys.pad_fov_up)) out.fov_nudge += 1;
    if (key_pressed(keys.fov_down) || pad_combo_pressed(keys.pad_fov_down)) out.fov_nudge -= 1;

    // 手柄：enabled=false 时快照全为“松开”，任何组合都不会触发
    if (cfg.pad_enabled) {
        out.toggle_mode = out.toggle_mode || pad_combo_pressed(keys.pad_toggle);
        out.toggle_menu = out.toggle_menu || pad_combo_pressed(keys.pad_menu);
        out.toggle_freeze = out.toggle_freeze || pad_combo_pressed(keys.pad_freeze);
        out.reset_view = out.reset_view || pad_combo_pressed(keys.pad_reset);

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

        // RT 上 / LT 下（模拟量）；LB 加速 / RB 减速
        out.move_up += pad_.rt - pad_.lt;
        out.boost = out.boost || pad_held(pad_, PadButton::LB);
        out.slow = out.slow || pad_held(pad_, PadButton::RB);
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
}

}  // namespace pbzpm
