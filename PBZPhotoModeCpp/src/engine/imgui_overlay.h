#pragma once

#ifdef PBZPM_WITH_UE4SS

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "core/config.h"
#include "core/gamepad.h"
#include "core/menu_view.h"

namespace pbzpm {

// 游戏内图形菜单：自挂 DXGI/D3D 交换链钩子，用自己的 ImGui context 直接画进游戏画面。
// 与 UE4SS 控制台 tab（register_tab）无关——UE4SS 3.0.1 自带 GUI 只有 DX11/OpenGL 后端，
// 在 DX12 游戏上画不出来，这里自己钩 Present，DX11/DX12 运行时自动探测。
//
// 线程模型：
//   引擎 Tick 线程：publish() 写快照、drain() 取操作、set_menu_open()/menu_open()/cursor_in_menu()
//   渲染线程（Present 钩子）：读快照、喂输入、渲染、写回操作与窗口矩形
//   两侧只通过 view_mutex_ 交换 MenuView/MenuActions，不共享指针。
//
// 失败降级：钩子装不上、后端初始化反复失败 → available()=false，上层继续用 UMG 文字菜单。
class ImGuiOverlay {
public:
    using LogFn = std::function<void(LogLevel, const std::string&)>;

    static ImGuiOverlay& instance();

    // 安装钩子（幂等）。返回 true 只代表钩子装上了，具体后端在第一次 Present 时初始化。
    bool start(LogFn log);
    bool start_impl(LogFn log);
    // 卸钩子并销毁 ImGui（只应在 mod 卸载时调用；会等渲染帧结束）
    void stop();
    // 钩子已装且未进入失败状态（引擎线程判断走图形菜单还是 UMG 文字菜单）
    bool available() const { return started_.load(std::memory_order_relaxed) && !failed_.load(std::memory_order_relaxed); }

    // ---- 引擎线程 ----
    void publish(const MenuView& view, const PadState& pad);
    // 取走渲染线程攒下的操作（调用方逐帧调用）
    void drain(MenuActions& out);
    void set_menu_open(bool open);
    bool menu_open() const { return menu_open_.load(std::memory_order_relaxed); }
    // 引擎线程把落在菜单上的滚轮转给 UI（数值会被取走喂给 ImGui）
    void feed_wheel(double notches);
    // 光标当前是否落在菜单窗口内（引擎线程判断滚轮/鼠标互斥用）
    bool cursor_in_menu() const;
    // UI 是否在捕获鼠标（拖滑条等）：true 时引擎侧视角/俯仰输入要静音
    bool ui_captures_mouse() const { return ui_mouse_.load(std::memory_order_relaxed); }

    // ---- 渲染线程入口（cpp 里的钩子转发进来；参数用 void* 避免头文件引 D3D 头）----
    bool shutting_down() const { return shutting_down_.load(std::memory_order_acquire); }
    // 窗口过程转发用（wndproc 是 cpp 里的普通函数，只能走公共接口）
    bool is_cursor_message(unsigned message) const { return cursor_message_ != 0 && message == cursor_message_; }
    void* original_wndproc() const { return orig_wndproc_; }
    // 在窗口线程执行：光标常显 + 解除裁剪（ShowCursor 计数是线程相关的）
    void on_cursor_show_message();
    void render_if_needed(void* swapchain);
    void on_execute_command_lists(void* queue, unsigned count, void* const* lists);

private:
    ImGuiOverlay() = default;
    ~ImGuiOverlay() = default;
    ImGuiOverlay(const ImGuiOverlay&) = delete;
    ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

    void refresh_should_render();
    void fail(const std::string& reason);
    void log(LogLevel level, const std::string& message);

    // 状态交换（引擎线程 + 渲染线程都碰，靠 view_mutex_）
    std::mutex view_mutex_;
    MenuView view_;
    PadState pad_{};
    MenuActions pending_;

    std::atomic<bool> started_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> in_frame_{false};
    std::atomic<bool> menu_open_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> should_render_{false};
    std::atomic<bool> present_seen_{false};
    std::atomic<bool> ui_mouse_{false};
    std::atomic<double> wheel_accum_{0.0};

    // 引擎线程专用：菜单打开后迟迟收不到渲染回调时自动退回文字菜单（交换链类型不匹配的兜底）
    double menu_open_steady_{0.0};
    bool no_present_warned_{false};

    // 菜单窗口矩形（渲染线程写，引擎线程读）
    std::atomic<float> ui_x_{0.0f};
    std::atomic<float> ui_y_{0.0f};
    std::atomic<float> ui_w_{0.0f};
    std::atomic<float> ui_h_{0.0f};
    std::atomic<bool> ui_rect_valid_{false};

    // 渲染线程独占状态（不与引擎线程共享）
    LogFn log_;
    void* imgui_ctx_{nullptr};
    bool imgui_inited_{false};
    bool cjk_ok_{false};
    void* hwnd_dummy_{nullptr};
    bool backend_inited() const { return backend_api_ != 0; }
    void* game_hwnd_{nullptr};           // 渲染线程认定的主窗口
    std::atomic<void*> game_hwnd_pub_{nullptr};  // 引擎线程读（cursor_in_menu 用）
    void* orig_wndproc_{nullptr};        // 子类化前的窗口过程（光标常显用）
    unsigned cursor_message_{0};         // RegisterWindowMessage 的自定义消息
    bool wndproc_subclassed_{false};
    int backend_api_{0};                 // 0=未定 11=DX11 12=DX12
    int init_fail_streak_{0};
    int queue_wait_frames_{0};
    bool queue_wait_warned_{false};
    bool esc_prev_{false};
    unsigned prev_key_bits_[8]{};        // ImGuiKey 按位记录上一帧状态，只在变化时喂事件
    // DX12 侧（backend_api_==12 时有效）
    void* d3d12_device_{nullptr};
    void* d3d12_queue_{nullptr};         // ExecuteCommandLists 钩子捕获的游戏队列
    std::atomic<bool> queue_captured_{false};
    void* d3d12_srv_heap_{nullptr};
    void* d3d12_rtv_heap_{nullptr};
    void* d3d12_ui_queue_{nullptr};      // ImGui 纹理上传专用队列（后端上传后会无限等 GPU，游戏队列被帧工作占满时就是死锁）
    void* d3d12_cmd_allocators_[3]{};    // 轮换命令分配器，避免 Reset 时上一帧还在 GPU 执行
    unsigned d3d12_frame_{0};
    void* d3d12_cmd_list_{nullptr};
    void* dxgi_swapchain3_{nullptr};
    unsigned rtv_increment_{0};
    unsigned srv_alloc_next_{0};

    bool init_imgui();
    bool init_backend_dx11(void* swapchain);
    bool init_backend_dx12(void* swapchain);
    void draw_frame(void* swapchain);
    void feed_keys_and_pad();
    void override_mouse_from_system();
    void render_dx12(void* swapchain);
    bool ensure_game_window(void* swapchain_window);
    bool install_window_cursor_subclass();
    void shutdown_imgui();
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
