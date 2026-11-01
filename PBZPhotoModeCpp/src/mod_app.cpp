// UE4SS mod 主体：把输入/引擎事件翻译成 core 层 Intent，再把结果交给 PhotoMode。
// 游戏内无任何菜单界面（v1.8.0 起纯快捷键，v1.8.1 起代码彻底移除）。
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
#include <mutex>
#include <string>

#include "core/version.h"
#include "engine/ue4ss_port.h"
#include "ui_tab.h"
#include "win_input.h"

namespace pbzpm {

// 引擎 GetInputMouseDelta 给的是「像素 ÷ 游戏鼠标灵敏度」，量级只有 0.1~0.2；
// 乘这个系数放回像素量级，再交给 core 的 look_sensitivity（度/单位）换算成角度。
constexpr double kEngineMouseScale = 40.0;
// 语音钩子重试上限（每 2 秒一次，300 次 = 10 分钟）：每次重试做两遍全对象数组扫描，
// 挂不上就说明环境有问题，无限重试等于周期性卡顿
constexpr int kMaxVoiceHookRetries = 300;
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

}  // namespace

PhotoModeMod::PhotoModeMod() {
    ModName = STR("PBZPhotoModeCpp");
    ModVersion = PBZPM_WIDEN(PBZPM_VERSION);
    ModDescription = STR("Photo mode: free camera / FOV / auto-pause time / ImGui tab");
    ModAuthors = STR("PBZPhotoMode");
    g_mod = this;
}

PhotoModeMod::~PhotoModeMod() {
    if (pbzfm_) pbzfm_->stop();  // 先停框架客户端线程，再拆输入/引擎侧
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
    if (pbzfm_) {
        pbzfm_->sync_from_config(cfg_);  // 菜单显示值跟着 config.ini 走
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

    input_source_ = &Win32InputSource::instance();
    // 排障开关：mod 目录里放一个 no_wheel_hook.txt 就不装低级鼠标钩子
    const bool wheel_hook = !std::filesystem::exists(std::filesystem::path(mod_directory()) / "no_wheel_hook.txt");
    if (!input_source_->install(wheel_hook)) {
        port_->log(LogLevel::Warn, "鼠标钩子安装失败：滚轮 FOV / 按住 Alt 转视角可能不可用");
    }
    // 排障开关：mod 目录里放一个 debug_input_diag.txt 才开启每秒输入诊断日志
    // （诊断行要做反射摘要 + 日志落盘，1.0.38 实机摄影模式里每秒一次，默认关）
    input_diag_enabled_ = std::filesystem::exists(std::filesystem::path(mod_directory()) / "debug_input_diag.txt");

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

    // PBZFM 菜单框架（可选组件）：后台线程轮询 PBZFM.dll，装了就注册设置页与诊断页
    pbzfm_->start(this);
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
        // 初始化窗口期词典尚未扫描：仅此一条保留代码内兜底文案
        ImGui::TextUnformatted("正在等待游戏引擎初始化…");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    pbzfm_->lang().RescanLanguages(mod_dir(), false);  // 内部 2 秒节流，热加载
    PbzmI18n& lang = pbzfm_->lang();
    ImGui::TextUnformatted(mode_->active() ? lang.T("#console_state_on") : lang.T("#console_state_off"));
    if (cfg_.pad_enabled) {
        ImGui::Text(lang.T("#console_hotkey_pad_fmt"), vk_to_name(keys_.toggle).c_str(),
                    pad_combo_name(keys_.pad_toggle).c_str(), vk_to_name(keys_.freeze).c_str());
    } else {
        ImGui::Text(lang.T("#console_hotkey_fmt"), vk_to_name(keys_.toggle).c_str(),
                    vk_to_name(keys_.freeze).c_str());
    }
    ImGui::Separator();

    UiCommands commands{};
    bool reload = false;
    render_photo_mode_tab(*mode_, cfg_, keys_, reload, commands, lang);

    if (reload) {
        reload_config();
    }
    if (commands.apply_fov) {
        mode_->set_fov(commands.new_fov);
    }
    if (commands.toggle_freeze) {
        mode_->toggle_freeze();
    }
    if (commands.exit_mode) {
        mode_->leave("imgui");
        input_.reset();
    }
}

void PhotoModeMod::handle_toggle() {
    if (!mode_) return;
    if (mode_->active()) {
        mode_->leave("hotkey");
    } else if (!mode_->enter()) {
        port_->log(LogLevel::Error, "进入摄影模式失败：" + mode_->last_error());
    } else {
        // 进模式时把 PC/Pawn/HUD/相机/ViewTarget 全打一遍，真机排障靠这个
        port_->dump_engine_state();
        if (input_source_ != nullptr) {
            // raw=0 说明 Raw Input 没注册上（退化到 GetCursorPos 差分）；
            // 计数一直不涨说明输入根本没派发到本进程
            port_->log(LogLevel::Info, std::string("鼠标输入源 raw=") + (input_source_->raw_ready() ? "1" : "0") +
                                           " 事件计数=" + std::to_string(input_source_->mouse_event_count()));
        }
    }
    input_.reset();
}

// ---- PbzfmClient::Host（PBZFM 菜单框架桥） ----

auto PhotoModeMod::mod_dir() -> std::string { return mod_directory(); }

auto PhotoModeMod::current_config() -> Config {
    std::lock_guard<std::mutex> lock(mutex_);
    return cfg_;
}

auto PhotoModeMod::submit_config(const Config& cfg) -> void {
    std::lock_guard<std::mutex> lock(pbzfm_mutex_);
    pbzfm_pending_cfg_ = cfg;
    pbzfm_cfg_dirty_ = true;
}

auto PhotoModeMod::submit_command(const PbzfmCommand& cmd) -> void {
    std::lock_guard<std::mutex> lock(pbzfm_mutex_);
    pbzfm_commands_.push_back(cmd);
}

auto PhotoModeMod::log(LogLevel level, const std::string& msg) -> void {
    if (port_ != nullptr) port_->log(level, msg);
}

// PBZFM 菜单提交的配置与动作在这里消化（引擎 Tick 线程，持 mutex_）。
// 框架的渲染/变更回调在框架线程触发，那边只做数据提交，绝不直接碰引擎。
void PhotoModeMod::pbzfm_apply_pending() {
    Config incoming{};
    bool has_config = false;
    std::vector<PbzfmCommand> commands;
    {
        std::lock_guard<std::mutex> lock(pbzfm_mutex_);
        if (pbzfm_cfg_dirty_) {
            incoming = pbzfm_pending_cfg_;
            pbzfm_cfg_dirty_ = false;
            has_config = true;
        }
        commands.swap(pbzfm_commands_);
    }

    if (has_config) {
        // 三个有实时通道的开关记下旧值，改动了就地生效；其余字段走 set_config
        // （下一轮冻结/下次进入/每秒重试路径按新配置执行）
        const bool old_block = cfg_.block_game_input;
        const bool old_player = cfg_.hide_player;
        const bool old_hud = cfg_.hide_game_hud;
        cfg_ = incoming;
        keys_ = build_keymap(cfg_, nullptr);
        if (mode_) {
            mode_->set_config(cfg_);
            if (mode_->active()) {
                if (cfg_.hide_player != old_player) mode_->set_hide_player_live(cfg_.hide_player);
                if (cfg_.block_game_input != old_block) mode_->set_block_game_input_live(cfg_.block_game_input);
                if (cfg_.hide_game_hud != old_hud) mode_->set_hide_game_hud_live(cfg_.hide_game_hud);
            }
        }
        port_->log(LogLevel::Info, "PBZFM 菜单：设置已应用并写回 config.ini");
    }

    for (const PbzfmCommand& cmd : commands) {
        switch (cmd.action) {
            case PbzfmAction::ToggleMode:
                handle_toggle();
                break;
            case PbzfmAction::ToggleFreeze:
                if (mode_ && mode_->active()) mode_->toggle_freeze();
                break;
            case PbzfmAction::ResetView:
                // 重置走 tick 的 reset_view 分支（与 R 键完全同一条路径）
                if (mode_ && mode_->active()) {
                    Intent reset;
                    reset.reset_view = true;
                    mode_->tick(reset, 0.0);
                }
                break;
            case PbzfmAction::SetFov:
                if (mode_) mode_->set_fov(cmd.value);
                break;
            case PbzfmAction::ReloadConfig:
                reload_config();  // 内部会同步 PBZFM 菜单显示值
                break;
        }
    }

    // 诊断页快照（每帧覆盖写；全是标量与短字符串，代价可忽略）
    if (pbzfm_) {
        PbzfmSnapshot s;
        s.active = mode_ != nullptr && mode_->active();
        s.frozen = mode_ != nullptr && mode_->frozen();
        s.fov = mode_ != nullptr ? mode_->fov() : cfg_.fov_default;
        s.fov_min = cfg_.fov_min;
        s.fov_max = cfg_.fov_max;
        if (mode_ != nullptr) {
            s.pos[0] = mode_->pose().loc.x;
            s.pos[1] = mode_->pose().loc.y;
            s.pos[2] = mode_->pose().loc.z;
            s.pitch = mode_->pose().rot.pitch;
            s.yaw = mode_->pose().rot.yaw;
            s.roll = mode_->pose().rot.roll;
        }
        s.toggle_key = vk_to_name(keys_.toggle);
        s.freeze_key = vk_to_name(keys_.freeze);
        s.reset_key = vk_to_name(keys_.reset);
        s.roll_reset_key = vk_to_name(keys_.roll_reset);
        if (input_source_ != nullptr) s.pad_backend = input_source_->pad_backend();
        pbzfm_->publish_snapshot(s);
    }
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
    // 注意：toggle_freeze 由 PhotoMode::tick 统一处理，这里不能再调一次（会双翻转）
    mode_->tick(intent, dt);
}

// 每秒打一行输入诊断：真机出问题时，这一行就能判断是“输入没到”还是“到了但没生效”。
// 默认关闭（放 debug_input_diag.txt 开启）：摘要要走路反射、日志要落盘，摄影模式里每秒一次
void PhotoModeMod::log_input_diagnostics(const Intent& intent, double dt) {
    if (!input_diag_enabled_) return;
    if (!mode_ || !mode_->active() || !port_ || input_source_ == nullptr) return;
    diag_accum_ += dt;
    if (diag_accum_ < 1.0) return;
    diag_accum_ = 0.0;

    char buf[360]{};
    std::snprintf(buf, sizeof(buf),
                  "输入诊断 alt=%d dx=%.1f dy=%.1f 引擎增量峰值=(%.2f,%.2f) 滚轮钩子=%d 滚轮事件=%lu raw=%d/%lu "
                  "手柄槽位=%d 手柄=%s",
                  input_.alt_down() ? 1 : 0, intent.look_dx, intent.look_dy, engine_peak_dx_, engine_peak_dy_,
                  input_source_->ll_ready() ? 1 : 0, input_source_->ll_wheel_events(),
                  input_source_->raw_ready() ? 1 : 0, input_source_->mouse_event_count(),
                  input_source_->pad_slot(), input_source_->pad_backend().c_str());
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

    // 对话语音钩子：游戏加载后尽早挂到 PBZSoundFXSubsystem 的 Post 函数上，
    // 才能截获之后所有 PlayingID（冻结时的真暂停靠这份名单）
    voice_hook_accum_ += dt;
    if (voice_hook_accum_ >= 2.0 && voice_hook_attempts_ < kMaxVoiceHookRetries) {
        voice_hook_accum_ = 0.0;
        ++voice_hook_attempts_;
        if (!port_->install_voice_hooks() && voice_hook_attempts_ == kMaxVoiceHookRetries) {
            port_->log(LogLevel::Warn, "对话语音钩子重试多次仍未挂上，停止重试（冻结将退回时间缩放模式）");
        }
    }

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

    // PBZFM 菜单提交的配置与动作：在游戏线程消化（框架线程只提交数据）
    pbzfm_apply_pending();

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

    const Intent intent = input_.build(cfg_, keys_, dt);
    handle_intent(intent, dt);
    retry_view_attach(dt);
    ensure_axis_hook();
    log_input_diagnostics(intent, dt);
    input_.end_frame();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
