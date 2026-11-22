#include "mods_menu_client.h"

#ifdef PBZPM_WITH_UE4SS

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "core/mods_menu.h"

namespace pbzpm {
namespace {

constexpr const char* kMenuConfigName = "PBZPhotoModeCpp.config.json";
constexpr const char* kSettingsName = "PBZPhotoModeCpp.settings.json";
constexpr const char* kDescriptorName = "PBZPhotoModeCpp.modsmenu.json";
constexpr const char* kBridgeDir = "PBZModsMenuBridge";
constexpr const char* kEventsName = "events-PBZPhotoModeCpp.txt";

enum class FileReadResult { Ok, Missing, Error };

FileReadResult read_text_file_result(const std::filesystem::path& path, std::string* out) {
    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (exists_error) return FileReadResult::Error;
    if (!exists) return FileReadResult::Missing;
    std::ifstream in(path, std::ios::binary);
    if (!in) return FileReadResult::Error;
    std::ostringstream stream;
    stream << in.rdbuf();
    if (!stream) {
        // 流式拷贝对 0 字节文件按标准会置 failbit（插入零字符）——那不是 I/O 错误，而是
        // 合法的空文件（Bridge 写一半崩溃、用户手清空）。按 Ok 交空文本，上层把空当 {}
        // 重新生成自愈；真正的读错误（权限/被锁）仍然报 Error。
        std::error_code size_error;
        const bool is_empty = !size_error && std::filesystem::file_size(path, size_error) == 0;
        if (!is_empty) return FileReadResult::Error;
        out->clear();
        return FileReadResult::Ok;
    }
    *out = stream.str();
    return FileReadResult::Ok;
}

bool read_text_file(const std::filesystem::path& path, std::string* out) {
    return read_text_file_result(path, out) == FileReadResult::Ok;
}

bool write_text_file_atomic(const std::filesystem::path& path, const std::string& text, std::string* error) {
    // 原生 path 追加，不走 string() 的 ANSI 码页回转：非 936 locale + 中文安装目录下
    // string() 会把宽字符变成 '?'，临时文件会落到错误路径。
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error != nullptr) *error = "无法创建临时文件：" + tmp.string();
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            if (error != nullptr) *error = "写入临时文件失败：" + tmp.string();
            return false;
        }
    }
    std::error_code rename_error;
    std::filesystem::rename(tmp, path, rename_error);
    if (rename_error) {
        // 保留旧目标文件：只有 copy_file（覆盖）成功后才删除临时文件。
        std::error_code copy_error;
        std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, copy_error);
        if (copy_error) {
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            if (error != nullptr) *error = "写入配置失败（旧文件已保留）：" + path.string() + "（" + copy_error.message() + "）";
            return false;
        }
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
    }
    return true;
}

std::string trim_ascii(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.pop_back();
    return text;
}

std::filesystem::file_time_type last_write_time_or_min(const std::filesystem::path& path) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    return error ? std::filesystem::file_time_type::min() : time;
}

bool parse_event_number(const std::string& text, std::uint64_t* out) {
    bool saw_end = false;
    bool saw_changed = false;
    std::uint64_t value = 0;
    std::size_t pos = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        pos = 3;
    }
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim_ascii(line);
        if (line == "End=1") {
            saw_end = true;
        } else if (line.rfind("Changed=", 0) == 0) {
            const std::string number = line.substr(8);
            if (!number.empty() &&
                std::all_of(number.begin(), number.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                value = std::strtoull(number.c_str(), nullptr, 10);
                saw_changed = true;
            }
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
    if (!saw_end || !saw_changed) return false;
    *out = value;
    return true;
}

}  // namespace

bool ModsMenuClient::start(const std::string& mod_dir, Config& cfg, bool* config_changed, bool settings_new) {
    if (config_changed != nullptr) *config_changed = false;
    if (mod_dir.empty()) return false;

    mod_dir_ = std::filesystem::path(mod_dir);
    menu_config_path_ = mod_dir_ / kMenuConfigName;
    settings_path_ = mod_dir_ / kSettingsName;
    descriptor_path_ = mod_dir_ / kDescriptorName;
    bridge_dir_ = mod_dir_.parent_path() / kBridgeDir;
    events_path_ = bridge_dir_ / kEventsName;

    std::error_code error;
    if (!std::filesystem::exists(descriptor_path_, error) || error) return false;
    error.clear();
    if (!std::filesystem::exists(bridge_dir_, error) || error) return false;

    active_ = true;

    std::string menu_text;
    const FileReadResult menu_read = read_text_file_result(menu_config_path_, &menu_text);
    if (menu_read == FileReadResult::Error) {
        status_ = "config.json 存在但读取失败，已保留原文件";
    } else if (menu_read == FileReadResult::Missing) {
        write_config_json(cfg);
        status_ = "已创建菜单子集 config.json（首次运行）";
    } else {
        ModsMenuValues values;
        std::string parse_error;
        if (!parse_mods_menu_json(menu_text, values, &parse_error)) {
            status_ = "config.json 解析失败，已按 settings.json 重建";
            write_config_json(cfg);
        } else {
            Config from_menu = cfg;
            std::vector<std::string> warnings;
            (void)mods_menu_apply_values(values, from_menu, &warnings);
            const bool same = mods_menu_config_json(from_menu) == mods_menu_config_json(cfg);
            if (settings_new) {
                // 3.0 首次生成 settings.json 时，2.2 遗留的 config.json 可能比它更早、
                // 但内容是玩家上次在 Mods Menu 里的保存：优先采用菜单值。
                if (!same) {
                    cfg = from_menu;
                    if (config_changed != nullptr) *config_changed = true;
                    status_ = "已载入 Mods Menu 上次保存（settings.json 新建）";
                } else {
                    status_ = "config.json 与 settings.json 已同步";
                }
                write_config_json(cfg);  // 归一化旧版数字 choice / 补全格式
            } else if (same) {
                status_ = "config.json 与 settings.json 已同步";
                write_config_json(cfg);  // 归一化旧版数字 choice / 补全格式
            } else if (last_write_time_or_min(menu_config_path_) > last_write_time_or_min(settings_path_)) {
                cfg = from_menu;
                if (config_changed != nullptr) *config_changed = true;
                status_ = "已载入 Mods Menu 上次保存（待写回 settings.json）";
                write_config_json(cfg);
            } else {
                write_config_json(cfg);
                status_ = "settings.json 较新，config.json 已刷新";
            }
        }
    }

    std::uint64_t last_event = 0;
    if (read_event_number(&last_event)) last_event_ = last_event;
    return true;
}

bool ModsMenuClient::poll(const Config& current, Config* out, std::vector<std::string>* warnings) {
    if (!active_ || out == nullptr) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ < std::chrono::milliseconds(250)) return false;
    last_poll_ = now;

    std::string events_text;
    if (!read_text_file(events_path_, &events_text)) return false;
    std::uint64_t number = 0;
    if (!parse_event_number(events_text, &number)) return false;  // End=1 未出现：Bridge 还在写
    if (number <= last_event_) return false;

    std::string config_text;
    {
        // 与 write_config_json 共用锁：Windows 上它的替换是先 remove 再 rename，
        // 不加锁可能读到短暂不存在/半写状态。
        std::lock_guard<std::mutex> lock(write_mutex_);
        const FileReadResult read_result = read_text_file_result(menu_config_path_, &config_text);
        if (read_result != FileReadResult::Ok) {
            if (!error_reported_) {
                std::lock_guard<std::mutex> state_lock(state_mutex_);
                last_error_ = read_result == FileReadResult::Missing
                                  ? "events 已更新但菜单 config.json 不存在"
                                  : "events 已更新但菜单 config.json 读取失败";
                error_reported_ = true;
            }
            return false;
        }
    }
    ModsMenuValues values;
    std::string parse_error;
    if (!parse_mods_menu_json(config_text, values, &parse_error)) {
        if (!error_reported_) {
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            last_error_ = "config.json 解析失败：" + parse_error;
            error_reported_ = true;
        }
        return false;
    }
    Config next = current;
    std::vector<std::string> local_warnings;
    const int applied = mods_menu_apply_values(values, next, &local_warnings);
    if (warnings != nullptr) {
        warnings->insert(warnings->end(), local_warnings.begin(), local_warnings.end());
    }
    // 只有完整读取并解析成功后才推进事件号：读到坏文件/临时不可读时下次继续重试。
    last_event_ = number;
    error_reported_ = false;
    if (applied <= 0) return false;
    *out = std::move(next);
    return true;
}

void ModsMenuClient::write_config_json(const Config& cfg) {
    if (!active_) return;
    std::lock_guard<std::mutex> lock(write_mutex_);
    std::string existing;
    const FileReadResult existing_read = read_text_file_result(menu_config_path_, &existing);
    if (existing_read == FileReadResult::Error) {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        if (!error_reported_) {
            last_error_ = "写菜单 config.json 前读取现有文件失败，已保留原文件";
            error_reported_ = true;
        }
        return;
    }
    const std::string text = mods_menu_merge_config_json(existing, cfg);
    std::string error;
    if (!write_text_file_atomic(menu_config_path_, text, &error)) {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        if (!error_reported_) {
            last_error_ = "写菜单 config.json 失败：" + error;
            error_reported_ = true;
        }
        return;
    }
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        error_reported_ = false;
    }
}

std::string ModsMenuClient::take_error() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::string out;
    out.swap(last_error_);
    return out;
}

bool ModsMenuClient::read_event_number(std::uint64_t* out) const {
    std::string text;
    if (!read_text_file(events_path_, &text)) return false;
    return parse_event_number(text, out);
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
