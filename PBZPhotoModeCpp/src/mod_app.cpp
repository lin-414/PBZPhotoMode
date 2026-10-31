// UE4SS mod 主体：把输入/引擎事件翻译成 core 层 Intent，再把结果交给 PhotoMode。
#ifdef PBZPM_WITH_UE4SS

#include "mod_app.h"
#include <algorithm>

#include <imgui.h>

#include <UE4SSProgram.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UEngine.hpp>

#include <chrono>
#include <filesystem>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <mutex>
#include <string>

#include "core/menu_model.h"
#include "core/menu_view.h"
#include "core/version.h"
#include "engine/imgui_overlay.h"
#include "engine/ue4ss_port.h"
#include "engine/umg2_layout.h"
#include "engine/wbp_layout.h"
#include "ui_tab.h"
#include "win_input.h"

namespace pbzpm {

// 引擎 GetInputMouseDelta 给的是「像素 ÷ 游戏鼠标灵敏度」，量级只有 0.1~0.2；
// 乘这个系数放回像素量级，再交给 core 的 look_sensitivity（度/单位）换算成角度。
constexpr double kEngineMouseScale = 40.0;
namespace {

// 单调时钟：冻结时间时引擎 delta 会变成 0，但相机还要能动，所以 dt 用真实时间算
auto monotonic_seconds() -> double {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

// UE4SS 的引擎 Tick 回调是普通函数指针，用全局实例桥回对象
PhotoModeMod* g_mod = nullptr;

// STR() 是 L##x 拼接，参数必须已经是字符串字面量；PBZPM_VERSION 是宏，需要两级展开
#define PBZPM_WIDEN_IMPL(x) L##x
#define PBZPM_WIDEN(x) PBZPM_WIDEN_IMPL(x)

// 交互菜单的宿主窗口：本进程里类名 UnrealWindow、可见、客户区最大的顶层窗口。
// FindWindowW 只认第一个匹配（闪屏/隐藏窗口可能排在前面），所以全量枚举挑最大的。
auto find_umg2_window() -> HWND {
    struct Scan {
        HWND best{nullptr};
        long best_area{0};
    } scan;
    EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
            auto* s = reinterpret_cast<Scan*>(lparam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId()) return TRUE;
            wchar_t cls[32]{};
            if (GetClassNameW(hwnd, cls, 32) == 0 || wcscmp(cls, L"UnrealWindow") != 0) return TRUE;
            if (!IsWindowVisible(hwnd)) return TRUE;
            RECT r{};
            if (!GetClientRect(hwnd, &r)) return TRUE;
            const long area = (r.right - r.left) * (r.bottom - r.top);
            if (area > s->best_area) {
                s->best_area = area;
                s->best = hwnd;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&scan));
    return scan.best;
}

// 窗口 DPI：每显示器感知的进程里 GetDpiForWindow 给真实值（拿不到就退 GetDeviceCaps）。
// UMG 坐标是 Slate 单位（逻辑像素），物理像素 = Slate × 缩放；缩放错一点命中区就整体偏移。
auto umg2_dpi(HWND window) -> UINT {
    if (window != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static const GetDpiForWindowFn get_dpi = reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
        UINT dpi = get_dpi != nullptr ? get_dpi(window) : 0;
        if (dpi == 0) {
            if (const HDC dc = GetDC(window); dc != nullptr) {
                dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
                ReleaseDC(window, dc);
            }
        }
        if (dpi != 0) return dpi;
    }
    return 96;
}

// mod 目录 umg2_scale.txt：写一个数（如 1.0）可强制命中判定用的 UI 缩放，
// 绕开游戏自定的 UI 缩放曲线；文件缺失或数值非法返回 0（表示跟随窗口 DPI）。
auto read_umg2_scale_override(const std::string& dir) -> float {
    std::ifstream f(std::filesystem::path(dir) / "umg2_scale.txt", std::ios::binary);
    if (!f) return 0.0f;
    char line[64]{};
    f.getline(line, sizeof(line));
    char* end = nullptr;
    const float v = std::strtof(line, &end);
    return (end != line && v > 0.01f && v < 10.0f) ? v : 0.0f;
}

}  // namespace

PhotoModeMod::PhotoModeMod() {
    ModName = STR("PBZPhotoModeCpp");
    ModVersion = PBZPM_WIDEN(PBZPM_VERSION);
    ModDescription = STR("Photo mode: free camera / FOV / auto-pause time / ImGui tab");
    ModAuthors = STR("PBZPhotoMode");
    g_mod = this;
}

PhotoModeMod::~PhotoModeMod() {
    if (overlay_ != nullptr) {
        overlay_->stop();
        overlay_ = nullptr;
    }
    if (input_source_ != nullptr) {
        input_source_->uninstall();
    }
    if (mode_ && mode_->active()) {
        std::lock_guard<std::mutex> lock(mutex_);
        mode_->leave("mod unload");
    }
    if (g_mod == this) g_mod = nullptr;
}

auto PhotoModeMod::mod_directory() -> std::string {
    auto directory = std::filesystem::path(RC::UE4SSProgram::get_program().get_mods_directory()) / "PBZPhotoModeCpp";
    return directory.string();
}

void PhotoModeMod::reload_config() {
    const std::filesystem::path path = std::filesystem::path(mod_directory()) / "config.ini";
    const ConfigLoadResult result = load_config_file(path.string());
    cfg_ = result.cfg;
    keys_ = build_keymap(cfg_, nullptr);

    if (port_) {
        for (const std::string& warning : result.warnings) {
            port_->log(LogLevel::Warn, "配置：" + warning);
        }
        port_->log(LogLevel::Info, "配置来源：" + result.source);
    }
    if (mode_) {
        mode_->set_config(cfg_);
    }
    if (port_) {
        port_->wbp_set_asset_path(cfg_.menu_asset_path);
    }
}

void PhotoModeMod::bind_engine_tick() {
    if (tick_hook_installed_) return;
    RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float delta_seconds) {
        if (g_mod != nullptr) g_mod->on_engine_tick(static_cast<double>(delta_seconds));
    });
    // post 回调在所有 actor/PC 的 Tick 之后跑：在这里重新夺一次视角，
    // 就能压过游戏自己每帧设置 ViewTarget 的行为
    RC::Unreal::Hook::RegisterEngineTickPostCallback([](RC::Unreal::UEngine*, float) {
        if (g_mod != nullptr) g_mod->on_engine_tick_post();
    });
    tick_hook_installed_ = true;
}

void PhotoModeMod::on_engine_tick_post() {
    if (!mode_ || !port_ || !mode_->active()) return;
    port_->reassert_view_target();
}

void PhotoModeMod::on_unreal_init() {
    reload_config();
    port_ = std::make_unique<Ue4ssPort>(std::filesystem::path(mod_directory()));
    mode_ = std::make_unique<PhotoMode>(*port_, cfg_);
    port_->wbp_set_asset_path(cfg_.menu_asset_path);

    // pak 自动安装（1.3.0）：WBP 资产必须放游戏 Paks\~mods 才会被挂载，
    // 模组管理器只往 ue4ss\Mods\ 装——所以由 DLL 启动时把 paks\ 三件套拷过去
    if (cfg_.menu_pak_autocopy) {
        ensure_pak_installed();
    }

    input_source_ = &Win32InputSource::instance();
    // 排障开关：mod 目录里放一个 no_wheel_hook.txt 就不装低级鼠标钩子
    const bool wheel_hook = !std::filesystem::exists(std::filesystem::path(mod_directory()) / "no_wheel_hook.txt");
    if (!input_source_->install(wheel_hook)) {
        port_->log(LogLevel::Warn, "鼠标钩子安装失败：滚轮 FOV / 按住 Alt 转视角可能不可用");
    }

    // 游戏内图形菜单：1.1.0 在这里直接创建 dummy D3D 设备并补 vtable，跟游戏 RHI 初始化
    // 撞车导致启动闪退。1.1.1 起延迟到首次进入摄影模式（引擎线程上 RHI 已就绪）才安装。
    // 排障开关：mod 目录放 no_imgui_menu.txt 可彻底禁用图形菜单（只用 UMG 文字菜单）。
    overlay_ = &ImGuiOverlay::instance();
    if (std::filesystem::exists(std::filesystem::path(mod_directory()) / "no_imgui_menu.txt")) {
        overlay_disabled_ = true;
        port_->log(LogLevel::Info, "检测到 no_imgui_menu.txt，图形菜单已禁用（使用文字菜单）");
    } else {
        port_->log(LogLevel::Info, "图形菜单：延迟初始化（首次进入摄影模式时安装钩子）");
    }

    bind_engine_tick();
    last_tick_time_ = monotonic_seconds();
    std::string load_message =
        std::string("加载完成 v") + PBZPM_VERSION + "，按 " + vk_to_name(keys_.toggle);
    if (cfg_.pad_enabled) {
        load_message += " 或手柄 " + pad_combo_name(keys_.pad_toggle);
        std::string pad_api = input_source_->pad_api_ready()
                                  ? "XInput 已加载：" + input_source_->pad_api_module()
                                  : "XInput 加载失败";
        // DualSense/DS4 不是 XInput 设备，XInputGetState 永远读不到，PS 手柄由 DInput 后端接手
        if (input_source_->pad_dinput_ready()) {
            pad_api += "；PS 手柄走 DInput";
        }
        if (!input_source_->pad_api_ready() && !input_source_->pad_dinput_ready()) {
            pad_api += "，手柄功能不可用！";
        }
        load_message += "（" + pad_api + "）";
    }
    load_message += " 进入摄影模式";
    port_->log(LogLevel::Info, load_message);
}

void PhotoModeMod::on_ui_init() {
    // 不调用这个宏的话，UI 线程的 imgui context 是空的，一画就崩
    UE4SS_ENABLE_IMGUI();
    register_tab(STR("PBZ Photo Mode"), &PhotoModeMod::render_tab_bridge);
}

void PhotoModeMod::on_update() {
    // 每帧只做轻量工作；真正的逻辑在引擎 Tick 里
}

void PhotoModeMod::render_tab_bridge(RC::CppUserModBase* owner) {
    if (owner == nullptr) return;
    static_cast<PhotoModeMod*>(owner)->render_tab_impl();
}

void PhotoModeMod::render_tab_impl() {
    if (!mode_ || !port_) {
        ImGui::TextUnformatted("正在等待游戏引擎初始化…");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    ImGui::TextUnformatted(mode_->active() ? "摄影模式：运行中" : "摄影模式：未启用");
    if (cfg_.pad_enabled) {
        ImGui::Text("快捷键 %s 或手柄 %s 进入 / 退出；%s 冻结时间", vk_to_name(keys_.toggle).c_str(),
                    pad_combo_name(keys_.pad_toggle).c_str(), vk_to_name(keys_.freeze).c_str());
    } else {
        ImGui::Text("快捷键 %s 进入 / 退出；%s 冻结时间", vk_to_name(keys_.toggle).c_str(),
                    vk_to_name(keys_.freeze).c_str());
    }
    ImGui::Separator();

    UiCommands commands{};
    bool reload = false;
    render_photo_mode_tab(*mode_, cfg_, keys_, reload, commands);

    if (reload) {
        reload_config();
    }
    if (commands.apply_fov) {
        mode_->set_fov(commands.new_fov);
    }
    if (commands.toggle_freeze) {
        mode_->toggle_freeze();
    }
    if (commands.hide_menu) {
        mode_->set_menu_visible(false);
    }
    if (commands.exit_mode) {
        mode_->leave("imgui");
        input_.reset();
    }
}

// 图形菜单延迟初始化：只在首次进入摄影模式时执行一次（游戏线程上 RHI 已就绪）。
// 失败或被禁用时永久退回 UMG 文字菜单，不再重试。
void PhotoModeMod::ensure_overlay_started() {
    if (overlay_disabled_ || overlay_tried_ || overlay_ == nullptr) return;
    overlay_tried_ = true;

    // 帧生成/插帧插件会包装交换链并管住 back buffer 的资源状态，
    // 我们注入的渲染 barrier 跟它们冲突会把游戏 RHI 打成致命错误（1.1.2 实机验证）。
    // 检测到这些模块就不启用 DX12 图形菜单；想强行尝试：mod 目录放 imgui_dx12_force.txt。
    static constexpr const wchar_t* kFrameGenModules[] = {
        L"amd_fidelityfx_framegeneration_dx12.dll",  // FSR3 帧生成
        L"nvngx_dlssg.dll",                          // DLSS 帧生成
        L"sl.interposer.dll",                        // NVIDIA Streamline（管理帧生成）
        L"libxess.dll",                              // XeSS（插手交换链）
    };
    for (const wchar_t* module_name : kFrameGenModules) {
        if (GetModuleHandleW(module_name) != nullptr) {
            const bool force = std::filesystem::exists(std::filesystem::path(mod_directory()) / "imgui_dx12_force.txt");
            if (!force) {
                overlay_disabled_ = true;
                std::string name(1024, '\0');
                const int len = WideCharToMultiByte(CP_UTF8, 0, module_name, -1, name.data(), static_cast<int>(name.size()), nullptr, nullptr);
                name.resize(len > 0 ? len - 1 : 0);
                port_->log(LogLevel::Warn, "检测到帧生成/插帧插件（" + name +
                                               "），图形菜单与它冲突，已自动停用（使用文字菜单）。\n"
                                               "想尝试启用：在游戏设置里关掉帧生成后，mod 目录放 imgui_dx12_force.txt 再试。");
                return;
            }
            port_->log(LogLevel::Warn, "检测到帧生成/插帧插件，但存在 imgui_dx12_force.txt：仍尝试启用图形菜单（可能不稳定）");
            break;
        }
    }
    if (overlay_->start([this](LogLevel level, const std::string& message) { port_->log(level, message); })) {
        port_->log(LogLevel::Info, "图形菜单已就绪：按 T 或手柄 START 打开（首次打开加载中文字体可能轻微卡顿）");
    } else {
        port_->log(LogLevel::Warn, "图形菜单初始化失败，沿用 UMG 文字菜单（原因见上一条日志）");
    }
}

void PhotoModeMod::handle_toggle() {
    if (!mode_) return;
    if (mode_->active()) {
        mode_->leave("hotkey");
        if (overlay_ != nullptr) overlay_->set_menu_open(false);
    } else if (!mode_->enter()) {
        port_->log(LogLevel::Error, "进入摄影模式失败：" + mode_->last_error());
    } else {
        // 进模式时把 PC/Pawn/HUD/相机/ViewTarget 全打一遍，真机排障靠这个
        port_->dump_engine_state();
        ensure_overlay_started();
        if (overlay_ != nullptr && overlay_->available()) overlay_->set_menu_open(true);
        if (input_source_ != nullptr) {
            // raw=0 说明 Raw Input 没注册上（退化到 GetCursorPos 差分）；
            // 计数一直不涨说明输入根本没派发到本进程
            port_->log(LogLevel::Info, std::string("鼠标输入源 raw=") + (input_source_->raw_ready() ? "1" : "0") +
                                           " 事件计数=" + std::to_string(input_source_->mouse_event_count()));
        }
    }
    input_.reset();
    overlay_ok_ = false;
}

// ViewTarget 没确认切过去时每秒重试一次：切不过去的话，后面所有相机操作都看不见效果
// 引擎输入钩子：PC 只有进游戏后才有，所以进模式后试一次，失败就每秒再试
void PhotoModeMod::ensure_axis_hook() {
    if (axis_hook_tried_ || !port_ || !mode_ || !mode_->active()) return;
    axis_hook_tried_ = true;
    PhotoModeMod* self = this;
    const bool ok = port_->hook_input_axis([self](const std::string& key_name, double amount) {
        std::lock_guard<std::mutex> lock(self->axis_mutex_);
        if (key_name == "MouseX") {
            self->axis_dx_ += amount;
        } else if (key_name == "MouseY") {
            self->axis_dy_ += amount;
        } else if (key_name == "MouseWheelAxis") {
            self->axis_wheel_ += amount;
        }
    });
    if (!ok) {
        // 下一帧再试，直到成功
        axis_hook_tried_ = false;
        axis_hook_retry_timer_ += 1.0;
        if (axis_hook_retry_timer_ > 30.0) axis_hook_tried_ = true;
        return;
    }
    port_->log(LogLevel::Info, "引擎输入钩子已启用（鼠标增量来自引擎，不再依赖系统钩子）");
}

void PhotoModeMod::retry_view_attach(double dt) {
    if (!mode_ || !port_ || !mode_->active()) {
        attach_retry_timer_ = 0.0;
        return;
    }
    if (port_->view_verified()) return;
    attach_retry_timer_ += dt;
    if (attach_retry_timer_ < 0.5) return;
    attach_retry_timer_ = 0.0;
    port_->attach_view();
}

void PhotoModeMod::handle_intent(const Intent& intent, double dt) {
    if (!mode_) return;

    if (intent.toggle_mode) {
        handle_toggle();
        return;
    }
    if (!mode_->active()) {
        return;
    }
    if (intent.toggle_menu) {
        if (overlay_ != nullptr && overlay_->available()) {
            const bool open = !overlay_->menu_open();
            overlay_->set_menu_open(open);
            mode_->set_menu_visible(false);  // 图形菜单顶替文字菜单
            port_->log(LogLevel::Info, open ? "图形菜单：开（T / ESC / 手柄 START 关闭）" : "图形菜单：关");
        } else {
            mode_->toggle_menu();
        }
    }
    // 注意：toggle_freeze 由 PhotoMode::tick 统一处理，这里不能再调一次（会双翻转）
    mode_->tick(intent, dt);
}

// 开菜单沿解析一次 UI 缩放（umg2/WBP 悬停判定共用）：umg2_scale.txt 覆盖 >
// 物理客户区/Slate视口 > 窗口 DPI。UE 的 UI 缩放走分辨率曲线，跟 Windows DPI 是两码事；
// 物理/Slate 才是真值（1.2.2 用 DPI 推算，2K/4K 或缩放曲线不对时命中区整体偏移）。
void PhotoModeMod::resolve_panel_scale() {
    umg2_scale_override_ = read_umg2_scale_override(mod_directory());
    umg2_scale_cached_ = 0.0f;
    const HWND w = find_umg2_window();
    RECT c{};
    const bool have_client = w != nullptr && GetClientRect(w, &c) != 0;
    float slate_w = 0.0f;
    float slate_h = 0.0f;
    const bool have_slate = umg2_scale_override_ <= 0.0f && have_client &&
                            port_->umg2_viewport_size(slate_w, slate_h);
    if (umg2_scale_override_ > 0.0f) {
        umg2_scale_cached_ = umg2_scale_override_;
    } else if (have_slate && slate_w > 1.0f) {
        umg2_scale_cached_ = static_cast<float>(c.right) / slate_w;
    }
    if (have_client) {
        const float s = umg2_scale_cached_ > 0.0f ? umg2_scale_cached_
                                                  : static_cast<float>(umg2_dpi(w)) / 96.0f;
        char buf[260]{};
        std::snprintf(buf, sizeof(buf),
                      "交互菜单命中映射：窗口=%p 客户区=%ldx%ld Slate视口=%.0fx%.0f DPI=%u 缩放=%.3f(%s) "
                      "面板左缘x=%.0f 顶y=%.0f",
                      reinterpret_cast<void*>(w), c.right, c.bottom, slate_w, slate_h, umg2_dpi(w), s,
                      umg2_scale_override_ > 0.0f ? "覆盖" : (have_slate ? "视口换算" : "DPI兜底"),
                      static_cast<float>(c.right) - umg2layout::kPanelFromRight * s,
                      umg2layout::kPanelTop * s);
        port_->log(LogLevel::Info, buf);
    }
}

// 光标 → WBP 面板内坐标（Slate 单位，右上角锚定）；不在面板内返回 false。
// 悬停静音与按钮命中判定共用这套换算（与 umg2 层同款缩放机器）。
bool PhotoModeMod::panel_cursor_local(float& px, float& py) {
    if (HWND game_window = find_umg2_window(); game_window != nullptr) {
        RECT client{};
        if (GetClientRect(game_window, &client)) {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(game_window, &cursor)) {
                float scale = umg2_scale_cached_ > 0.0f ? umg2_scale_cached_
                                                        : static_cast<float>(umg2_dpi(game_window)) / 96.0f;
                scale = std::clamp(scale, 0.25f, 8.0f);
                const float panel_left_px = static_cast<float>(client.right) - wbplayout::kPanelFromRight * scale;
                px = (static_cast<float>(cursor.x) - panel_left_px) / scale;
                py = (static_cast<float>(cursor.y) - wbplayout::kPanelTop * scale) / scale;
                return px >= 0.0f && py >= 0.0f && px <= wbplayout::kPanelWidth && py <= wbplayout::kPanelHeight;
            }
        }
    }
    return false;
}

void PhotoModeMod::update_overlay() {
    if (!mode_ || !port_) return;

    // 优先级 1：DX12 ImGui 浮窗（无帧生成 + imgui_dx12_force.txt 的进阶路径）
    if (overlay_ != nullptr && overlay_->available()) {
        port_->wbp_hide();
        port_->umg2_hide();
        port_->hide_overlay();
        return;
    }

    const bool want_menu = mode_->active() && mode_->menu_visible();

    // 优先级 2：WBP 资产菜单（离线制作的真滑条面板；1.3.0）。
    // 滑条输入 = 每帧读 Value 属性 → WbpBridge（core，可单测）→ 命令；
    // 按钮/页签 = 纯视觉，点击由命中区表判定（wbp_layout.h，1.2.x 机器）；
    // 回显/标签 = SetValue + SetText；页切换 = SetVisibility。
    if (cfg_.menu_asset && want_menu && port_->wbp_available()) {
        if (port_->wbp_show()) {
            port_->hide_overlay();
            port_->umg2_hide();
            if (!wbp_menu_open_prev_) {
                wbp_menu_open_prev_ = true;
                wbp_bridge_.on_opened(mode_->fov(), cfg_.move_speed, cfg_.look_sensitivity);
                resolve_panel_scale();
            }

            // 按钮命中（左键按下沿；面板外时沿丢弃，避免误触发）
            umg2_cursor_in_panel_ = false;
            float px = 0.0f;
            float py = 0.0f;
            if (panel_cursor_local(px, py)) {
                umg2_cursor_in_panel_ = true;  // 悬停静音：相机视角/滚轮/升降让位给菜单
                const bool lmb = input_.left_down();
                const bool click = lmb && !lmb_prev_;
                lmb_prev_ = lmb;
                if (click) {
                    for (const auto& hit : wbplayout::kHits) {
                        if (px < hit.x || px > hit.x + hit.w || py < hit.y || py > hit.y + hit.h) continue;
                        if (strcmp(hit.id, "tab_camera") == 0) {
                            port_->wbp_set_page(wbplayout::kPageCamera);
                        } else if (strcmp(hit.id, "tab_keys") == 0) {
                            port_->wbp_set_page(wbplayout::kPageKeys);
                        } else if (strcmp(hit.id, "freeze") == 0) {
                            mode_->toggle_freeze();
                        } else if (strcmp(hit.id, "reset") == 0) {
                            menu_reset_view_ = true;
                        } else if (strcmp(hit.id, "exit") == 0) {
                            mode_->leave("menu");
                            input_.reset();
                            overlay_ok_ = false;
                        }
                    }
                }
            } else {
                lmb_prev_ = input_.left_down();
            }

            // 滑条 → 桥 → 命令 + 回显
            WbpVars vars;
            port_->wbp_read_vars(vars);
            WbpWriteBack back;
            const WbpCommands commands =
                wbp_bridge_.step(vars, mode_->fov(), cfg_.move_speed, cfg_.look_sensitivity, mode_->frozen(),
                                 cfg_.fov_min, cfg_.fov_max, 0.01, 0.50, back);
            port_->wbp_apply(back);
            apply_wbp_commands(commands);

            // 状态行（变化才 SetText）
            const MenuModel model = build_menu_model(*mode_, keys_);
            port_->wbp_set_status(model.status);

            // Esc 关闭菜单
            const bool esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (esc_down && !esc_prev_) mode_->set_menu_visible(false);
            esc_prev_ = esc_down;
            return;
        }
        // wbp_show 失败但还没标记不可用（世界未就绪/重建节流中）：本帧先不显示任何菜单，
        // 下一帧重试；一旦标记不可用就落到下面的 UMG2/文字层
        if (port_->wbp_available()) return;
    }
    wbp_menu_open_prev_ = false;
    wbp_bridge_.reset();

    // 优先级 3：UMG 交互菜单（游戏原生控件；帧生成环境下零冲突。构建失败自动落回文字版）
    if (want_menu && port_->umg2_available()) {
        // 鼠标命中判定（不走 Slate 路由）：窗口客户区 → 面板内坐标 → 命中区表。
        // 坐标系必须与 ue4ss_port 的摆放一致（右上角锚定）；Slate 单位按缩放换算成物理像素。
        // 1.2.1 的教训：摆放锚在左上、命中按右上算，点击永远落空，左键漏进相机移动。
        Umg2Clicks clicks{};
        umg2_cursor_in_panel_ = false;
        if (!umg2_menu_open_prev_) {
            umg2_menu_open_prev_ = true;
            resolve_panel_scale();
        }
        if (HWND game_window = find_umg2_window(); game_window != nullptr) {
            RECT client{};
            if (GetClientRect(game_window, &client)) {
                POINT cursor{};
                if (GetCursorPos(&cursor) && ScreenToClient(game_window, &cursor)) {
                    float scale = umg2_scale_cached_ > 0.0f ? umg2_scale_cached_
                                                            : static_cast<float>(umg2_dpi(game_window)) / 96.0f;
                    scale = std::clamp(scale, 0.25f, 8.0f);
                    const float panel_left_px = static_cast<float>(client.right) - umg2layout::kPanelFromRight * scale;
                    const float px = (static_cast<float>(cursor.x) - panel_left_px) / scale;
                    const float py = (static_cast<float>(cursor.y) - umg2layout::kPanelTop * scale) / scale;
                    umg2_cursor_in_panel_ = px >= 0.0f && py >= 0.0f &&
                                            px <= umg2layout::kPanelWidth && py <= umg2layout::kPanelHeight;
                    const bool lmb = input_.left_down();
                    const bool click = lmb && !lmb_prev_;
                    lmb_prev_ = lmb;
                    if (click) {
                        for (const auto& item : umg2layout::kHits) {
                            if (px < item.x || px > item.x + item.w || py < item.y || py > item.y + item.h) continue;
                            if (strcmp(item.id, "fov_dec") == 0) clicks.fov_dec = true;
                            else if (strcmp(item.id, "fov_inc") == 0) clicks.fov_inc = true;
                            else if (strcmp(item.id, "speed_dec") == 0) clicks.speed_dec = true;
                            else if (strcmp(item.id, "speed_inc") == 0) clicks.speed_inc = true;
                            else if (strcmp(item.id, "sens_dec") == 0) clicks.sens_dec = true;
                            else if (strcmp(item.id, "sens_inc") == 0) clicks.sens_inc = true;
                            else if (strcmp(item.id, "freeze") == 0) clicks.freeze = true;
                            else if (strcmp(item.id, "reset") == 0) clicks.reset = true;
                            else if (strcmp(item.id, "exit") == 0) clicks.exit = true;
                        }
                        // 点在面板里却没命中任何按钮：把面板内坐标打出来（每秒一条），对着映射日志查偏差
                        if (!(clicks.fov_dec || clicks.fov_inc || clicks.speed_dec || clicks.speed_inc ||
                              clicks.sens_dec || clicks.sens_inc || clicks.freeze || clicks.reset || clicks.exit)) {
                            const double now = monotonic_seconds();
                            if (now - umg2_last_miss_log_ > 1.0) {
                                umg2_last_miss_log_ = now;
                                char buf[160]{};
                                std::snprintf(buf, sizeof(buf), "交互菜单点击未命中：面板内(%.0f,%.0f) 缩放=%.3f", px, py, scale);
                                port_->log(LogLevel::Info, buf);
                            }
                        }
                    }
                }
            }
        }

        Umg2Commands commands;
        const MenuModel model = build_menu_model(*mode_, keys_);
        if (port_->umg2_tick(model.status, mode_->fov(), cfg_.move_speed, cfg_.look_sensitivity, clicks, commands)) {
            port_->hide_overlay();
            apply_umg2_commands(commands);

            // Esc 关闭菜单
            const bool esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (esc_down && !esc_prev_) mode_->set_menu_visible(false);
            esc_prev_ = esc_down;
            return;
        }
    }
    umg2_cursor_in_panel_ = false;
    umg2_menu_open_prev_ = false;

    if (!want_menu) {
        port_->wbp_hide();
        port_->umg2_hide();
        port_->hide_overlay();
        return;
    }

    // 兜底：文字菜单（交互菜单构建失败时）
    const MenuModel model = build_menu_model(*mode_, keys_);
    const bool ok = port_->show_overlay(render_menu_text_ascii(model));
    if (ok != overlay_ok_) {
        overlay_ok_ = ok;
        if (!ok) {
            port_->log(LogLevel::Warn, "游戏内菜单不可用（UMG 控件构建失败，原因见上一条日志）");
        } else {
            port_->log(LogLevel::Info, "游戏内菜单已显示（UMG 控件）");
        }
    }
}

// 应用 WBP 资产菜单攒下的动作（bridge 每帧轮询产生；绝对值语义，与 umg2 的步进语义不同）
void PhotoModeMod::apply_wbp_commands(const WbpCommands& commands) {
    if (commands.fov_changed) mode_->set_fov(commands.fov);  // set_fov 自带 [fov_min, fov_max] 夹紧
    if (commands.speed_changed) {
        cfg_.move_speed = std::clamp(commands.speed, 0.0, static_cast<double>(kWbpSpeedMax));
        mode_->set_config(cfg_);
    }
    if (commands.sens_changed) {
        cfg_.look_sensitivity = std::clamp(commands.sens, 0.01, 0.50);
        mode_->set_config(cfg_);
    }
    if (commands.toggle_freeze) mode_->toggle_freeze();
    if (commands.reset_view) menu_reset_view_ = true;
    if (commands.exit_mode) {
        mode_->leave("menu");
        input_.reset();
        overlay_ok_ = false;
    }
}

// pak 自动安装（1.3.0）：模组管理器只把 zip 解到 ue4ss\Mods\PBZPhotoModeCpp\，
// 而 WBP 资产 pak 必须在 <游戏>\ProjectPBZ\Content\Paks\~mods\ 才会被引擎挂载
// （本机 ~mods 已有 4 个成功加载的先例）。启动时把 paks\ 三件套拷过去；失败只 Warn 不阻断。
void PhotoModeMod::ensure_pak_installed() {
    if (pak_copy_done_) return;
    pak_copy_done_ = true;
    namespace fs = std::filesystem;
    const fs::path src_dir = fs::path(mod_directory()) / "paks";
    if (!fs::exists(src_dir)) {
        port_->log(LogLevel::Info, "pak 自动安装：模组目录没有 paks\\，跳过（资产菜单不可用时自动用文字面板）");
        return;
    }
    // 模组目录在 <游戏>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp，上溯 4 级到游戏根
    fs::path game_root = fs::path(mod_directory());
    for (int i = 0; i < 4; ++i) game_root = game_root.parent_path();
    const fs::path mods_dir = game_root / "ProjectPBZ" / "Content" / "Paks" / "~mods";
    if (!fs::exists(mods_dir)) {
        port_->log(LogLevel::Warn, "pak 自动安装：找不到 " + mods_dir.string() +
                                       "；请手动把模组 paks\\ 里的三个文件拷进游戏 ProjectPBZ\\Content\\Paks\\~mods\\");
        return;
    }
    int copied = 0;
    int skipped = 0;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(src_dir)) {
        const fs::path dst = mods_dir / entry.path().filename();
        if (fs::exists(dst)) {
            const auto src_time = fs::last_write_time(entry.path(), ec);
            const auto dst_time = fs::last_write_time(dst, ec);
            const auto src_size = fs::file_size(entry.path(), ec);
            const auto dst_size = fs::file_size(dst, ec);
            if (!ec && src_size == dst_size && src_time == dst_time) {
                ++skipped;  // 内容没变（拷贝会保留源 mtime），不动它
                continue;
            }
        }
        ec.clear();
        fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            port_->log(LogLevel::Warn, "pak 自动安装失败：" + entry.path().filename().string() + " → " + ec.message());
        } else {
            ++copied;
        }
    }
    port_->log(LogLevel::Info, "pak 自动安装：新拷 " + std::to_string(copied) + " 个，未变跳过 " + std::to_string(skipped) +
                                   " 个（目标 " + mods_dir.string() + "）");
}

// 应用 UMG 交互菜单攒下的动作（每帧轮询产生）
void PhotoModeMod::apply_umg2_commands(const Umg2Commands& commands) {
    if (commands.fov_step != 0) mode_->set_fov(mode_->fov() + commands.fov_step * cfg_.fov_step);
    if (commands.speed_delta != 0.0) {
        cfg_.move_speed = std::clamp(cfg_.move_speed + commands.speed_delta, 50.0, 2000.0);
        mode_->set_config(cfg_);
    }
    if (commands.sens_delta != 0.0) {
        cfg_.look_sensitivity = std::clamp(cfg_.look_sensitivity + commands.sens_delta, 0.01, 1.0);
        mode_->set_config(cfg_);
    }
    if (commands.toggle_freeze) mode_->toggle_freeze();
    if (commands.reset_view) menu_reset_view_ = true;
    if (commands.exit_mode) {
        mode_->leave("menu");
        input_.reset();
        overlay_ok_ = false;
    }
}

// 图形菜单快照：引擎线程每帧发布一次（渲染线程只读快照，绝不直接碰 PhotoMode/Config）
void PhotoModeMod::publish_menu_snapshot() {
    const MenuModel model = build_menu_model(*mode_, keys_);
    MenuView view;
    view.active = mode_->active();
    view.menu_open = overlay_->menu_open();
    view.status = model.status;
    view.status_frozen = model.status_frozen;
    view.fov = mode_->fov();
    view.fov_min = cfg_.fov_min;
    view.fov_max = cfg_.fov_max;
    view.move_speed = cfg_.move_speed;
    view.look_sensitivity = cfg_.look_sensitivity;
    view.pad_look_speed = cfg_.pad_look_speed;
    view.pad_deadzone = cfg_.pad_deadzone;
    view.pad_enabled = cfg_.pad_enabled;
    view.hide_player = cfg_.hide_player;
    view.block_game_input = cfg_.block_game_input;
    view.hide_game_hud = cfg_.hide_game_hud;
    view.auto_pause_time = cfg_.auto_pause_time;
    view.pos[0] = mode_->pose().loc.x;
    view.pos[1] = mode_->pose().loc.y;
    view.pos[2] = mode_->pose().loc.z;
    view.rot[0] = mode_->pose().rot.pitch;
    view.rot[1] = mode_->pose().rot.yaw;
    view.hints = model.hints;
    view.hints_ascii = model.hints_ascii;
    overlay_->publish(view, input_.gamepad());
}

// 应用图形菜单攒下的操作（渲染线程上一帧写入的）
void PhotoModeMod::apply_menu_actions() {
    MenuActions actions;
    overlay_->drain(actions);

    if (actions.close_menu && overlay_->menu_open()) {
        overlay_->set_menu_open(false);
        port_->log(LogLevel::Info, "图形菜单：关");
    }
    if (actions.exit_mode) {
        overlay_->set_menu_open(false);
        mode_->leave("menu");
        input_.reset();
        overlay_ok_ = false;
        return;
    }
    if (actions.fov_changed) mode_->set_fov(actions.fov);
    if (actions.speed_changed) {
        cfg_.move_speed = actions.speed;
        mode_->set_config(cfg_);
    }
    if (actions.sens_changed) {
        cfg_.look_sensitivity = actions.sens;
        mode_->set_config(cfg_);
    }
    if (actions.pad_speed_changed) {
        cfg_.pad_look_speed = actions.pad_speed;
        mode_->set_config(cfg_);
    }
    if (actions.pad_deadzone_changed) {
        cfg_.pad_deadzone = actions.pad_deadzone;
        mode_->set_config(cfg_);
    }
    if (actions.flag_pad_enabled) {
        cfg_.pad_enabled = actions.v_pad_enabled;
        mode_->set_config(cfg_);
    }
    if (actions.flag_auto_pause) {
        cfg_.auto_pause_time = actions.v_auto_pause;
        mode_->set_config(cfg_);
    }
    if (actions.flag_hide_player) mode_->set_hide_player_live(actions.v_hide_player);
    if (actions.flag_block_input) mode_->set_block_game_input_live(actions.v_block_input);
    if (actions.flag_hide_hud) mode_->set_hide_game_hud_live(actions.v_hide_hud);
    if (actions.toggle_freeze) mode_->toggle_freeze();
    if (actions.reset_view) menu_reset_view_ = true;
}

// 每秒打一行输入诊断：真机出问题时，这一行就能判断是“输入没到”还是“到了但没生效”
void PhotoModeMod::log_input_diagnostics(const Intent& intent, double dt) {
    if (!mode_ || !mode_->active() || !port_ || input_source_ == nullptr) return;
    diag_accum_ += dt;
    if (diag_accum_ < 1.0) return;
    diag_accum_ = 0.0;

    std::string graphic_state;
    if (overlay_ != nullptr && overlay_->available()) {
        graphic_state = overlay_->menu_open() ? "DX12开" : "DX12关";
    } else if (port_->wbp_available()) {
        graphic_state = port_->wbp_state();  // 未创建/资产/隐藏/不可用
    } else {
        graphic_state = port_->umg2_state();
    }
    char buf[420]{};
    std::snprintf(buf, sizeof(buf),
                  "输入诊断 alt=%d dx=%.1f dy=%.1f 引擎增量峰值=(%.2f,%.2f) 滚轮钩子=%d 滚轮事件=%lu raw=%d/%lu "
                  "菜单=%s 图形=%s 手柄槽位=%d 手柄=%s",
                  input_.alt_down() ? 1 : 0, intent.look_dx, intent.look_dy, engine_peak_dx_, engine_peak_dy_,
                  input_source_->ll_ready() ? 1 : 0, input_source_->ll_wheel_events(),
                  input_source_->raw_ready() ? 1 : 0, input_source_->mouse_event_count(),
                  port_->umg_menu_state().c_str(), graphic_state.c_str(), input_source_->pad_slot(),
                  input_source_->pad_backend().c_str());
    engine_peak_dx_ = 0.0;
    engine_peak_dy_ = 0.0;
    port_->log(LogLevel::Info, std::string(buf) + " | " + port_->camera_state_summary());
}

void PhotoModeMod::on_engine_tick(double delta_seconds) {
    if (!mode_ || !port_) return;

    double dt = monotonic_seconds() - last_tick_time_;
    last_tick_time_ = monotonic_seconds();
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;  // 卡帧保护，避免相机瞬移
    (void)delta_seconds;

    if (input_source_ != nullptr) {
        input_source_->poll(input_, cfg_.pad_enabled);
        // DInput 后端的一次性诊断（接入成功/初始化失败时各产出一条）
        std::string pad_diag;
        if (input_source_->take_pad_dinput_diag(pad_diag)) {
            port_->log(LogLevel::Info, pad_diag);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 手柄插拔日志：只在状态变化时打一行，方便确认手柄模式是否可用
    if (input_source_ != nullptr && cfg_.pad_enabled) {
        const bool connected = input_source_->pad_connected();
        if (connected != pad_logged_connected_) {
            pad_logged_connected_ = connected;
            if (connected) {
                port_->log(LogLevel::Info, std::string("手柄已连接（") + input_source_->pad_backend() +
                                               "）：手柄 " + pad_combo_name(keys_.pad_toggle) + " 进入摄影模式");
            } else {
                port_->log(LogLevel::Info, "手柄已断开");
            }
        } else if (!pad_scan_logged_) {
            // 启动 10 秒后报一次扫描状态：出现这行说明 XInput 轮询管线活着；
            // 没有这行 = 引擎 Tick 都没跑（那是另一个层面的问题）
            pad_scan_log_timer_ += dt;
            if (pad_scan_log_timer_ >= 10.0) {
                pad_scan_logged_ = true;
                port_->log(LogLevel::Info, input_source_->pad_connected()
                    ? std::string("手柄扫描正常：已连接（") + input_source_->pad_backend() + "）"
                    : "手柄扫描正常：暂未发现手柄，持续扫描中（手柄指示灯亮起后即插即用）");
            }
        }
    }

    // 首选引擎自己的鼠标增量（PlayerController::GetInputMouseDelta）：
    // 它就是游戏用来转视角的那份数据，不依赖 OS 钩子，前台/窗口化/光标捕获都不影响。
    //
    // 重要：**没进摄影模式时绝不碰引擎**。上一版（1.0.4）在这里无条件调用，
    // 结果游戏刚启动、UWorld 都还没有的时候就去 FindAllOf(PlayerController)，
    // 直接崩在 UE4SS.dll 里（crash_2026_10_30_09_49_58 那次）。
    if (mode_ != nullptr && mode_->active()) {
        if (port_->read_mouse_delta(engine_dx_, engine_dy_)) {
            engine_delta_ok_ = true;
        }
    }

    // 引擎侧钩子的增量先并进来（同一帧内多条轴事件会被累加）
    {
        std::lock_guard<std::mutex> axis_lock(axis_mutex_);
        if (axis_dx_ != 0.0 || axis_dy_ != 0.0) {
            // InputAxis 的 MouseY 与引擎 GetInputMouseDelta 一样：正值 = 鼠标向上；
            // Intent.look_dy 约定正值 = 鼠标向下（与 Win32 原始坐标一致），这里翻转 Y。
            input_.on_mouse_move(axis_dx_, -axis_dy_);
            axis_move_active_ = true;
            axis_dx_ = 0.0;
            axis_dy_ = 0.0;
        }
        if (axis_wheel_ != 0.0) {
            input_.on_wheel(axis_wheel_);
            axis_wheel_active_ = true;
            axis_wheel_ = 0.0;
        }
    }
    if (engine_delta_ok_ && (engine_dx_ != 0.0 || engine_dy_ != 0.0)) {
        // 引擎给的增量很小（真机日志里是 0.1~0.2 量级，等于「像素÷游戏鼠标灵敏度」），
        // 直接喂给 core 几乎看不见转动，这里乘一个标定系数放大到「像素」量级。
        const double scaled_x = engine_dx_ * kEngineMouseScale;
        // UE 的 MouseY 正值 = 鼠标向上；core 的 look_dy 正值 = 向下（见 InputLogic 约定）。
        const double scaled_y = -engine_dy_ * kEngineMouseScale;
        input_.on_mouse_move(scaled_x, scaled_y);
        engine_move_seen_ = true;
        // 用 if 比较而不是 std::max：Windows.h 的 max 宏会在这里打架
        const double abs_dx = engine_dx_ < 0.0 ? -engine_dx_ : engine_dx_;
        const double abs_dy = engine_dy_ < 0.0 ? -engine_dy_ : engine_dy_;
        if (abs_dx > engine_peak_dx_) engine_peak_dx_ = abs_dx;
        if (abs_dy > engine_peak_dy_) engine_peak_dy_ = abs_dy;
        if (!engine_delta_logged_) {
            engine_delta_logged_ = true;
            char buf[160]{};
            std::snprintf(buf, sizeof(buf), "引擎鼠标增量生效：dx=%.2f dy=%.2f（已放大 %.0f 倍）", engine_dx_,
                          engine_dy_, kEngineMouseScale);
            port_->log(LogLevel::Info, buf);
        }
    }
    if (input_source_ != nullptr) input_source_->set_mouse_muted(axis_move_active_ || engine_delta_ok_);

    const Intent intent = input_.build(cfg_, keys_);
    Intent effective = intent;
    if (menu_reset_view_) {
        effective.reset_view = true;
        menu_reset_view_ = false;
    }
    if (overlay_ != nullptr && overlay_->available() && overlay_->menu_open()) {
        // 落在菜单上的滚轮转给 UI；UI 捕获鼠标（拖滑条等）时静音视角输入
        if (effective.wheel != 0.0 && overlay_->cursor_in_menu()) {
            overlay_->feed_wheel(effective.wheel);
            effective.wheel = 0.0;
        }
        if (overlay_->ui_captures_mouse()) {
            effective.look_dx = 0.0;
            effective.look_dy = 0.0;
            effective.pitch_up_click = false;
            effective.pitch_down_click = false;
        }
    }
    if (umg2_cursor_in_panel_) {
        // 指针在交互菜单面板上：相机输入让位给菜单（上一帧判定，滞后一帧无感知）。
        // move_up 必须一起静音：translate 模式左右键直接叠加进 move_up，
        // 不静音的话点击面板会推高相机（1.2.1 实机症状）。
        effective.look_dx = 0.0;
        effective.look_dy = 0.0;
        effective.pitch_up_click = false;
        effective.pitch_down_click = false;
        effective.wheel = 0.0;
        effective.move_up = 0.0;
    }
    handle_intent(effective, dt);
    retry_view_attach(dt);
    ensure_axis_hook();
    log_input_diagnostics(intent, dt);
    input_.end_frame();
    update_overlay();
    if (overlay_ != nullptr && overlay_->available()) {
        publish_menu_snapshot();
        apply_menu_actions();
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
