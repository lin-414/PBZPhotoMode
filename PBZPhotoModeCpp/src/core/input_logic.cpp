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
    map.shutter = resolve_key(cfg.key_shutter, map.shutter, "key_shutter", warnings, map.all_resolved);
    map.fov_up = resolve_key(cfg.key_fov_up, map.fov_up, "key_fov_up", warnings, map.all_resolved);
    map.fov_down = resolve_key(cfg.key_fov_down, map.fov_down, "key_fov_down", warnings, map.all_resolved);
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
    out.shutter = key_pressed(keys.shutter);
    return out;
}

void InputLogic::end_frame() {
    pressed_.clear();
    dx_ = 0.0;
    dy_ = 0.0;
    wheel_ = 0.0;
}

void InputLogic::reset() {
    held_.clear();
    pressed_.clear();
    dx_ = 0.0;
    dy_ = 0.0;
    wheel_ = 0.0;
    lmb_ = false;
    rmb_ = false;
}

}  // namespace pbzpm
