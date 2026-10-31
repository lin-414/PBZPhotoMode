#ifdef PBZPM_WITH_UE4SS

#include "engine/imgui_overlay.h"

#include <Windows.h>

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <chrono>
#include <cfloat>
#include <exception>
#include <stdexcept>
#include <cmath>
#include <cstring>

#include <imgui.h>
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include "game_menu.h"

namespace pbzpm {
namespace {

// COM vtable 槽位（x64）：IDXGISwapChain::Present=8；ID3D12CommandQueue::ExecuteCommandLists=10
constexpr int kPresentSlot = 8;
constexpr int kExecuteCommandListsSlot = 10;
// 后端初始化连续失败超过这个帧数判定不可用（60fps 下约 4 秒）
constexpr int kMaxInitFailStreak = 240;

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ExecuteCommandListsFn = void(WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

// D3D 入口点全部运行时加载：不让 d3d11/d3d12/dxgi 出现在 DLL 导入表里。
// 1.1.0 的教训：模组加载阶段就创建 dummy 设备 + 补丁 vtable，会跟游戏 RHI 初始化撞车（启动闪退）。
template <typename Fn>
Fn load_proc(HMODULE module, const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}
using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                               const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**,
                                               D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PFN_D3D12CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using PFN_CreateDXGIFactory = HRESULT(WINAPI*)(REFIID, void**);

// 钩子原函数与被改的 vtable 槽位（卸钩时还原）。同一 TU 内由 install/stop 成员函数维护。
PresentFn g_orig_present_1 = nullptr;
PresentFn g_orig_present_2 = nullptr;
ExecuteCommandListsFn g_orig_ecl = nullptr;
void** g_vtable_present_1 = nullptr;
void** g_vtable_present_2 = nullptr;
void** g_vtable_ecl = nullptr;
bool g_present_hook_shared = false;  // DX11/DX12 的 Present 是同一个地址时只补一处

HRESULT WINAPI present_hook_1(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
    ImGuiOverlay& overlay = ImGuiOverlay::instance();
    if (!overlay.shutting_down()) overlay.render_if_needed(swapchain);
    return g_orig_present_1(swapchain, sync_interval, flags);
}

HRESULT WINAPI present_hook_2(IDXGISwapChain* swapchain, UINT sync_interval, UINT flags) {
    ImGuiOverlay& overlay = ImGuiOverlay::instance();
    if (!overlay.shutting_down()) overlay.render_if_needed(swapchain);
    return g_orig_present_2(swapchain, sync_interval, flags);
}

void WINAPI execute_command_lists_hook(ID3D12CommandQueue* queue, UINT count,
                                       ID3D12CommandList* const* lists) {
    ImGuiOverlay& overlay = ImGuiOverlay::instance();
    overlay.on_execute_command_lists(queue, count, reinterpret_cast<void* const*>(lists));
    g_orig_ecl(queue, count, lists);
}

// 游戏主窗口的子类化过程：只处理我们的自定义消息（光标常显），其余全部透传。
// ShowCursor 的显示计数按线程算，渲染线程直接调管不到游戏窗口，所以借窗口线程之手。
LRESULT CALLBACK overlay_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    ImGuiOverlay& overlay = ImGuiOverlay::instance();
    if (message == WM_DESTROY) {
        const auto original = static_cast<WNDPROC>(overlay.original_wndproc());
        return CallWindowProcW(original != nullptr ? original : DefWindowProcW, window, message, wparam, lparam);
    }
    if (overlay.is_cursor_message(message)) {
        overlay.on_cursor_show_message();
        return 0;
    }
    const auto original = static_cast<WNDPROC>(overlay.original_wndproc());
    return original != nullptr ? CallWindowProcW(original, window, message, wparam, lparam)
                               : DefWindowProcW(window, message, wparam, lparam);
}

bool patch_vtable_slot(void** vtable, int slot, void* detour, void** original) {
    void** entry = &vtable[slot];
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old_protect)) return false;
    *original = *entry;
    *entry = detour;
    VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);
    return true;
}

void unpatch_vtable_slot(void** vtable, int slot, void* original) {
    if (vtable == nullptr || original == nullptr) return;
    void** entry = &vtable[slot];
    DWORD old_protect = 0;
    if (VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        *entry = original;
        VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);
    }
}

// 交换链缓冲格式是 typeless 的（flip 模型），RTV 与着色管线要用对应的 typed 格式
DXGI_FORMAT typed_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return format;
    }
}

}  // namespace

// 渲染帧防重入：开帧生成（FSR3/DLSS FG）后 Present 可能来自帧生成线程，
// ImGui 上下文不是线程安全的，同一时刻只允许一个线程画。
struct FrameGuard {
    std::atomic<bool>& flag_;
    explicit FrameGuard(std::atomic<bool>& flag) : flag_(flag) {}
    ~FrameGuard() { flag_.store(false, std::memory_order_release); }
};

ImGuiOverlay& ImGuiOverlay::instance() {
    static ImGuiOverlay overlay;
    return overlay;
}

void ImGuiOverlay::log(LogLevel level, const std::string& message) {
    if (log_) log_(level, message);
}

void ImGuiOverlay::fail(const std::string& reason) {
    if (failed_.exchange(true)) return;
    log(LogLevel::Error, "图形菜单不可用（" + reason + "），已退回 UMG 文字菜单");
    should_render_.store(false);
}

void ImGuiOverlay::refresh_should_render() {
    should_render_.store(active_.load(std::memory_order_relaxed) && menu_open_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
}

bool ImGuiOverlay::start(LogFn log_fn) {
    try {
        return start_impl(std::move(log_fn));
    } catch (const std::exception& error) {
        log(LogLevel::Error, std::string("图形菜单：初始化异常 ") + error.what());
    } catch (...) {
        log(LogLevel::Error, "图形菜单：初始化异常（未知）");
    }
    return false;
}

bool ImGuiOverlay::start_impl(LogFn log_fn) {
    if (started_.load(std::memory_order_relaxed)) return available();
    log_ = std::move(log_fn);

    // dummy 窗口：创建 dummy swapchain 必须要 HWND（钩完立刻没用处，但要活到 stop）
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"PBZPM_ImGuiDummy";
    RegisterClassExW(&window_class);  // 重复注册失败无妨
    HWND dummy = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                 0, 0, 8, 8, nullptr, nullptr, window_class.hInstance, nullptr);
    if (dummy == nullptr) {
        log(LogLevel::Error, "图形菜单：dummy 窗口创建失败");
        return false;
    }
    hwnd_dummy_ = dummy;
    cursor_message_ = RegisterWindowMessageW(L"PBZPM_OverlayShowCursor");
    log(LogLevel::Info, "图形菜单：[1/5] dummy 窗口 OK");

    HMODULE d3d11_module = LoadLibraryW(L"d3d11.dll");
    HMODULE d3d12_module = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgi_module = LoadLibraryW(L"dxgi.dll");
    if (d3d11_module == nullptr && d3d12_module == nullptr) {
        log(LogLevel::Error, "图形菜单：d3d11.dll / d3d12.dll 都加载失败");
        return false;
    }

    // 造 dummy D3D11/D3D12 设备拿 vtable。两个都失败才放弃。
    bool dx11 = false;
    bool dx12 = false;
    // DX11：设备 + 交换链 → Present
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGIDevice* dxgi_device = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        IDXGISwapChain* swapchain = nullptr;
        const auto create_device11 = load_proc<PFN_D3D11CreateDevice>(d3d11_module, "D3D11CreateDevice");
        if (create_device11 != nullptr &&
            SUCCEEDED(create_device11(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                      D3D11_SDK_VERSION, &device, nullptr, &context)) && device != nullptr) {
            if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) && dxgi_device != nullptr &&
                SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && adapter != nullptr &&
                SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) && factory != nullptr) {
                DXGI_SWAP_CHAIN_DESC desc{};
                desc.BufferDesc.Width = 8;
                desc.BufferDesc.Height = 8;
                desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.OutputWindow = dummy;
                desc.Windowed = TRUE;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                if (SUCCEEDED(factory->CreateSwapChain(device, &desc, &swapchain)) && swapchain != nullptr) {
                    void** vtable = *reinterpret_cast<void***>(swapchain);
                    void* target = vtable[kPresentSlot];
                    if (target != nullptr) {
                        g_vtable_present_1 = vtable;
                        if (patch_vtable_slot(vtable, kPresentSlot, &present_hook_1,
                                              reinterpret_cast<void**>(&g_orig_present_1))) {
                            dx11 = true;
                            log(LogLevel::Info, "图形菜单：[2/5] DX11 Present 补丁 OK");
                        }
                    }
                }
            }
        }
        if (swapchain != nullptr) swapchain->Release();
        if (factory != nullptr) factory->Release();
        if (adapter != nullptr) adapter->Release();
        if (dxgi_device != nullptr) dxgi_device->Release();
        if (context != nullptr) context->Release();
        if (device != nullptr) device->Release();
    }

    // DX12：设备 + 命令队列 + 交换链 → ExecuteCommandLists + Present
    {
        ID3D12Device* device = nullptr;
        const auto create_device12 = load_proc<PFN_D3D12CreateDevice>(d3d12_module, "D3D12CreateDevice");
        if (create_device12 != nullptr &&
            SUCCEEDED(create_device12(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) && device != nullptr) {
            D3D12_COMMAND_QUEUE_DESC queue_desc{};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ID3D12CommandQueue* queue = nullptr;
            IDXGIFactory* factory = nullptr;
            IDXGISwapChain* swapchain = nullptr;
            const auto create_factory = load_proc<PFN_CreateDXGIFactory>(dxgi_module, "CreateDXGIFactory");
            if (create_factory != nullptr &&
                SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) && queue != nullptr &&
                SUCCEEDED(create_factory(IID_PPV_ARGS(&factory))) && factory != nullptr) {
                DXGI_SWAP_CHAIN_DESC desc{};
                desc.BufferDesc.Width = 8;
                desc.BufferDesc.Height = 8;
                desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.OutputWindow = dummy;
                desc.Windowed = TRUE;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                if (SUCCEEDED(factory->CreateSwapChain(queue, &desc, &swapchain)) && swapchain != nullptr) {
                    void** queue_vtable = *reinterpret_cast<void***>(queue);
                    void* ecl_target = queue_vtable[kExecuteCommandListsSlot];
                    if (ecl_target != nullptr) {
                        g_vtable_ecl = queue_vtable;
                        if (patch_vtable_slot(queue_vtable, kExecuteCommandListsSlot, &execute_command_lists_hook,
                                              reinterpret_cast<void**>(&g_orig_ecl))) {
                            dx12 = true;
                            log(LogLevel::Info, "图形菜单：[3/5] DX12 ExecuteCommandLists 补丁 OK");
                        }
                    }
                    // Present：DX12 交换链与 DX11 交换链可能是同一个 dxgi 实现（vtable 地址相同）。
                    // 相同就只挂一处（present_hook_1 对两种交换链都工作）；不同再补一处。
                    void** swapchain_vtable = *reinterpret_cast<void***>(swapchain);
                    void* present_target = swapchain_vtable[kPresentSlot];
                    void* already_patched = g_vtable_present_1 != nullptr ? g_vtable_present_1[kPresentSlot] : nullptr;
                    if (present_target != nullptr && present_target != already_patched) {
                        g_vtable_present_2 = swapchain_vtable;
                        if (patch_vtable_slot(swapchain_vtable, kPresentSlot, &present_hook_2,
                                              reinterpret_cast<void**>(&g_orig_present_2))) {
                            // ok
                        } else {
                            g_vtable_present_2 = nullptr;
                        }
                    } else {
                        g_present_hook_shared = true;
                        log(LogLevel::Info, "图形菜单：[4/5] DX12 Present 与 DX11 共享同一补丁");
                    }
                }
            }
            if (swapchain != nullptr) swapchain->Release();
            if (factory != nullptr) factory->Release();
            if (queue != nullptr) queue->Release();
            device->Release();
        }
    }

    if (!dx11 && !dx12) {
        log(LogLevel::Error, "图形菜单：D3D11/D3D12 dummy 设备都创建失败");
        return false;
    }

    // ImGui 上下文与字体图集在这里（游戏线程、F2 时刻）就建好，
    // 首次 Present 的渲染线程只做轻量的后端初始化，避免卡住游戏渲染。
    init_imgui();

    started_.store(true, std::memory_order_relaxed);
    std::string apis = dx11 ? "DX11" : "";
    if (dx12) apis += apis.empty() ? "DX12" : "+DX12";
    if (!dx12 || g_vtable_present_2 == nullptr) log(LogLevel::Info, "图形菜单：[4/5] DX12 Present 补丁跳过（共享或不适用）");
    log(LogLevel::Info, "图形菜单：[5/5] 钩子已安装（" + apis + "，运行时自动探测游戏后端）");
    return true;
}

void ImGuiOverlay::stop() {
    if (!started_.exchange(false, std::memory_order_relaxed)) return;
    shutting_down_.store(true, std::memory_order_release);
    // 等当前帧画完（最多约 3 秒），避免拆钩子时渲染线程还在跑我们免的代码
    for (int i = 0; i < 3000 && in_frame_.load(std::memory_order_relaxed); ++i) Sleep(1);

    // 还原 vtable
    if (!g_present_hook_shared) unpatch_vtable_slot(g_vtable_present_2, kPresentSlot, g_orig_present_2);
    unpatch_vtable_slot(g_vtable_present_1, kPresentSlot, g_orig_present_1);
    unpatch_vtable_slot(g_vtable_ecl, kExecuteCommandListsSlot, g_orig_ecl);
    g_orig_present_1 = nullptr;
    g_orig_present_2 = nullptr;
    g_orig_ecl = nullptr;

    if (wndproc_subclassed_ && game_hwnd_ != nullptr && orig_wndproc_ != nullptr) {
        SetWindowLongPtrW(static_cast<HWND>(game_hwnd_), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig_wndproc_));
        wndproc_subclassed_ = false;
    }

    shutdown_imgui();

    // 释放 DX12 资源（渲染线程已停）
    auto release = [](void*& object) {
        if (object != nullptr) {
            static_cast<IUnknown*>(object)->Release();
            object = nullptr;
        }
    };
    release(dxgi_swapchain3_);
    release(d3d12_cmd_list_);
    for (int i = 0; i < 3; ++i) release(d3d12_cmd_allocators_[i]);
    release(d3d12_ui_queue_);
    release(d3d12_rtv_heap_);
    release(d3d12_srv_heap_);
    release(d3d12_queue_);
    release(d3d12_device_);
    if (hwnd_dummy_ != nullptr) {
        DestroyWindow(static_cast<HWND>(hwnd_dummy_));
        hwnd_dummy_ = nullptr;
    }
}

void ImGuiOverlay::shutdown_imgui() {
    if (imgui_ctx_ == nullptr) return;
    if (backend_api_ == 11) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
    } else if (backend_api_ == 12) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
    }
    backend_api_ = 0;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imgui_ctx_));
    ImGui::DestroyContext(static_cast<ImGuiContext*>(imgui_ctx_));
    imgui_ctx_ = nullptr;
}

// ---- 引擎线程 ----

void ImGuiOverlay::publish(const MenuView& view, const PadState& pad) {
    {
        std::lock_guard<std::mutex> lock(view_mutex_);
        view_ = view;
        pad_ = pad;
    }
    active_.store(view.active, std::memory_order_relaxed);
    refresh_should_render();

    // 打开菜单 3 秒仍没有任何 Present 回调 → 钩子没钩中游戏的交换链类型，
    // 按不可用处理，上层自动退回 UMG 文字菜单（否则用户按 T 毫无反应）。
    if (!failed_.load(std::memory_order_relaxed) && menu_open_.load(std::memory_order_relaxed) &&
        !present_seen_.load(std::memory_order_relaxed)) {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (menu_open_steady_ > 0.0 && now - menu_open_steady_ > 3.0) {
            if (!no_present_warned_) {
                no_present_warned_ = true;
                log(LogLevel::Warn, "图形菜单：打开 3 秒未收到渲染回调（交换链类型未匹配），退回文字菜单");
            }
            fail("未收到渲染回调");
        }
    }
}

void ImGuiOverlay::drain(MenuActions& out) {
    std::lock_guard<std::mutex> lock(view_mutex_);
    out.merge(pending_);
    pending_.clear();
}

void ImGuiOverlay::set_menu_open(bool open) {
    menu_open_.store(open, std::memory_order_relaxed);
    if (open) {
        present_seen_.store(false, std::memory_order_relaxed);
        menu_open_steady_ = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        no_present_warned_ = false;
    }
    refresh_should_render();
}

void ImGuiOverlay::feed_wheel(double notches) {
    if (notches == 0.0) return;
    double current = wheel_accum_.load(std::memory_order_relaxed);
    while (!wheel_accum_.compare_exchange_weak(current, current + notches, std::memory_order_relaxed)) {
    }
}

bool ImGuiOverlay::cursor_in_menu() const {
    if (!menu_open() || !ui_rect_valid_.load(std::memory_order_relaxed)) return false;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    const HWND game_window = static_cast<HWND>(game_hwnd_pub_.load(std::memory_order_relaxed));
    if (game_window != nullptr && !ScreenToClient(game_window, &cursor)) return false;
    const float x = static_cast<float>(cursor.x);
    const float y = static_cast<float>(cursor.y);
    const float left = ui_x_.load(std::memory_order_relaxed);
    const float top = ui_y_.load(std::memory_order_relaxed);
    const float right = left + ui_w_.load(std::memory_order_relaxed);
    const float bottom = top + ui_h_.load(std::memory_order_relaxed);
    return x >= left && x <= right && y >= top && y <= bottom;
}

// ---- 渲染线程 ----

void ImGuiOverlay::on_execute_command_lists(void* queue, unsigned count, void* const* lists) {
    (void)count;
    (void)lists;
    if (!queue_captured_.load(std::memory_order_relaxed)) {
        auto* command_queue = static_cast<ID3D12CommandQueue*>(queue);
        const D3D12_COMMAND_QUEUE_DESC desc = command_queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            d3d12_queue_ = queue;
            command_queue->AddRef();  // 我们留一份引用，游戏销毁队列时不悬空
            queue_captured_.store(true, std::memory_order_relaxed);
        }
    }
}

bool ImGuiOverlay::init_imgui() {
    imgui_ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imgui_ctx_));
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 绝不往游戏目录写 imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = false;

    // 中文字体：系统字体直接读，失败退 ImGui 默认字体（界面退英文）
    const char* font_candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
    };
    for (const char* path : font_candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font != nullptr) {
            cjk_ok_ = true;
            break;
        }
    }
    if (!cjk_ok_) io.Fonts->AddFontDefault();
    // 立刻光栅化字体图集：这步最重（中文 TTF 解析+光栅化约 0.5~1s），
    // 放在 start()（游戏线程、F2 时刻）而不是第一次 Present（渲染线程卡住游戏 1 秒）。
    io.Fonts->Build();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.94f;
    imgui_inited_ = true;
    return true;
}

bool ImGuiOverlay::init_backend_dx11(void* swapchain_ptr) {
    auto* swapchain = static_cast<IDXGISwapChain*>(swapchain_ptr);
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) return false;
    device->GetImmediateContext(&context);
    if (context == nullptr) {
        device->Release();
        return false;
    }
    const bool win32_ok = ImGui_ImplWin32_Init(static_cast<HWND>(game_hwnd_));
    const bool dx11_ok = win32_ok && ImGui_ImplDX11_Init(device, context);
    device->Release();
    context->Release();  // 后端持自己的引用
    if (!dx11_ok) {
        if (win32_ok) ImGui_ImplWin32_Shutdown();
        return false;
    }
    backend_api_ = 11;
    return true;
}

bool ImGuiOverlay::init_backend_dx12(void* swapchain_ptr) {
    if (!queue_captured_.load(std::memory_order_relaxed)) return false;  // 等队列，不算失败
    auto* swapchain = static_cast<IDXGISwapChain*>(swapchain_ptr);
    auto* device = static_cast<ID3D12Device*>(d3d12_device_);
    if (device == nullptr) {
        if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) return false;
        d3d12_device_ = device;  // 引用归我们
    }
    DXGI_SWAP_CHAIN_DESC swapchain_desc{};
    if (FAILED(swapchain->GetDesc(&swapchain_desc))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 64;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    if (FAILED(device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap))) || srv_heap == nullptr) return false;
    d3d12_srv_heap_ = srv_heap;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = 8;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    if (FAILED(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap))) || rtv_heap == nullptr) {
        srv_heap->Release();
        d3d12_srv_heap_ = nullptr;
        return false;
    }
    d3d12_rtv_heap_ = rtv_heap;
    rtv_increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 纹理上传专用队列：后端上传字体纹理后会在队列上 Signal 并**无限等待 GPU 完成**。
    // 绝不能传游戏渲染队列——我们的 Present 钩子正卡着渲染线程，游戏队列被帧工作
    // 排满（叠加 FSR3 帧生成的多队列）时这个等待就是死锁（1.1.1 实机卡死原因）。
    // 专用队列空闲，等待立即完成；上传完的纹理是 device 级资源，游戏队列照常采样。
    D3D12_COMMAND_QUEUE_DESC ui_queue_desc{};
    ui_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* ui_queue = nullptr;
    if (FAILED(device->CreateCommandQueue(&ui_queue_desc, IID_PPV_ARGS(&ui_queue))) || ui_queue == nullptr) {
        rtv_heap->Release();
        d3d12_rtv_heap_ = nullptr;
        srv_heap->Release();
        d3d12_srv_heap_ = nullptr;
        return false;
    }
    d3d12_ui_queue_ = ui_queue;

    for (int i = 0; i < 3; ++i) {
        ID3D12CommandAllocator* allocator = nullptr;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&allocator))) || allocator == nullptr) {
            for (int j = 0; j < i; ++j) {
                static_cast<ID3D12CommandAllocator*>(d3d12_cmd_allocators_[j])->Release();
                d3d12_cmd_allocators_[j] = nullptr;
            }
            ui_queue->Release();
            d3d12_ui_queue_ = nullptr;
            rtv_heap->Release();
            d3d12_rtv_heap_ = nullptr;
            srv_heap->Release();
            d3d12_srv_heap_ = nullptr;
            return false;
        }
        d3d12_cmd_allocators_[i] = allocator;
    }

    ID3D12GraphicsCommandList* command_list = nullptr;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         static_cast<ID3D12CommandAllocator*>(d3d12_cmd_allocators_[0]),
                                         nullptr, IID_PPV_ARGS(&command_list))) || command_list == nullptr) {
        for (int j = 0; j < 3; ++j) {
            static_cast<ID3D12CommandAllocator*>(d3d12_cmd_allocators_[j])->Release();
            d3d12_cmd_allocators_[j] = nullptr;
        }
        ui_queue->Release();
        d3d12_ui_queue_ = nullptr;
        rtv_heap->Release();
        d3d12_rtv_heap_ = nullptr;
        srv_heap->Release();
        d3d12_srv_heap_ = nullptr;
        return false;
    }
    d3d12_cmd_list_ = command_list;
    command_list->Close();

    IDXGISwapChain3* swapchain3 = nullptr;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain3)))) swapchain3 = nullptr;
    dxgi_swapchain3_ = swapchain3;  // 可空：拿不到就用 0 号缓冲

    ImGui_ImplDX12_InitInfo info{};
    info.Device = device;
    info.CommandQueue = static_cast<ID3D12CommandQueue*>(d3d12_ui_queue_);  // 纹理上传专用，绝不给游戏队列
    info.NumFramesInFlight = swapchain_desc.BufferCount > 0 ? static_cast<int>(swapchain_desc.BufferCount) : 3;
    info.RTVFormat = typed_format(swapchain_desc.BufferDesc.Format);
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = this;
    info.SrvDescriptorHeap = static_cast<ID3D12DescriptorHeap*>(d3d12_srv_heap_);
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init_info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* self = static_cast<ImGuiOverlay*>(init_info->UserData);
        auto* heap = static_cast<ID3D12DescriptorHeap*>(self->d3d12_srv_heap_);
        const unsigned index = self->srv_alloc_next_++ % 64;
        *cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu->ptr += static_cast<SIZE_T>(index) * init_info->Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        *gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu->ptr += static_cast<UINT64>(index) * init_info->Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                  D3D12_GPU_DESCRIPTOR_HANDLE) {
        // bump 分配，64 个槽位对字体纹理绰绰有余，不做回收
    };

    const bool win32_ok = ImGui_ImplWin32_Init(static_cast<HWND>(game_hwnd_));
    const bool dx12_ok = win32_ok && ImGui_ImplDX12_Init(&info);
    if (!dx12_ok) {
        if (win32_ok) ImGui_ImplWin32_Shutdown();
        if (dxgi_swapchain3_ != nullptr) { static_cast<IDXGISwapChain3*>(dxgi_swapchain3_)->Release(); dxgi_swapchain3_ = nullptr; }
        if (d3d12_cmd_list_ != nullptr) { static_cast<ID3D12GraphicsCommandList*>(d3d12_cmd_list_)->Release(); d3d12_cmd_list_ = nullptr; }
        for (int j = 0; j < 3; ++j) {
            if (d3d12_cmd_allocators_[j] != nullptr) { static_cast<ID3D12CommandAllocator*>(d3d12_cmd_allocators_[j])->Release(); d3d12_cmd_allocators_[j] = nullptr; }
        }
        if (d3d12_ui_queue_ != nullptr) { static_cast<ID3D12CommandQueue*>(d3d12_ui_queue_)->Release(); d3d12_ui_queue_ = nullptr; }
        if (d3d12_rtv_heap_ != nullptr) { static_cast<ID3D12DescriptorHeap*>(d3d12_rtv_heap_)->Release(); d3d12_rtv_heap_ = nullptr; }
        if (d3d12_srv_heap_ != nullptr) { static_cast<ID3D12DescriptorHeap*>(d3d12_srv_heap_)->Release(); d3d12_srv_heap_ = nullptr; }
        return false;
    }
    backend_api_ = 12;
    return true;
}

bool ImGuiOverlay::ensure_game_window(void* swapchain_window) {
    if (game_hwnd_ != nullptr && IsWindow(static_cast<HWND>(game_hwnd_))) return true;
    game_hwnd_ = nullptr;
    // 优先认 UnrealWindow（UE 系主窗口类名），找不到就认第一个非空窗口
    HWND unreal = FindWindowW(L"UnrealWindow", nullptr);
    if (unreal != nullptr && unreal == swapchain_window) {
        game_hwnd_ = unreal;
    } else if (swapchain_window != nullptr) {
        game_hwnd_ = swapchain_window;
    }
    if (game_hwnd_ != nullptr) {
        game_hwnd_pub_.store(game_hwnd_, std::memory_order_relaxed);
        install_window_cursor_subclass();
        log(LogLevel::Info, "图形菜单：已锁定游戏主窗口");
        return true;
    }
    return false;
}

// 光标常显：ShowCursor 计数是线程相关的，渲染线程调它管不到游戏窗口，
// 所以把游戏窗口子类化，渲染线程投递自定义消息，让窗口线程自己把光标放出来。
void ImGuiOverlay::on_cursor_show_message() {
    while (ShowCursor(TRUE) < 0) {}
    ClipCursor(nullptr);
}

bool ImGuiOverlay::install_window_cursor_subclass() {
    if (wndproc_subclassed_ || game_hwnd_ == nullptr) return false;
    const HWND window = static_cast<HWND>(game_hwnd_);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC,
                                                reinterpret_cast<LONG_PTR>(&overlay_wndproc));
    if (previous == 0) return false;
    orig_wndproc_ = reinterpret_cast<void*>(previous);
    wndproc_subclassed_ = true;
    return true;
}

void ImGuiOverlay::render_if_needed(void* swapchain) {
    if (failed_.load(std::memory_order_relaxed)) return;
    if (!should_render_.load(std::memory_order_relaxed)) return;

    auto* sc = static_cast<IDXGISwapChain*>(swapchain);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc)) || desc.OutputWindow == nullptr) return;
    if (!ensure_game_window(desc.OutputWindow)) return;
    if (desc.OutputWindow != game_hwnd_) return;  // 只画主窗口的交换链

    // 防重入：另一个线程（如帧生成线程）正在渲染时本线程直接放行
    if (in_frame_.exchange(true, std::memory_order_acq_rel)) return;
    FrameGuard guard{in_frame_};
    present_seen_.store(true, std::memory_order_relaxed);

    if (!imgui_inited_) {
        init_imgui();
    }
    if (!backend_inited()) {
        // 先 DX12（UE5 默认），队列还没捕获到或初始化失败再试 DX11
        bool inited = false;
        if (queue_captured_.load(std::memory_order_relaxed)) {
            inited = init_backend_dx12(sc) || init_backend_dx11(sc);
        } else {
            inited = init_backend_dx11(sc);
            if (!inited && ++queue_wait_frames_ > 600 && !queue_wait_warned_) {
                queue_wait_warned_ = true;
                log(LogLevel::Warn, "图形菜单：尚未捕获 DX12 命令队列，菜单暂不能渲染");
            }
        }
        if (!inited) {
            if (++init_fail_streak_ > kMaxInitFailStreak) fail("渲染后端初始化连续失败");
            return;
        }
        init_fail_streak_ = 0;
        queue_wait_frames_ = 0;
        log(LogLevel::Info, backend_api_ == 12 ? "图形菜单：DX12 后端已初始化" : "图形菜单：DX11 后端已初始化");
    }

    draw_frame(sc);
}

void ImGuiOverlay::draw_frame(void* swapchain) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imgui_ctx_));

    // 快照（渲染期间引擎线程可以继续写共享区）
    MenuView view;
    {
        std::lock_guard<std::mutex> lock(view_mutex_);
        view = view_;
    }
    view.font_cjk_ok = cjk_ok_;
    view.menu_open = true;

    feed_keys_and_pad();
    if (backend_api_ == 11) ImGui_ImplDX11_NewFrame();
    else ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    override_mouse_from_system();  // Win32 后端收不到窗口消息，鼠标状态自己读系统
    ImGui::NewFrame();

    MenuActions frame_actions;
    const bool escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escape_down && !esc_prev_) frame_actions.close_menu = true;
    esc_prev_ = escape_down;

    render_game_menu(view, frame_actions);

    ImGui::Render();

    if (frame_actions.rect_valid) {
        ui_x_.store(frame_actions.rect[0], std::memory_order_relaxed);
        ui_y_.store(frame_actions.rect[1], std::memory_order_relaxed);
        ui_w_.store(frame_actions.rect[2], std::memory_order_relaxed);
        ui_h_.store(frame_actions.rect[3], std::memory_order_relaxed);
        ui_rect_valid_.store(true, std::memory_order_relaxed);
    }
    ui_mouse_.store(ImGui::GetIO().WantCaptureMouse, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(view_mutex_);
        pending_.merge(frame_actions);
    }

    if (backend_api_ == 11) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    } else {
        render_dx12(swapchain);
    }

    // 光标常显：直接调一次（窗口线程那条消息是兜底）
    if (game_hwnd_ != nullptr) {
        PostMessageW(static_cast<HWND>(game_hwnd_), cursor_message_, 0, 0);
    }
}

void ImGuiOverlay::feed_keys_and_pad() {
    ImGuiIO& io = ImGui::GetIO();

    // 键盘：只在状态变化时喂（ImGui 事件流），覆盖导航会用到的一小组
    struct KeyEntry { int vk; ImGuiKey key; };
    static constexpr KeyEntry keys[] = {
        {VK_UP, ImGuiKey_UpArrow}, {VK_DOWN, ImGuiKey_DownArrow},
        {VK_LEFT, ImGuiKey_LeftArrow}, {VK_RIGHT, ImGuiKey_RightArrow},
        {VK_RETURN, ImGuiKey_Enter}, {VK_ESCAPE, ImGuiKey_Escape},
        {VK_TAB, ImGuiKey_Tab}, {VK_SPACE, ImGuiKey_Space},
        {VK_HOME, ImGuiKey_Home}, {VK_END, ImGuiKey_End},
        {VK_PRIOR, ImGuiKey_PageUp}, {VK_NEXT, ImGuiKey_PageDown},
        {VK_BACK, ImGuiKey_Backspace}, {VK_DELETE, ImGuiKey_Delete},
        {VK_INSERT, ImGuiKey_Insert},
        {'A', ImGuiKey_A}, {'D', ImGuiKey_D}, {'W', ImGuiKey_W}, {'S', ImGuiKey_S},
        {'E', ImGuiKey_E}, {'Q', ImGuiKey_Q},
    };
    for (const KeyEntry& entry : keys) {
        const bool down = (GetAsyncKeyState(entry.vk) & 0x8000) != 0;
        const int key_index = static_cast<int>(entry.key) - static_cast<int>(ImGuiKey_NamedKey_BEGIN);
        const unsigned bit = 1u << (key_index % 32);
        const unsigned word = static_cast<unsigned>(key_index / 32);
        const bool was_down = (prev_key_bits_[word] & bit) != 0;
        if (down != was_down) {
            io.AddKeyEvent(entry.key, down);
            if (down) prev_key_bits_[word] |= bit;
            else prev_key_bits_[word] &= ~bit;
        }
    }

    // 手柄导航（快照来自引擎线程）
    PadState pad;
    {
        std::lock_guard<std::mutex> lock(view_mutex_);
        pad = pad_;
    }
    struct PadEntry { PadButton button; ImGuiKey key; };
    static constexpr PadEntry pad_keys[] = {
        {PadButton::Up, ImGuiKey_GamepadDpadUp}, {PadButton::Down, ImGuiKey_GamepadDpadDown},
        {PadButton::Left, ImGuiKey_GamepadDpadLeft}, {PadButton::Right, ImGuiKey_GamepadDpadRight},
        {PadButton::A, ImGuiKey_GamepadFaceDown},   // A = 确认
        {PadButton::B, ImGuiKey_GamepadFaceRight},  // B = 返回
        {PadButton::X, ImGuiKey_GamepadFaceLeft},
        {PadButton::Y, ImGuiKey_GamepadFaceUp},
        {PadButton::LB, ImGuiKey_GamepadL1}, {PadButton::RB, ImGuiKey_GamepadR1},
        {PadButton::LS, ImGuiKey_GamepadL3}, {PadButton::RS, ImGuiKey_GamepadR3},
        {PadButton::Start, ImGuiKey_GamepadStart},
        {PadButton::Back, ImGuiKey_GamepadBack},
    };
    for (const PadEntry& entry : pad_keys) {
        const int key_index = static_cast<int>(entry.key) - static_cast<int>(ImGuiKey_NamedKey_BEGIN);
        const unsigned bit = 1u << (key_index % 32);
        const unsigned word = static_cast<unsigned>(key_index / 32);
        const bool down = pad_held(pad, entry.button);
        const bool was_down = (prev_key_bits_[word] & bit) != 0;
        if (down != was_down) {
            io.AddKeyEvent(entry.key, down);
            if (down) prev_key_bits_[word] |= bit;
            else prev_key_bits_[word] &= ~bit;
        }
    }
    // 左摇杆当导航模拟量（阈值 + 行程）
    struct Stick { double value; ImGuiKey key; };
    const Stick sticks[] = {
        {pad.ly, ImGuiKey_GamepadLStickUp}, {-pad.ly, ImGuiKey_GamepadLStickDown},
        {pad.lx, ImGuiKey_GamepadLStickRight}, {-pad.lx, ImGuiKey_GamepadLStickLeft},
    };
    for (const Stick& stick : sticks) {
        const float held = static_cast<float>(stick.value);
        const bool down = held > 0.3f;
        const int key_index = static_cast<int>(stick.key) - static_cast<int>(ImGuiKey_NamedKey_BEGIN);
        const unsigned bit = 1u << (key_index % 32);
        const unsigned word = static_cast<unsigned>(key_index / 32);
        const bool was_down = (prev_key_bits_[word] & bit) != 0;
        if (down != was_down) {
            io.AddKeyAnalogEvent(stick.key, down, down ? held : 0.0f);
            if (down) prev_key_bits_[word] |= bit;
            else prev_key_bits_[word] &= ~bit;
        } else if (down) {
            io.AddKeyAnalogEvent(stick.key, true, held);
        }
    }
}

void ImGuiOverlay::override_mouse_from_system() {
    ImGuiIO& io = ImGui::GetIO();
    if (GetForegroundWindow() != static_cast<HWND>(game_hwnd_)) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        return;
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(static_cast<HWND>(game_hwnd_), &cursor)) {
        io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
    }
    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    const double wheel = wheel_accum_.exchange(0.0, std::memory_order_relaxed);
    if (wheel != 0.0) {
        io.AddMouseWheelEvent(0.0f, static_cast<float>(wheel));
    }
}

void ImGuiOverlay::render_dx12(void* swapchain_ptr) {
    auto* swapchain = static_cast<IDXGISwapChain*>(swapchain_ptr);
    auto* device = static_cast<ID3D12Device*>(d3d12_device_);
    auto* queue = static_cast<ID3D12CommandQueue*>(d3d12_queue_);
    auto* allocator = static_cast<ID3D12CommandAllocator*>(d3d12_cmd_allocators_[d3d12_frame_ % 3]);
    d3d12_frame_++;
    auto* command_list = static_cast<ID3D12GraphicsCommandList*>(d3d12_cmd_list_);
    auto* rtv_heap = static_cast<ID3D12DescriptorHeap*>(d3d12_rtv_heap_);
    if (device == nullptr || queue == nullptr || allocator == nullptr || command_list == nullptr || rtv_heap == nullptr) return;

    UINT back_buffer_index = 0;
    if (dxgi_swapchain3_ != nullptr) {
        back_buffer_index = static_cast<IDXGISwapChain3*>(dxgi_swapchain3_)->GetCurrentBackBufferIndex();
    }
    ID3D12Resource* back_buffer = nullptr;
    if (FAILED(swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer))) || back_buffer == nullptr) return;

    DXGI_SWAP_CHAIN_DESC desc{};
    swapchain->GetDesc(&desc);
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{};
    rtv_desc.Format = typed_format(desc.BufferDesc.Format);
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(back_buffer, &rtv_desc, rtv_handle);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (FAILED(command_list->Reset(allocator, nullptr))) {
        back_buffer->Release();
        return;
    }
    command_list->ResourceBarrier(1, &barrier);
    ID3D12DescriptorHeap* heaps[] = {static_cast<ID3D12DescriptorHeap*>(d3d12_srv_heap_)};
    command_list->SetDescriptorHeaps(1, heaps);
    command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list->ResourceBarrier(1, &barrier);
    command_list->Close();

    ID3D12CommandList* lists[] = {command_list};
    queue->ExecuteCommandLists(1, lists);
    back_buffer->Release();

    // 设备被移除（驱动重置/资源状态冲突）时不再渲染，避免把游戏 RHI 拖进致命错误
    const HRESULT removed_reason = device->GetDeviceRemovedReason();
    if (FAILED(removed_reason)) {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(removed_reason));
        fail("DX12 设备已移除（" + std::string(buf) + "）");
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
