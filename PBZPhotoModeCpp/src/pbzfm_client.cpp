#include "pbzfm_client.h"

#ifdef PBZPM_WITH_UE4SS

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <imgui.h>

#include "core/input_logic.h"
#include "core/version.h"
#include "pbzfm_schema_map.h"

// 自绘页直接画进框架的 ImGui 上下文：两边必须链接同一版 ImGui（struct 布局一致），
// 1.92.1 / 19210 由两仓的 third_party/imgui 同步固定，升版本时两边一起动
static_assert(IMGUI_VERSION_NUM == 19210, "PBZFM 自绘页要求客户端与框架同版 ImGui (1.92.1)");

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

// 改键捕获用：候选 VK → ImGuiKey。与框架输入层的 VkToImGuiKey 保持一致
// （键喂进 ImGui 后再查回来），不支持的键返回 ImGuiKey_None。
constexpr ImGuiKey VkToImGuiKey(int vk) {
    switch (vk) {
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
    worker_ = std::thread([this] { worker_main(); });
}

void PbzfmClient::stop() {
    quit_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    // 不撤销注册：框架没有反注册 API。回调入口都查 alive_，DLL 卸载前先停线程，
    // 之后即便框架再调进来也是空转（与 UE4SS 控制台 tab 回调同等的存在期约束）
    alive_.store(false, std::memory_order_release);
}

void PbzfmClient::sync_from_config(const Config& cfg) {
    if (!alive_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(mirror_mutex_);
    mirror_ = cfg;  // 自绘页每次渲染都读 mirror_，重载后显示值即跟随 config.ini
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
    }
    refresh_language();  // 树节点名按启动时语言注册；此后语言变化经 RenameSection 热改名
    renamed_lang_id_ = lang_.CurrentId();
    node_names_.clear();
    group_keys_.clear();
    node_names_["language"] = lang_.T("#node_language");
    node_names_["status"] = lang_.T("#node_status");
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
        api_.AddSectionItem(node_names_["settings"].c_str(), &PbzfmClient::RenderSettingsThunk, this);
        api_.AddSectionItem(node_names_["status"].c_str(), &PbzfmClient::RenderDiagThunk, this);
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
    const char* backend = ImGui::GetIO().BackendPlatformName;
    if (backend == nullptr || std::strcmp(backend, kBackendFingerprint) != 0) {
        host_->log(LogLevel::Warn, "PBZFM ImGui 上下文指纹不匹配（版本不一致？），跳过自绘页");
        return false;
    }

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
        static std::string s_pbzfm_lang;
        static double s_next_check = 0.0;
        const double now = SteadySeconds();
        if (now >= s_next_check) {
            s_next_check = now + 1.0;
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
                    s_pbzfm_lang = value.substr(b, e - b + 1);
                    break;
                }
            }
        }
        idx = lang_.FindLanguage(s_pbzfm_lang.c_str());
    }
    lang_.SetLanguage(idx >= 0 ? idx : 0);
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

void PbzfmClient::commit_settings(const Config& draft, const std::string& what) {
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        mirror_ = draft;
    }
    std::string err;
    const std::string ini_path = (std::filesystem::path(host_->mod_dir()) / "config.ini").string();
    if (!save_config_file(ini_path, draft, &err)) {
        host_->log(LogLevel::Warn, "PBZFM 菜单：config.ini 写回失败：" + err);
    }
    host_->submit_config(draft);  // 引擎 Tick 线程应用（键位表重建 + 实时开关）
    host_->log(LogLevel::Info, "PBZFM 菜单：" + what + " 已更新并写回 config.ini");
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
    const int lang_idx = lang_.FindLanguage(draft.menu_language.c_str());
    const char* lang_preview = lang_idx >= 0 ? lang_.LanguageDisplay(lang_idx) : lang_.T("#lang_auto");
    if (ImGui::BeginCombo(lang_.T("#language"), lang_preview)) {
        if (ImGui::Selectable(lang_.T("#lang_auto"), lang_idx < 0) && lang_idx >= 0) {
            draft.menu_language = "auto";
            commit_settings(draft, "语言：跟随 PBZFM");
        }
        for (int i = 0; i < lang_.LanguageCount(); ++i) {
            const bool selected = i == lang_idx;
            if (ImGui::Selectable(lang_.LanguageDisplay(i), selected) && !selected) {
                draft.menu_language = lang_.LanguageId(i);
                commit_settings(draft, std::string("语言：") + lang_.LanguageId(i));
                lang_.SetLanguage(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(lang_.T("#rescan_dicts"))) {
        lang_.RescanLanguages(host_->mod_dir(), true);
    }
}

// 单个字段的控件 + 提交语义（Bool/Enum/键位即时提交，Float 松手提交）
void PbzfmClient::render_field_widget(const PbzfmField& f, Config& draft) {
    switch (f.kind) {
        case PbzfmFieldKind::Bool: {
            bool b = f.get_bool(draft);
            if (ImGui::Checkbox(lang_.T(f.label), &b)) {
                f.set_bool(draft, b);
                commit_settings(draft, lang_.T(f.label));
            }
            break;
        }
        case PbzfmFieldKind::Float: {
            float v = static_cast<float>(f.get_num(draft));
            if (ImGui::SliderFloat(lang_.T(f.label), &v, static_cast<float>(f.mn), static_cast<float>(f.mx), f.fmt)) {
                f.set_num(draft, v);  // 拖动中先落 draft，松手才提交（与框架 schema 同语义）
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit_settings(draft, lang_.T(f.label));
            }
            break;
        }
        case PbzfmFieldKind::Enum: {
            int idx = f.get_enum(draft);
            const char* opts[8]{};
            const int n = f.option_count < 8 ? f.option_count : 8;
            for (int i = 0; i < n; ++i) opts[i] = lang_.T(f.options[i]);
            if (ImGui::Combo(lang_.T(f.label), &idx, opts, n)) {
                f.set_enum(draft, idx);
                commit_settings(draft, lang_.T(f.label));
            }
            break;
        }
        case PbzfmFieldKind::Key:
            render_key_field(f, draft);
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

    Config draft;
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        draft = mirror_;
    }

    render_language_controls(draft);
    ImGui::TextDisabled(lang_.T("#hint"));
    ImGui::Separator();

    const char* current_group = nullptr;
    for (const PbzfmField& f : pbzfm_fields()) {
        if (current_group == nullptr || std::strcmp(current_group, f.group) != 0) {
            ImGui::SeparatorText(lang_.T(f.group));
            current_group = f.group;
        }
        render_field_widget(f, draft);
    }
    if (font_pushed) ImGui::PopFont();
}

// 0.5.0+ 分类树：「设置」分类页 = 语言切换 + 词典重扫（各分组是它的子节点）
void PbzfmClient::render_language_page() {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);
    lang_.RescanLanguages(host_->mod_dir(), false);

    Config draft;
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        draft = mirror_;
    }
    render_language_controls(draft);
    ImGui::TextDisabled(lang_.T("#hint"));
    if (font_pushed) ImGui::PopFont();
}

// 0.5.0+ 分类树：单个分组的字段（userdata = 组名字符串，指向静态字段表）
void PbzfmClient::render_group_page(const char* group) {
    if (!alive_.load(std::memory_order_acquire) || !ensure_imgui()) return;
    refresh_language();
    const bool font_pushed = ensure_page_font();
    if (font_pushed) ImGui::PushFont(page_font_, page_font_->LegacySize);

    Config draft;
    {
        std::lock_guard<std::mutex> lock(mirror_mutex_);
        draft = mirror_;
    }
    for (const PbzfmField& f : pbzfm_fields()) {
        if (std::strcmp(f.group, group) != 0) continue;
        render_field_widget(f, draft);
    }
    if (font_pushed) ImGui::PopFont();
}

void PbzfmClient::render_key_field(const PbzfmField& f, Config& draft) {
    // 捕获态只存在于渲染线程（static），5 秒未按键自动取消；
    // 菜单被关掉（v2 菜单事件）时立即取消，避免重开后停在「按下新按键…」
    static bool capturing = false;
    static const PbzfmField* capture_field = nullptr;
    static double capture_started = 0.0;
    if (menu_closed_tick_.exchange(false, std::memory_order_acq_rel)) {
        capturing = false;
    }

    ImGui::TextUnformatted(lang_.T(f.label));
    ImGui::SameLine(0.0f, 12.0f);
    const int current_vk = vk_from_name(draft.*f.key_name);
    char kb[64]{};
    std::snprintf(kb, sizeof(kb), "[%s]", current_vk != 0 ? vk_to_name(current_vk).c_str() : "?");

    if (capturing && capture_field == &f) {
        ImGui::Button(lang_.T("#capturing"), ImVec2(150.0f, 0.0f));
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
                capturing = false;
                break;
            }
        }
        if (capturing && SteadySeconds() - capture_started > kCaptureTimeoutSec) {
            capturing = false;
        }
    } else if (ImGui::Button(kb, ImVec2(150.0f, 0.0f))) {
        capturing = true;
        capture_field = &f;
        capture_started = SteadySeconds();
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
        api_.Text(s.frozen ? lang_.T("#status_frozen") : lang_.T("#status_live"));
    } else {
        api_.TextDisabled(lang_.T("#status_off"));
    }
    api_.Separator();

    float fov = static_cast<float>(s.fov);
    if (api_.SliderFloat("FOV", &fov, static_cast<float>(s.fov_min), static_cast<float>(s.fov_max), "%.1f")) {
        host_->submit_command(PbzfmCommand{PbzfmAction::SetFov, static_cast<double>(fov)});
    }

    if (api_.Button(s.active ? lang_.T("#btn_exit") : lang_.T("#btn_enter"))) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ToggleMode, 0.0});
    }
    api_.SameLine();
    if (api_.Button(s.frozen ? lang_.T("#btn_resume") : lang_.T("#btn_freeze"))) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ToggleFreeze, 0.0});
    }
    api_.SameLine();
    if (api_.Button(lang_.T("#btn_reset"))) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ResetView, 0.0});
    }
    if (api_.Button(lang_.T("#btn_reload"))) {
        host_->submit_command(PbzfmCommand{PbzfmAction::ReloadConfig, 0.0});
    }
    api_.Separator();

    char buf[320]{};
    std::snprintf(buf, sizeof(buf), lang_.T("#pose"), s.pos[0], s.pos[1], s.pos[2], s.pitch, s.yaw, s.roll);
    api_.Text(buf);
    api_.TextDisabled(lang_.T("#help_look"));
    std::snprintf(buf, sizeof(buf), lang_.T("#help_keys"), s.toggle_key.c_str(), s.freeze_key.c_str(),
                  s.reset_key.c_str(), s.roll_reset_key.c_str());
    api_.TextDisabled(buf);
    api_.TextDisabled((std::string(lang_.T("#pad_prefix")) + s.pad_backend).c_str());
    api_.TextDisabled(PBZPM_DISPLAY_NAME " " PBZPM_VERSION);
    if (font_pushed) ImGui::PopFont();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
