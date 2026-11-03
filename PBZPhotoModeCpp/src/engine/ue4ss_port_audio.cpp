// =============================================================================
//  Ue4ssPort 成员函数实现：Wwise 音频（组件 / 逐语音 PlayingID / 整引擎 / 钩子）
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

// Wwise 音频组件：对话语音走 Wwise 时世界暂停拦不住。这里只做两件事：
// 数量诊断（每次冻结顺带）与可选的一次性静音（stop_wwise_on_freeze，停止后不可恢复）。
int Ue4ssPort::wwise_component_count() {
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), components);
    probe_wwise_components(components);
    return static_cast<int>(components.size());
}

// 对话语音双保险（两层都做，静音不依赖任何单一通道）：
//   第一层 截获的 PlayingID → 游戏自有 PBZPausePlayingID / PBZResumePlayingID
//   （Wwise 原生暂停，解冻后当前句从原位置续播）；
//   第二层 组件音量归零/恢复——盖住没截获到的声音（钩子装好前已在播的、
//   绕过子系统直接走集成层的）。
int Ue4ssPort::pause_wwise_voices(bool pause) {
    if (pause) {
        // 对话 Flow 只读诊断（1.0.74）：钩子尽早安装；冻结期记录 ForceFinishAudio/StopAudio 是否被调用。
        // 只加日志，不改任何播放行为。
        install_dialogue_debug_hooks();
        dialogue_flow_diag_active_ = true;
        dialogue_flow_event_count_ = 0;
    }
    // 返回值只统计「逐 PlayingID 实际执行数」：core 用它判断是否必须落整引擎 Suspend 兜底，
    // 组件音量兜底不算（它只静音，不能保证当前句停在原位置）。1.0.73 实机教训：
    // 返回 total（含组件音量）会让 core 误以为逐语音已生效。
    int voice_acted = 0;
    // 钩子线程在写、这里在 Tick 线程读：先取快照再调用引擎，避免持锁跨越 ProcessEvent
    //（钩子回调可能重入），也避免遍历途中 vector 被 push_back 重分配。
    std::vector<int32_t> playing_ids_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ids_snapshot = playing_ids_;
    }
    if (!playing_ids_snapshot.empty()) {
        UObject* soundfx = nullptr;
        {
            std::vector<UObject*> subsystems;
            RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZSoundFXSubsystem"), subsystems);
            for (UObject* candidate : subsystems) {
                if (is_ok(candidate)) {
                    soundfx = candidate;
                    break;
                }
            }
        }
        if (soundfx != nullptr) {
            const char* fn = pause ? "PBZPausePlayingID" : "PBZResumePlayingID";
            UFunction* fn_ptr = find_function(soundfx, fn);
            int acted = 0;
            if (pause) {
                // 暂停：清空上一次的记录，逐 ID 暂停后立刻读位置（只读诊断，不改播放行为）
                voice_positions_at_pause_.clear();
                UObject* world_ctx = world();
                for (int32_t id : playing_ids_snapshot) {
                    if (invoke_pause_id(soundfx, fn, id, 0.0, world_ctx)) ++acted;  // FadeTime=0：立即暂停
                    double position_ms = 0.0;
                    if (read_playing_id_position_ms(soundfx, id, world_ctx, position_ms)) {
                        voice_positions_at_pause_.emplace_back(id, position_ms);
                    }
                }
                // 位置接口不可用/签名不符：只提示一次；诊断缺失不影响暂停本身
                if (voice_positions_at_pause_.empty() && !voice_position_probe_logged_) {
                    voice_position_probe_logged_ = true;
                    log(LogLevel::Info,
                        find_function(soundfx, "GetPlayingIDPositionMs") == nullptr
                            ? std::string("语音位置校验：PBZSoundFXSubsystem 无 GetPlayingIDPositionMs（仅提示一次）")
                            : std::string("语音位置校验：GetPlayingIDPositionMs 签名不符或位置不可读（仅提示一次）"));
                }
            } else {
                // 恢复：必须在 PBZResumePlayingID 之前再读位置——声音还停着时 delta 才能证明
                // 暂停是否真的冻住播放头；统计完再逐 ID 恢复。
                int readable = 0;
                int held = 0;
                int advanced = 0;
                double max_advance_ms = 0.0;
                UObject* world_ctx = world();
                for (const auto& entry : voice_positions_at_pause_) {
                    double position_ms = 0.0;
                    if (!read_playing_id_position_ms(soundfx, entry.first, world_ctx, position_ms)) continue;
                    ++readable;
                    const double delta = position_ms - entry.second;
                    if (delta > 100.0) {
                        ++advanced;
                        if (delta > max_advance_ms) max_advance_ms = delta;
                    } else if (delta >= -100.0) {
                        ++held;  // |delta| <= 100ms：位置没变（允许计时抖动）
                    }
                }
                if (!voice_positions_at_pause_.empty()) {
                    char buf[220]{};
                    std::snprintf(buf, sizeof(buf),
                                  "语音位置校验：可读 %d 个，暂停期位置未变 %d 个，前进 %d 个（最大 +%.0f ms）",
                                  readable, held, advanced, max_advance_ms);
                    log(LogLevel::Info, buf);
                    if (advanced > 0) log(LogLevel::Warn, "语音暂停未冻住：当前句在冻结期继续前进");
                }
                for (int32_t id : playing_ids_snapshot) {
                    if (invoke_pause_id(soundfx, fn, id, 0.0, world_ctx)) ++acted;  // FadeTime=0：立即恢复
                }
                voice_positions_at_pause_.clear();
            }
            if (acted == 0 && fn_ptr != nullptr && !pause_signature_logged_) {
                pause_signature_logged_ = true;
                log(LogLevel::Warn, std::string(fn) + " 参数签名=" + describe_params(fn_ptr) +
                                        "（全部调用被签名校验拦下，回落音量兜底）");
            }
            log(LogLevel::Info, std::string(pause ? "对话语音暂停" : "对话语音恢复") + "（PlayingID）：截获 " +
                                    std::to_string(playing_ids_snapshot.size()) + "，执行 " + std::to_string(acted));
            voice_acted = acted;
        }
    }

    // 第二层（兜底）：组件音量归零/恢复——盖住没截获到的声音；不计入返回值
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), components);
    probe_wwise_components(components);
    const double volume = pause ? 0.0 : 1.0;
    int acted = 0;
    for (UObject* component : components) {
        if (!is_ok(component)) continue;
        if (invoke_set_number(component, "SetOutputBusVolume", volume)) ++acted;
    }
    log(LogLevel::Info, std::string(pause ? "Wwise 组件静音（音量兜底）" : "Wwise 组件恢复（音量兜底）") +
                            "：组件 " + std::to_string(components.size()) + "，执行 " + std::to_string(acted));
    if (!pause) dialogue_flow_diag_active_ = false;  // 恢复完成，停止冻结期诊断计数
    return voice_acted;
}

// Wwise 整引擎挂起：AK::SoundEngine::Suspend(true,false) / WakeupFromSuspend(0)。
// Wwise 2024.1 签名 Suspend(bool in_bRenderAnyway, bool in_bFadeOut)：in_bRenderAnyway=true 时
// 音频线程继续处理（事件队列/语音流状态保留）只是不输出；false 会把整个音频线程硬停，
// 解冻后已经播到一半的语句会丢状态、不能原位置续播（1.0.66 起的实机问题）。
constexpr int kAkResultSuccess = 1;  // Wwise AKRESULT：AK_Success = 1（不是 0）
// M-8：导出名是 MSVC C++ mangled 名，会随 Wwise 版本/ABI 变化。候选表按“新→旧”排列，
// 逐个 GetProcAddress；全部失败才回落逐语音/组件暂停。新增候选时保持签名兼容。
// Suspend 候选分别是 (bool,bool) / (bool) / ()；x64 下多传的寄存器参数会被被调用方忽略。
const char* const kAkSuspendCandidates[] = {
    "?Suspend@SoundEngine@AK@@YA?AW4AKRESULT@@_N0@Z",  // 2024.1+ (in_bRenderAnyway, in_bFadeOut)
    "?Suspend@SoundEngine@AK@@YA?AW4AKRESULT@@_N@Z",   // 旧版 (in_bFadeOut)
    "?Suspend@SoundEngine@AK@@YA?AW4AKRESULT@@XZ",     // 更旧/裁剪版 ()
    nullptr,
};
const char* const kAkWakeupCandidates[] = {
    "?WakeupFromSuspend@SoundEngine@AK@@YA?AW4AKRESULT@@I@Z",  // (uint32)
    "?WakeupFromSuspend@SoundEngine@AK@@YA?AW4AKRESULT@@XZ",   // ()
    nullptr,
};
const char* const kAkIsInitializedCandidates[] = {
    "?IsInitialized@SoundEngine@AK@@YA_NXZ",
    nullptr,
};

template <typename Fn>
Fn find_export(HMODULE module, const char* const* candidates) {
    for (const char* const* name = candidates; *name != nullptr; ++name) {
        if (FARPROC address = GetProcAddress(module, *name); address != nullptr) {
            return reinterpret_cast<Fn>(address);
        }
    }
    return nullptr;
}

// 旧版/部分安装从 AkSoundEngineDLL.dll 取导出；当前 PBZ 把 Wwise 静态链接进主程序，
// 同一组 C++ 符号从 PBZ.exe 导出——两条路径都试，避免像 1.0.65 那样静默落回兜底。
// 成功时一次覆盖环境/音乐/语音/UI 全部声音；失败返回 false，由 core 回落逐 PlayingID/组件路径。
bool Ue4ssPort::resolve_wwise_engine() {
    if (wwise_engine_resolved_) return ak_suspend_ != nullptr;

    auto try_module = [this](HMODULE module, const char* label) -> bool {
        if (module == nullptr) return false;
        auto suspend = find_export<AkSuspendFn>(module, kAkSuspendCandidates);
        auto wakeup = find_export<AkWakeupFn>(module, kAkWakeupCandidates);
        auto initialized = find_export<AkIsInitializedFn>(module, kAkIsInitializedCandidates);
        if (suspend == nullptr || wakeup == nullptr) return false;
        ak_suspend_ = suspend;
        ak_wakeup_ = wakeup;
        ak_is_initialized_ = initialized;
        wwise_engine_module_ = label;
        return true;
    };

    // 1) 当前 PBZ：Wwise 静态链接进 PBZ.exe，符号由主程序导出（优先，避免误选未初始化的独立 DLL）
    if (try_module(GetModuleHandleW(nullptr), "PBZ.exe（静态链接）")) {
        wwise_engine_resolved_ = true;
        log(LogLevel::Info, "Wwise 整引擎接口已解析：" + wwise_engine_module_);
        return true;
    }
    // 2) Wwise 作为独立 DLL 加载（PBZPause 原方案；旧版游戏或某些安装形态）
    if (try_module(GetModuleHandleW(L"AkSoundEngineDLL.dll"), "AkSoundEngineDLL.dll")) {
        wwise_engine_resolved_ = true;
        log(LogLevel::Info, "Wwise 整引擎接口已解析：" + wwise_engine_module_);
        return true;
    }

    if (!wwise_engine_failure_logged_) {
        wwise_engine_failure_logged_ = true;
        log(LogLevel::Error,
            "Wwise 整引擎接口未找到（PBZ.exe 与 AkSoundEngineDLL.dll 都没有匹配的 Suspend/WakeupFromSuspend 导出，"
            "非 MSVC 构建或 Wwise 改名都会触发）；当前只剩逐语音暂停，可能出现画面冻结但声音继续");
    }
    return false;  // 模块还没加载/初始化：下次冻结再试
}

bool Ue4ssPort::suspend_wwise_engine(bool suspend) {
    if (suspend) {
        if (!resolve_wwise_engine()) return false;
        if (ak_is_initialized_ != nullptr && !ak_is_initialized_()) {
            if (!wwise_engine_failure_logged_) {
                wwise_engine_failure_logged_ = true;
                log(LogLevel::Warn, "Wwise 音频引擎尚未初始化，先用逐语音/组件暂停");
            }
            return false;
        }
        // in_bRenderAnyway=true：音频继续渲染处理但不输出，保住 Wwise 事件队列和语音流状态；
        // in_bFadeOut=false：立即静音，不做淡出（摄影模式进入要干脆）。
        const int result = ak_suspend_ != nullptr ? ak_suspend_(true, false) : -1;
        if (result != kAkResultSuccess) {
            log(LogLevel::Warn, "Wwise 整引擎挂起失败（AKRESULT=" + std::to_string(result) +
                                    "），回落逐语音/组件暂停");
            return false;
        }
        wwise_engine_suspended_ = true;
        log(LogLevel::Info, "Wwise 音频引擎已静音挂起（AK::SoundEngine::Suspend(true,false)：render-anyway 保语音状态，不输出）（接口=" +
                                wwise_engine_module_ + "）");
        return true;
    }

    if (!wwise_engine_suspended_) return false;
    const int result = ak_wakeup_ != nullptr ? ak_wakeup_(0) : -1;
    if (result != kAkResultSuccess) {
        // H-1：唤醒失败必须保留挂起标记，否则下一次重试会直接早退，整局静音且再无机会恢复
        log(LogLevel::Warn, "Wwise 音频引擎唤醒失败（AKRESULT=" + std::to_string(result) +
                                "），保持挂起标记以便重试");
        return false;
    }
    wwise_engine_suspended_ = false;
    log(LogLevel::Info, "Wwise 音频引擎已唤醒（WakeupFromSuspend(0)）");
    return true;
}

// 钩住 PBZSoundFXSubsystem 的 Post 系列函数...
// 记下来冻结时才能真暂停。游戏加载后尽早挂（mod_app 的引擎 tick 里每 2 秒重试）。
bool Ue4ssPort::install_voice_hooks() {
    if (voice_hooks_installed_) return true;
    UObject* soundfx = nullptr;
    {
        std::vector<UObject*> subsystems;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZSoundFXSubsystem"), subsystems);
        for (UObject* candidate : subsystems) {
            if (is_ok(candidate)) {
                soundfx = candidate;
                break;
            }
        }
    }
    if (soundfx == nullptr) return false;

    int hooked = 0;
    auto on_voice_post = [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
        auto* self = static_cast<Ue4ssPort*>(custom);
        if (self != nullptr) self->on_voice_post(context.RESULT_DECL);
    };
    for (const char* name : {"PBZPostEventOnAkComponent", "PBZPostEventOnAkComponent_SoftRef",
                             "PBZPostEventOnAkComponent_Animation", "PBZPostEventOnAkComponent_Animation_SoftRef",
                             "PBZPostEventOnActor", "PBZPostEventOnActor_SoftRef", "PBZPostEventGlobal",
                             "PBZPostEventGlobal_SoftRef", "PBZPostEventAtLocation", "PBZPostEventAtLocation_SoftRef",
                             "PBZPostAssociatedEvent"}) {
        UFunction* function = find_function(soundfx, name);
        if (function == nullptr) continue;
        const std::string target = std::string("PBZSoundFXSubsystem.") + name;
        if (register_tracked_script_hook(hooks_, target.c_str(), function, nullptr, on_voice_post, this)) ++hooked;
    }

    // 集成层入口兜底：绕过游戏子系统、直接走 Wwise BP 的声音也截获
    // （UAkGameplayStatics 的全局/按位置 post、UAkComponent 的成员 post——类级钩子对所有实例生效）
    UObject* ak_statics = find_object("/Script/AkAudio.Default__AkGameplayStatics");
    if (is_ok(ak_statics)) {
        for (const char* name : {"PostEvent", "PostEventAtLocation", "PostAndWaitForEndOfEvent"}) {
            UFunction* function = find_function(ak_statics, name);
            if (function == nullptr) continue;
            const std::string target = std::string("AkGameplayStatics.") + name;
            if (register_tracked_script_hook(hooks_, target.c_str(), function, nullptr, on_voice_post, this)) ++hooked;
        }
    }
    {
        std::vector<UObject*> ak_components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), ak_components);
        for (UObject* component : ak_components) {
            if (!is_ok(component)) continue;
            bool component_hooked = false;
            for (const char* name : {"PostAkEvent", "PostAssociatedAkEvent"}) {
                UFunction* function = find_function(component, name);
                if (function == nullptr) continue;
                const std::string target = std::string("AkComponent.") + name;
                if (register_tracked_script_hook(hooks_, target.c_str(), function, nullptr, on_voice_post, this)) {
                    ++hooked;
                    component_hooked = true;
                }
            }
            if (component_hooked) break;  // 类级函数挂一次即覆盖所有实例
        }
    }

    if (hooked == 0) return false;
    voice_hooks_installed_ = true;
    log(LogLevel::Info, "对话语音钩子已挂：" + std::to_string(hooked) + " 个 Post 函数（PBZSoundFXSubsystem），冻结将走真暂停");
    // 顺手把暂停/恢复接口的真实参数签名打出来：调用被拦时不用再猜
    for (const char* name : {"PBZPausePlayingID", "PBZResumePlayingID"}) {
        if (UFunction* fn = find_function(soundfx, name)) {
            log(LogLevel::Info, std::string(name) + " 参数签名=" + describe_params(fn));
        }
    }
    // 语音位置校验接口（1.0.73 只读诊断）：存在就把真实参数签名打一次，签名不符时看这里
    if (UFunction* position_fn = find_function(soundfx, "GetPlayingIDPositionMs")) {
        log(LogLevel::Info, std::string("GetPlayingIDPositionMs 参数签名=") + describe_params(position_fn));
    }
    // 对话 Flow 诊断钩子尽早可用（1.0.74）；失败不影响语音钩子本身
    install_dialogue_debug_hooks();
    return true;
}

// 对话 Flow 只读诊断钩子（1.0.74 方案 A）：FindFirstOf 按类名找 PlayAudio_C / StopAudio_C，
// 分别挂 ForceFinishAudio / ExecuteUbergraph_StopAudio 的 pre 回调。只在钩子里计数+写日志，
// 不改任何播放行为。至少挂上一个才算成功；一个都没找到时下次冻结重试（不刷屏）。
bool Ue4ssPort::install_dialogue_debug_hooks() {
    if (dialogue_debug_hooks_installed_) return true;
    std::string hooked_labels;

    if (UObject* sample = RC::Unreal::UObjectGlobals::FindFirstOf(STR("PlayAudio_C")); is_ok(sample)) {
        if (UFunction* function = find_function(sample, "ForceFinishAudio"); function != nullptr) {
            const char* target = "PlayAudio_C.ForceFinishAudio";
            const bool ok = register_tracked_script_hook(
                hooks_, target, function,
                [](RC::Unreal::UnrealScriptFunctionCallableContext&, void* custom) {
                    auto* self = static_cast<Ue4ssPort*>(custom);
                    if (self != nullptr) self->on_dialogue_flow_event("PlayAudio_C.ForceFinishAudio");
                },
                // 非原生 Blueprint 函数走 UE4SS 的 GlobalScriptHook 路径：pre/post 两个回调
                // 必须都非空，否则另一侧的空 std::function 会抛 bad_function_call 并移除钩子。
                [](RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {},
                this);
            if (ok) hooked_labels += target;
        }
    }
    if (UObject* sample = RC::Unreal::UObjectGlobals::FindFirstOf(STR("StopAudio_C")); is_ok(sample)) {
        if (UFunction* function = find_function(sample, "ExecuteUbergraph_StopAudio"); function != nullptr) {
            const char* target = "StopAudio_C.ExecuteUbergraph_StopAudio";
            const bool ok = register_tracked_script_hook(
                hooks_, target, function,
                [](RC::Unreal::UnrealScriptFunctionCallableContext&, void* custom) {
                    auto* self = static_cast<Ue4ssPort*>(custom);
                    if (self != nullptr) self->on_dialogue_flow_event("StopAudio_C.ExecuteUbergraph_StopAudio");
                },
                [](RC::Unreal::UnrealScriptFunctionCallableContext&, void*) {},
                this);
            if (ok) {
                if (!hooked_labels.empty()) hooked_labels += "、";
                hooked_labels += target;
            }
        }
    }
    if (hooked_labels.empty()) return false;
    dialogue_debug_hooks_installed_ = true;
    log(LogLevel::Info, "对话 Flow 诊断钩子已挂：" + hooked_labels);
    return true;
}

void Ue4ssPort::on_dialogue_flow_event(const char* label) {
    if (!dialogue_flow_diag_active_) return;
    ++dialogue_flow_event_count_;
    if (dialogue_flow_event_count_ <= 8) {
        log(LogLevel::Info, std::string("冻结期对话 Flow 诊断：") + (label != nullptr ? label : "unknown") +
                                " 被调用（第 " + std::to_string(dialogue_flow_event_count_) + " 次）");
    }
}

void Ue4ssPort::on_voice_post(void* result_decl) {
    if (result_decl == nullptr) return;
    const int32_t id = *static_cast<const int32_t*>(result_decl);
    // Wwise PlayingID 是从 1 递增的正数；过界值说明该函数返回的不是 PlayingID（如 bool），丢弃
    if (id <= 0 || id > 10000000) return;
    // 线程归属：本函数跑在 UE4SS 钩子线程，playing_ids_ 的读方在引擎 Tick 线程，
    // 统一由 mutex_ 串行化（选锁而不是队列：写入者只有这一个回调，读方每帧取一次快照，
    // 锁的临界区只做 vector/set 操作，不跨越 ProcessEvent）。
    std::lock_guard<std::mutex> lock(mutex_);
    if (!playing_ids_seen_.insert(id).second) return;
    playing_ids_.push_back(id);
    if (playing_ids_.size() > 1024) {
        // 只留最近 512 个：已结束的播放 ID 会被 Wwise 回收复用，截获列表要保持新鲜；
        // 上限给足是让长命的环境/音乐循环也在名单里（发得早就被记下了）
        const size_t keep = 512;
        playing_ids_.erase(playing_ids_.begin(), playing_ids_.end() - static_cast<long>(keep));
        playing_ids_seen_.clear();
        for (int32_t id_kept : playing_ids_) playing_ids_seen_.insert(id_kept);
    }
}

void Ue4ssPort::probe_wwise_components(const std::vector<UObject*>& components) {
    if (wwise_probe_done_) return;
    wwise_probe_done_ = true;
    if (components.empty()) {
        log(LogLevel::Info, "Wwise 组件诊断：当前场景没有 AkComponent 实例（仅探测一次）");
        return;
    }
    std::string available;
    for (const char* candidate : {"Stop", "AkStopAll", "AkStop", "StopAll", "Pause", "AkPause"}) {
        bool exists = false;
        for (UObject* component : components) {
            if (is_ok(component) && find_function(component, candidate) != nullptr) {
                exists = true;
                break;
            }
        }
        if (exists) available += available.empty() ? candidate : std::string(",") + candidate;
    }
    log(LogLevel::Info, "Wwise 组件诊断：AkComponent=" + std::to_string(components.size()) +
                            "，可用函数=" + (available.empty() ? "无" : available) + "（仅探测一次）");
    dump_audio_objects_once();
}

// 一次性音频对象普查：把场景里所有疑似音频/语音/对话相关的类找出来。
// 背景：AkComponent 上的 Stop 只能拦“挂在组件上”的声音；对话经常是 Wwise 直接
// post 在游戏对象上（没有组件），那部分要靠游戏自己的管理器类去停——这里把
// 候选管理器和它们可用的控制函数列出来，给下一版做可恢复的音频暂停定目标。
void Ue4ssPort::dump_audio_objects_once() {
    if (audio_dump_done_) return;
    audio_dump_done_ = true;

    struct AudioClassInfo {
        int count{0};
        UObject* sample{nullptr};
    };
    std::map<std::string, AudioClassInfo> found;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        const std::string class_name = narrow(cls->GetName());
        if (!is_audio_related_name(class_name)) return RC::LoopAction::Continue;
        AudioClassInfo& info = found[class_name];
        ++info.count;
        if (info.sample == nullptr) info.sample = object;
        return RC::LoopAction::Continue;
    });

    if (found.empty()) {
        log(LogLevel::Info, "音频对象普查：未发现 Ak/Audio/Sound/Voice/Dialog 相关类（仅一次）");
        return;
    }
    for (const auto& entry : found) {
        log(LogLevel::Info, "音频对象普查：" + entry.first + " x" + std::to_string(entry.second.count));
    }
    // 全局静态库（UAkGameplayStatics）：Wwise 的 BP 入口都在这，直接列全量函数
    if (UObject* ak_statics = find_object("/Script/AkAudio.Default__AkGameplayStatics")) {
        if (is_ok(ak_statics)) {
            log(LogLevel::Info, "AkGameplayStatics 函数：" + list_all_functions(ak_statics, 80));
        }
    }

    // 优先看“能管全局”的类：子系统 > 函数库 > 任务/对话图/导演 > 管理器/系统 > 组件形态
    auto priority_of = [](const std::string& lower) -> int {
        if (lower.find("subsystem") != std::string::npos) return 0;
        if (lower.find("statics") != std::string::npos || lower.find("library") != std::string::npos) return 1;
        if (lower.find("task") != std::string::npos || lower.find("graph") != std::string::npos ||
            lower.find("director") != std::string::npos) {
            return 2;
        }
        if (lower.find("manager") != std::string::npos || lower.find("controller") != std::string::npos ||
            lower.find("system") != std::string::npos) {
            return 3;
        }
        if (lower.find("component") != std::string::npos) return 4;
        return 9;
    };
    std::vector<std::pair<int, const std::map<std::string, AudioClassInfo>::value_type*>> ranked;
    for (const auto& entry : found) {
        ranked.emplace_back(priority_of(lower_ascii(entry.first)), &entry);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    int dumped_classes = 0;
    for (const auto& item : ranked) {
        if (dumped_classes >= 12) break;
        const AudioClassInfo& info = item.second->second;
        if (info.sample == nullptr || !is_ok(info.sample)) continue;
        if (item.first >= 9) continue;  // 形态不像“能管东西”的类（事件资产等）不转储
        ++dumped_classes;
        // 子系统/静态库用全量函数表（它们最可能藏着“按 ID 暂停/枚举播放”这类接口），
        // 其余（任务/对话图/组件）仍按关键词筛
        const bool full_list = item.first <= 1;
        const std::string functions = full_list ? list_all_functions(info.sample, 120)
                                                : list_audio_functions(info.sample);
        log(LogLevel::Info, "音频类函数：" + item.second->first + " → " + functions);
    }

    // 1.0.73：对话相关类的一次性函数清单（只加日志）——逐 PlayingID 暂停失效时，从这里
    // 找游戏自己的对话/语音控制接口。每类最多 20 个（list_audio_functions 已 cap），
    // 找不到的类跳过，总行数不超过 9 行。
    for (const char* class_name :
         {"STTask_PlayDialogue_C", "KeepSeqDialogue_C", "DialogueGraphNew_C", "DialogueGraph_C",
          "DialogueFlowAsset", "FromActor_Dialogue_C", "PlayAudio_C", "StopAudio_C", "BP_DialogueCameraType_C"}) {
        UObject* sample = RC::Unreal::UObjectGlobals::FindFirstOf(widen(class_name));
        if (!is_ok(sample)) continue;
        log(LogLevel::Info, std::string("对话类函数：") + class_name + " → " + list_audio_functions(sample));
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
