#pragma once

// 只在真正编译 DLL 时需要 UE4SS 头文件；核心层测试不包含这个文件。
#include <Mod/CppUserModBase.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/mod_runtime.h"
#include "core/photo_mode.h"
#include "pbzfm_client.h"

namespace pbzpm {

class Ue4ssPort;
class Win32InputSource;
class ProcessFreezer;

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
    void on_game_viewport_tick_post();
    void retry_view_attach(double dt);
    void ensure_axis_hook();
    void handle_intent(const Intent& intent, double dt);
    void handle_toggle();
    // 帧定格（F7）：独立 Win32 线程轮询快捷键；定格时挂起本进程除自己外的所有线程，
    // 画面停在最后一帧，再按一次恢复。线程不碰任何 UE 对象/锁，只用 Win32 API。
    void frame_freeze_watch_loop();
    // 画面定格准备/恢复：在引擎 Tick 线程处理 watcher 的请求，负责与摄影模式已有的
    // Wwise 状态协调（watcher 不能碰 UE 对象，也不能在挂起线程时打印日志）。
    void handle_frame_freeze_request(const Config& cfg, bool mode_active);
    void pbzfm_apply_pending();  // 消化 PBZFM 菜单提交的配置与动作（引擎 Tick，持 mutex_）
    void log_input_diagnostics(const Intent& intent, double dt);
    auto mod_directory() -> std::string;

    std::unique_ptr<Ue4ssPort> port_;
    std::unique_ptr<PhotoMode> mode_;
    Config cfg_{};
    KeyMap keys_{};
    // 引擎侧编排（进/退、tick 分工、配置应用与实时开关差异）放在 core/ModRuntime，
    // 依赖按引用注入，便于用 FakeEngine 覆盖完整进退路径（见 tests/test_mod_runtime.cpp）。
    std::unique_ptr<ModRuntime> runtime_;
    InputLogic input_{};
    Win32InputSource* input_source_{nullptr};
    // 帧定格状态：watcher 线程只读写原子量；allowed 由引擎 Tick 根据摄影模式 active 设置
    std::thread frame_freeze_thread_;
    std::atomic<bool> frame_freeze_stop_{false};
    std::atomic<bool> frame_freeze_allowed_{false};
    std::atomic<int> frame_freeze_vk_{0x76};  // VK_F7，reload_config 时按配置文件更新
    std::atomic<unsigned long long> frame_freeze_timeout_ms_{60000};
    // watcher ↔ 引擎 Tick 的握手：1=请求定格准备，2=请求恢复音频，3=拒绝（已退出摄影模式）。
    std::atomic<int> frame_freeze_request_{0};
    std::atomic<int> frame_freeze_ack_{0};
    std::atomic<bool> frame_freeze_wwise_owned_{false};  // 本次定格是否由 F7 挂了 Wwise（退出/恢复时只还原自己挂的）
    // 恢复报告：引擎 Tick 恢复后打一条日志（watcher 在定格期间不能安全写日志）。
    std::atomic<bool> frame_freeze_resume_report_{false};
    std::atomic<int> frame_freeze_resume_threads_{0};
    std::atomic<int> frame_freeze_resume_skipped_{0};
    std::atomic<unsigned long long> frame_freeze_resume_duration_ms_{0};
    std::unique_ptr<ProcessFreezer> process_freezer_;

    // 保护 cfg_ / keys_ / input_ / mode_ 与诊断字段：引擎 Tick 线程是主写者，
    // PBZFM 控制台渲染线程与 mod 卸载路径会读/偶尔写；持锁期间不做全对象扫描等重活。
    std::mutex mutex_{};
    bool tick_hook_installed_{false};
    // GameViewportClientTickPost 是否注册成功：接管模式的 POV 只能写在这个点之后。
    // 默认 true（尚未探测）；注册失败置 false，并让 takeover_current_camera 在本会话内失效。
    bool gvc_tick_hook_ok_{true};
    double attach_retry_timer_{0.0};

    // 引擎侧鼠标增量（InputAxis 钩子喂进来，引擎 Tick 里取走）。
    // 线程归属：钩子回调写 axis_*，Tick 读取并清零 engine_*；原则上同线程，仍加锁防框架切换线程。
    std::mutex axis_mutex_;
    double axis_dx_{0.0};
    double axis_dy_{0.0};
    double axis_wheel_{0.0};
    bool axis_move_active_{false};
    // 引擎输入钩子安装状态（M-11）：done 之后每帧只做一次布尔判断（成功路径零成本）；
    // failures 决定退避档位（0.5s→1s→2s→4s→8s 封顶），next_retry_at 是单调秒时间戳，
    // 到点才再试。旧实现按调用次数 +1.0 累加并累计 30 次后永久放弃，已废弃。
    bool axis_hook_done_{false};
    int axis_hook_failures_{0};
    double axis_hook_next_retry_at_{0.0};
    double engine_dx_{0.0};
    double engine_dy_{0.0};
    bool engine_delta_ok_{false};
    bool engine_delta_logged_{false};
    double engine_peak_dx_{0.0};
    double engine_peak_dy_{0.0};
    // 引擎鼠标增量接口持续不可用的告警（port 内 *_missing_ 永久缓存本轮不能清，只能明确告警）
    double engine_delta_missing_accum_{0.0};
    bool engine_delta_missing_warned_{false};
    double diag_accum_{0.0};
    double voice_hook_accum_{0.0};  // 对话语音钩子重试节流（每 2 秒试一次直到挂上）
    int voice_hook_attempts_{0};    // 重试次数（有上限：每次重试都是两遍全对象数组扫描）
    // 1.0.69 一次性修复：旧版本可能把字幕/场景交互提示设置写坏并让游戏存盘
    double ui_repair_accum_{0.0};   // 重试节流（每 3 秒一次）
    int ui_repair_attempts_{0};     // 重试次数上限（设置对象可能晚就绪）
    bool ui_repair_done_{false};    // 已完成（成功 / 之前修过 / 配置关闭）
    double last_tick_time_{0.0};
    bool pad_logged_connected_{false};  // 手柄连接状态只在与上次不同时打日志
    bool pad_scan_logged_{false};       // 启动 10 秒后报一次扫描状态，证明轮询管线活着
    double pad_scan_log_timer_{0.0};
    bool input_diag_enabled_{false};    // 每秒输入诊断默认关（反射摘要+日志落盘有开销），mod 目录放 debug_input_diag.txt 开启
    bool menu_open_last_{false};        // PBZFM 菜单开/关只在状态变化时打一行日志

    // ---- PBZFM 菜单框架客户端（可选组件；框架线程只写下面三个字段，引擎 Tick 消化） ----
    // 构造期即创建（start() 在引擎初始化时才调）：控制台面板首帧就能共用 lang() 词典
    std::unique_ptr<PbzfmClient> pbzfm_{std::make_unique<PbzfmClient>()};
    // 保护 pbzfm_pending_cfg_ / pbzfm_cfg_dirty_ / pbzfm_commands_：PBZFM 框架线程写入，
    // 引擎 Tick 消费；锁内只搬移/拷贝，不调用框架 API。
    std::mutex pbzfm_mutex_;
    Config pbzfm_pending_cfg_{};
    bool pbzfm_cfg_dirty_{false};
    std::vector<PbzfmCommand> pbzfm_commands_;
};

}  // namespace pbzpm
