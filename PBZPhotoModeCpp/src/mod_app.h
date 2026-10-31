#pragma once

// 只在真正编译 DLL 时需要 UE4SS 头文件；核心层测试不包含这个文件。
#include <Mod/CppUserModBase.hpp>

#include <memory>
#include <mutex>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/menu_bridge.h"
#include "core/photo_mode.h"

namespace pbzpm {

class Ue4ssPort;
class Win32InputSource;
class ImGuiOverlay;
struct Umg2Commands;
struct WbpCommands;

// UE4SS mod 本体：把输入/引擎事件翻译成 core 层 Intent，再把结果交给 PhotoMode。
class PhotoModeMod final : public RC::CppUserModBase {
public:
    PhotoModeMod();
    ~PhotoModeMod() override;

    auto on_unreal_init() -> void override;
    auto on_ui_init() -> void override;
    auto on_update() -> void override;

    // ImGui 面板（UE4SS 控制台里的 tab）——register_tab 只接受普通函数指针，
    // 因此这里用静态桥接函数把 this 转回来。
    static auto render_tab_bridge(RC::CppUserModBase* owner) -> void;
    auto render_tab_impl() -> void;

private:
    void reload_config();
    void bind_engine_tick();
    void on_engine_tick(double delta_seconds);
    void on_engine_tick_post();
    void retry_view_attach(double dt);
    void ensure_axis_hook();
    void handle_intent(const Intent& intent, double dt);
    void handle_toggle();
    void update_overlay();
    void publish_menu_snapshot();
    void apply_menu_actions();
    void apply_umg2_commands(const Umg2Commands& commands);
    void apply_wbp_commands(const WbpCommands& commands);
    // WBP 资产菜单：指针是否落在面板上（悬停静音，与 umg2 的命中判定同款缩放机器）
    bool cursor_in_scaled_panel(float from_right, float top, float width, float height);
    // 开菜单沿解析一次 UI 缩放（umg2_scale.txt 覆盖 > 物理/Slate 视口 > DPI），并打映射日志
    void resolve_panel_scale();
    // pak 自动安装：把 mod 目录 paks/ 的三件套拷进游戏 ProjectPBZ\Content\Paks\~mods
    void ensure_pak_installed();
    // 图形菜单延迟初始化：首次进入摄影模式才安装钩子（1.1.1：不要在游戏 RHI 初始化期碰 D3D）
    void ensure_overlay_started();
    void log_input_diagnostics(const Intent& intent, double dt);
    auto mod_directory() -> std::string;

    std::unique_ptr<Ue4ssPort> port_;
    std::unique_ptr<PhotoMode> mode_;
    Config cfg_{};
    KeyMap keys_{};
    InputLogic input_{};
    Win32InputSource* input_source_{nullptr};
    // 游戏内图形菜单（D3D 钩子 + 独立 ImGui context）；nullptr/不可用时退回 UMG 文字菜单
    ImGuiOverlay* overlay_{nullptr};
    // 菜单「重置视角」按钮：下一帧注入一次 reset_view intent
    bool menu_reset_view_{false};
    // UMG 菜单的 Esc 关闭沿检测
    bool esc_prev_{false};
    // UMG 交互菜单：左键按下沿跟踪 + 指针是否在面板上（上一帧判定，用于输入门控）
    bool lmb_prev_{false};
    bool umg2_cursor_in_panel_{false};
    // 菜单打开沿检测（开菜单时解析一次 UI 缩放并打一条映射日志）
    bool umg2_menu_open_prev_{false};
    float umg2_scale_override_{0.0f};  // 0 = 未覆盖（umg2_scale.txt）
    float umg2_scale_cached_{0.0f};    // 0 = 未定（物理/Slate 或 DPI 推算）
    double umg2_last_miss_log_{0.0};   // 点击未命中日志的节流
    // WBP 资产菜单：桥 + 打开沿（悬停静音与 umg2 共用 umg2_cursor_in_panel_ / 缩放缓存）
    WbpBridge wbp_bridge_{};
    bool wbp_menu_open_prev_{false};
    bool pak_copy_done_{false};
    // 图形菜单延迟初始化状态
    bool overlay_tried_{false};
    bool overlay_disabled_{false};

    std::mutex mutex_{};
    bool tick_hook_installed_{false};
    bool overlay_ok_{false};
    double attach_retry_timer_{0.0};

    // 引擎侧鼠标增量（InputAxis 钩子喂进来，引擎 Tick 里取走）
    std::mutex axis_mutex_;
    double axis_dx_{0.0};
    double axis_dy_{0.0};
    double axis_wheel_{0.0};
    bool axis_move_active_{false};
    bool axis_wheel_active_{false};
    bool axis_hook_tried_{false};
    double engine_dx_{0.0};
    double engine_dy_{0.0};
    bool engine_delta_ok_{false};
    bool engine_move_seen_{false};
    bool engine_delta_logged_{false};
    double engine_peak_dx_{0.0};
    double engine_peak_dy_{0.0};
    double axis_hook_retry_timer_{0.0};
    double diag_accum_{0.0};
    double last_tick_time_{0.0};
    bool pad_logged_connected_{false};  // 手柄连接状态只在与上次不同时打日志
    bool pad_scan_logged_{false};       // 启动 10 秒后报一次扫描状态，证明轮询管线活着
    double pad_scan_log_timer_{0.0};
};

}  // namespace pbzpm
