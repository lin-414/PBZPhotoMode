#include "core/mods_menu.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

#include "pbzfm_schema_map.h"

namespace pbzpm {
namespace {

bool ascii_iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

void append_utf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : text_(text) {}

    bool parse(ModsMenuValues& out) {
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
        skip_ws();
        if (!consume('{')) return fail("顶层必须是 JSON 对象");
        if (!parse_object(&out, 0)) return false;
        skip_ws();
        if (pos_ != text_.size()) return fail("对象结束后还有多余字符");
        return true;
    }

    const std::string& error() const { return error_; }

private:
    bool fail(const std::string& message) {
        if (error_.empty()) {
            error_ = "JSON 解析失败（字节 " + std::to_string(pos_) + "）：" + message;
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        if (pos_ >= text_.size() || text_[pos_] != expected) {
            return fail(std::string("期望 '") + expected + "'");
        }
        ++pos_;
        return true;
    }

    bool parse_object(ModsMenuValues* out, int depth) {
        if (depth > 32) return fail("嵌套层数过深");
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (!consume(':')) return false;
            skip_ws();
            ModsMenuValue value;
            if (!parse_value(&value, depth + 1)) return false;
            if (out != nullptr) (*out)[key] = std::move(value);
            skip_ws();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (!consume(',')) return false;
        }
    }

    bool parse_value(ModsMenuValue* out, int depth) {
        if (depth > 32) return fail("嵌套层数过深");
        skip_ws();
        if (pos_ >= text_.size()) return fail("值缺失");
        const char c = text_[pos_];
        if (c == '{') {
            if (out) out->type = ModsMenuValueType::Other;
            ++pos_;
            return parse_object(nullptr, depth + 1);
        }
        if (c == '[') {
            if (out) out->type = ModsMenuValueType::Other;
            return parse_array(depth + 1);
        }
        if (c == '"') {
            if (out) out->type = ModsMenuValueType::String;
            return parse_string(out ? out->text : dummy_);
        }
        if (c == 't') {
            if (!literal("true")) return false;
            if (out) {
                out->type = ModsMenuValueType::Bool;
                out->boolean = true;
            }
            return true;
        }
        if (c == 'f') {
            if (!literal("false")) return false;
            if (out) {
                out->type = ModsMenuValueType::Bool;
                out->boolean = false;
            }
            return true;
        }
        if (c == 'n') {
            if (!literal("null")) return false;
            if (out) out->type = ModsMenuValueType::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            double number = 0.0;
            if (!parse_number(number)) return false;
            if (out) {
                out->type = ModsMenuValueType::Number;
                out->number = number;
            }
            return true;
        }
        return fail("无法识别的值");
    }

    bool parse_array(int depth) {
        if (depth > 32) return fail("嵌套层数过深");
        if (!consume('[')) return false;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            if (!parse_value(nullptr, depth + 1)) return false;
            skip_ws();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (!consume(',')) return false;
        }
    }

    bool literal(std::string_view literal_text) {
        if (text_.compare(pos_, literal_text.size(), literal_text.data(), literal_text.size()) != 0) {
            return fail("无效字面量");
        }
        pos_ += literal_text.size();
        return true;
    }

    bool parse_number(double& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        // JSON 数字文法：整数部分至少一位（拒绝 ".5"），小数点后至少一位（拒绝 "1."
        // ——这俩是 strtod/from_chars 兼容的宽容写法，但不是 JSON）。
        const std::size_t int_start = pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ == int_start) return fail("数字缺失");
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            const std::size_t frac_start = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == frac_start) return fail("小数点后缺数字");
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        // std::from_chars 与 locale 无关（全库约定，见 config.cpp parse_double）：
        // strtod 在逗号小数点 locale 下会把 "0.5" 解析成 0，整个 JSON 随之报废。
        double value = 0.0;
        const char* const first = text_.data() + start;
        const char* const last = text_.data() + pos_;
        const std::from_chars_result res = std::from_chars(first, last, value);
        if (res.ec != std::errc() || res.ptr != last || !std::isfinite(value)) return fail("数字格式错误");
        out = value;
        return true;
    }

    bool parse_string(std::string& out) {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != '"') return fail("期望字符串");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= text_.size()) return fail("字符串转义不完整");
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!parse_hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // 高位代理：如果后面紧跟 \uDC00..\uDFFF，合成一个码点
                            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                                const std::size_t save = pos_;
                                pos_ += 2;
                                std::uint32_t low = 0;
                                if (!parse_hex4(low)) return false;
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                } else {
                                    pos_ = save;
                                }
                            }
                        }
                        append_utf8(cp, out);
                        break;
                    }
                    default: return fail("未知转义序列");
                }
            } else if (c < 0x20) {
                return fail("字符串含未转义控制字符");
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return fail("字符串未闭合");
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) return fail("\\u 转义不完整");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return fail("\\u 转义含非十六进制字符");
            }
        }
        out = value;
        return true;
    }

    const std::string& text_;
    std::size_t pos_{0};
    std::string error_;
    std::string dummy_;
};

void write_json_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    out << '"';
}

std::string format_number(double value) {
    if (!std::isfinite(value)) return "0";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

// 字符串 → double，locale 无关（同 config.cpp parse_double 口径）：去首尾空白、要求完整消费。
bool parse_double_token(const std::string& text, double& out) {
    const std::size_t b = text.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    const std::size_t e = text.find_last_not_of(" \t\r\n");
    double value = 0.0;
    const std::from_chars_result res = std::from_chars(text.data() + b, text.data() + e + 1, value);
    if (res.ec != std::errc() || res.ptr != text.data() + e + 1 || !std::isfinite(value)) return false;
    out = value;
    return true;
}

const PbzfmField* find_pbzfm_field(const std::string& key) {
    for (const PbzfmField& field : pbzfm_fields()) {
        if (field.key != nullptr && key == field.key) return &field;
    }
    return nullptr;
}

bool value_to_double(const ModsMenuValue& value, ModsMenuSettingKind kind, const std::string& key, double& out) {
    switch (kind) {
        case ModsMenuSettingKind::Bool:
            if (value.type == ModsMenuValueType::Bool) {
                out = value.boolean ? 1.0 : 0.0;
                return true;
            }
            if (value.type == ModsMenuValueType::Number) {
                out = value.number != 0.0 ? 1.0 : 0.0;
                return true;
            }
            if (value.type == ModsMenuValueType::String) {
                if (ascii_iequals(value.text, "true") || ascii_iequals(value.text, "on") ||
                    ascii_iequals(value.text, "yes") || value.text == "1") {
                    out = 1.0;
                    return true;
                }
                if (ascii_iequals(value.text, "false") || ascii_iequals(value.text, "off") ||
                    ascii_iequals(value.text, "no") || value.text == "0") {
                    out = 0.0;
                    return true;
                }
            }
            return false;
        case ModsMenuSettingKind::Number:
            if (value.type == ModsMenuValueType::Number) {
                out = value.number;
                return true;
            }
            if (value.type == ModsMenuValueType::Bool) {
                out = value.boolean ? 1.0 : 0.0;
                return true;
            }
            if (value.type == ModsMenuValueType::String) {
                double parsed = 0.0;
                if (parse_double_token(value.text, parsed)) {
                    out = parsed;
                    return true;
                }
            }
            return false;
        case ModsMenuSettingKind::Key:
            // 键位走字符串键名（vk_from_name），不经过 numeric 通道。
            return false;
        case ModsMenuSettingKind::Choice:
            if (value.type == ModsMenuValueType::String) {
                const std::string text = value.text;
                if (key == "freeze_mode") {
                    if (ascii_iequals(text, "pause")) {
                        out = 1.0;
                        return true;
                    }
                    if (ascii_iequals(text, "dilation") || ascii_iequals(text, "slow")) {
                        out = 0.0;
                        return true;
                    }
                } else if (key == "mouse_vertical_mode") {
                    if (ascii_iequals(text, "look") || ascii_iequals(text, "pitch")) {
                        out = 1.0;
                        return true;
                    }
                    if (ascii_iequals(text, "translate") || ascii_iequals(text, "move")) {
                        out = 0.0;
                        return true;
                    }
                }
            }
            if (value.type == ModsMenuValueType::Number) {
                out = value.number;
                return true;
            }
            if (value.type == ModsMenuValueType::Bool) {
                out = value.boolean ? 1.0 : 0.0;
                return true;
            }
            return false;
    }
    return false;
}

// 菜单子集 JSON 的值生成（mods_menu_config_json / mods_menu_merge_config_json 共用）。
// 键位字段直接保留 config 里的原始键名：vk 表外的手改名（docs/MODS_MENU_149.md 指引的
// 手改路径）原样回写——绝不能经 pbzfm_read_field 的 KeyMap{} 兜底换成默认键，那会在
// 下一次菜单保存时把手改热键静默洗回默认（应用侧对未知名是拒识+告警，配置不会被覆盖）。
// 可解析的名字归一成规范名，Bridge 的下拉选项才能对上。
ModsMenuValue mods_menu_value_from_config(const ModsMenuSettingSpec& spec, const PbzfmField& field,
                                          const Config& cfg) {
    ModsMenuValue item;
    if (spec.kind == ModsMenuSettingKind::Key) {
        item.type = ModsMenuValueType::String;
        if (field.key_name != nullptr) {
            const std::string raw = cfg.*field.key_name;
            const int vk = vk_from_name(raw);
            item.text = vk != 0 ? vk_to_name(vk) : raw;
        }
        return item;
    }
    const double value = pbzfm_read_field(field, cfg, KeyMap{});
    if (spec.kind == ModsMenuSettingKind::Bool) {
        item.type = ModsMenuValueType::Bool;
        item.boolean = value != 0.0;
    } else if (spec.kind == ModsMenuSettingKind::Choice) {
        item.type = ModsMenuValueType::String;
        if (std::string(spec.key) == "freeze_mode") {
            item.text = value >= 1.0 ? "pause" : "dilation";
        } else if (std::string(spec.key) == "mouse_vertical_mode") {
            item.text = value >= 1.0 ? "look" : "translate";
        } else {
            item.text = value >= 1.0 ? "1" : "0";
        }
    } else {
        item.type = ModsMenuValueType::Number;
        item.number = value;
    }
    return item;
}

}  // namespace

bool parse_mods_menu_json(const std::string& text, ModsMenuValues& out, std::string* error) {
    out.clear();
    // 空/全空白文件（含只有 BOM 的）按空对象处理：流式拷贝对 0 字节文件会按标准置
    // failbit，读层已把它当 Ok 交上来；这里再归一成空对象，让 start/write_config_json/
    // poll 把「Bridge 写一半崩溃、用户手清空」当成普通空值重新生成自愈，而不是当作
    // 解析失败永久卡死往返。与旧 config.ini 路径「空文件=默认值」同口径。
    std::size_t probe = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        probe = 3;
    }
    if (text.find_first_not_of(" \t\r\n", probe) == std::string::npos) {
        if (error != nullptr) error->clear();
        return true;
    }
    JsonReader reader(text);
    if (!reader.parse(out)) {
        if (error != nullptr) *error = reader.error();
        out.clear();
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

std::string write_mods_menu_json(const ModsMenuValues& values) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n";
    std::size_t index = 0;
    for (const auto& [key, value] : values) {
        out << "  ";
        write_json_string(out, key);
        out << ": ";
        switch (value.type) {
            case ModsMenuValueType::Bool: out << (value.boolean ? "true" : "false"); break;
            case ModsMenuValueType::Number: out << format_number(value.number); break;
            case ModsMenuValueType::String: write_json_string(out, value.text); break;
            case ModsMenuValueType::Null:
            case ModsMenuValueType::Other: out << "null"; break;
        }
        if (++index < values.size()) out << ',';
        out << '\n';
    }
    out << "}\n";
    return out.str();
}

const std::vector<ModsMenuSettingSpec>& mods_menu_settings() {
    static const std::vector<ModsMenuSettingSpec> table = {
        // 行为
        {"auto_pause_time", ModsMenuSettingKind::Bool},
        {"block_game_input", ModsMenuSettingKind::Bool},
        {"wheel_fov", ModsMenuSettingKind::Bool},
        {"wheel_fov_invert", ModsMenuSettingKind::Bool},
        {"ultrawide_unlock", ModsMenuSettingKind::Bool},
        {"takeover_current_camera", ModsMenuSettingKind::Bool},
        {"pad_enabled", ModsMenuSettingKind::Bool},
        {"freeze_mode", ModsMenuSettingKind::Choice},
        // 隐藏
        {"hide_player", ModsMenuSettingKind::Bool},
        {"hide_game_hud", ModsMenuSettingKind::Bool},
        {"hide_interact_prompt", ModsMenuSettingKind::Bool},
        {"hide_enemy_health_bars", ModsMenuSettingKind::Bool},
        {"hide_lockon_marker", ModsMenuSettingKind::Bool},
        {"hide_subtitles", ModsMenuSettingKind::Bool},
        // 冻结
        {"freeze_sequences", ModsMenuSettingKind::Bool},
        {"freeze_dialogue_flow", ModsMenuSettingKind::Bool},
        {"stop_wwise_on_freeze", ModsMenuSettingKind::Bool},
        {"wwise_engine_suspend", ModsMenuSettingKind::Bool},
        {"stop_camera_shakes_on_freeze", ModsMenuSettingKind::Bool},
        {"freeze_wind", ModsMenuSettingKind::Bool},
        {"freeze_mpc", ModsMenuSettingKind::Bool},
        {"freeze_cloth", ModsMenuSettingKind::Bool},
        {"freeze_skeletal_animation", ModsMenuSettingKind::Bool},
        {"freeze_motion_blur_off", ModsMenuSettingKind::Bool},
        // 相机与移动
        {"fov_default", ModsMenuSettingKind::Number},
        {"fov_step", ModsMenuSettingKind::Number},
        {"move_speed", ModsMenuSettingKind::Number},
        {"boost_mult", ModsMenuSettingKind::Number},
        {"slow_mult", ModsMenuSettingKind::Number},
        {"slow_look_mult", ModsMenuSettingKind::Number},
        {"look_sensitivity", ModsMenuSettingKind::Number},
        {"pitch_limit", ModsMenuSettingKind::Number},
        {"vertical_speed", ModsMenuSettingKind::Number},
        {"roll_speed", ModsMenuSettingKind::Number},
        {"pitch_speed", ModsMenuSettingKind::Number},
        {"yaw_speed", ModsMenuSettingKind::Number},
        {"mouse_vertical_mode", ModsMenuSettingKind::Choice},
        // 景深只保留总开关；详细参数留在 PBZFM / settings.json。
        {"dof_enabled", ModsMenuSettingKind::Bool},
        // 手柄
        {"pad_look_speed", ModsMenuSettingKind::Number},
        {"pad_move_scale", ModsMenuSettingKind::Number},
        {"pad_deadzone", ModsMenuSettingKind::Number},
        // 快捷键（键盘）：自定义 choice 行，值是我们自己的键名（F2 / MOUSE_LEFT / ADD ...）
        // 放在菜单最后，避免把常用设置挤到长列表后面。
        {"key_toggle", ModsMenuSettingKind::Key},
        {"key_freeze", ModsMenuSettingKind::Key},
        {"key_frame_freeze", ModsMenuSettingKind::Key},
        {"key_reset", ModsMenuSettingKind::Key},
        {"key_roll_reset", ModsMenuSettingKind::Key},
        {"key_fov_up", ModsMenuSettingKind::Key},
        {"key_fov_down", ModsMenuSettingKind::Key},
        {"key_pitch_up", ModsMenuSettingKind::Key},
        {"key_pitch_down", ModsMenuSettingKind::Key},
        {"key_yaw_left", ModsMenuSettingKind::Key},
        {"key_yaw_right", ModsMenuSettingKind::Key},
        {"key_roll_left", ModsMenuSettingKind::Key},
        {"key_roll_right", ModsMenuSettingKind::Key},
        {"key_vertical_up", ModsMenuSettingKind::Key},
        {"key_vertical_down", ModsMenuSettingKind::Key},
        {"key_move_forward", ModsMenuSettingKind::Key},
        {"key_move_back", ModsMenuSettingKind::Key},
        {"key_move_left", ModsMenuSettingKind::Key},
        {"key_move_right", ModsMenuSettingKind::Key},
        {"key_boost", ModsMenuSettingKind::Key},
        {"key_slow", ModsMenuSettingKind::Key},
        {"key_free_look", ModsMenuSettingKind::Key},
        {"key_dof_toggle", ModsMenuSettingKind::Key},
        {"key_focus_near", ModsMenuSettingKind::Key},
        {"key_focus_far", ModsMenuSettingKind::Key},
        {"key_aperture_open", ModsMenuSettingKind::Key},
        {"key_aperture_close", ModsMenuSettingKind::Key},
    };
    return table;
}

const ModsMenuSettingSpec* mods_menu_find_setting(const std::string& key) {
    for (const ModsMenuSettingSpec& spec : mods_menu_settings()) {
        if (key == spec.key) return &spec;
    }
    return nullptr;
}

int mods_menu_apply_values(const ModsMenuValues& values, Config& cfg, std::vector<std::string>* warnings) {
    int applied = 0;
    const auto warn = [warnings](std::string message) {
        if (warnings != nullptr) warnings->push_back(std::move(message));
    };
    for (const auto& [key, value] : values) {
        const ModsMenuSettingSpec* spec = mods_menu_find_setting(key);
        if (spec == nullptr) continue;  // 未知键：留给其它版本/其它工具，忽略
        const PbzfmField* field = find_pbzfm_field(key);
        if (field == nullptr) {
            warn("Mods Menu 字段 " + key + " 在本模组字段表中不存在，已忽略");
            continue;
        }
        const bool kind_ok = (spec->kind == ModsMenuSettingKind::Bool && field->kind == PbzfmFieldKind::Bool) ||
                             (spec->kind == ModsMenuSettingKind::Number && field->kind == PbzfmFieldKind::Float) ||
                             (spec->kind == ModsMenuSettingKind::Choice && field->kind == PbzfmFieldKind::Enum) ||
                             (spec->kind == ModsMenuSettingKind::Key && field->kind == PbzfmFieldKind::Key);
        if (!kind_ok) {
            warn("Mods Menu 字段 " + key + " 的类型与字段表不一致，已忽略");
            continue;
        }
        if (spec->kind == ModsMenuSettingKind::Key) {
            if (value.type != ModsMenuValueType::String) {
                warn("Mods Menu 字段 " + key + " 的键位必须是字符串，已忽略");
                continue;
            }
            const int vk = vk_from_name(value.text);
            if (vk == 0) {
                warn("Mods Menu 字段 " + key + " 的键名无法识别：" + value.text + "，已忽略");
                continue;
            }
            if (!pbzfm_apply_field(*field, static_cast<double>(vk), cfg)) {
                warn("Mods Menu 字段 " + key + " 无法应用，已忽略");
                continue;
            }
            ++applied;
            continue;
        }
        double numeric = 0.0;
        if (!value_to_double(value, spec->kind, key, numeric)) {
            warn("Mods Menu 字段 " + key + " 的值类型不合法，已忽略");
            continue;
        }
        if (!pbzfm_apply_field(*field, numeric, cfg)) {
            warn("Mods Menu 字段 " + key + " 无法应用，已忽略");
            continue;
        }
        ++applied;
    }
    return applied;
}

std::string mods_menu_config_json(const Config& cfg) {
    ModsMenuValues values;
    for (const ModsMenuSettingSpec& spec : mods_menu_settings()) {
        const PbzfmField* field = find_pbzfm_field(spec.key);
        if (field == nullptr) continue;
        values[spec.key] = mods_menu_value_from_config(spec, *field, cfg);
    }
    return write_mods_menu_json(values);
}

std::string mods_menu_merge_config_json(const std::string& existing, const Config& cfg) {
    ModsMenuValues values;
    if (!existing.empty()) {
        std::string ignored;
        (void)parse_mods_menu_json(existing, values, &ignored);
    }
    for (const ModsMenuSettingSpec& spec : mods_menu_settings()) {
        const PbzfmField* field = find_pbzfm_field(spec.key);
        if (field == nullptr) continue;
        values[spec.key] = mods_menu_value_from_config(spec, *field, cfg);
    }
    return write_mods_menu_json(values);
}

bool mods_menu_apply_json_text(const std::string& text, Config& cfg, std::vector<std::string>* warnings) {
    ModsMenuValues values;
    if (!parse_mods_menu_json(text, values, nullptr)) return false;
    (void)mods_menu_apply_values(values, cfg, warnings);
    return true;
}

}  // namespace pbzpm
