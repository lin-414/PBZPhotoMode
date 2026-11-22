#include "core/config_json.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace pbzpm {
namespace {

bool read_text_file(const std::string& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream stream;
    stream << in.rdbuf();
    if (!stream) {
        // 流式拷贝对 0 字节文件按标准置 failbit（插入零字符）：手清空的 settings.json
        // 不是 I/O 错误。空文本按 Ok 交给 parse_config_json（空=全默认，与旧 config.ini
        // 路径同口径）；非空文件走到这里才是真正的读错误。
        std::error_code size_error;
        if (!std::filesystem::exists(path, size_error) || size_error) return false;
        if (std::filesystem::file_size(path, size_error) != 0 || size_error) return false;
        out->clear();
        return true;
    }
    *out = stream.str();
    return true;
}

bool write_text_file_atomic(const std::string& path, const std::string& text, std::string* error) {
    if (path.empty()) {
        if (error != nullptr) *error = "配置文件路径为空";
        return false;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error != nullptr) *error = "无法创建临时文件：" + tmp;
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            if (error != nullptr) *error = "写入临时文件失败：" + tmp;
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
            if (error != nullptr) *error = "写入配置失败（旧文件已保留）：" + path + "（" + copy_error.message() + "）";
            return false;
        }
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
    }
    return true;
}

}  // namespace

ConfigLoadResult load_config_json_file(const std::string& path) {
    ConfigLoadResult result;
    result.source = path;
    if (path.empty()) {
        result.ok = false;
        result.read_error = true;
        result.warnings.push_back("配置文件路径为空");
        return result;
    }
    std::string text;
    if (!read_text_file(path, &text)) {
        std::error_code exists_error;
        const bool exists = std::filesystem::exists(path, exists_error);
        if (!exists && !exists_error) {
            result.file_missing = true;
            return result;
        }
        result.ok = false;
        result.read_error = true;
        result.warnings.push_back("配置文件读取失败：" + path);
        return result;
    }
    return parse_config_json(text, path.c_str());
}

bool save_config_json_file(const std::string& path, const Config& cfg, std::string* error) {
    // PBZFM 菜单的后台写盘线程与引擎 Tick（Mods Menu 149 提交）都可能调用这里：
    // 同一时刻只允许一个写者，避免两个临时文件互相覆盖/读到半写文件。
    static std::mutex save_mutex;
    std::lock_guard<std::mutex> lock(save_mutex);
    return write_text_file_atomic(path, config_json_text(cfg), error);
}

}  // namespace pbzpm
