#pragma once

#include <string>
#include <vector>

namespace pbzpm {

// XInput 风格的手柄按钮。PS 手柄的叫法也都能解析（CROSS=A、L3=LS、OPTIONS=START…），
// 对外统一用 Xbox 名字。LT/RT 是扳机：数值超过 kPadTriggerHeld 视为按下。
enum class PadButton {
    A, B, X, Y,
    LB, RB,
    LS, RS,  // 摇杆按下（L3 / R3）
    Back, Start, Guide,
    Up, Down, Left, Right,  // 十字键
    LT, RT,
};

inline constexpr int kPadButtonCount = 17;
// 扳机视为「按下」的阈值（0..1）
inline constexpr double kPadTriggerHeld = 0.3;

// 每帧手柄快照（与具体 API 解耦：win_input 从 XInput 翻译，测试直接构造）。
// 摇杆 lx/ly/rx/ry：-1..1，正值 = 右 / 上（XInput 原始方向）；
// 扳机 lt/rt：0..1。
struct PadState {
    bool held[kPadButtonCount]{};
    double lx{}, ly{}, rx{}, ry{};
    double lt{}, rt{};
};

// 按钮名 -> 枚举，大小写不敏感。返回 false 表示无法识别。
bool pad_button_from_name(const std::string& name, PadButton& out);

// 枚举 -> 规范名（Xbox 风格大写），日志/菜单用
std::string pad_button_name(PadButton b);

// 组合键解析结果
struct PadCombo {
    std::vector<PadButton> buttons{};
    bool ok{true};
};

// 解析 "LS+RS" / "START" / "NONE"（= 解绑）这类组合键写法。
// 任一按钮无法识别时回退 fallback 并记 warning；NONE 返回空组合（视为未绑定）。
PadCombo parse_pad_combo(const std::string& text, const std::vector<PadButton>& fallback, const char* label,
                         std::vector<std::string>* warnings);

// 组合键 -> 可读名字（日志/菜单用）；空组合输出 NONE
std::string pad_combo_name(const std::vector<PadButton>& combo);

// 摇杆径向死区：分量先夹紧到 [-1,1]；幅度 <= deadzone 归零，其余线性缩放回 0..1。
void apply_stick_deadzone(double& x, double& y, double deadzone);

// 按钮是否处于按下状态（LT/RT 按阈值判，其余按 held[] 判）
inline bool pad_held(const PadState& s, PadButton b) {
    if (b == PadButton::LT) return s.lt >= kPadTriggerHeld;
    if (b == PadButton::RT) return s.rt >= kPadTriggerHeld;
    const int i = static_cast<int>(b);
    return i >= 0 && i < kPadButtonCount && s.held[i];
}

}  // namespace pbzpm
