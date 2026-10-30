#include "core/vk.h"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace pbzpm {
namespace {

struct Entry {
    const char* name;
    int vk;
};

// 顺序无关，查找时先精确匹配（大小写不敏感）。
constexpr Entry kTable[] = {
    {"BACKSPACE", 0x08}, {"TAB", 0x09}, {"ENTER", 0x0D}, {"RETURN", 0x0D}, {"SHIFT", 0x10},
    {"CONTROL", 0x11}, {"CTRL", 0x11}, {"ALT", 0x12}, {"PAUSE", 0x13}, {"CAPS_LOCK", 0x14},
    {"ESCAPE", 0x1B}, {"ESC", 0x1B}, {"SPACE", 0x20}, {"PAGE_UP", 0x21}, {"PRIOR", 0x21},
    {"PAGE_DOWN", 0x22}, {"NEXT", 0x22}, {"END", 0x23}, {"HOME", 0x24},
    {"LEFT", 0x25}, {"UP", 0x26}, {"RIGHT", 0x27}, {"DOWN", 0x28},
    {"INSERT", 0x2D}, {"DELETE", 0x2E}, {"DEL", 0x2E},
    {"NUM_ZERO", 0x60}, {"NUM_ONE", 0x61}, {"NUM_TWO", 0x62}, {"NUM_THREE", 0x63},
    {"NUM_FOUR", 0x64}, {"NUM_FIVE", 0x65}, {"NUM_SIX", 0x66}, {"NUM_SEVEN", 0x67},
    {"NUM_EIGHT", 0x68}, {"NUM_NINE", 0x69},
    {"MULTIPLY", 0x6A}, {"ADD", 0x6B}, {"SUBTRACT", 0x6D}, {"DECIMAL", 0x6E}, {"DIVIDE", 0x6F},
    {"F1", 0x70}, {"F2", 0x71}, {"F3", 0x72}, {"F4", 0x73}, {"F5", 0x74}, {"F6", 0x75},
    {"F7", 0x76}, {"F8", 0x77}, {"F9", 0x78}, {"F10", 0x79}, {"F11", 0x7A}, {"F12", 0x7B},
    {"LEFT_SHIFT", 0xA0}, {"RIGHT_SHIFT", 0xA1}, {"LEFT_CONTROL", 0xA2}, {"LEFT_CTRL", 0xA2},
    {"RIGHT_CONTROL", 0xA3}, {"RIGHT_CTRL", 0xA3}, {"LEFT_ALT", 0xA4}, {"RIGHT_ALT", 0xA5},
    {"OEM_1", 0xBA}, {"SEMICOLON", 0xBA}, {"OEM_PLUS", 0xBB}, {"EQUALS", 0xBB},
    {"OEM_COMMA", 0xBC}, {"COMMA", 0xBC}, {"OEM_MINUS", 0xBD}, {"MINUS", 0xBD},
    {"OEM_PERIOD", 0xBE}, {"PERIOD", 0xBE}, {"OEM_2", 0xBF}, {"SLASH", 0xBF},
    {"TILDE", 0xC0}, {"OEM_3", 0xC0}, {"OEM_4", 0xDB}, {"OEM_5", 0xDC}, {"OEM_6", 0xDD},
    {"OEM_7", 0xDE}, {"QUOTE", 0xDE},
    {"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'}, {"E", 'E'}, {"F", 'F'}, {"G", 'G'},
    {"H", 'H'}, {"I", 'I'}, {"J", 'J'}, {"K", 'K'}, {"L", 'L'}, {"M", 'M'}, {"N", 'N'},
    {"O", 'O'}, {"P", 'P'}, {"Q", 'Q'}, {"R", 'R'}, {"S", 'S'}, {"T", 'T'},
};

std::string upper_trim(std::string_view in) {
    size_t b = 0;
    size_t e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    std::string out(in.substr(b, e - b));
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

int vk_from_name(const std::string& name) {
    const std::string key = upper_trim(name);
    if (key.empty()) return 0;
    for (const Entry& e : kTable) {
        if (key == e.name) return e.vk;
    }
    // 单字符 U/V/W/X/Y/Z 与 0..9 未全部列在表里，这里补上字母数字兜底
    if (key.size() == 1) {
        const char c = key[0];
        if (c >= 'A' && c <= 'Z') return c;
        if (c >= '0' && c <= '9') return c;
    }
    return 0;
}

std::string vk_to_name(int vk) {
    for (const Entry& e : kTable) {
        if (e.vk == vk) return e.name;
    }
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "0x%02X", static_cast<unsigned>(vk));
    return buffer;
}

}  // namespace pbzpm
