#include "core/lang_follow.h"

#include <algorithm>
#include <cctype>

namespace pbzpm {
namespace {

auto trim(const std::string& text) -> std::string {
    const size_t b = text.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = text.find_last_not_of(" \t\r\n");
    return text.substr(b, e - b + 1);
}

auto lower(std::string text) -> std::string {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// 去掉成对引号（PBZFM 写出不带引号，手改文件可能加）
auto unquote(const std::string& text) -> std::string {
    if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                             (text.front() == '\'' && text.back() == '\''))) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

auto contains(const std::vector<std::string>& ids, const std::string& id) -> bool {
    return !id.empty() && std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

std::string parse_pbzfm_language(const std::string& ini_text) {
    bool in_framework = false;
    bool saw_framework_section = false;
    std::string fallback;

    size_t pos = 0;
    while (pos <= ini_text.size()) {
        size_t end = ini_text.find('\n', pos);
        if (end == std::string::npos) end = ini_text.size();
        const std::string raw = ini_text.substr(pos, end - pos);
        pos = end + 1;

        const std::string line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;

        if (line.front() == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) {
                const std::string section = lower(trim(line.substr(1, close - 1)));
                in_framework = section == "framework";
                if (in_framework) saw_framework_section = true;
                continue;
            }
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (lower(trim(line.substr(0, eq))) != "language") continue;

        const std::string value = unquote(trim(line.substr(eq + 1)));
        if (value.empty()) continue;
        if (in_framework) return value;  // [framework] 段内的第一个 language
        if (fallback.empty()) fallback = value;  // 先记着，文件没有 [framework] 段时才用
    }

    return saw_framework_section ? std::string{} : fallback;
}

std::string decide_menu_language(const std::string& settings_value, const std::string& ini_language,
                                 const std::vector<std::string>& available, const std::string& fallback_id) {
    if (available.empty()) return {};
    if (settings_value != "auto" && contains(available, settings_value)) return settings_value;
    if (contains(available, ini_language)) return ini_language;
    if (contains(available, fallback_id)) return fallback_id;
    return {};
}

}  // namespace pbzpm
