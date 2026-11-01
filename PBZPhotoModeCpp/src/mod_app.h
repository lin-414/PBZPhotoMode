#pragma once

// 只在真正编译 DLL 时需要 UE4SS 头文件；核心层测试不包含这个文件。
#include <Mod/CppUserModBase.hpp>

#include <memory>
#include <mutex>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/photo_mode.h"
#include "pbzfm_client.h"

namespace pbzpm {

class Ue4ssPort;
class Win32InputSource;

// UE4SS mod 本体：把输入/引擎事件翻译成 core 层 Intent，再把结果交给 PhotoMode。
// 游戏内无任何菜单界面（v1.8.0 起纯快捷键）；UI 只有 UE4SS 控制台里的诊断面板。
class PhotoModeMod final : public RC::CppUserModBase, public PbzfmClient::Host {
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

    // ---- PbzfmClient::Host（PBZFM 菜单框架桥；见 pbzfm_client.h） ----
    // 客户端在框架线程上只调这四个入口：取配置副本 / 提交配置与动作 / 打日志。
    // 所有引擎交互由引擎 Tick 线程在 pbzfm_apply_pending() 里执行。
    // 只声明不内联：log 要解引用 Ue4ssPort（此处仅前置声明）。
    auto mod_dir() -> std::string override;
    auto current_config() -> Config override;
    auto submit_config(const Config& cfg) -> void override;
    auto submit_command(const PbzfmCommand& cmd) -> void override;
    auto log(LogLevel level, const std::string& msg) -> void override;

private:
    void reload_config();
    void bind_engine_tick();
    void on_engine_tick(double delta_seconds);
    void on_engine_tick_post();
    void retry_view_attach(double dt);
    void ensure_axis_hook();
    void handle_intent(const Intent& intent, double dt);
    void handle_toggle();
    void pbzfm_apply_pending();  // 消化 PBZFM 菜单提交的配置与动作（引擎 Tick，持 mutex_）
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
    double attach_retry_timer_{0.0};

    // 引擎侧鼠标增量（InputAxis 钩子喂进来，引擎 Tick 里取走）
    std::mutex axis_mutex_;
    double axis_dx_{0.0};
    double axis_dy_{0.0};
    double axis_wheel_{0.0};
    bool axis_move_active_{false};
    bool axis_hook_tried_{false};
    double engine_dx_{0.0};
    double engine_dy_{0.0};
    bool engine_delta_ok_{false};
    bool engine_delta_logged_{false};
    double engine_peak_dx_{0.0};
    double engine_peak_dy_{0.0};
    double axis_hook_retry_timer_{0.0};
    double diag_accum_{0.0};
    double voice_hook_accum_{0.0};  // 对话语音钩子重试节流（每 2 秒试一次直到挂上）
    int voice_hook_attempts_{0};    // 重试次数（有上限：每次重试都是两遍全对象数组扫描）
    double last_tick_time_{0.0};
    bool pad_logged_connected_{false};  // 手柄连接状态只在与上次不同时打日志
    bool pad_scan_logged_{false};       // 启动 10 秒后报一次扫描状态，证明轮询管线活着
    double pad_scan_log_timer_{0.0};
    bool input_diag_enabled_{false};    // 每秒输入诊断默认关（反射摘要+日志落盘有开销），mod 目录放 debug_input_diag.txt 开启

    // ---- PBZFM 菜单框架客户端（可选组件；框架线程只写下面三个字段，引擎 Tick 消化） ----
    // 构造期即创建（start() 在引擎初始化时才调）：控制台面板首帧就能共用 lang() 词典
    std::unique_ptr<PbzfmClient> pbzfm_{std::make_unique<PbzfmClient>()};
    std::mutex pbzfm_mutex_;
    Config pbzfm_pending_cfg_{};
    bool pbzfm_cfg_dirty_{false};
    std::vector<PbzfmCommand> pbzfm_commands_;
};

}  // namespace pbzpm
