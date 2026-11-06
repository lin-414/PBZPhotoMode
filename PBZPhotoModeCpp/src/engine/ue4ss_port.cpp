// =============================================================================
//  Ue4ssPort 成员函数实现：核心/世界/日志
//
//  本文件保留：构造/析构、钩子记账与注销、上下文查找（controller/world/camera_manager 等）、
//  世界换代清理、超宽屏画幅解锁、日志级别与函数缓存重探。
//  其余成员函数按子系统分散在同目录的 ue4ss_port_*.cpp，见 ue4ss_port.h 顶部说明。
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

// ------------------------------------------------------------------ 构造/析构

Ue4ssPort::Ue4ssPort(std::filesystem::path mod_dir) : mod_dir_(std::move(mod_dir)) {}

Ue4ssPort::~Ue4ssPort() {
    // 兜底：正常卸载由 PhotoModeMod 在拆其他组件之前调用；这里保证即使有人直接销毁 port，
    // 也不会留下指向已释放对象的钩子。已注销的条目不会重复处理。
    unregister_all_hooks();
    if (camera_actor_ != nullptr) {
        destroy_camera();
    }
}

bool Ue4ssPort::track_global_callback(const std::string& target, std::uint64_t callback_id) {
    // GlobalCallbackId 的 ERROR_ID 是 0：拿到 0 说明注册没成功，登记为 Failed 而不是假装装上。
    if (callback_id == 0) {
        hooks_.record_failure(target, HookKind::Global);
        return false;
    }
    return hooks_.record_install(target, HookKind::Global, nullptr, static_cast<long long>(callback_id), -1);
}

void Ue4ssPort::track_global_failure(const std::string& target) {
    hooks_.record_failure(target, HookKind::Global);
}

// 卸载钩子（H-5）。顺序与原因在 PhotoModeMod::~PhotoModeMod 里有说明；这里只负责逐条注销。
// 注意 UE4SS 的限制：绝不能在回调内部调用 UnregisterHook/UnregisterCallback（会死锁），
// 本函数只会在卸载路径（非回调线程）执行。
int Ue4ssPort::unregister_all_hooks() {
    const std::vector<HookRecord> pending = hooks_.take_pending_unregister();
    int failed = 0;
    for (const HookRecord& record : pending) {
        bool ok = false;
        std::string why;
        if (record.kind == HookKind::Global) {
            if (record.pre_id > 0) {
                ok = RC::Unreal::Hook::UnregisterCallback(
                    static_cast<RC::Unreal::Hook::GlobalCallbackId>(record.pre_id));
                if (!ok) why = "UnregisterCallback 返回 false（id 失效或已被 UE4SS 移除）";
            } else {
                why = "记录的 GlobalCallbackId 无效";
            }
        } else {
            auto* function = static_cast<RC::Unreal::UFunction*>(record.function);
            if (function == nullptr) {
                why = "缺少注册时的 UFunction*";
            } else {
                try {
                    RC::Unreal::UObjectGlobals::UnregisterHook(
                        function, {static_cast<int>(record.pre_id), static_cast<int>(record.post_id)});
                    ok = true;
                } catch (const std::exception& e) {
                    why = std::string("UnregisterHook 抛异常：") + e.what();
                } catch (...) {
                    why = "UnregisterHook 抛异常（未知类型）";
                }
            }
        }
        if (ok) {
            hooks_.record_unregistered(record.target);
        } else {
            ++failed;
            hooks_.record_unregister_failure(record.target);
            log(LogLevel::Error, "钩子注销失败：" + record.target + "，原因：" + why +
                                     "；残留回调仍会在 UE4SS 派发时进入本 DLL，卸载/重载后有崩溃风险");
        }
    }
    // 生命周期已结束：标记位复位，真相以 hooks_ 的注册表为准（失败条目会保留 UnregisterFailed）。
    voice_hooks_installed_ = false;
    dialogue_debug_hooks_installed_ = false;
    axis_hook_installed_ = false;
    axis_function_ = nullptr;
    axis_sink_ = nullptr;
    if (!pending.empty()) {
        log(failed == 0 ? LogLevel::Info : LogLevel::Error,
            "钩子注销完成：成功 " + std::to_string(pending.size() - failed) + " 个，失败 " +
                std::to_string(failed) + " 个");
    }
    return failed;
}

// ------------------------------------------------------------------ 上下文查找


RC::Unreal::UObject* Ue4ssPort::controller() const {
    if (is_ok(controller_cache_)) return controller_cache_;

    // FindFirstOf 会返回对象数组里第一个 PlayerController，可能是过场/spectator 的，
    // 甚至是已经失效的旧实例。真正在玩的那个一定同时有 Player(ULocalPlayer) 和 Pawn。
    std::vector<UObject*> candidates;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("PlayerController"), candidates);

    UObject* fallback = nullptr;
    for (UObject* candidate : candidates) {
        if (!is_ok(candidate)) continue;
        if (fallback == nullptr) fallback = candidate;

        UObject* player = nullptr;
        UObject* pawn = nullptr;
        const bool has_player = read_object(find_property(candidate, "Player"), candidate, player);
        const bool has_pawn = read_object(find_property(candidate, "Pawn"), candidate, pawn);
        if (has_player && has_pawn) {
            controller_cache_ = candidate;
            return candidate;
        }
    }
    controller_cache_ = fallback;
    return fallback;
}

// 不做世界身份同步的原始世界查找：world()/world_settings() 内部用（避免递归换代）。
RC::Unreal::UObject* Ue4ssPort::raw_world() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    return safe_get_world(pc);
}

// AWorldSettings 是 ULevel.WorldSettings（UWorld 本身没有这个属性）。
// 先走 PersistentLevel.WorldSettings，失败再退到 UWorld::K2_GetWorldSettings()。
RC::Unreal::UObject* Ue4ssPort::raw_world_settings(UObject* scene) const {
    if (!is_ok(scene)) return nullptr;

    UObject* level = nullptr;
    if (read_object(find_property(scene, "PersistentLevel"), scene, level) && is_ok(level)) {
        UObject* settings = nullptr;
        if (read_object(find_property(level, "WorldSettings"), level, settings) && is_ok(settings)) {
            return settings;
        }
    }

    UFunction* function = find_function(scene, "K2_GetWorldSettings");
    if (function == nullptr) return nullptr;
    ParamBlock params(function);
    if (!params.valid()) return nullptr;
    // ProcessEvent 在对象/函数无效时会硬崩，单包一层 SEH（H-3）
    if (!seh_process_event(scene, function, params.data())) return nullptr;
    UObject* settings = nullptr;
    if (read_object(params.prop({"ReturnValue"}), params.data(), settings)) return settings;
    return nullptr;
}

RC::Unreal::UObject* Ue4ssPort::world() {
    UObject* scene = raw_world();
    if (is_ok(scene)) observe_world(scene, nullptr);
    return scene;
}

RC::Unreal::UObject* Ue4ssPort::world_settings() {
    UObject* scene = raw_world();
    if (!is_ok(scene)) return nullptr;
    UObject* settings = raw_world_settings(scene);
    if (is_ok(settings)) observe_world(scene, settings);
    return settings;
}

// 世界换代语义（H-2/H-3）：
//   · 旧一代的 UObject 缓存（相机/ViewTarget/HUD/提示/血条/锁定/字幕/控制器/锁定系统）
//     随关卡卸载一起失效，直接清空——保留裸指针只会在下一次 tick 解引用已释放内存；
//   · 旧一代的原始值快照（时间字段/暂停字段/画幅/风源/MPC）全部作废：旧世界的字段已被
//     引擎带走，不能把旧世界的原值写进新世界（写到新世界就是把新世界的设置改坏）；
//   · 跨世界仍存活的全局设置对象（交互提示/血条/字幕开关）在作废前尽力回滚一次，
//     否则换图后这些设置会一直停在摄影模式的取值上（回滚失败不重试：旧对象归属不确定）；
//   · 进程级状态（Wwise 整引擎挂起、函数指针与钩子）不属于世界，保持不动。
void Ue4ssPort::observe_world(UObject* scene, UObject* settings) {
    if (!world_identity_.observe(scene, settings)) return;
    invalidate_world_scoped_state();
    log(LogLevel::Info, "检测到世界换代（generation=" + std::to_string(world_identity_.generation()) +
                            "）：旧世界的对象缓存与原始值快照已作废");
}

RC::Unreal::UObject* Ue4ssPort::camera_manager() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    UObject* manager = nullptr;
    if (!read_object(find_property(pc, "PlayerCameraManager"), pc, manager)) return nullptr;
    return manager;
}

RC::Unreal::UObject* Ue4ssPort::player_pawn() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    UObject* pawn = nullptr;
    if (!read_object(find_property(pc, "Pawn"), pc, pawn)) return nullptr;
    return pawn;
}

RC::Unreal::UObject* Ue4ssPort::camera_component() {
    // 缓存指针每次都要过 is_ok：换世界后旧 CameraActor/CameraComponent 已销毁（H-3）
    if (is_ok(camera_component_cache_)) return camera_component_cache_;
    camera_component_cache_ = nullptr;
    if (!is_ok(camera_actor_)) return nullptr;
    UObject* component = nullptr;
    if (!read_object(find_property(camera_actor_, "CameraComponent"), camera_actor_, component)) {
        return nullptr;
    }
    camera_component_cache_ = component;
    return camera_component_cache_;
}


// 世界换代时的统一失效处理（H-2/H-3）。
// 语义见 observe_world 的注释：全局设置对象先尽力回滚，世界内对象与快照直接丢弃。
// 放在这里（而不是文件开头）是因为要用到提示/血条/字幕的设置 spec 与写入 helper。
void Ue4ssPort::invalidate_world_scoped_state() {
    // ---- 跨世界存活的全局设置对象：尽力回滚一次，再做废记账 ----
    if (prompt_setting_saved_) {
        if (is_ok(prompt_setting_owner_)) {
            write_bool_setting(prompt_setting_owner_, kPromptSettingSpec, prompt_setting_original_);
        }
        prompt_setting_saved_ = false;
        prompt_setting_owner_ = nullptr;
    }
    if (bars_setting_saved_) {
        if (is_ok(bars_setting_owner_)) {
            invoke(bars_setting_owner_, "SetMonsterHealthBarMode",
                   [&](ParamBlock& params) { return fill_bar_mode_param(params, bars_setting_original_); });
        }
        bars_setting_saved_ = false;
        bars_setting_owner_ = nullptr;
    }
    if (subtitle_setting_touched_) {
        if (is_ok(subtitle_setting_owner_)) {
            write_bool_setting(subtitle_setting_owner_, kSubtitleSettingSpec, subtitle_setting_original_);
        }
        subtitle_setting_touched_ = false;
        subtitle_setting_owner_ = nullptr;
    }

    // ---- 世界内对象缓存：旧对象已随关卡卸载，直接丢弃，不再解引用 ---- 
    // 相机不在这里 DestroyActor：旧世界销毁时引擎已经处理过它。
    camera_actor_ = nullptr;
    camera_component_cache_ = nullptr;
    // 画面效果体积属于旧世界，已随关卡销毁：只清指针，绝不能再去 K2_DestroyActor。
    post_volume_ = nullptr;
    // 接管相机同样是世界级状态：每帧写的是旧世界那份 CameraCache.POV，换代后旧相机已销毁。
    // 这里直接作废接管，并置 takeover_render_lost_ 让 core 走既有的回退路径
    // （fallback_from_takeover → spawn 独立相机）。
    if (takeover_active_) {
        takeover_active_ = false;
        takeover_pose_known_ = false;
        takeover_fov_ = 90.0;
        takeover_pov_mismatch_streak_ = 0;
        takeover_render_lost_ = true;
        log(LogLevel::Warn,
            "世界换代：接管当前相机已失效（旧相机随旧世界作废），本次将回退独立相机");
    }
    // 未在接管中也把残值清掉：上一代可能刚退出接管，位姿留着没有意义。
    takeover_pose_known_ = false;
    takeover_pov_mismatch_streak_ = 0;
    original_view_target_ = nullptr;
    original_view_target_saved_ = false;
    view_attached_by_port_ = false;
    view_verified_ = false;
    controller_cache_ = nullptr;
    aspect_saved_ = false;
    aspect_manager_ = nullptr;
    aspect_original_constrain_ = false;
    aspect_original_value_ = 16.0 / 9.0;
    aspect_transient_cinematic_ = false;
    aspect_reassert_logged_ = false;
    aspect_reassert_warned_ = false;
    hud_widgets_.clear();
    pv_hide_active_ = false;
    prompt_components_.clear();
    lockon_widgets_.clear();
    lockon_components_.clear();
    lockon_indicator_suppressed_.clear();
    lockon_cleared_targets_.clear();
    lockon_absent_ = false;
    subtitle_widgets_.clear();
    subtitle_scanned_ = false;
    flow_dialog_tick_paused_ = false;
    // 逐语音暂停的 PlayingID 属于旧世界的音频实例，换世界后不能再用旧 ID 去恢复
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ids_.clear();
        playing_ids_seen_.clear();
    }
    voice_positions_at_pause_.clear();
    // 冻结期治理的对象/原始值（相机修饰器、风源、MPC）同样是世界级裸指针
    camera_modifier_states_.clear();
    cloth_states_.clear();
    skeletal_anim_states_.clear();
    motion_blur_active_ = false;
    motion_blur_original_ = 0;
    wind_backups_.clear();
    mpc_snapshots_.clear();
    mpc_diag_last_.clear();
    mpc_diag_has_last_ = false;
    // 时间/暂停原始值快照：与当前代次不一致即视为无效（这里直接清掉，下次写入重新采样）
    time_snapshot_.reset();
    pause_snapshot_.reset();
}

// ------------------------------------------------------------------ 超宽屏画幅解锁

namespace {

// 进入/重申画幅时判断当前是否有 Sequencer 在播。处决演出与过场都用 Sequencer，
// 它们在演出期间会把相机管理器压成 16:9；这个锁属于演出瞬时状态，不能当“进入前原值”还原。
auto any_sequence_playing() -> bool {
    std::vector<UObject*> players;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("MovieSceneSequencePlayer"), players);
    for (UObject* player : players) {
        if (!is_ok(player)) continue;
        if (narrow(player->GetFullName()).find("Default__") != std::string::npos) continue;
        bool playing = false;
        if (invoke_bool(player, "IsPlaying", [](ParamBlock&) { return true; }, playing) && playing) {
            return true;
        }
    }
    return false;
}

}  // namespace

// 游戏把画幅锁在 16:9，超宽屏（21:9/32:9）两侧补黑边。UE 里这把锁最常挂在两处：
// ① APlayerCameraManager.bDefaultConstrainAspectRatio / DefaultAspectRatio —— 对任何 ViewTarget 生效，
//   就算 ViewTarget 换成摄影相机，黑边照样在（用户反馈的超宽屏黑边就是它）；
// ② 相机组件自己的 bConstrainAspectRatio —— 我们生成的 CameraActor 默认不锁，显式再关一次保险。
//
// 关键教训（2.0.2 实机）：进入摄影模式时“没锁”不代表以后不会锁。游戏在 SetPause 之后
// （暂停菜单/处决演出）会重新把 bDefaultConstrainAspectRatio 打开，所以：
//   - 进入时无论锁没锁都要保存原值（false 也要保存），退出才能把“原本就是超宽”写回去；
//   - 摄影模式每帧在渲染前重申一次解锁（reassert_aspect_ratio）。
bool Ue4ssPort::unlock_aspect_ratio() {
    bool ok = false;

    UObject* manager = camera_manager();
    if (is_ok(manager)) {
        FProperty* constrain_prop = find_property(manager, "bDefaultConstrainAspectRatio");
        FProperty* value_prop = find_property(manager, "DefaultAspectRatio");
        double constrain = 0.0;
        if (constrain_prop == nullptr || value_prop == nullptr ||
            !read_number(constrain_prop, manager, constrain)) {
            log(LogLevel::Warn,
                "画幅解锁：相机管理器读不到 bDefaultConstrainAspectRatio/DefaultAspectRatio"
                "（黑边若来自别的机制，把这条日志发回来）");
        } else {
            // 无论当前值 true 还是 false 都记账：暂停/处决演出很可能在接下来把它改成 true。
            aspect_saved_ = true;
            aspect_manager_ = manager;
            aspect_original_constrain_ = constrain != 0.0;
            aspect_original_value_ = 16.0 / 9.0;
            if (!read_number(value_prop, manager, aspect_original_value_)) {
                aspect_original_value_ = 16.0 / 9.0;
            }
            aspect_transient_cinematic_ = aspect_original_constrain_ && any_sequence_playing();
            if (aspect_transient_cinematic_) {
                log(LogLevel::Info,
                    "画幅：进入时 16:9 锁来自正在播放的过场/处决演出，退出时按解锁状态还原（不保留演出锁）");
            }
            if (constrain == 0.0) {
                log(LogLevel::Info, "画幅：相机管理器未锁定画幅，保持超宽（退出时仍会还原该状态）");
                ok = true;
            } else if (write_bool(constrain_prop, manager, false)) {
                char buf[128]{};
                std::snprintf(buf, sizeof(buf), "画幅解锁：已解除 16:9 锁定（原比例=%.3f），超宽屏黑边消除",
                              aspect_original_value_);
                log(LogLevel::Info, buf);
                ok = true;
            } else {
                aspect_saved_ = false;
                aspect_manager_ = nullptr;
                log(LogLevel::Warn, "画幅解锁失败：bDefaultConstrainAspectRatio 写不进去");
            }
        }
    } else {
        log(LogLevel::Warn, "画幅解锁：拿不到相机管理器，跳过管理器这一路");
    }

    // 摄影相机自己的约束一并关掉（CameraActor 默认不锁，这里防类默认值被改过）
    if (UObject* component = camera_component(); is_ok(component)) {
        write_bool(find_property(component, "bConstrainAspectRatio"), component, false);
    }
    return ok;
}

void Ue4ssPort::reassert_aspect_ratio() {
    // 暂停菜单/处决演出会重新打开 bDefaultConstrainAspectRatio —— 每帧在渲染前压回 false。
    UObject* manager = camera_manager();
    if (is_ok(manager)) {
        FProperty* constrain_prop = find_property(manager, "bDefaultConstrainAspectRatio");
        double constrain = 0.0;
        if (constrain_prop != nullptr && read_number(constrain_prop, manager, constrain) && constrain != 0.0) {
            if (write_bool(constrain_prop, manager, false)) {
                if (!aspect_reassert_logged_) {
                    aspect_reassert_logged_ = true;
                    log(LogLevel::Info,
                        "画幅：游戏在暂停/演出中重新锁定了 16:9，已每帧压回超宽（退出时恢复进入前状态）");
                }
            } else if (!aspect_reassert_warned_) {
                aspect_reassert_warned_ = true;
                log(LogLevel::Warn, "画幅重申失败：bDefaultConstrainAspectRatio 写不回去（16:9 可能会短暂出现）");
            }
        }
    }
    // 独立相机组件自身的约束也一并保持关闭。
    if (UObject* component = camera_component(); is_ok(component)) {
        write_bool(find_property(component, "bConstrainAspectRatio"), component, false);
    }
}

void Ue4ssPort::restore_aspect_ratio() {
    if (!aspect_saved_) return;
    aspect_saved_ = false;

    UObject* manager = camera_manager();
    const bool restore_lock = aspect_original_constrain_ && !aspect_transient_cinematic_;
    if (!is_ok(manager)) {
        log(LogLevel::Warn, "画幅还原：拿不到相机管理器，跳过写回（画面可能仍停在 16:9）");
        aspect_manager_ = nullptr;
        aspect_transient_cinematic_ = false;
        aspect_reassert_logged_ = false;
        aspect_reassert_warned_ = false;
        return;
    }

    // 管理器换了（重新持有等）：只有“原本就没锁”可以安全地把 false 写给新管理器；
    // 原本锁着的 16:9 原值属于旧对象，不能写进新管理器。
    if (manager != aspect_manager_ && restore_lock) {
        log(LogLevel::Info, "画幅还原：相机管理器已不是进入时的对象，跳过写回");
        aspect_manager_ = nullptr;
        aspect_transient_cinematic_ = false;
        aspect_reassert_logged_ = false;
        aspect_reassert_warned_ = false;
        return;
    }
    aspect_manager_ = nullptr;

    FProperty* constrain_prop = find_property(manager, "bDefaultConstrainAspectRatio");
    if (write_bool(constrain_prop, manager, restore_lock)) {
        if (restore_lock) {
            if (FProperty* value_prop = find_property(manager, "DefaultAspectRatio"); value_prop != nullptr) {
                write_number(value_prop, manager, aspect_original_value_);
            }
            log(LogLevel::Info, "画幅还原：相机管理器已恢复 16:9 锁定");
        } else {
            log(LogLevel::Info, "画幅还原：已恢复进入前的解锁状态（超宽屏 21:9/32:9）");
        }
    } else {
        log(LogLevel::Warn, "画幅还原失败：bDefaultConstrainAspectRatio 写不回去");
    }
    aspect_transient_cinematic_ = false;
    aspect_reassert_logged_ = false;
    aspect_reassert_warned_ = false;
}

void Ue4ssPort::set_log_level(LogLevel level) {
    log_level_ = level;
    g_param_warn_threshold.store(static_cast<int>(level));
}

void Ue4ssPort::refresh_function_caches() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mouse_delta_fn_ = nullptr;
        mouse_delta_fn_missing_ = false;
        set_view_target_fn_ = nullptr;
        set_view_target_fn_missing_ = false;
    }
    log(LogLevel::Info, "运行期函数查找缓存已清除，下次调用将重新探测鼠标增量/SetViewTarget");
}

void Ue4ssPort::log(LogLevel level, const std::string& message) {
    // M-6：按 config.log_level 过滤；级别判定是 core/engine_port.h 的纯函数，Error 永不过滤。
    if (!log_level_enabled(log_level_, level)) return;
    emit(level, widen("[PBZPhotoMode] " + message + "\n"));
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
