// UE4SS mod 主体：把输入/引擎事件翻译成 core 层 Intent，再把结果交给 PhotoMode。
// 游戏内无任何菜单界面（v1.8.0 起纯快捷键，v1.8.1 起代码彻底移除）。
#ifdef PBZPM_WITH_UE4SS

#include "mod_app.h"
#include <algorithm>

#include <imgui.h>

#include <UE4SSProgram.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UEngine.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>

#include <windows.h>
#include <tlhelp32.h>

#include "core/tuning.h"
#include "core/retry_policy.h"
#include "core/version.h"
#include "engine/process_freeze.h"
#include "engine/ue4ss_port.h"
#include "ui_tab.h"
#include "win_input.h"

namespace pbzpm {

// 输入/时间步长常量统一放在 core/tuning.h（与 core/photo_mode.cpp 共用一份）。
using tuning::kEngineMouseScale;

// 语音钩子重试上限已并入 core/tuning.h（数值与注释原样保留）。
using tuning::kMaxVoiceHookRetries;
// 引擎输入钩子重试：真实时间指数退避（0.5s → 1s → 2s → 4s → 8s 封顶），此后固定 8 秒一次，
// 一直重试（环境修好后无需重启游戏即可自愈）。旧实现按调用次数 +1.0 秒累加、累计超过 30 次就永久
// 放弃，且注释写的是「下一帧再试」，与实现不符——已一并改掉。
constexpr RetryBackoff kAxisHookBackoff{0.5, 8.0};
// 引擎鼠标增量接口连续不可用多久后告警：port 内的 *_missing_ 是「一次 miss 永久缓存」，
// 本模块（mod_app）无法清除，只能明确告诉用户需要重启游戏才能恢复。
constexpr double kEngineDeltaMissingWarnSeconds = 3.0;
namespace {

// 单调时钟：冻结时间时引擎 delta 会变成 0，但相机还要能动，所以 dt 用真实时间算
auto monotonic_seconds() -> double {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

// UE4SS 的引擎 Tick 回调是普通函数指针，用全局实例桥回对象。
// 原子指针：卸载路径先注销回调再把这里置空，晚到的派发只会看到 nullptr（H-5）。
std::atomic<PhotoModeMod*> g_mod{nullptr};

// STR() 是 L##x 拼接，参数必须已经是字符串字面量；PBZPM_VERSION_FULL 是宏，
// 且可能由多段字面量拼成，所以同样需要两级展开
#define PBZPM_WIDEN_IMPL(x) L##x
#define PBZPM_WIDEN(x) PBZPM_WIDEN_IMPL(x)

}  // namespace

PhotoModeMod::PhotoModeMod() {
    ModName = STR("PBZPhotoModeCpp");
    ModVersion = PBZPM_WIDEN(PBZPM_VERSION_FULL);
    ModDescription = STR("Photo mode: free camera / FOV / auto-pause time / ImGui tab");
    ModAuthors = STR("PBZPhotoMode");
    g_mod.store(this, std::memory_order_release);
}

PhotoModeMod::~PhotoModeMod() {
    // 先停画面定格 watcher（若它还挂着暂停，会在退出前恢复所有线程）。
    frame_freeze_stop_.store(true, std::memory_order_relaxed);
    if (frame_freeze_thread_.joinable()) frame_freeze_thread_.join();
    // watcher 退出时可能没等到引擎 Tick 处理“恢复音频”请求（例如游戏退出/模组卸载）：
    // 这里兜底唤醒只由 F7 挂起的 Wwise，避免卸载后整局静音。
    if (frame_freeze_wwise_owned_.exchange(false, std::memory_order_relaxed) && port_) {
        port_->suspend_wwise_engine(false);
    }
    // 卸载顺序（H-5，顺序不能改）：
    //   1) 先注销 UE4SS 钩子与 Engine Tick 回调：此后引擎不再派发新回调，避免回调进入正在
    //      拆卸的成员；注销失败会在这里打 Error 并说明残留后果。
    //   2) 再把 g_mod 置空：注销后偶有已入队的派发，也只看到 nullptr，不会解引用已析构对象。
    //   3) 停 PBZFM 线程：框架线程会调 host_->log()/current_config()，必须在 port_ 销毁前停。
    //   4) 卸载输入层：低级鼠标钩子是 Windows 全局回调，必须先于 DLL 卸载撤掉。
    //   5) 最后 mode_->leave()：恢复引擎状态（视角/时间缩放/隐藏项）仍需要 port_ 存活。
    //   6) 析构结束由成员析构顺序销毁 port_/mode_（delete 由 uninstall_mod 负责）。
    if (port_) {
        port_->unregister_all_hooks();
    }
    g_mod.store(nullptr, std::memory_order_release);

    if (pbzfm_) pbzfm_->stop();
    if (input_source_ != nullptr) {
        input_source_->uninstall();
    }
    if (mode_ && mode_->active()) {
        std::lock_guard<std::mutex> lock(mutex_);
        mode_->leave("mod unload");
    }
}

auto PhotoModeMod::mod_directory() -> std::string {
    auto directory = std::filesystem::path(RC::UE4SSProgram::get_program().get_mods_directory()) / "PBZPhotoModeCpp";
    return directory.string();
}

void PhotoModeMod::frame_freeze_watch_loop() {
    bool was_down = false;
    bool frozen = false;
    ULONGLONG frozen_since = 0;
    int registered_vk = 0;
    MSG msg{};
    // 先建立本线程的消息队列：RegisterHotKey 要求注册线程自己取消息。
    ::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

    auto ensure_hotkey = [&](int vk) {
        if (vk == 0 || vk == registered_vk) return;
        if (registered_vk != 0) {
            ::UnregisterHotKey(nullptr, 1);
            ::UnregisterHotKey(nullptr, 2);
        }
        registered_vk = 0;
        if (::RegisterHotKey(nullptr, 1, MOD_NOREPEAT, vk)) {
            registered_vk = vk;
        } else if (::RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_NOREPEAT, vk)) {
            registered_vk = vk;
        }
    };

    // 与引擎 Tick 握手：预制时间钩子/Wwise 挂起必须在游戏线程完成；这里只等结果。
    // 500ms 超时后仍然继续挂线程（时间补偿可能没装上，但不影响“画面定格”本身）。
    auto request_engine = [&](int request) -> int {
        frame_freeze_ack_.store(0, std::memory_order_relaxed);
        frame_freeze_request_.store(request, std::memory_order_release);
        const ULONGLONG deadline = ::GetTickCount64() + 500;
        while (!frame_freeze_stop_.load(std::memory_order_relaxed) && ::GetTickCount64() < deadline) {
            const int ack = frame_freeze_ack_.load(std::memory_order_acquire);
            if (ack != 0) return ack;
            ::Sleep(1);
        }
        return frame_freeze_ack_.load(std::memory_order_acquire);
    };

    auto resume_all = [&]() {
        const int threads = process_freezer_ ? process_freezer_->suspended_count() : 0;
        const int skipped = process_freezer_ ? process_freezer_->skipped_count() : 0;
        const unsigned long long duration =
            (frozen && frozen_since != 0) ? static_cast<unsigned long long>(::GetTickCount64() - frozen_since) : 0;
        if (process_freezer_) {
            // PBZ Pause 的顺序：先把本次定格时长累加进时间偏移，再 ResumeThread。
            process_freezer_->end_time_freeze();
            // 冻结期间低级鼠标钩子线程没停，用户为截图移动/框选的鼠标位移会累积；
            // 这里在恢复线程前清掉，避免恢复后相机按截图轨迹瞬移。
            if (input_source_) input_source_->discard_pending();
            process_freezer_->resume_all_threads();
        }
        if (frozen) {
            // 线程恢复后再让引擎 Tick 唤醒 Wwise（与 PBZ Pause 的 ResumeAllThreads -> WakeupFromSuspend 同序）。
            request_engine(2);
            frame_freeze_resume_threads_.store(threads, std::memory_order_relaxed);
            frame_freeze_resume_skipped_.store(skipped, std::memory_order_relaxed);
            frame_freeze_resume_duration_ms_.store(duration, std::memory_order_relaxed);
            frame_freeze_resume_report_.store(true, std::memory_order_release);
        }
        frozen = false;
        frozen_since = 0;
    };

    while (!frame_freeze_stop_.load(std::memory_order_relaxed)) {
        const int vk = frame_freeze_vk_.load(std::memory_order_relaxed);
        ensure_hotkey(vk);

        bool hotkey_pressed = false;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && (msg.wParam == 1 || msg.wParam == 2)) hotkey_pressed = true;
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        const bool down = vk != 0 && (::GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool trigger = hotkey_pressed || (down && !was_down);

        if (trigger) {
            if (!frozen) {
                if (frame_freeze_allowed_.load(std::memory_order_relaxed)) {
                    // 1) 让引擎 Tick 先做 Wwise Suspend（若摄影模式没挂）并安装时间补偿钩子。
                    // 2) 再挂起本进程除 watcher 外的所有线程：渲染线程停住后屏幕保持最后一帧。
                    // RegisterHotKey 保证即使其他线程全停，第二次 F7 也能通过 watcher 消息队列收到。
                    const int ack = request_engine(1);
                    if (ack == 1 && process_freezer_ != nullptr) {
                        // 只挂游戏引擎模块（PBZ.exe / MRTCEngine / Wwise）内的线程：
                        // 不碰 PBZFM、Steam/NVIDIA 覆盖层、本模组输入钩子等第三方线程。
                        // 否则 WH_KEYBOARD_LL/WH_MOUSE_LL 的安装线程被挂起，整个桌面的键鼠
                        // 输入会等 LowLevelHooksTimeout：Ctrl+P 截图和 F7 恢复都会卡。
                        process_freezer_->clear_suspend_modules();  // 保持“除跳过名单外全部挂起”
                        process_freezer_->clear_skip_targets();
                        if (input_source_) process_freezer_->add_skip_thread(input_source_->raw_thread_id());
                        if (HMODULE self_module = ::GetModuleHandleW(L"PBZPhotoModeCpp.dll")) {
                            process_freezer_->add_skip_module(self_module);
                        }
                        HMODULE pbzfm = ::GetModuleHandleW(L"PBZFM.dll");
                        if (pbzfm == nullptr) pbzfm = ::GetModuleHandleW(L"PBZFM");
                        if (pbzfm != nullptr) process_freezer_->add_skip_module(pbzfm);
                        // 其他已知的桌面覆盖层/输入钩子模块：它们的线程也绝不能挂起。
                        static const wchar_t* const kOtherHookModules[] = {
                            L"GameOverlayRenderer64.dll", L"GameOverlayRenderer.dll",
                            L"nvspcap64.dll", L"DiscordHook64.dll", L"RTSSHooks64.dll",
                            L"RivaTuner64.dll", L"MSIAfterburner.dll",
                        };
                        for (const wchar_t* name : kOtherHookModules) {
                            if (HMODULE module = ::GetModuleHandleW(name)) {
                                process_freezer_->add_skip_module(module);
                            }
                        }
                        const int count = process_freezer_->suspend_all_threads();
                        if (count > 0) {
                            process_freezer_->begin_time_freeze();
                            frozen = true;
                            frozen_since = ::GetTickCount64();
                        } else {
                            // 挂起失败（枚举不到线程/权限问题）：把 F7 可能挂起的 Wwise 还回去。
                            request_engine(2);
                        }
                    } else if (ack == 3) {
                        // 引擎 Tick 判定已退出摄影模式：不冻结。
                    } else {
                        // 准备超时/未确认：不冻结，并清掉可能已排队的准备请求，
                        // 避免引擎 Tick 在定格结束后才处理它、把 Wwise 挂在错误的时间。
                        request_engine(2);
                    }
                }
            } else {
                resume_all();
            }
        }
        // 安全阀：即使按键消息因系统/其他热键冲突完全收不到，也最多定格 N 秒后自动恢复，
        // 避免整机卡死在定格状态只能结束进程（默认 20 秒，可配置）。
        const unsigned long long timeout_ms = frame_freeze_timeout_ms_.load(std::memory_order_relaxed);
        if (frozen && frozen_since != 0 && timeout_ms > 0 &&
            static_cast<unsigned long long>(::GetTickCount64() - frozen_since) > timeout_ms) {
            resume_all();
        }
        was_down = down;
        ::Sleep(16);
    }
    // 退出线程前必须把还挂着暂停的线程恢复，否则 DLL 卸载后游戏会永远停住。
    resume_all();
}

void PhotoModeMod::handle_frame_freeze_request(const Config& cfg, bool mode_active) {
    const int request = frame_freeze_request_.exchange(0, std::memory_order_acq_rel);
    if (request == 0) return;

    // 恢复请求：只唤回“F7 自己挂起”的 Wwise；摄影模式自己挂的由 PhotoMode 的恢复路径处理。
    if (request == 2) {
        if (frame_freeze_wwise_owned_.exchange(false, std::memory_order_acq_rel) && port_) {
            if (!port_->suspend_wwise_engine(false)) {
                port_->log(LogLevel::Warn, "画面定格恢复：Wwise 音频引擎唤醒失败，保持挂起标记以便退出时重试");
            }
        }
        frame_freeze_ack_.store(2, std::memory_order_release);
        return;
    }
    if (request != 1) return;
    frame_freeze_wwise_owned_.store(false, std::memory_order_release);

    // 请求到达时若已退出摄影模式：让 watcher 不要挂线程（否则会把游戏永久停住）。
    if (!mode_active) {
        frame_freeze_ack_.store(3, std::memory_order_release);
        return;
    }

    // 时间补偿：只在 F7 全线程定格时启用。1.0.64~1.0.66 的教训是摄影模式
    // 保留渲染/相机线程时绝不能冻结 QPC，否则移动视角会规律性卡顿。
    ProcessFreezer::TimeHookReport hook_report{};
    if (cfg.frame_freeze_time_compensation && process_freezer_) {
        hook_report = process_freezer_->install_time_hooks();
    }

    bool wwise_owned = false;
    bool wwise_already = false;
    if (port_) {
        wwise_already = port_->wwise_engine_suspended();
        if (cfg.frame_freeze_audio_suspend && !wwise_already) {
            // render-anyway=true：只静音不毁语音状态；F7 结束后再由本函数唤醒。
            wwise_owned = port_->suspend_wwise_engine(true);
        }
    }
    frame_freeze_wwise_owned_.store(wwise_owned, std::memory_order_release);

    if (port_) {
        std::string message = "画面定格准备：";
        if (!cfg.frame_freeze_time_compensation) {
            message += "时间补偿=关闭（游戏恢复时可能有时间跳变）；";
        } else if (hook_report.installed) {
            message += "时间补偿=已安装（模块 " + std::to_string(hook_report.modules_patched) +
                       "，IAT 槽 " + std::to_string(hook_report.slots_patched) + "）；";
        } else {
            message += "时间补偿=不可用（找不到可改写的时间 API 导入）；";
        }
        if (wwise_owned) {
            message += "Wwise=已整引擎挂起；";
        } else if (wwise_already) {
            message += "Wwise=沿用摄影模式挂起；";
        } else {
            message += "Wwise=未挂起（音频线程将被直接停住）；";
        }
        port_->log(LogLevel::Info, message);
    }
    frame_freeze_ack_.store(1, std::memory_order_release);
}

void PhotoModeMod::reload_config() {
    const std::filesystem::path path = std::filesystem::path(mod_directory()) / "config.ini";
    const ConfigLoadResult result = load_config_file(path.string());
    cfg_ = result.cfg;
    // 渲染前写点不可用时不允许再启用接管（见 bind_engine_tick）：否则 F2 重载配置会把它翻回 true，
    // 又把摄影模式变成“镜头不动”。
    if (!gvc_tick_hook_ok_) cfg_.takeover_current_camera = false;
    keys_ = build_keymap(cfg_, nullptr);
    {
        const int frame_freeze_vk = vk_from_name(cfg_.key_frame_freeze);
        if (frame_freeze_vk != 0) frame_freeze_vk_.store(frame_freeze_vk, std::memory_order_relaxed);
        const double timeout_seconds = std::clamp(cfg_.frame_freeze_timeout_seconds, 5.0, 300.0);
        frame_freeze_timeout_ms_.store(static_cast<unsigned long long>(timeout_seconds * 1000.0),
                                       std::memory_order_relaxed);
    }

    if (port_) {
        for (const std::string& warning : result.warnings) {
            port_->log(LogLevel::Warn, "配置：" + warning);
        }
        port_->log(LogLevel::Info, "配置来源：" + result.source);
    }

    // 已废弃的键（[dof] 段、色差/胶片颗粒）不从文件里删掉就会一直留在用户机器上：
    // 解析时收集到 legacy_keys 后，这里按当前格式原子重写一次 config.ini，把它们物理清掉。
    if (!result.legacy_keys.empty()) {
        std::string names;
        const std::size_t shown = std::min<std::size_t>(result.legacy_keys.size(), 8);
        for (std::size_t i = 0; i < shown; ++i) {
            if (!names.empty()) names += ", ";
            names += result.legacy_keys[i];
        }
        if (result.legacy_keys.size() > shown) {
            names += " 等 " + std::to_string(result.legacy_keys.size()) + " 项";
        }
        std::string error;
        if (save_config_file(path.string(), cfg_, &error)) {
            port_->log(LogLevel::Info, "配置清理：已从 config.ini 删除 " + std::to_string(result.legacy_keys.size()) +
                                           " 个已废弃键并重写文件（" + names + "）");
        } else if (port_) {
            port_->log(LogLevel::Warn, "配置清理：删除已废弃键后重写 config.ini 失败：" + error);
        }
    }
    if (mode_) {
        mode_->set_config(cfg_);
    }
    if (pbzfm_) {
        pbzfm_->sync_from_config(cfg_);  // 菜单显示值跟着 config.ini 走
    }
}

void PhotoModeMod::bind_engine_tick() {
    if (tick_hook_installed_) return;  // 同一实例只挂一次（幂等）

    // 用带 FCallbackOptions 的重载：它返回 GlobalCallbackId，卸载时才能用
    // Hook::UnregisterCallback 真正注销；旧 void 重载注册后没有任何注销手段。
    // 代价是回调签名多一个 TCallbackIterationData<void>& 参数。
    RC::Unreal::Hook::FCallbackOptions pre_options{};
    pre_options.OwnerModName = STR("PBZPhotoModeCpp");
    pre_options.HookName = STR("EngineTickPre");
    const RC::Unreal::Hook::GlobalCallbackId pre_id = RC::Unreal::Hook::RegisterEngineTickPreCallback(
        [](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float delta_seconds, bool) {
            PhotoModeMod* mod = g_mod.load(std::memory_order_acquire);
            if (mod != nullptr) mod->on_engine_tick(static_cast<double>(delta_seconds));
        },
        pre_options);
    if (pre_id == RC::Unreal::Hook::ERROR_ID) {
        if (port_) {
            port_->track_global_failure("UE4SS.EngineTickPre");
            port_->log(LogLevel::Error, "Engine Tick Pre 回调注册失败（UE4SS 未安装对应 detour）");
        }
    } else if (port_) {
        port_->track_global_callback("UE4SS.EngineTickPre", pre_id);
    }

    // post 回调在所有 actor/PC 的 Tick 之后跑：在这里重新夺一次视角，
    // 就能压过游戏自己每帧设置 ViewTarget 的行为
    RC::Unreal::Hook::FCallbackOptions post_options{};
    post_options.OwnerModName = STR("PBZPhotoModeCpp");
    post_options.HookName = STR("EngineTickPost");
    const RC::Unreal::Hook::GlobalCallbackId post_id = RC::Unreal::Hook::RegisterEngineTickPostCallback(
        [](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float, bool) {
            PhotoModeMod* mod = g_mod.load(std::memory_order_acquire);
            if (mod != nullptr) mod->on_engine_tick_post();
        },
        post_options);
    if (post_id == RC::Unreal::Hook::ERROR_ID) {
        if (port_) {
            port_->track_global_failure("UE4SS.EngineTickPost");
            port_->log(LogLevel::Error, "Engine Tick Post 回调注册失败（UE4SS 未安装对应 detour）");
        }
    } else if (port_) {
        port_->track_global_callback("UE4SS.EngineTickPost", post_id);
    }

    // UE 帧顺序：World->Tick（所有 actor tick）→ PlayerController->UpdateCameraManager
    // （游戏重算 CameraCache.POV）→ GameViewportClient->Tick → RedrawViewports（画这一帧）。
    // 所以接管模式的 POV 只能写在 GVC Tick 之后、RedrawViewports 之前：
    // EngineTickPost 已在 RedrawViewports 之后（1.0.75 镜头完全不动就是写在这里导致的）。
    RC::Unreal::Hook::FCallbackOptions gvc_options{};
    gvc_options.OwnerModName = STR("PBZPhotoModeCpp");
    gvc_options.HookName = STR("GameViewportClientTickPost");
    RC::Unreal::Hook::GlobalCallbackId gvc_id = RC::Unreal::Hook::ERROR_ID;
    try {
        gvc_id = RC::Unreal::Hook::RegisterGameViewportClientTickPostCallback(
            [](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UGameViewportClient*, float) {
                PhotoModeMod* mod = g_mod.load(std::memory_order_acquire);
                if (mod != nullptr) mod->on_game_viewport_tick_post();
            },
            gvc_options);
    } catch (...) {
        // UE4SS 在 detour 元数据缺失/被配置禁用时会抛异常；不能让整个 Mod 初始化失败。
        gvc_id = RC::Unreal::Hook::ERROR_ID;
    }
    if (gvc_id == RC::Unreal::Hook::ERROR_ID) {
        if (port_) {
            port_->track_global_failure("UE4SS.GameViewportClientTickPost");
            port_->log(LogLevel::Error,
                       "GameViewportClient Tick Post 回调注册失败：接管模式无法在渲染前写入 POV，"
                       "镜头将完全无法移动；请在 config.ini [camera] 把 takeover_current_camera 设为 false");
        }
    } else if (port_) {
        port_->track_global_callback("UE4SS.GameViewportClientTickPost", gvc_id);
    }

    // 接管模式全部依赖上面这个回调：没有它，POV 只能写在 EngineTickPost（RedrawViewports 之后），
    // 赶不上当帧渲染 → 表现就是“镜头完全不动”。以前这里只记一条 Error 让用户自己去改 config.ini，
    // 一旦钩子真的不可用，摄影模式等于废掉。现在本会话直接降级到独立相机，保证还能用。
    gvc_tick_hook_ok_ = (gvc_id != RC::Unreal::Hook::ERROR_ID);
    if (!gvc_tick_hook_ok_ && cfg_.takeover_current_camera) {
        cfg_.takeover_current_camera = false;
        if (mode_) mode_->set_config(cfg_);
        if (port_) {
            port_->log(LogLevel::Warn,
                       "接管模式的渲染前写点不可用：本会话已自动降级为独立相机（spawn + SetViewTarget），"
                       "摄影模式仍可正常使用；要永久走独立相机可把 config.ini [camera] 的 "
                       "takeover_current_camera 设为 false");
        }
    }

    tick_hook_installed_ = true;
}

void PhotoModeMod::on_engine_tick_post() {
    if (!mode_ || !port_ || !mode_->active()) return;
    port_->reassert_view_target();
}

void PhotoModeMod::on_game_viewport_tick_post() {
    if (!mode_ || !port_) return;
    // 与引擎 tick 线程同一线程，但 imgui/框架线程可能正在 leave()：加锁避免接管状态竞争。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mode_->active()) return;
    // dilation 模式：游戏会在 World Tick 里把 TimeDilation 写回 1.0，必须在
    // GameViewportClient Tick 之后、RedrawViewports 之前每帧重申，否则人物会在
    // 两次每秒重申之间按正常速度移动（实机 19:58 日志：每秒“时间缩放被游戏改写”）。
    mode_->reassert_time_dilation_if_needed();
    // 暂停/处决演出会让相机管理器重新打开 16:9 画幅锁：必须在世界 Tick 之后、渲染之前压回，
    // 否则进入摄影模式后画面会突然变成 16:9，退出也无法恢复。
    mode_->reassert_aspect_ratio_if_needed();
    mode_->reassert_render_tuning_if_needed();
    // MPC 快照必须在 World Tick 之后写：游戏在 World Tick 里会覆盖参数，写在 pre-tick 会被下一帧盖回去。
    mode_->reassert_mpc_hold_if_needed();
    port_->write_takeover_pov_now();
    // 相机 POV 的 FPostProcessSettings 每帧由 UpdateCamera 重建，AA/动态模糊/Lumen 覆盖也在这里重申。
    port_->post_process_override_tick();
}

void PhotoModeMod::on_unreal_init() {
    reload_config();
    port_ = std::make_unique<Ue4ssPort>(std::filesystem::path(mod_directory()));
    process_freezer_ = std::make_unique<ProcessFreezer>();
    mode_ = std::make_unique<PhotoMode>(*port_, cfg_);
    runtime_ = std::make_unique<ModRuntime>(*port_, *mode_, cfg_, keys_);

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
    if (!frame_freeze_thread_.joinable()) {
        try {
            frame_freeze_thread_ = std::thread(&PhotoModeMod::frame_freeze_watch_loop, this);
            if (port_) {
                port_->log(LogLevel::Info,
                           "画面定格快捷键已启用：" + vk_to_name(frame_freeze_vk_.load(std::memory_order_relaxed)) +
                               "（摄影模式内按一次定格、再按恢复）");
            }
        } catch (...) {
            if (port_) port_->log(LogLevel::Warn, "画面定格快捷键线程启动失败；F7 画面定格不可用");
        }
    }
    last_tick_time_ = monotonic_seconds();
    std::string load_message =
        std::string("加载完成 v") + PBZPM_VERSION_FULL + "，按 " + vk_to_name(keys_.toggle);
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
    // imgui_tab 之前是一个没人读取的配置项（解析、dump、ini 示例都齐全，唯一缺消费者），
    // 于是面板永远无条件注册。这里补上唯一的消费点：false 时不注册该 tab。
    if (!cfg_.imgui_tab) {
        if (port_) port_->log(LogLevel::Info, "配置 imgui_tab=false：不注册 UE4SS 控制台面板（可写设置仍在 PBZFM 菜单页）");
        return;
    }
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
        // 初始化窗口期词典通常还没扫到：T() 在无词典时会原样返回键名，
        // 所以这里既要用词典键（否则非中文用户首帧看到中文），又要处理"只拿到键名"的情况。
        const std::string wait = pbzfm_->lang().T("#console_wait_init");
        const bool translated = !wait.empty() && wait.front() != '#';
        ImGui::TextUnformatted(translated ? wait.c_str() : "Waiting for the game engine to initialize...");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    pbzfm_->lang().RescanLanguages(mod_dir(), false);  // 内部 2 秒节流，热加载
    PbzmI18n& lang = pbzfm_->lang();
    const std::string state_text = mode_->active() ? lang.T("#console_state_on") : lang.T("#console_state_off");
    ImGui::TextUnformatted(state_text.c_str());
    if (cfg_.pad_enabled) {
        ImGui::Text(lang.T("#console_hotkey_pad_fmt").c_str(), vk_to_name(keys_.toggle).c_str(),
                    pad_combo_name(keys_.pad_toggle).c_str(), vk_to_name(keys_.freeze).c_str());
    } else {
        ImGui::Text(lang.T("#console_hotkey_fmt").c_str(), vk_to_name(keys_.toggle).c_str(),
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
    if (!runtime_) return;
    const ToggleOutcome outcome = runtime_->toggle();
    if (outcome == ToggleOutcome::Entered) {
        // 每次进入摄影模式都当作一个新的探测窗口：清掉引擎输入钩子的退避计数，
        // 让它在下一帧就重新尝试一次（不必等满 8 秒的封顶间隔）
        axis_hook_failures_ = 0;
        axis_hook_next_retry_at_ = 0.0;
        // 进模式时把 PC/Pawn/HUD/相机/ViewTarget 全打一遍，真机排障靠这个
        port_->dump_engine_state();
        if (input_source_ != nullptr) {
            // raw=0 说明 Raw Input 没注册上（退化到 GetCursorPos 差分）；
            // 计数一直不涨说明输入根本没派发到本进程
            port_->log(LogLevel::Info, std::string("鼠标输入源 raw=") + (input_source_->raw_ready() ? "1" : "0") +
                                           " 事件计数=" + std::to_string(input_source_->mouse_event_count()));
        }
    }
    // 进入失败时 ModRuntime 已打 Error 日志（含 PhotoMode::last_error()）
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
        // 渲染前写点不可用时，菜单也不能把接管打开：接管靠每帧在 GameViewportClient Tick 之后
        // 写 POV，钩子缺失时写早了会表现为「镜头完全不动」。这里按 false 应用（菜单随即回显为关，
        // 与实际情况一致），而不是让用户点开一个必然失效的开关。
        if (!gvc_tick_hook_ok_ && incoming.takeover_current_camera) {
            incoming.takeover_current_camera = false;
            port_->log(LogLevel::Warn,
                       "接管不可用（缺 GameViewportClientTickPost 钩子）：菜单里的接管开关已按关闭处理");
        }
        // 需要实时通道的开关由 ModRuntime 按差异就地生效（含相机后端热切换）；
        // 其余字段走 set_config（下一轮冻结/下次进入/每秒重试路径按新配置执行）
        if (runtime_) runtime_->apply_config(incoming);
        // 画面定格热键在独立 watcher 线程里，不走 keys_：菜单里改了 key_frame_freeze
        // 必须同步原子量，否则要等下一次 reload_config 才换键。
        const int freeze_vk = vk_from_name(incoming.key_frame_freeze);
        if (freeze_vk != 0) frame_freeze_vk_.store(freeze_vk, std::memory_order_relaxed);
        frame_freeze_timeout_ms_.store(
            static_cast<unsigned long long>(std::clamp(incoming.frame_freeze_timeout_seconds, 5.0, 300.0) * 1000.0),
            std::memory_order_relaxed);
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
        s.free_look_key = vk_to_name(keys_.free_look);
        s.move_forward_key = vk_to_name(keys_.move_forward);
        s.move_back_key = vk_to_name(keys_.move_back);
        s.move_left_key = vk_to_name(keys_.move_left);
        s.move_right_key = vk_to_name(keys_.move_right);
        s.boost_key = vk_to_name(keys_.boost);
        s.slow_key = vk_to_name(keys_.slow);
        s.roll_left_key = vk_to_name(keys_.roll_left);
        s.roll_right_key = vk_to_name(keys_.roll_right);
        s.wheel_fov = cfg_.wheel_fov;
        if (input_source_ != nullptr) s.pad_backend = input_source_->pad_backend();
        pbzfm_->publish_snapshot(s);
    }
}

// 引擎输入钩子：PlayerController 只有进游戏后才有，所以进摄影模式后开始尝试。
// 重试节奏（真实时间，不是按调用次数累加）：第 1 次失败后等 0.5s，之后 1s / 2s / 4s / 8s 封顶，
// 此后固定每 8 秒一次，一直重试——环境修好后无需重启游戏即可自愈（旧实现累计 30 次就永久放弃）。
// 成功一次后 axis_hook_done_ 置位，之后每帧只做一次布尔判断，成功路径没有任何额外开销。
void PhotoModeMod::ensure_axis_hook() {
    if (axis_hook_done_ || !port_ || !mode_ || !mode_->active()) return;
    const double now = monotonic_seconds();
    if (!retry_due(now, axis_hook_next_retry_at_)) return;  // 退避未到点，本帧直接跳过（不碰引擎）

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
        ++axis_hook_failures_;
        axis_hook_next_retry_at_ = now + kAxisHookBackoff.delay_seconds(axis_hook_failures_);
        return;
    }
    axis_hook_done_ = true;
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
    if (!runtime_) return;

    if (intent.toggle_mode) {
        handle_toggle();
        return;
    }
    // 注意：toggle_freeze 由 PhotoMode::tick 统一处理，这里不能再调一次（会双翻转）
    // inactive 但 recovery_pending 时也要进 tick：退出后的恢复失败要按退避节奏重试
    runtime_->dispatch(intent, dt);
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
                  "输入诊断 free_look=%d dx=%.1f dy=%.1f 引擎增量峰值=(%.2f,%.2f) 滚轮钩子=%d 滚轮事件=%lu raw=%d/%lu "
                  "手柄槽位=%d 手柄=%s",
                  input_.held(keys_.free_look) ? 1 : 0, intent.look_dx, intent.look_dy, engine_peak_dx_, engine_peak_dy_,
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
    if (dt > tuning::kMaxFrameDt) dt = tuning::kMaxFrameDt;  // 卡帧保护，避免相机瞬移
    (void)delta_seconds;

    // ---- 共享状态与配置快照（M-9）----
    // 线程归属：
    //   cfg_ / keys_ 的写者 = 本线程的 pbzfm_apply_pending()（持 mutex_）+ 渲染线程
    //     render_tab_impl() → reload_config()（持 mutex_）；读者 = 本函数、
    //     PBZFM 框架线程的 current_config()（持 mutex_）。
    //     所以本函数在入口持锁一次性拷出 cfg_snap / keys_snap，之后整帧只读这份副本，
    //     锁外阶段不再直接读 cfg_ / keys_（锁外读 std::string / std::vector 会与写者构成数据竞争）。
    //   mode_ / port_：只在入口快照里持锁读一次 active，其余访问一律留在下面的持锁段。
    //   input_ / input_source_ / 各计时成员：只有引擎 Tick 线程访问（渲染线程不碰）。
    //   axis_dx_ / axis_dy_ / axis_wheel_：引擎 InputAxis 回调线程写（axis_mutex_ 保护），
    //     本线程在持锁段内取走。
    //   playing_ids_：ue4ss_port.cpp 内由语音钩子回调线程写、tick 线程读，目前没有加锁，
    //     属于待处理的竞态点（本轮按约定不改 ue4ss_port，只在此标注）。
    // 取舍：每帧持锁拷一份 Config / KeyMap（约 30 个短字符串基本走 SSO，只有少量堆分配），
    // 换到锁外阶段完全不碰共享配置。反方案（把语音钩子安装 / UI 修复 / 输入轮询挪进锁内）
    // 会让这些走引擎反射的步骤拖长渲染线程等锁的时间，所以不用。
    // 副作用：本帧开头 PBZFM 菜单刚改的配置从下一帧生效（差 1 帧，肉眼不可见）；
    // 三个实时开关（hide_player / block_game_input / hide_game_hud）仍由 pbzfm_apply_pending
    // 当帧直接下发到 mode_，不受影响。
    Config cfg_snap;
    KeyMap keys_snap;
    bool mode_active_snap = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cfg_snap = cfg_;
        keys_snap = keys_;
        mode_active_snap = mode_->active();
    }
    const Config& cfg = cfg_snap;
    const KeyMap& keys = keys_snap;
    // 画面定格热键只在摄影模式内生效；watcher 线程只读这个原子量。
    frame_freeze_allowed_.store(mode_active_snap, std::memory_order_relaxed);
    // F7 画面定格的准备/恢复请求（Wwise 挂起、时间钩子安装）都在本线程执行；
    // watcher 只负责挂起/恢复线程，绝不能在挂起期间碰 UE/日志对象。
    handle_frame_freeze_request(cfg, mode_active_snap);
    if (frame_freeze_resume_report_.exchange(false, std::memory_order_acquire)) {
        std::string message = "画面定格已恢复：恢复 " +
                              std::to_string(frame_freeze_resume_threads_.load(std::memory_order_relaxed)) +
                              " 个线程（跳过 " +
                              std::to_string(frame_freeze_resume_skipped_.load(std::memory_order_relaxed)) +
                              " 个非游戏/输入钩子线程），定格 " +
                              std::to_string(frame_freeze_resume_duration_ms_.load(std::memory_order_relaxed)) + " ms";
        if (process_freezer_ && process_freezer_->time_hooks_installed()) {
            message += "（时间轴已扣掉定格时长）";
        }
        port_->log(LogLevel::Info, message);
    }

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

    // 1.0.69 一次性修复：旧版本可能把字幕/场景交互提示开关写坏（1.0.30 起的老问题），
    // 首次运行本版时把两个开关强制打开。设置对象/玩家 Pawn 可能比引擎 Tick 晚就绪：
    // 每 3 秒试一次、上限 20 次（约 1 分钟，覆盖读档时间）。
    if (!ui_repair_done_) {
        if (!cfg.repair_ui_settings_once) {
            ui_repair_done_ = true;  // 配置关闭：不再尝试
        } else if (ui_repair_attempts_ < 20 && !mode_active_snap) {
            // 摄影模式内不要做这项修复：模式内正把字幕/提示开关压成关闭，此时强开会和退出还原打架。
            ui_repair_accum_ += dt;
            if (ui_repair_accum_ >= 3.0) {
                ui_repair_accum_ = 0.0;
                ++ui_repair_attempts_;
                ui_repair_done_ = port_->recover_ui_settings_once();
                if (!ui_repair_done_ && ui_repair_attempts_ >= 20) {
                    port_->log(LogLevel::Warn, "UI 设置修复多次重试未成功（设置对象没就绪），跳过；可在游戏设置里手动打开字幕/场景交互提示");
                }
            }
        } else {
            ui_repair_done_ = true;
        }
    }

    // PBZFM 菜单是“同一进程内的覆盖窗”：Win32InputSource 的 foreground_is_ours()
    // 对它同样返回真，而鼠标键走 GetAsyncKeyState 全量轮询（VK 1..255）——
    // 菜单里的左键会直接命中 key_vertical_up（默认 MOUSE_LEFT）→ 相机上升。
    // 菜单可见期间暂停采集（框架 v1+ 导出 PBZFM_IsMenuOpen），关菜单后恢复。
    const bool pbzfm_menu_open = pbzfm_ != nullptr && pbzfm_->menu_open();
    if (pbzfm_menu_open != menu_open_last_) {
        menu_open_last_ = pbzfm_menu_open;
        port_->log(LogLevel::Info,
                   pbzfm_menu_open ? "PBZFM 菜单已打开：摄影模式输入暂停（菜单里的键鼠/手柄不再驱动相机）"
                                   : "PBZFM 菜单已关闭：摄影模式输入恢复");
    }

    if (input_source_ != nullptr) {
        input_source_->set_ui_open(pbzfm_menu_open);
        input_source_->poll(input_, cfg.pad_enabled);
        // DInput 后端的一次性诊断（接入成功/初始化失败时各产出一条）
        std::string pad_diag;
        if (input_source_->take_pad_dinput_diag(pad_diag)) {
            port_->log(LogLevel::Info, pad_diag);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 手柄插拔日志：只在状态变化时打一行，方便确认手柄模式是否可用
    if (input_source_ != nullptr && cfg.pad_enabled) {
        const bool connected = input_source_->pad_connected();
        if (connected != pad_logged_connected_) {
            pad_logged_connected_ = connected;
            if (connected) {
                port_->log(LogLevel::Info, std::string("手柄已连接（") + input_source_->pad_backend() +
                                               "）：手柄 " + pad_combo_name(keys.pad_toggle) + " 进入摄影模式");
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
    if (mode_ != nullptr && mode_->active() && !pbzfm_menu_open) {
        if (port_->read_mouse_delta(engine_dx_, engine_dy_)) {
            engine_delta_ok_ = true;
            engine_delta_missing_accum_ = 0.0;
        } else {
            // 持续失败：ue4ss_port 对 GetInputMouseDelta 的函数查找是「一次 miss 永久缓存」
            //（M-11）。本模块没有清除入口（flags 是 port 的私有成员，本轮不改 ue4ss_port），
            // 只能按需求打一条明确告警；3 秒足以排除刚进模式时对象尚未就绪的短暂失败。
            engine_delta_missing_accum_ += dt;
            if (!engine_delta_missing_warned_ && engine_delta_missing_accum_ >= kEngineDeltaMissingWarnSeconds) {
                engine_delta_missing_warned_ = true;
                port_->log(LogLevel::Warn,
                           "引擎鼠标增量接口 PlayerController::GetInputMouseDelta 持续不可用："
                           "该函数的查找结果已被缓存为「缺失」，运行中无法重新探测，"
                           "需要重启游戏才能恢复鼠标；系统级鼠标输入不受影响");
            }
        }
    } else {
        engine_delta_missing_accum_ = 0.0;  // 不在摄影模式不累计；再次进模式后重新计时
        engine_dx_ = 0.0;                   // 菜单打开时不能残留上一帧的增量反复应用
        engine_dy_ = 0.0;
    }

    // 引擎侧钩子的增量先并进来（同一帧内多条轴事件会被累加）
    {
        std::lock_guard<std::mutex> axis_lock(axis_mutex_);
        if (pbzfm_menu_open) {
            // 菜单打开：InputAxis 回调可能仍在写入，整帧丢弃，避免关菜单后累积跳变
            axis_dx_ = 0.0;
            axis_dy_ = 0.0;
            axis_wheel_ = 0.0;
        } else {
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

    const Intent intent = input_.build(cfg, keys, dt);
    handle_intent(intent, dt);
    retry_view_attach(dt);
    ensure_axis_hook();
    log_input_diagnostics(intent, dt);
    input_.end_frame();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
