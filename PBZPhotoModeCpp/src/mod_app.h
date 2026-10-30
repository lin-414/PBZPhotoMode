#pragma once

// 只在真正编译 DLL 时需要 UE4SS 头文件；核心层测试不包含这个文件。
#include <Mod/CppUserModBase.hpp>

#include <memory>
#include <mutex>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/photo_mode.h"

namespace pbzpm {

class Ue4ssPort;
class Win32InputSource;

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
    void log_input_diagnostics(const Intent& intent, double dt);
    auto mod_directory() -> std::string;

    std::unique_ptr<Ue4ssPort> port_;
    std::unique_ptr<PhotoMode> mode_;
    Config cfg_{};
    KeyMap keys_{};
    InputLogic input_{};
    Win32InputSource* input_source_{nullptr};

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
};

}  // namespace pbzpm
