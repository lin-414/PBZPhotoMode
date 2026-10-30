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

}  // namespace pbzpm
