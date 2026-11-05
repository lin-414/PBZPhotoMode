#include "pbzfm_client_lang.h"

#include "core/i18n_logic.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace pbzpm {
namespace {

// 稳定秒（节流用）
double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 读小文件为 UTF-8 文本，剥 BOM。失败返回空串。
std::string ReadFileUtf8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string out;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) out.append(buf, static_cast<size_t>(in.gcount()));
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

// 扁平「"key": "value"」JSON 解析（与框架同款语义：转义支持 \\ \" \/ \n \t \r \b \f \uXXXX；
// 不支持注释/嵌套/数字/布尔）。失败返回 false，out 内容未定义。
bool ParseFlatJson(const std::string& utf8, std::map<std::string, std::string>& out) {
    size_t i = 0;
    const size_t n = utf8.size();
    auto skipWs = [&](size_t& p) {
        while (p < n && (utf8[p] == ' ' || utf8[p] == '\t' || utf8[p] == '\r' || utf8[p] == '\n')) ++p;
    };
    auto parseString = [&](size_t& p, std::string& str) -> bool {
        skipWs(p);
        if (p >= n || utf8[p] != '"') return false;
        ++p;
        str.clear();
        while (p < n) {
            char c = utf8[p++];
            if (c == '"') return true;
            if (c == '\\' && p < n) {
                char e = utf8[p++];
                switch (e) {
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    case '/': str += '/'; break;
                    case 'n': str += '\n'; break;
                    case 't': str += '\t'; break;
                    case 'r': str += '\r'; break;
                    case 'b': str += '\b'; break;
                    case 'f': str += '\f'; break;
                    case 'u': {
                        if (p + 4 > n) return false;
                        unsigned cp = (unsigned)std::strtoul(utf8.substr(p, 4).c_str(), nullptr, 16);
                        p += 4;
                        if (cp < 0x80) str += (char)cp;
                        else if (cp < 0x800) { str += (char)(0xC0 | (cp >> 6)); str += (char)(0x80 | (cp & 0x3F)); }
                        else { str += (char)(0xE0 | (cp >> 12)); str += (char)(0x80 | ((cp >> 6) & 0x3F)); str += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: return false;
                }
            } else {
                str += c;
            }
        }
        return false;
    };
    skipWs(i);
    if (i >= n || utf8[i] != '{') return false;
    ++i;
    while (true) {
        skipWs(i);
        if (i < n && utf8[i] == '}') return true;
        std::string key, val;
        if (!parseString(i, key)) return false;
        skipWs(i);
        if (i >= n || utf8[i] != ':') return false;
        ++i;
        if (!parseString(i, val)) return false;
        out[key] = val;
        skipWs(i);
        if (i < n && utf8[i] == ',') { ++i; continue; }
        if (i < n && utf8[i] == '}') return true;
        return false;
    }
}

}  // namespace

PbzmI18n::SnapshotPtr PbzmI18n::Load() const {
    return snapshot_.load(std::memory_order_acquire);
}

void PbzmI18n::Publish(SnapshotPtr next) {
    snapshot_.store(std::move(next), std::memory_order_release);
}

void PbzmI18n::Upsert(std::vector<Lang>& langs, const std::string& id, const std::string& display,
                      std::map<std::string, std::string>&& strings) {
    for (Lang& l : langs) {
        if (l.id == id) {
            l.display = display;
            l.strings = std::move(strings);
            return;
        }
    }
    langs.push_back(Lang{id, display, std::move(strings)});
}

void PbzmI18n::SortAndClamp(Snapshot& snap) {
    if (snap.langs.empty()) {
        snap.current = 0;
        return;
    }
    // 已知语言顺序优先，其余按 id 排序；current 按 id 重新定位
    const int old_current = snap.current >= 0 && snap.current < static_cast<int>(snap.langs.size()) ? snap.current : 0;
    const std::string keep = snap.langs[static_cast<size_t>(old_current)].id;
    std::sort(snap.langs.begin(), snap.langs.end(),
              [](const Lang& a, const Lang& b) { return language_display_less(a.id, b.id); });
    snap.current = 0;
    for (size_t i = 0; i < snap.langs.size(); ++i) {
        if (snap.langs[i].id == keep) snap.current = static_cast<int>(i);
    }
}

void PbzmI18n::RescanLanguages(const std::string& mod_dir, bool force) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    const double now = SteadySeconds();
    if (!force && now < next_scan_) return;
    next_scan_ = now + 2.0;
    const std::filesystem::path dir = std::filesystem::path(mod_dir) / "languages";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;

    // 复制当前快照（保留目录里已消失的旧语言），写侧独占修改后整体发布。
    const SnapshotPtr old = Load();
    Snapshot next = old ? *old : Snapshot{};
    for (std::filesystem::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".json") continue;
        const std::string id = it->path().stem().string();
        if (id.empty() || id == "__display") continue;
        const std::string text = ReadFileUtf8(it->path());
        if (text.empty()) continue;
        std::map<std::string, std::string> parsed;
        if (!ParseFlatJson(text, parsed) || parsed.empty()) continue;  // 坏词典整份跳过，保留现状
        std::string display = id;
        if (auto d = parsed.find("__display"); d != parsed.end()) {
            if (!d->second.empty()) display = d->second;
            parsed.erase(d);
        }
        Upsert(next.langs, id, display, std::move(parsed));
    }
    SortAndClamp(next);
    Publish(std::make_shared<Snapshot>(std::move(next)));
}

int PbzmI18n::LanguageCount() const {
    const SnapshotPtr snap = Load();
    return snap ? static_cast<int>(snap->langs.size()) : 0;
}

std::vector<PbzmI18n::LanguageInfo> PbzmI18n::Languages() const {
    std::vector<LanguageInfo> out;
    const SnapshotPtr snap = Load();
    if (!snap) return out;
    out.reserve(snap->langs.size());
    for (const Lang& l : snap->langs) out.push_back(LanguageInfo{l.id, l.display});
    return out;
}

int PbzmI18n::FindLanguage(const char* id) const {
    if (id == nullptr || *id == '\0') return -1;
    const SnapshotPtr snap = Load();
    if (!snap) return -1;
    for (size_t i = 0; i < snap->langs.size(); ++i) {
        if (snap->langs[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void PbzmI18n::SetLanguageById(const std::string& id) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    const SnapshotPtr old = Load();
    Snapshot next = old ? *old : Snapshot{};
    next.current = 0;  // 未找到（含空 id）归位到 0
    for (size_t i = 0; i < next.langs.size(); ++i) {
        if (next.langs[i].id == id) {
            next.current = static_cast<int>(i);
            break;
        }
    }
    Publish(std::make_shared<Snapshot>(std::move(next)));
}

std::string PbzmI18n::CurrentId() const {
    const SnapshotPtr snap = Load();
    if (!snap || snap->langs.empty()) return {};
    const int idx = snap->current >= 0 && snap->current < static_cast<int>(snap->langs.size()) ? snap->current : 0;
    return snap->langs[static_cast<size_t>(idx)].id;
}

std::string PbzmI18n::T(const char* key) const {
    if (key == nullptr || *key == '\0') return {};
    const SnapshotPtr snap = Load();
    if (!snap || snap->langs.empty()) return key;  // 没有任何词典：键名退化（label 本身是中文原文）
    const int idx = snap->current >= 0 && snap->current < static_cast<int>(snap->langs.size()) ? snap->current : 0;
    const Lang& cur = snap->langs[static_cast<size_t>(idx)];
    const std::map<std::string, std::string>* base = nullptr;
    if (cur.id != "en-US") {
        // 基准词典 en-US 兜底：某语言缺键时显示英文原文，而不是键名
        for (const Lang& l : snap->langs) {
            if (l.id != "en-US") continue;
            base = &l.strings;
            break;
        }
    }
    return resolve_translation(cur.strings, base, key);
}

}  // namespace pbzpm
