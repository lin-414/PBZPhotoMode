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

// ---------------- DirectInput（PS 手柄）映射 ----------------
// DualSense / DualShock4 不是 XInput 设备，Windows 把它们枚举成 DirectInput 手柄，
// 按钮位和轴布局与 Xbox 不同，这里做 DIJOYSTATE2 → PadState 的纯翻译。
inline constexpr int kDInputButtonCount = 14;  // rgbButtons 0..13；13=触摸板按键，无对应，返回 false

// DIJOYSTATE2.rgbButtons 下标（0 基）→ PadButton。DualSense/DS4 标准 Windows HID 布局：
// 0=□(X) 1=╳(A) 2=○(B) 3=△(Y) 4=L1(LB) 5=R1(RB) 6=L2(LT) 7=R2(RT)
// 8=CREATE(Back) 9=OPTIONS(Start) 10=L3(LS) 11=R3(RS) 12=PS(Guide)。越界/无对应返回 false。
bool dinput_button_to_pad(int index, PadButton& out);

// POV 帽子开关（DIJOYSTATE2.rgdwPOV，单位 1/100 度：0=上 9000=右 18000=下 27000=左，
// 斜向 4500 等；>35999 视为居中/无效）→ 四方向。out 顺序固定：[0]=上 [1]=右 [2]=下 [3]=左。
void dinput_pov_to_buttons(unsigned long pov, bool out[4]);

// DIJOYSTATE2 轴 → PadState 数值。摇杆 LX/LZ：0..65535 中心 32767.5 → -1..1；
// LY/LRz 额外取反成「正=上」（HID 的 Y 是向下为正，翻成 XInput 约定）；扳机 LRx/LRy → 0..1。
enum class DInputAxis { LX, LY, LZ, LRz, LRx, LRy };
double dinput_axis_to_state(DInputAxis axis, long value);

// 按钮是否处于按下状态（LT/RT 按阈值判，其余按 held[] 判）
inline bool pad_held(const PadState& s, PadButton b) {
    if (b == PadButton::LT) return s.lt >= kPadTriggerHeld;
    if (b == PadButton::RT) return s.rt >= kPadTriggerHeld;
    const int i = static_cast<int>(b);
    return i >= 0 && i < kPadButtonCount && s.held[i];
}

}  // namespace pbzpm
