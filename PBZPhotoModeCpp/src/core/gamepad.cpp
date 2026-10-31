#include "core/gamepad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace pbzpm {
namespace {

struct PadAlias {
    PadButton button;
    const char* names;  // 以逗号分隔的别名表（首位是规范名）
};

constexpr PadAlias kPadAliases[] = {
    {PadButton::A, "A,CROSS"},
    {PadButton::B, "B,CIRCLE"},
    {PadButton::X, "X,SQUARE"},
    {PadButton::Y, "Y,TRIANGLE"},
    {PadButton::LB, "LB,L1"},
    {PadButton::RB, "RB,R1"},
    {PadButton::LS, "LS,L3"},
    {PadButton::RS, "RS,R3"},
    {PadButton::Back, "BACK,SHARE,SELECT"},
    {PadButton::Start, "START,OPTIONS,MENU"},
    {PadButton::Guide, "GUIDE,PS,HOME,XBOX"},
    {PadButton::Up, "UP,DPAD_UP,PAD_UP"},
    {PadButton::Down, "DOWN,DPAD_DOWN,PAD_DOWN"},
    {PadButton::Left, "LEFT,DPAD_LEFT,PAD_LEFT"},
    {PadButton::Right, "RIGHT,DPAD_RIGHT,PAD_RIGHT"},
    {PadButton::LT, "LT,L2"},
    {PadButton::RT, "RT,R2"},
};

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

}  // namespace

bool pad_button_from_name(const std::string& name, PadButton& out) {
    const std::string key = upper(name);
    if (key.empty()) return false;
    for (const PadAlias& alias : kPadAliases) {
        std::istringstream stream(alias.names);
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (token == key) {
                out = alias.button;
                return true;
            }
        }
    }
    return false;
}

std::string pad_button_name(PadButton b) {
    for (const PadAlias& alias : kPadAliases) {
        if (alias.button == b) {
            std::istringstream stream(alias.names);
            std::string token;
            std::getline(stream, token, ',');
            return token;
        }
    }
    return "UNKNOWN";
}

PadCombo parse_pad_combo(const std::string& text, const std::vector<PadButton>& fallback, const char* label,
                         std::vector<std::string>* warnings) {
    const auto fail = [&](const std::string& reason) -> PadCombo {
        if (warnings != nullptr) {
            warnings->push_back(std::string(label) + " " + reason + "，已回退 " + pad_combo_name(fallback));
        }
        return {fallback, false};
    };

    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    std::string trimmed = text;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), not_space));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), not_space).base(), trimmed.end());
    if (trimmed.empty()) return fail("为空");

    std::vector<PadButton> buttons;
    std::istringstream stream(trimmed);
    std::string token;
    while (std::getline(stream, token, '+')) {
        std::string key = upper(token);
        // 去掉首尾空白（"LS + RS" 这种写法）
        key.erase(key.begin(), std::find_if(key.begin(), key.end(), not_space));
        key.erase(std::find_if(key.rbegin(), key.rend(), not_space).base(), key.end());
        if (key.empty()) return fail("含空按钮名：" + text);
        if (key == "NONE") {
            if (buttons.empty() && stream.eof()) return {std::vector<PadButton>{}, true};  // 显式解绑
            return fail("NONE 只能单独写");
        }
        PadButton b{};
        if (!pad_button_from_name(key, b)) return fail("无法识别按钮 " + key);
        buttons.push_back(b);
    }
    return {buttons, true};
}

std::string pad_combo_name(const std::vector<PadButton>& combo) {
    if (combo.empty()) return "NONE";
    std::string out;
    for (size_t i = 0; i < combo.size(); ++i) {
        if (i > 0) out += '+';
        out += pad_button_name(combo[i]);
    }
    return out;
}

void apply_stick_deadzone(double& x, double& y, double deadzone) {
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    if (y > 1.0) y = 1.0;
    if (y < -1.0) y = -1.0;
    if (deadzone <= 0.0) return;
    if (deadzone >= 1.0) {
        x = 0.0;
        y = 0.0;
        return;
    }
    const double mag = std::sqrt(x * x + y * y);
    if (mag <= deadzone || mag <= 1e-9) {
        x = 0.0;
        y = 0.0;
        return;
    }
    const double scaled = std::min(1.0, (mag - deadzone) / (1.0 - deadzone));
    const double k = scaled / mag;
    x *= k;
    y *= k;
}

// ---------------- DirectInput（PS 手柄）映射 ----------------

bool dinput_button_to_pad(int index, PadButton& out) {
    switch (index) {
        case 0: out = PadButton::X; return true;       // □
        case 1: out = PadButton::A; return true;       // ╳
        case 2: out = PadButton::B; return true;       // ○
        case 3: out = PadButton::Y; return true;       // △
        case 4: out = PadButton::LB; return true;      // L1
        case 5: out = PadButton::RB; return true;      // R1
        case 6: out = PadButton::LT; return true;      // L2（数字镜像，L2 还另有模拟量）
        case 7: out = PadButton::RT; return true;      // R2（数字镜像）
        case 8: out = PadButton::Back; return true;    // CREATE / SHARE
        case 9: out = PadButton::Start; return true;   // OPTIONS
        case 10: out = PadButton::LS; return true;     // L3
        case 11: out = PadButton::RS; return true;     // R3
        case 12: out = PadButton::Guide; return true;  // PS 键
        default: return false;                         // 13=触摸板按键等，无对应
    }
}

void dinput_pov_to_buttons(unsigned long pov, bool out[4]) {
    out[0] = out[1] = out[2] = out[3] = false;
    if (pov > 35999UL) return;  // 居中（低字 0xFFFF）或无效值
    // 每 45° 一个扇区：0=上 1=上右 2=右 … 7=左上
    const int sector = static_cast<int>(((pov + 2250UL) / 4500UL) % 8UL);
    out[0] = sector == 7 || sector <= 1;  // 上
    out[1] = sector >= 1 && sector <= 3;  // 右
    out[2] = sector >= 3 && sector <= 5;  // 下
    out[3] = sector >= 5 && sector <= 7;  // 左
}

double dinput_axis_to_state(DInputAxis axis, long value) {
    double v = 0.0;
    switch (axis) {
        case DInputAxis::LX:
        case DInputAxis::LZ:
            v = (static_cast<double>(value) - 32767.5) / 32767.5;
            break;
        case DInputAxis::LY:
        case DInputAxis::LRz:
            v = -(static_cast<double>(value) - 32767.5) / 32767.5;  // HID 向下为正 → 取反成 XInput 约定
            break;
        case DInputAxis::LRx:
        case DInputAxis::LRy:
            v = static_cast<double>(value) / 65535.0;  // 扳机：0=松开
            break;
    }
    if (v > 1.0) v = 1.0;
    if (v < -1.0) v = -1.0;
    return v;
}

}  // namespace pbzpm
