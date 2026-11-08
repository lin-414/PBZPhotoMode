#include "pbzfm_client.h"

#ifdef PBZPM_WITH_UE4SS

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>

#include "core/input_logic.h"
#include "core/look_preset.h"
#include "core/version.h"
#include "pbzfm_schema_map.h"

// 自绘页直接画进框架的 ImGui 上下文：两边必须链接 ABI 兼容的 ImGui（struct 布局一致）。
// 编译期只保留与具体版本号无关的基本约束（用到的 API/特性必须存在）；与框架是否同版
// 改为 ensure_imgui() 里的运行时检测（上下文指纹 + GetVersion() 字符串），UE4SS 升级
// 框架时不再因为 static_assert 写死版本号而编译失败。
static_assert(IMGUI_VERSION_NUM >= 18900, "PBZFM 自绘页需要 ImGui 1.89+ 的 PushFont/字体图集 API");
#if !defined(IMGUI_HAS_TEXTURES)
#error "PBZFM 自绘页需要带动态字体图集的 ImGui（1.92+，IMGUI_HAS_TEXTURES）"
#endif

namespace pbzpm {
namespace {

// 框架侧的 section 标题（根节点名；品牌名，保持 ASCII）
constexpr const char* kSectionName = "PBZ Photo Mode";
// 树节点名 / 页名走词典键 #node_settings / #node_status / #group_*：
// 注册前先按 menu_language（auto=跟随 PBZFM.ini）解析语言，用翻译后的名字注册。
// 注意：树节点名注册后固化，游戏内切语言不重命名（重启生效）；页内内容仍实时切换。
// 等框架 DLL 出现的轮询窗口：250ms × 2400 次 = 10 分钟；装了 PBZFM 时它由 UE4SS
// 一并加载（先后不定），没装就安静退回纯快捷键模式
constexpr int kPollAttempts = 2400;
constexpr DWORD kPollIntervalMs = 250;
// 改键捕获 5 秒没按键自动取消（Esc 会直接关菜单，无法在同一帧取消）
constexpr double kCaptureTimeoutSec = 5.0;
// 框架自绘后端的指纹（InitImGui 里设置），用来确认拿到的是框架的 ImGui 上下文
constexpr const char* kBackendFingerprint = "pbzfm_dc_overlay";

// 分组子节点的 userdata 只能携带组名，实例指针放这里（本 mod 进程内单实例）
PbzfmClient* g_client_self = nullptr;

double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 原子写小文本文件（风格预设槽位）：临时文件 + 原子替换（save_config_file 同款语义，
// 但落盘内容由调用方给定，不是整份 config.ini）。
bool save_text_file_atomic(const std::string& path, const std::string& text, std::string* error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "无法创建临时文件：" + tmp;
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code remove_ec;
            std::filesystem::remove(tmp, remove_ec);
            if (error) *error = "写入临时文件失败：" + tmp;
            return false;
        }
    }
    std::error_code rename_ec;
    std::filesystem::rename(tmp, path, rename_ec);
    if (rename_ec) {
        std::error_code remove_ec;
        std::filesystem::remove(tmp, remove_ec);
        if (error) *error = "原子替换失败：" + path + "（" + rename_ec.message() + "）";
        return false;
    }
    return true;
}

// 读小文本文件（风格预设槽位）：失败/不存在返回空串（BOM 剥离，pbzfm_client_lang.cpp 同款）。
std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string out;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) out.append(buf, static_cast<size_t>(in.gcount()));
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB &&
        (unsigned char)out[2] == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

// ---- 风格预设文件（<mod>/look_preset_<编号>.ini）的枚举与编号 ----
// 编号从文件名后缀解析；保存永远用「最大编号 + 1」，绝不覆盖已有预设（删除走菜单下拉行的
// 「删除预设」按钮，两击确认——见 render_look_presets）。

int preset_index_from_path(const std::string& path) {
    const std::string name = std::filesystem::path(path).stem().string();
    if (name.rfind("look_preset_", 0) != 0) return -1;
    const std::string digits = name.substr(12);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) return -1;
    return std::atoi(digits.c_str());
}

// 枚举 <mod>/look_preset_*.ini，按编号升序（编号缺失/非法的排最后，按文件名兜底）。
// 目录条目极少，逐帧枚举可接受：外部手动增删文件即时反映，无需缓存与失效逻辑。
std::vector<std::string> list_look_presets(const std::string& mod_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(mod_dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("look_preset_", 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".ini") == 0) {
            out.push_back(it->path().string());
        }
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        const int ia = preset_index_from_path(a);
        const int ib = preset_index_from_path(b);
        if (ia >= 0 && ib >= 0 && ia != ib) return ia < ib;
        return a < b;
    });
    return out;
}

std::string next_look_preset_path(const std::string& mod_dir) {
    int max_index = 0;
    for (const std::string& path : list_look_presets(mod_dir)) {
        const int index = preset_index_from_path(path);
        if (index > max_index) max_index = index;
    }
    return (std::filesystem::path(mod_dir) / ("look_preset_" + std::to_string(max_index + 1) + ".ini")).string();
}

// ImGui 1.92 applies the framework's panel zoom / UI scale through
// style.FontScaleMain, which only scales glyphs - explicit sizes such as
// ImVec2(150, 0) stay in unscaled pixels. A fixed-width key-cap button
// therefore clipped long names (MOUSE_LEFT/MOUSE_RIGHT...) as soon as the
// menu was enlarged. Size the button from the current font metrics instead:
// keep the old 150px footprint as the minimum (scaled with the font), and
// always reserve enough room for the label plus the frame padding.
float KeyButtonWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fontScale = style.FontSizeBase > 0.0f ? ImGui::GetFontSize() / style.FontSizeBase : 1.0f;
    const float minWidth = 150.0f * fontScale;
    const float textWidth = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f + 2.0f;
    return textWidth > minWidth ? textWidth : minWidth;
}

// 改键捕获用：候选 VK → ImGuiKey。与框架输入层的 VkToImGuiKey 保持一致
// （键喂进 ImGui 后再查回来），不支持的键返回 ImGuiKey_None。
constexpr ImGuiKey VkToImGuiKey(int vk) {
    switch (vk) {
        case 0x01: return ImGuiKey_MouseLeft; case 0x02: return ImGuiKey_MouseRight;
        case 0x04: return ImGuiKey_MouseMiddle; case 0x05: return ImGuiKey_MouseX1;
        case 0x06: return ImGuiKey_MouseX2;
        case '0': return ImGuiKey_0; case '1': return ImGuiKey_1; case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3; case '4': return ImGuiKey_4; case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6; case '7': return ImGuiKey_7; case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
        case 'A': return ImGuiKey_A; case 'B': return ImGuiKey_B; case 'C': return ImGuiKey_C;
        case 'D': return ImGuiKey_D; case 'E': return ImGuiKey_E; case 'F': return ImGuiKey_F;
        case 'G': return ImGuiKey_G; case 'H': return ImGuiKey_H; case 'I': return ImGuiKey_I;
        case 'J': return ImGuiKey_J; case 'K': return ImGuiKey_K; case 'L': return ImGuiKey_L;
        case 'M': return ImGuiKey_M; case 'N': return ImGuiKey_N; case 'O': return ImGuiKey_O;
        case 'P': return ImGuiKey_P; case 'Q': return ImGuiKey_Q; case 'R': return ImGuiKey_R;
        case 'S': return ImGuiKey_S; case 'T': return ImGuiKey_T; case 'U': return ImGuiKey_U;
        case 'V': return ImGuiKey_V; case 'W': return ImGuiKey_W; case 'X': return ImGuiKey_X;
        case 'Y': return ImGuiKey_Y; case 'Z': return ImGuiKey_Z;
        case 0x60: return ImGuiKey_Keypad0; case 0x61: return ImGuiKey_Keypad1;
        case 0x62: return ImGuiKey_Keypad2; case 0x63: return ImGuiKey_Keypad3;
        case 0x64: return ImGuiKey_Keypad4; case 0x65: return ImGuiKey_Keypad5;
        case 0x66: return ImGuiKey_Keypad6; case 0x67: return ImGuiKey_Keypad7;
        case 0x68: return ImGuiKey_Keypad8; case 0x69: return ImGuiKey_Keypad9;
        case 0x6A: return ImGuiKey_KeypadMultiply; case 0x6B: return ImGuiKey_KeypadAdd;
        case 0x6D: return ImGuiKey_KeypadSubtract; case 0x6E: return ImGuiKey_KeypadDecimal;
        case 0x6F: return ImGuiKey_KeypadDivide;
        case 0x70: return ImGuiKey_F1; case 0x71: return ImGuiKey_F2; case 0x72: return ImGuiKey_F3;
        case 0x73: return ImGuiKey_F4; case 0x74: return ImGuiKey_F5; case 0x75: return ImGuiKey_F6;
        case 0x76: return ImGuiKey_F7; case 0x77: return ImGuiKey_F8; case 0x78: return ImGuiKey_F9;
        case 0x79: return ImGuiKey_F10; case 0x7A: return ImGuiKey_F11; case 0x7B: return ImGuiKey_F12;
        case 0x25: return ImGuiKey_LeftArrow; case 0x27: return ImGuiKey_RightArrow;
        case 0x26: return ImGuiKey_UpArrow; case 0x28: return ImGuiKey_DownArrow;
        case 0x20: return ImGuiKey_Space; case 0x09: return ImGuiKey_Tab;
        case 0x0D: return ImGuiKey_Enter; case 0x08: return ImGuiKey_Backspace;
        case 0x1B: return ImGuiKey_Escape; case 0x2D: return ImGuiKey_Insert;
        case 0x2E: return ImGuiKey_Delete; case 0x23: return ImGuiKey_Home;
        case 0x24: return ImGuiKey_End; case 0x21: return ImGuiKey_PageUp;
        case 0x22: return ImGuiKey_PageDown;
        case 0xBB: return ImGuiKey_Equal; case 0xBD: return ImGuiKey_Minus;
        case 0xBE: return ImGuiKey_Period; case 0xBC: return ImGuiKey_Comma;
        case 0xBA: return ImGuiKey_Semicolon; case 0xBF: return ImGuiKey_Slash;
        case 0xC0: return ImGuiKey_GraveAccent; case 0xDB: return ImGuiKey_LeftBracket;
        case 0xDC: return ImGuiKey_Backslash; case 0xDD: return ImGuiKey_RightBracket;
        case 0xDE: return ImGuiKey_Apostrophe;
        default: return ImGuiKey_None;
    }
}

// 改键捕获的候选 VK（修饰键不收——Shift/Ctrl 是保留的行为键；捕获时 Esc 直接关菜单）
const int kCandidateVks[] = {
    0x01, 0x02, 0x04, 0x05, 0x06,  // 鼠标左/右/中键、侧键 X1/X2（捕获时也可直接按鼠标键）
    0x08, 0x09, 0x0D, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
    0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x6B, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x7B, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE,
};

}  // namespace

PbzfmClient::~PbzfmClient() {
    stop();
}

void PbzfmClient::start(Host* host) {
    if (!host || worker_.joinable()) return;
    host_ = host;
    g_client_self = this;
    {
        std::lock_guard<std::mutex> lock(save_mutex_);
        save_path_ = (std::filesystem::path(host->mod_dir()) / "config.ini").string();
        save_quit_ = false;
    }
    saver_ = std::thread([this] { saver_main(); });
    saver_running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { worker_main(); });
}

void PbzfmClient::stop() {
    quit_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();

    // 反注册菜单事件：PBZFM_OnMenuEvent 的返回值就是给 PBZFM_RemoveMenuEvent 用的句柄
    // （见 third_party/pbzfm/pbzfm.h:196-198）。旧注释写"框架没有反注册 API"与事实不符，
    // 后果是 menu_event_id_ 只写不读、事件一直挂到进程结束。
    if (menu_event_id_ != 0 && api_.RemoveMenuEvent != nullptr) {
        api_.RemoveMenuEvent(menu_event_id_);
        menu_event_id_ = 0;
    }

    // 设置页/诊断页回调没有反注册入口：入口都查 alive_，DLL 卸载前先停线程，
    // 之后即便框架再调进来也是空转（与 UE4SS 控制台 tab 回调同等的存在期约束）
    alive_.store(false, std::memory_order_release);

    // 停后台写盘线程：置退出标记并唤醒，线程在退出前会把最后一次脏数据 flush 掉
    {
        std::lock_guard<std::mutex> lock(save_mutex_);
        save_quit_ = true;
    }
    save_cv_.notify_all();
    if (saver_.joinable()) saver_.join();
    saver_running_.store(false, std::memory_order_release);
}

void PbzfmClient::sync_from_config(const Config& cfg) {
    if (!alive_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(mirror_mutex_);
    mirror_ = cfg;  // 自绘页每次渲染都读 mirror_，重载后显示值即跟随 config.ini
    ++mirror_generation_;  // 外部重载：丢弃渲染线程手里的旧草案，别让拖动中的值回写覆盖它
}

bool PbzfmClient::menu_open() const {
    // alive_ 的 release/acquire 保证 api_ 在注册完成后才被读；注册前一律 false。
    if (!alive_.load(std::memory_order_acquire)) return false;
    return api_.IsMenuOpen != nullptr && api_.IsMenuOpen() != 0;
}

bool PbzfmClient::request_close_menu() {
    if (api_.CloseMenu == nullptr) return false;  // 旧框架：没有程序化关菜单
    api_.CloseMenu();
    return true;
}

Config PbzfmClient::take_draft(std::uint64_t& generation_out) {
    std::lock_guard<std::mutex> lock(mirror_mutex_);
    generation_out = mirror_generation_;
    return mirror_;
}

void PbzfmClient::store_draft(const Config& draft, std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(mirror_mutex_);
    if (generation != mirror_generation_) return;  // 期间有重载/提交：不拿旧草案覆盖
    mirror_ = draft;
}

void PbzfmClient::publish_snapshot(const PbzfmSnapshot& snap) {
    std::lock_guard<std::mutex> lock(snap_mutex_);
    snapshot_ = snap;
}

void PbzfmClient::worker_main() {
    bool loaded = false;
    for (int i = 0; i < kPollAttempts && !quit_.load(std::memory_order_acquire); ++i) {
        if (PBZFM_LoadApi(&api_)) {
            loaded = true;
            break;
        }
        Sleep(kPollIntervalMs);
    }
    if (!loaded) {
        if (host_ && !quit_.load(std::memory_order_acquire)) {
            host_->log(LogLevel::Info, "未检测到 PBZFM 菜单框架（可选组件），保持纯快捷键模式");
        }
        return;
    }
    register_all();
}

void PbzfmClient::register_all() {
    lang_.RescanLanguages(host_->mod_dir(), true);
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        mirror_ = host_->current_config();
        ++mirror_generation_;
    }
    refresh_language();  // 树节点名按启动时语言注册；此后语言变化经 RenameSection 热改名
    renamed_lang_id_ = lang_.CurrentId();
    node_names_.clear();
    group_keys_.clear();
    node_names_["language"] = lang_.T("#node_language");
    node_names_["status"] = lang_.T("#node_status");
    // 0.4.x 回退分支的整页名；此前该键从未赋值 → 向框架注册空名 tab
    node_names_["settings"] = lang_.T("#node_settings");
    {
        const char* last_group = nullptr;
        for (const PbzfmField& f : pbzfm_fields()) {
            if (last_group != nullptr && std::strcmp(last_group, f.group) == 0) continue;
            last_group = f.group;  // 组名字符串指向静态表，生命周期等同进程
            group_keys_.push_back(f.group);
            node_names_[f.group] = lang_.T(f.group);
        }
    }

    const std::string root = kSectionName;
    api_.SetSection(kSectionName);
    if (api_.AddSectionItemPath != nullptr) {
        // 0.5.0+ 分类树：六个分组 / 状态 / 语言（垫底）全部为顶层节点（无「设置」中间层）
        for (const std::string& group_key : group_keys_) {
            // 树节点显示名 = 词典译文；userdata 仍是原始组名键（render_group_page 按它过滤字段）
            api_.AddSectionItemPath((root + "/" + node_names_[group_key]).c_str(),
                                    &PbzfmClient::RenderGroupThunk, const_cast<char*>(group_key.c_str()));
        }
        api_.AddSectionItemPath((root + "/" + node_names_["status"]).c_str(), &PbzfmClient::RenderDiagThunk, this);
        api_.AddSectionItemPath((root + "/" + node_names_["language"]).c_str(),
                                &PbzfmClient::RenderLanguageThunk, this);
    } else {
        // 0.4.x 及更早：根节点 + 页内页签（整页 = 语言切换 + 分组分隔线）
        // 兜底：词典缺 #node_settings 或译文为空时，注册名不能为空（空名 tab 会变成不可点/无标题）
        const std::string settings_name = node_names_["settings"].empty() ? "Settings" : node_names_["settings"];
        const std::string status_name = node_names_["status"].empty() ? "Status" : node_names_["status"];
        api_.AddSectionItem(settings_name.c_str(), &PbzfmClient::RenderSettingsThunk, this);
        api_.AddSectionItem(status_name.c_str(), &PbzfmClient::RenderDiagThunk, this);
    }

    // v2 菜单事件（旧框架此指针为 NULL）：关菜单当帧取消改键捕获
    if (api_.OnMenuEvent != nullptr) {
        menu_event_id_ = api_.OnMenuEvent(&PbzfmClient::OnMenuEventThunk, this);
    }

    alive_.store(true, std::memory_order_release);
    if (lang_.LanguageCount() == 0) {
        host_->log(LogLevel::Warn,
                   std::string("未找到语言词典：") + host_->mod_dir() +
                       "/languages/*.json（发行包自带 en/zh/ja/ko 四份，en-US 为基准词典；升级安装请整个目录覆盖，不要只拷 DLL）；页面将显示键名");
    }
    std::string fw = std::to_string(api_.GetAbiVersion());
    if (api_.VersionString != nullptr && api_.VersionString() != nullptr) fw = api_.VersionString();
    host_->log(LogLevel::Info,
               std::string("已接入 PBZFM 菜单框架 v") + fw + "：游戏中按呼出键（默认 F8）→ " + kSectionName +
               "，设置实时生效并写回 config.ini（词典从 languages/*.json 加载，默认 en-US，对照它改译文或新增语言）");
}

// ---- 自绘页公共部分（框架渲染线程） ----

bool PbzfmClient::ensure_imgui() {
    if (imgui_checked_) return imgui_ok_;
    imgui_checked_ = true;
    imgui_ok_ = false;
    auto* ctx = static_cast<ImGuiContext*>(api_.GetImGuiContext());
    if (ctx == nullptr) {
        host_->log(LogLevel::Warn, "PBZFM 未提供 ImGui 上下文，跳过自绘页");
        return false;
    }
    // 我们的 imgui 静态副本各自持有 current-context 指针；切过去之后本线程的
    // ImGui 调用才落在框架的上下文上（框架与 UE4SS 控制台各用各的副本，互不干扰）
    ImGui::SetCurrentContext(ctx);
    if (ImGui::GetCurrentContext() != ctx || ImGui::GetIO().Fonts == nullptr) {
        host_->log(LogLevel::Warn, "PBZFM ImGui 上下文不可用（SetCurrentContext 未生效），跳过自绘页");
        return false;
    }
    // 运行时版本检测：客户端与框架的 ImGui 必须是同一版本（struct 布局一致）。
    // 用 GetVersion() 字符串比较，框架升级时只需重编本 mod，不再被编译期版本号焊死。
    const char* version = ImGui::GetVersion();
    if (version == nullptr || std::strcmp(version, IMGUI_VERSION) != 0) {
        host_->log(LogLevel::Warn, std::string("PBZFM ImGui 版本不一致（框架 ") + (version ? version : "未知") +
                                       "，客户端 " IMGUI_VERSION "），跳过自绘页以免结构错位");
        return false;
    }
    const char* backend = ImGui::GetIO().BackendPlatformName;
    if (backend == nullptr || std::strcmp(backend, kBackendFingerprint) != 0) {
        host_->log(LogLevel::Warn, "PBZFM ImGui 上下文指纹不匹配（版本不一致？），跳过自绘页");
        return false;
    }
    host_->log(LogLevel::Info, std::string("PBZFM 自绘页 ImGui 版本匹配：") + version);

    // 框架启动只按它自己的语言加载一个字体（中文环境 = 雅黑，没有谚文字形），分类树里
    // 我们注册的韩语节点名（词典译文）会整串渲染成 '?'。往框架默认字体合并 Malgun
    // Gothic（谚文来自 Malgun，汉字/假名/拉丁仍以雅黑优先）；ImGui 1.92 动态图集按需
    // 光栅化，框架自绘的树节点从下一帧起即可显示韩语。
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* base = ImGui::GetIO().FontDefault != nullptr
                       ? ImGui::GetIO().FontDefault
                       : (atlas->Fonts.empty() ? nullptr : atlas->Fonts[0]);
    if (base != nullptr) {
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.DstFont = base;
        if (atlas->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", base->LegacySize, &merge)) {
            host_->log(LogLevel::Info, "PBZFM 框架默认字体已合并 Malgun Gothic，韩语树节点可显示");
        } else {
            host_->log(LogLevel::Warn, "PBZFM 框架默认字体合并 Malgun Gothic 失败，韩语节点将显示 ?");
        }
    }
    imgui_ok_ = true;
    return true;
}

// 自绘页专用字体：框架启动时只按它自己的语言加载了一个字体（中文环境 = 雅黑），
// 而雅黑没有谚文——语言下拉里的「한국어」与整页韩语都会渲染成 '?'，且框架字体
// 只在启动时加载一次、菜单内切语言不会重载。这里懒建一个「雅黑 + Malgun Gothic」
// 合并字体（谚文来自 Malgun，汉字/假名/拉丁以雅黑优先），画我们的页面时 push 上去。
bool PbzfmClient::ensure_page_font() {
    if (page_font_ != nullptr) return true;
    if (page_font_failed_) return false;

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* base = atlas->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f);
    if (base == nullptr) base = atlas->AddFontFromFileTTF("C:/Windows/Fonts/msyhbd.ttc", 18.0f);
    if (base == nullptr) {
        page_font_failed_ = true;
        host_->log(LogLevel::Warn, "PBZFM 自绘页：系统字体加载失败，沿用框架字体（韩语可能显示 ?）");
        return false;
    }
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.DstFont = base;
    if (!atlas->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 18.0f, &merge)) {
        host_->log(LogLevel::Warn, "PBZFM 自绘页：Malgun Gothic 不可用，韩语将显示 ?");
    }
    page_font_ = base;
    return true;
}

void PbzfmClient::refresh_language() {
    // 生效语言：config.ini 的 menu_language 非 auto 且已加载 → 用之；否则跟随 PBZFM.ini
    std::string want;
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        want = mirror_.menu_language;
    }
    int idx = lang_.FindLanguage(want.c_str());
    if (idx < 0) {
        // PBZFM.ini 的 [framework] language（与自绘页同目录树；至多每秒重读一次）
        const double now = SteadySeconds();
        if (now >= pbzfm_ini_next_check_) {
            pbzfm_ini_next_check_ = now + 1.0;
            std::ifstream ini(std::filesystem::path(host_->mod_dir()).parent_path() / "PBZFM" / "PBZFM.ini",
                              std::ios::binary);
            if (ini) {
                std::string line;
                while (std::getline(ini, line)) {
                    const size_t eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = line.substr(0, eq);
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t' || key.back() == '\r')) key.pop_back();
                    if (key != "language") continue;
                    std::string value = line.substr(eq + 1);
                    const size_t b = value.find_first_not_of(" \t");
                    if (b == std::string::npos) break;
                    const size_t e = value.find_last_not_of(" \t\r\n");
                    pbzfm_ini_lang_ = value.substr(b, e - b + 1);
                    break;
                }
            }
        }
        // 按 id 选择：重扫可能重排语言表，用旧下标会选错
        lang_.SetLanguageById(pbzfm_ini_lang_);
    } else {
        lang_.SetLanguageById(want);
    }
    rename_tree_for_language();
}

// 生效语言变化时，把 0.5.0+ 分类树节点重命名为新语言（页内内容本来就地实时）。
// 顺序：先改分组（路径含旧父名），再改父与兄弟；改名成功才更新 node_names_。
void PbzfmClient::rename_tree_for_language() {
    if (api_.RenameSection == nullptr || api_.AddSectionItemPath == nullptr) return;  // 0.4.x：无树
    const std::string lang_id = lang_.CurrentId();
    if (renamed_lang_id_ == lang_id || node_names_.empty()) return;
    renamed_lang_id_ = lang_id;

    const std::string root = kSectionName;
    for (const std::string& group_key : group_keys_) {
        const std::string new_name = lang_.T(group_key.c_str());
        const std::string old_name = node_names_[group_key];
        if (old_name == new_name) continue;
        const std::string path = root + "/" + old_name;
        if (api_.RenameSection(path.c_str(), new_name.c_str()) == 1) {
            node_names_[group_key] = new_name;
        }
    }
    const std::string new_language = lang_.T("#node_language");
    if (node_names_["language"] != new_language) {
        const std::string path = root + "/" + node_names_["language"];
        if (api_.RenameSection(path.c_str(), new_language.c_str()) == 1) node_names_["language"] = new_language;
    }
    // 语言节点已移到末位：改名次序与显示次序无耦合，仅保持代码可读
    const std::string new_status = lang_.T("#node_status");
    if (node_names_["status"] != new_status) {
        const std::string path = root + "/" + node_names_["status"];
        if (api_.RenameSection(path.c_str(), new_status.c_str()) == 1) node_names_["status"] = new_status;
    }
    api_.SetSection(kSectionName);  // 文档建议：改名后重设调用线程 current
}

void PbzfmClient::request_save(const Config& draft) {
    {
        std::lock_guard<std::mutex> lock(save_mutex_);
        save_cfg_ = draft;
        save_dirty_ = true;
    }
    save_cv_.notify_one();
}

// 后台写盘线程：同一时刻最多一个写者；写盘期间新到的改动把 save_dirty_ 再置位，
// 醒来后写最后一次快照（last-write-wins）。退出时 save_quit_ 置位，循环会先把
// 剩余脏数据写完再结束，因此 stop() 返回后磁盘上一定是最新提交的配置。
void PbzfmClient::saver_main() {
    std::unique_lock<std::mutex> lock(save_mutex_);
    for (;;) {
        save_cv_.wait(lock, [this] { return save_quit_ || save_dirty_; });
        if (save_dirty_) {
            const Config cfg = save_cfg_;
            const std::string path = save_path_;
            save_dirty_ = false;
            lock.unlock();
            std::string err;
            if (save_config_file(path, cfg, &err)) {
                host_->log(LogLevel::Info, "PBZFM 菜单：config.ini 已原子写回");
            } else {
                host_->log(LogLevel::Warn, "PBZFM 菜单：config.ini 写回失败：" + err);
            }
            lock.lock();
            continue;
        }
        if (save_quit_) break;
    }
}

void PbzfmClient::commit_settings(const Config& draft, const std::string& what) {
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        mirror_ = draft;
        ++mirror_generation_;  // 本帧草案已提交：页面收尾不必再写回
    }
    // 渲染线程不再直接写盘：落盘由 saver_main 用「临时文件 + 原子替换」完成，
    // 避免框架渲染线程与引擎 tick 线程同时读写 config.ini 造成半写/读到旧值。
    if (saver_running_.load(std::memory_order_acquire)) {
        request_save(draft);
    } else {
        // 兜底（正常不会走到：commit 只可能由注册后的渲染回调触发）：同步原子写
        std::string err;
        const std::string ini_path = (std::filesystem::path(host_->mod_dir()) / "config.ini").string();
        if (!save_config_file(ini_path, draft, &err)) {
            host_->log(LogLevel::Warn, "PBZFM 菜单：config.ini 写回失败：" + err);
        }
    }
    host_->submit_config(draft);  // 引擎 Tick 线程应用（键位表重建 + 实时开关）
    host_->log(LogLevel::Info, "PBZFM 菜单：" + what + " 已更新（config.ini 写回已排队）");
}

void PbzfmClient::RenderSettingsThunk(void* userdata) {
    static_cast<PbzfmClient*>(userdata)->render_settings_page();
}

void PbzfmClient::RenderLanguageThunk(void* userdata) {
    static_cast<PbzfmClient*>(userdata)->render_language_page();
}

void PbzfmClient::RenderGroupThunk(void* userdata) {
    // userdata 是组名（字符串字面量，指向静态字段表），不是实例指针；
    // 实例走文件级 g_client_self。此前把组名错当 this 用，成员锁直接崩（dump: RtlAcquireSRWLockExclusive 读 0x1）
    if (g_client_self == nullptr) return;
    g_client_self->render_group_page(static_cast<const char*>(userdata));
}

void PbzfmClient::RenderDiagThunk(void* userdata) {
    static_cast<PbzfmClient*>(userdata)->render_diag_page();
}

void PbzfmClient::OnMenuEventThunk(int open, void* userdata) {
    auto* self = static_cast<PbzfmClient*>(userdata);
    if (!open) self->menu_closed_tick_.store(true, std::memory_order_release);
}

// 语言切换区块（语言页与 0.4.x 回退整页共用）：写进 config.ini 的 [ui] menu_language
void PbzfmClient::render_language_controls(Config& draft) {
    // 一次性拷出整份语言列表：后续遍历期间即便另一线程重扫词典，本帧仍然一致。
    const std::vector<PbzmI18n::LanguageInfo> languages = lang_.Languages();
    int lang_idx = -1;
    for (size_t i = 0; i < languages.size(); ++i) {
        if (languages[i].id == draft.menu_language) {
            lang_idx = static_cast<int>(i);
            break;
        }
    }
    const std::string auto_label = lang_.T("#lang_auto");
    // 框架风格下拉：0 号位固定为"跟随 PBZFM"，其后逐个语言词典。
    // 窄下拉（SetNextItemWidth），给右侧"重扫词典"按钮留位。
    std::vector<std::string> lang_items;
    lang_items.push_back(auto_label);
    for (const auto& info : languages) lang_items.push_back(info.display);
    std::vector<const char*> lang_ptrs;
    lang_ptrs.reserve(lang_items.size());
    for (const auto& text : lang_items) lang_ptrs.push_back(text.c_str());
    int sel = lang_idx < 0 ? 0 : lang_idx + 1;
    const float rescan_w = ImGui::CalcTextSize(lang_.T("#rescan_dicts").c_str()).x +
                           ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x -
                                                 rescan_w - ImGui::GetStyle().ItemSpacing.x));
    if (api_.Combo(lang_.T("#language").c_str(), &sel, lang_ptrs.data(),
                   static_cast<int>(lang_ptrs.size()))) {
        if (sel == 0) {
            draft.menu_language = "auto";
            commit_settings(draft, "语言：跟随 PBZFM");
        } else if (sel - 1 < static_cast<int>(languages.size())) {
            draft.menu_language = languages[static_cast<size_t>(sel - 1)].id;
            commit_settings(draft, std::string("语言：") + languages[static_cast<size_t>(sel - 1)].id);
            lang_.SetLanguageById(languages[static_cast<size_t>(sel - 1)].id);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(lang_.T("#rescan_dicts").c_str())) {
        lang_.RescanLanguages(host_->mod_dir(), true);
    }
}

// 单个字段的控件 + 提交语义（Bool/Enum/键位即时提交；Float 拖动实时预览、松手落盘提交）
// 读值统一走 pbzfm_read_field（schema_map 里的唯一实现，测试已覆盖）。此前这里把四个
// case 各内联重写了一遍，Key 支漏了 key_fallback——ini 里写了别名的键在菜单显示 "?"，
// 而引擎实际用的是回退默认键（14 个 Key(...) 的 fallback lambda 因此全部失效）。
void PbzfmClient::render_field_widget(const PbzfmField& f, Config& draft, const KeyMap& keys) {
    switch (f.kind) {
        case PbzfmFieldKind::Bool: {
            bool b = pbzfm_read_field(f, draft, keys) != 0.0;
            int bv = b ? 1 : 0;
            // 框架自绘药丸开关（v0.7.0 PBZFM_Checkbox 通道，行高与原 Checkbox 一致）
            if (api_.Checkbox(lang_.T(f.label).c_str(), &bv)) {
                f.set_bool(draft, bv != 0);
                commit_settings(draft, lang_.T(f.label));
            }
            break;
        }
        case PbzfmFieldKind::Float: {
            const std::string label_text = lang_.T(f.label);
            const std::string reset_text = lang_.T("#reset_item");
            const ImGuiStyle& style = ImGui::GetStyle();
            const float scale = style.FontSizeBase > 0.0f ? ImGui::GetFontSize() / style.FontSizeBase : 1.0f;
            // 画面效果五项才有独立开关（f.get_enabled 非空）；其它数值滑块只有重置按钮。
            const bool has_switch = f.get_enabled != nullptr && f.set_enabled != nullptr;
            bool enabled = has_switch ? pbzfm_read_enabled(f, draft) : true;

            if (has_switch) {
                // 独立开关走框架药丸（隐藏标签只画开关；EndGroup 后 IsItemHovered
                // 覆盖整组，tooltip 语义不变）
                int en = enabled ? 1 : 0;
                if (api_.Checkbox((std::string("##toggle_") + f.key).c_str(), &en)) {
                    if (pbzfm_write_enabled(f, en != 0, draft)) {
                        enabled = en != 0;
                        commit_settings(draft, label_text + " " +
                                            lang_.T(enabled ? "#switch_on" : "#switch_off"));
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lang_.T("#switch_tip").c_str());
                ImGui::SameLine();
            }

            // 旧框架没有 v4 SliderFloatEx 时回退原生滑条：恢复"标签在左"的旧行布局
            const bool use_fw = api_.SliderFloatEx != nullptr;
            if (!use_fw) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label_text.c_str());
                ImGui::SameLine();
            }

            float v = static_cast<float>(pbzfm_read_field(f, draft, keys));
            const float reset_w =
                ImGui::CalcTextSize(reset_text.c_str()).x + style.FramePadding.x * 2.0f + 2.0f * scale;
            const float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(std::max(80.0f * scale, avail - reset_w - style.ItemSpacing.x));
            if (!enabled) ImGui::BeginDisabled();
            const bool changed =
                use_fw
                    // 框架自绘滑条：标签画在轨道内（金色进度填充 + Ctrl+点击弹输入框）
                    ? api_.SliderFloatEx(label_text.c_str(), &v, static_cast<float>(f.mn),
                                         static_cast<float>(f.mx), f.fmt,
                                         f.logarithmic ? PBZFM_SLIDER_LOGARITHMIC : 0) != 0
                    : ImGui::SliderFloat((std::string("##slider_") + f.key).c_str(), &v,
                                         static_cast<float>(f.mn), static_cast<float>(f.mx),
                                         f.fmt,
                                         ImGuiSliderFlags_AlwaysClamp |
                                             (f.logarithmic && f.mn > 0.0 ? ImGuiSliderFlags_Logarithmic
                                                                          : 0)) != 0;
            if (!enabled) ImGui::EndDisabled();
            // 拖动实时预览：草案直接推引擎（引擎 Tick 应用，下一帧上屏），不写盘不打日志；
            // 松手 IsItemDeactivatedAfterEdit 再走完整提交（落盘 + 正式日志 + 再次应用）。
            // 松手帧 ImGui 的 SliderBehaviorT 先 ClearActiveID 再返回（imgui_widgets.cpp：
            // `if (!MouseDown[0]) ClearActiveID()` 在取值更新之前），所以该帧 changed 恒为
            // false——提交不能挂在 changed 上，必须在 DeactivatedAfterEdit 上无条件做。
            // （曾试过用帧内局部 pending_commit 把两帧缝起来：IsItemActive 与
            //   IsItemDeactivatedAfterEdit 互斥，局部变量跨不过帧边界，提交永远丢失。）
            // 框架自绘滑条的 Ctrl+点击输入框在弹窗里改值（轨道已非活动项），changed 且
            // 轨道不在拖动态时直接提交，与"松手提交"同语义；此时 DeactivatedItemData.ID
            // 是输入框不是滑条，两个分支不会对同一次编辑双提交。
            if (changed) {
                f.set_num(draft, v);
                host_->submit_config_preview(draft);
                if (!ImGui::IsItemActive()) commit_settings(draft, label_text);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit_settings(draft, label_text);
            }
            ImGui::SameLine();
            if (ImGui::Button((reset_text + "##reset_" + f.key).c_str())) {
                if (pbzfm_reset_field(f, draft)) {
                    enabled = pbzfm_read_enabled(f, draft);
                    commit_settings(draft, label_text + " " + reset_text);
                }
            }
            break;
        }
        case PbzfmFieldKind::Enum: {
            int idx = static_cast<int>(pbzfm_read_field(f, draft, keys));
            std::string opt_text[8];
            const char* opts[8]{};
            const int n = f.option_count < 8 ? f.option_count : 8;
            for (int i = 0; i < n; ++i) {
                opt_text[i] = lang_.T(f.options[i]);
                opts[i] = opt_text[i].c_str();
            }
            // 框架风格下拉：标签在轨道内、值在右、鎏金箭头、选中行朱砂条
            if (api_.Combo(lang_.T(f.label).c_str(), &idx, opts, n)) {
                f.set_enum(draft, idx);
                commit_settings(draft, lang_.T(f.label));
            }
            break;
        }
        case PbzfmFieldKind::Key:
            render_key_field(f, draft, keys);
            break;
    }
}

// 0.4.x 回退整页：语言切换 + 全部分组（分隔线）
void PbzfmClient::render_settings_page() {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);
    lang_.RescanLanguages(host_->mod_dir(), false);  // 外部词典热加载（内部 2 秒节流）

    std::uint64_t draft_gen = 0;
    Config draft = take_draft(draft_gen);

    render_language_controls(draft);
    ImGui::TextDisabled(lang_.T("#hint").c_str());
    ImGui::Separator();

    const char* current_group = nullptr;
    // 键位回退表：与引擎侧 build_keymap 同一实现（每帧一次；只在键名解析失败时被读到）
    const KeyMap keys = build_keymap(draft, nullptr);
    for (const PbzfmField& f : pbzfm_fields()) {
        if (current_group == nullptr || std::strcmp(current_group, f.group) != 0) {
            ImGui::SeparatorText(lang_.T(f.group).c_str());
            if (std::strcmp(f.group, "#group_look") == 0) {
                ImGui::TextDisabled(lang_.T("#look_defaults_hint").c_str());
                render_look_presets(draft);  // 0.4.x 整页回退：预设区与分组页同款
                ImGui::Separator();
            }
            current_group = f.group;
        }
        render_field_widget(f, draft, keys);
    }
    store_draft(draft, draft_gen);  // Float 拖动值跨帧保持（松手提交前不能丢）
    if (font_pushed) ImGui::PopFont();
}

// 0.5.0+ 分类树：「设置」分类页 = 语言切换 + 词典重扫（各分组是它的子节点）
void PbzfmClient::render_language_page() {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);
    lang_.RescanLanguages(host_->mod_dir(), false);

    std::uint64_t draft_gen = 0;
    Config draft = take_draft(draft_gen);
    render_language_controls(draft);
    ImGui::TextDisabled(lang_.T("#hint").c_str());
    store_draft(draft, draft_gen);
    if (font_pushed) ImGui::PopFont();
}

// 0.5.0+ 分类树：单个分组的字段（userdata = 组名字符串，指向静态字段表）
void PbzfmClient::render_group_page(const char* group) {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);

    std::uint64_t draft_gen = 0;
    Config draft = take_draft(draft_gen);
    const KeyMap keys = build_keymap(draft, nullptr);  // 键位回退表（同 render_settings_page）
    if (std::strcmp(group, "#group_look") == 0) {
        ImGui::TextDisabled(lang_.T("#look_defaults_hint").c_str());
        ImGui::Separator();
        render_look_presets(draft);  // 风格预设：保存 + 下拉套用（0.5.0+ 分组页）
        ImGui::Separator();
    }
    for (const PbzfmField& f : pbzfm_fields()) {
        if (std::strcmp(f.group, group) != 0) continue;
        render_field_widget(f, draft, keys);
    }
    store_draft(draft, draft_gen);  // Float 拖动值跨帧保持（松手提交前不能丢）
    if (font_pushed) ImGui::PopFont();
}

// 「画面效果」组页的风格预设区：保存预设按钮 + 预设下拉框（选中即套用）+ 删除按钮。
// 演进（2026-11-08 同日）：5 个写死初值按钮 → 3 固定槽位导入导出 → 保存/下拉 → +删除
//（两击确认防误删：第一击进入确认态，5 秒不点自动复位——与改键捕获 kCaptureTimeoutSec 同节奏）。
// 不走 schema（PbzfmField 没有「一次写多字段」的类型）：直接改 draft 走 commit_settings
//（mirror + saver 队列 + submit_config，与滑条松手提交同一管线），预设文件文本的真源在
// core/look_preset.h（look_preset_<编号>.ini，只含 [look] 的 14 个风格字段）。preset_msg_ /
// preset_selected_ / preset_delete_* 是框架渲染线程独占状态（pbzfm_client.h 线程归属）。
void PbzfmClient::render_look_presets(Config& draft) {
    auto file_text = [this](const char* key, const std::string& path) {
        const std::string fmt = lang_.T(key);
        char buf[256]{};
        std::snprintf(buf, sizeof(buf), fmt.c_str(), path.c_str());
        return std::string(buf);
    };
    // 显示名：look_preset_<n>.ini → 「预设 n」（#preset_name）；编号缺失/非法退回文件名
    auto display_label = [this](const std::string& path) {
        const int index = preset_index_from_path(path);
        char buf[64]{};
        if (index >= 0) {
            const std::string fmt = lang_.T("#preset_name");
            std::snprintf(buf, sizeof(buf), fmt.c_str(), index);
            return std::string(buf);
        }
        std::snprintf(buf, sizeof(buf), "%s", std::filesystem::path(path).filename().string().c_str());
        return std::string(buf);
    };

    const std::string mod_dir = host_->mod_dir();
    const std::vector<std::string> paths = list_look_presets(mod_dir);
    std::vector<std::string> labels;
    labels.reserve(paths.size());
    for (const std::string& path : paths) labels.push_back(display_label(path));

    // ---- 保存预设：写「最大编号 + 1」（绝不覆盖已有预设），成功后选中新项 ----
    if (ImGui::Button(lang_.T("#preset_save").c_str())) {
        const std::string path = next_look_preset_path(mod_dir);
        std::string err;
        if (save_text_file_atomic(path, look_preset_ini_text(draft), &err)) {
            preset_msg_ = file_text("#preset_saved_to", std::filesystem::path(path).filename().string());
            // 本帧枚举发生在保存前，不能直接用 paths.size() 当新项下标：目录里若有合法名但
            // 非法序号的文件（如 look_preset_backup.ini），它们排在有效序号之后，新预设会插在
            // 中间。重枚举一次按路径定位；刚写完的文件目录迭代器立即可见，找不到再退回末尾。
            const std::vector<std::string> fresh = list_look_presets(mod_dir);
            const auto it = std::find(fresh.begin(), fresh.end(), path);
            preset_selected_ =
                it != fresh.end() ? static_cast<int>(it - fresh.begin()) : static_cast<int>(fresh.size());
            host_->log(LogLevel::Info, "PBZFM 菜单：风格预设已保存 " + path);
        } else {
            preset_msg_ = err;
            host_->log(LogLevel::Warn, "PBZFM 菜单：风格预设保存失败：" + err);
        }
    }
    ImGui::SameLine();

    // ---- 预设下拉：逐帧枚举（外部手删文件即时反映）；选中即套用（滑条跳值就是反馈）----
    std::vector<std::string> combo_items = labels;
    if (combo_items.empty()) combo_items.push_back(lang_.T("#preset_none"));  // 空列表占位（禁用）
    int selection = preset_selected_;
    if (selection >= static_cast<int>(combo_items.size())) selection = -1;  // 外部删除后的残留索引
    const bool has_presets = !paths.empty();
    if (!has_presets) {
        selection = 0;
        ImGui::BeginDisabled();
    }
    // 框架风格下拉（api_.Combo 取 items 数组；逐帧物化指针，字符串本体活到 EndPopup 后）
    std::vector<const char*> item_ptrs;
    item_ptrs.reserve(combo_items.size());
    for (const auto& text : combo_items) item_ptrs.push_back(text.c_str());
    // 删除按钮先量宽（确认态会换文案，取两态最大值给下拉留位，避免按钮换字时下拉宽度跳变）
    const ImGuiStyle& style = ImGui::GetStyle();
    const float del_width =
        std::max(ImGui::CalcTextSize(lang_.T("#preset_delete").c_str()).x,
                 ImGui::CalcTextSize(lang_.T("#preset_delete_confirm").c_str()).x) +
        style.FramePadding.x * 2.0f + 2.0f;
    ImGui::SetNextItemWidth(std::max(140.0f, ImGui::GetContentRegionAvail().x - del_width - style.ItemSpacing.x));
    if (api_.Combo("##look_preset_select", &selection, item_ptrs.data(),
                   static_cast<int>(item_ptrs.size())) &&
        has_presets && selection >= 0 && selection < static_cast<int>(paths.size())) {
        preset_selected_ = selection;
        const std::string& path = paths[static_cast<size_t>(selection)];
        const std::string text = read_text_file(path);
        if (text.empty() || !parse_look_preset_ini(text, draft)) {
            preset_msg_ = file_text("#preset_missing", std::filesystem::path(path).filename().string());
            host_->log(LogLevel::Warn, "PBZFM 菜单：风格预设套用失败（文件为空或无 [look] 段）：" + path);
        } else {
            const std::string what = file_text("#preset_loaded", display_label(path));
            commit_settings(draft, what);
            preset_msg_ = what;
        }
    } else if (has_presets) {
        preset_selected_ = selection;
    }

    // ---- 删除预设：两击确认（目标按文件路径锁定，换选中即失效），删文件本体 ----
    if (!preset_delete_target_.empty() && SteadySeconds() >= preset_delete_deadline_) {
        preset_delete_target_.clear();  // 超时懒复位：确认态只由这两个成员表达
        preset_delete_deadline_ = 0.0;
    }
    const bool can_delete =
        has_presets && preset_selected_ >= 0 && preset_selected_ < static_cast<int>(paths.size());
    const bool confirming = can_delete && !preset_delete_target_.empty() &&
                            preset_delete_target_ == paths[static_cast<size_t>(preset_selected_)];
    ImGui::SameLine();  // 删除按钮与下拉同行（del_width 已给下拉留位）
    if (!can_delete) ImGui::BeginDisabled();
    if (ImGui::Button((confirming ? lang_.T("#preset_delete_confirm") : lang_.T("#preset_delete")).c_str())) {
        if (confirming) {
            const std::string path = preset_delete_target_;  // 拷贝：清空确认态不能让引用失效
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) {
                preset_msg_ = "删除失败：" + ec.message();
                host_->log(LogLevel::Warn, "PBZFM 菜单：风格预设删除失败：" + ec.message() + "：" + path);
            } else {
                preset_msg_ = file_text("#preset_deleted", display_label(path));
                host_->log(LogLevel::Info, "PBZFM 菜单：风格预设已删除 " + path);
                preset_selected_ = -1;  // 该项已不存在：清选中，防止残留索引指向列表里的下一项
            }
            preset_delete_target_.clear();
            preset_delete_deadline_ = 0.0;
        } else {
            preset_delete_target_ = paths[static_cast<size_t>(preset_selected_)];
            preset_delete_deadline_ = SteadySeconds() + kCaptureTimeoutSec;
        }
    }
    if (!can_delete) ImGui::EndDisabled();

    if (!has_presets) {
        ImGui::EndDisabled();
        preset_selected_ = -1;
    }

    if (!preset_msg_.empty()) {
        ImGui::TextDisabled("%s", preset_msg_.c_str());
    }
}

void PbzfmClient::render_key_field(const PbzfmField& f, Config& draft, const KeyMap& keys) {
    // 捕获态是成员变量（H-4：原函数内 static），线程归属见 pbzfm_client.h：
    // 仅框架渲染线程读写；5 秒未按键自动取消，菜单被关掉（v2 菜单事件）时立即取消。
    if (menu_closed_tick_.exchange(false, std::memory_order_acq_rel)) {
        key_capturing_ = false;
    }

    const std::string label_text = lang_.T(f.label);
    // 读值走 pbzfm_read_field（单一真源）：键名解析失败时回退 KeyMap 的默认键，
    // 不再显示 "?"——引擎实际用的就是那个回退键。
    const int current_vk = static_cast<int>(pbzfm_read_field(f, draft, keys));
    char kb[64]{};
    std::snprintf(kb, sizeof(kb), "[%s]", current_vk != 0 ? vk_to_name(current_vk).c_str() : "?");
    const float button_w = KeyButtonWidth(kb);
    const float label_w = ImGui::CalcTextSize(label_text.c_str()).x;
    const bool same_line = label_w + 12.0f + button_w <= ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(label_text.c_str());
    if (same_line) ImGui::SameLine(0.0f, 12.0f);

    if (key_capturing_ && key_capture_field_ == &f) {
        ImGui::Button(lang_.T("#capturing").c_str(), ImVec2(button_w, 0.0f));
        for (const int cand : kCandidateVks) {
            const ImGuiKey key = VkToImGuiKey(cand);
            if (key != ImGuiKey_None && ImGui::IsKeyPressed(key)) {
                Config next = draft;
                if (pbzfm_apply_field(f, cand, next)) {
                    commit_settings(next, lang_.T(f.label));
                    draft = next;
                } else {
                    host_->log(LogLevel::Warn, std::string("PBZFM 菜单：") + lang_.T(f.label) + " 不支持该键，保持原键");
                }
                key_capturing_ = false;
                break;
            }
        }
        if (key_capturing_ && SteadySeconds() - key_capture_started_ > kCaptureTimeoutSec) {
            key_capturing_ = false;
        }
    } else if (ImGui::Button(kb, ImVec2(button_w, 0.0f))) {
        key_capturing_ = true;
        key_capture_field_ = &f;
        key_capture_started_ = SteadySeconds();
    }
}

void PbzfmClient::render_diag_page() {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);
    PbzfmSnapshot s;
    {
        std::lock_guard<std::mutex> lock(snap_mutex_);
        s = snapshot_;
    }

    if (s.active) {
        const std::string state_text = s.frozen ? lang_.T("#status_frozen") : lang_.T("#status_live");
        api_.Text(state_text.c_str());
    } else {
        api_.TextDisabled(lang_.T("#status_off").c_str());
    }
    api_.Separator();

    float fov = static_cast<float>(s.fov);
    if (api_.SliderFloat("FOV", &fov, static_cast<float>(s.fov_min), static_cast<float>(s.fov_max), "%.1f")) {
        host_->submit_command(PbzfmCommand{PbzfmAction::SetFov, static_cast<double>(fov)});
    }

    const std::string mode_label = s.active ? lang_.T("#btn_exit") : lang_.T("#btn_enter");
    if (api_.Button(mode_label.c_str())) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ToggleMode, 0.0});
    }
    api_.SameLine();
    const std::string freeze_label = s.frozen ? lang_.T("#btn_resume") : lang_.T("#btn_freeze");
    if (api_.Button(freeze_label.c_str())) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ToggleFreeze, 0.0});
    }
    api_.SameLine();
    if (api_.Button(lang_.T("#btn_reset").c_str())) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ResetView, 0.0});
    }
    if (api_.Button(lang_.T("#btn_reload").c_str())) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ReloadConfig, 0.0});
    }
    api_.Separator();

    char buf[640]{};
    std::snprintf(buf, sizeof(buf), lang_.T("#pose").c_str(), s.pos[0], s.pos[1], s.pos[2], s.pitch, s.yaw, s.roll);
    api_.Text(buf);
    std::snprintf(buf, sizeof(buf), lang_.T("#help_look").c_str(), s.free_look_key.c_str(),
                  s.move_forward_key.c_str(), s.move_back_key.c_str(), s.move_left_key.c_str(),
                  s.move_right_key.c_str(), s.roll_left_key.c_str(), s.roll_right_key.c_str(),
                  s.boost_key.c_str(), s.slow_key.c_str());
    api_.TextDisabled(buf);
    if (s.wheel_fov) api_.TextDisabled(lang_.T("#help_wheel").c_str());
    std::snprintf(buf, sizeof(buf), lang_.T("#help_keys").c_str(), s.toggle_key.c_str(), s.freeze_key.c_str(),
                  s.reset_key.c_str(), s.roll_reset_key.c_str());
    api_.TextDisabled(buf);
    api_.TextDisabled((lang_.T("#pad_prefix") + s.pad_backend).c_str());
    api_.TextDisabled(PBZPM_DISPLAY_NAME " " PBZPM_VERSION_FULL);
    if (font_pushed) ImGui::PopFont();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS