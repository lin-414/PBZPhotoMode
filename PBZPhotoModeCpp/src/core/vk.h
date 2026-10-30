#pragma once

#include <string>

namespace pbzpm {

// 键名 -> Win32 虚拟键码。写法与 UE Key 枚举保持一致：
//   F1..F12、A..Z、0..9、ADD/SUBTRACT/MULTIPLY/DIVIDE、OEM_PLUS/OEM_MINUS、
//   PAGE_UP/PAGE_DOWN/HOME/END/INSERT/DELETE、方向键、SPACE/TAB/ENTER/ESCAPE、
//   LEFT_ALT/RIGHT_ALT/LEFT_SHIFT/RIGHT_SHIFT/LEFT_CONTROL/RIGHT_CONTROL、TILDE。
// 返回 0 表示无法识别（调用方应记 warning 并保留默认键）。
int vk_from_name(const std::string& name);

// 生成可读名字（日志/菜单用）；未知返回 "0x.." 形式
std::string vk_to_name(int vk);

// Alt 键的三种形态（通用 VK_MENU 以及左右 Alt）
inline constexpr int kAltGeneric = 0x12;
inline constexpr int kAltLeft = 0xA4;
inline constexpr int kAltRight = 0xA5;

// 功能键（通用 + 左右）
inline constexpr int kShiftGeneric = 0x10;
inline constexpr int kShiftLeft = 0xA0;
inline constexpr int kShiftRight = 0xA1;
inline constexpr int kCtrlGeneric = 0x11;
inline constexpr int kCtrlLeft = 0xA2;
inline constexpr int kCtrlRight = 0xA3;

inline bool is_alt(int vk) {
    return vk == kAltGeneric || vk == kAltLeft || vk == kAltRight;
}

}  // namespace pbzpm
