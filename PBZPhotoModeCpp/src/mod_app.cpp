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
#include <mutex>
#include <string>

#include "core/menu_model.h"
#include "core/version.h"
#include "engine/ue4ss_port.h"
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

}  // namespace

PhotoModeMod::PhotoModeMod() {
    ModName = STR("PBZPhotoModeCpp");
    ModVersion = PBZPM_WIDEN(PBZPM_VERSION);
    ModDescription = STR("Photo mode: free camera / FOV / auto-pause time / ImGui tab");
    ModAuthors = STR("PBZPhotoMode");
    g_mod = this;
}

PhotoModeMod::~PhotoModeMod() {
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

    bind_engine_tick();
    last_tick_time_ = monotonic_seconds();
    std::string load_message =
        std::string("加载完成 v") + PBZPM_VERSION + "，按 " + vk_to_name(keys_.toggle);
    if (cfg_.pad_enabled) {
        load_message += " 或手柄 " + pad_combo_name(keys_.pad_toggle);
        load_message += input_source_->pad_api_ready()
                            ? "（XInput 已加载：" + input_source_->pad_api_module() + "）"
                            : "（XInput 加载失败，手柄功能不可用！）";
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
        mode_->toggle_menu();
    }
    // 注意：toggle_freeze 由 PhotoMode::tick 统一处理，这里不能再调一次（会双翻转）
    mode_->tick(intent, dt);
}

void PhotoModeMod::update_overlay() {
    if (!mode_ || !port_) return;

    // 菜单关掉 / 退出模式时把屏幕上的字擦掉（HUD 只在有内容时画）
    if (!mode_->active() || !mode_->menu_visible()) {
        port_->hide_overlay();
        return;
    }

    const MenuModel model = build_menu_model(*mode_, keys_);
    const bool ok = port_->show_overlay(render_menu_text_ascii(model));
    if (ok != overlay_ok_) {
        overlay_ok_ = ok;
        if (!ok) {
            port_->log(LogLevel::Warn, "游戏内菜单暂不可用（HUD 还没建好），稍后会自动重试");
        } else {
            port_->log(LogLevel::Info, "游戏内菜单已显示（HUD Canvas 或 PrintString 兜底）");
        }
    }
}

// 每秒打一行输入诊断：真机出问题时，这一行就能判断是“输入没到”还是“到了但没生效”
void PhotoModeMod::log_input_diagnostics(const Intent& intent, double dt) {
    if (!mode_ || !mode_->active() || !port_ || input_source_ == nullptr) return;
    diag_accum_ += dt;
    if (diag_accum_ < 1.0) return;
    diag_accum_ = 0.0;

    char buf[320]{};
    std::snprintf(buf, sizeof(buf),
                  "输入诊断 alt=%d dx=%.1f dy=%.1f 引擎增量峰值=(%.2f,%.2f) 滚轮钩子=%d 滚轮事件=%lu raw=%d/%lu "
                  "HUD回调=%u 手柄槽位=%d",
                  input_.alt_down() ? 1 : 0, intent.look_dx, intent.look_dy, engine_peak_dx_, engine_peak_dy_,
                  input_source_->ll_ready() ? 1 : 0, input_source_->ll_wheel_events(),
                  input_source_->raw_ready() ? 1 : 0, input_source_->mouse_event_count(),
                  port_->overlay_hook_hits(), input_source_->pad_slot());
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
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 手柄插拔日志：只在状态变化时打一行，方便确认手柄模式是否可用
    if (input_source_ != nullptr && cfg_.pad_enabled) {
        const bool connected = input_source_->pad_connected();
        if (connected != pad_logged_connected_) {
            pad_logged_connected_ = connected;
            if (connected) {
                port_->log(LogLevel::Info, std::string("手柄已连接（XInput 槽位 ") +
                                                   std::to_string(input_source_->pad_slot()) + "）：手柄 " +
                                                   pad_combo_name(keys_.pad_toggle) + " 进入摄影模式");
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
                    ? std::string("手柄扫描正常：已连接（槽位 ") + std::to_string(input_source_->pad_slot()) + "）"
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
    handle_intent(intent, dt);
    retry_view_attach(dt);
    ensure_axis_hook();
    log_input_diagnostics(intent, dt);
    input_.end_frame();
    update_overlay();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
