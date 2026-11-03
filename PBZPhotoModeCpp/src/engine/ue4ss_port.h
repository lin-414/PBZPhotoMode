#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/engine_port.h"
#include "core/hook_registry.h"
#include "core/world_snapshot.h"

// 说明：本文件只在 PBZPM_WITH_UE4SS 下参与编译。
// 上层逻辑（core/）不接触引擎类型，所有 UE4SS/UE 反射调用都收在本目录的实现文件里。
//
// Ue4ssPort 的类声明（含成员变量）全部留在本文件；成员函数定义按子系统拆分到：
//   ue4ss_port.cpp                  —— 构造/析构、钩子记账与注销、上下文查找、世界换代清理、
//                                      画幅解锁、日志级别与函数缓存重探
//   ue4ss_port_camera_view.cpp      —— 相机/ViewTarget/位姿/FOV、鼠标轴钩子、游戏输入屏蔽
//   ue4ss_port_hud_ui.cpp           —— HUD/交互提示/血条/字幕/对话 Flow/UI 设置修复/锁定标记
//   ue4ss_port_time_pause.cpp       —— 时间缩放、世界暂停、剧情播放器
//   ue4ss_port_micro_motion.cpp     —— 相机 shake/修饰器、风源、MPC 快照、POV 微动
//   ue4ss_port_audio.cpp            —— Wwise 组件/逐语音 PlayingID/整引擎/语音钩子
//   ue4ss_port_diagnostics.cpp      —— 冻结诊断与引擎状态转储
// 跨文件共用的反射/参数块/设置读写 helper 在 ue4ss_port_internal.h（namespace pbzpm::detail，
// inline 定义，全工程单一份实体）。
//
// 注意：前向声明必须放在 RC::Unreal 里 —— UE4SS 自己的头文件也在同名空间下声明 UObject，
// 多写一个 RC::UObject 会让 UE4SS 的头文件出现“不明确符号”的编译错误。

namespace RC::Unreal {
class UnrealScriptFunctionCallableContext;
class UObject;
class UWorld;
class AActor;
class UClass;
class UFunction;
}  // namespace RC::Unreal

namespace pbzpm {

class Ue4ssPort final : public EnginePort {
public:
    explicit Ue4ssPort(std::filesystem::path mod_dir);
    ~Ue4ssPort() override;

    bool world_valid() override;
    // 当前世界代次（每次识别到 UWorld/WorldSettings 变化自增）。
    // 诊断/测试用；引擎侧所有世界级缓存的原始值都按它校验。
    std::uint64_t world_generation() const { return world_identity_.generation(); }
    bool read_pov(CameraPose& out) override;

    bool spawn_camera(const CameraPose& pose) override;
    bool attach_view() override;
    // 引擎 Tick 之后（游戏自己的相机逻辑跑完）再夺一次视角：
    // 有些游戏的 BP 每帧 SetViewTarget，只在 pre-tick 抢是抢不过的
    void reassert_view_target();
    bool restore_view() override;
    bool apply_pose(const CameraPose& pose) override;
    bool apply_fov(double fov) override;
    bool destroy_camera() override;

    bool set_ignore_move_input(bool ignore) override;
    bool set_game_input_blocked(bool blocked) override;
    bool set_player_hidden(bool hidden) override;
    bool set_hud_visible(bool visible) override;
    bool set_interact_prompts_visible(bool visible) override;
    bool set_enemy_health_bars_visible(bool visible) override;
    bool set_lockon_marker_visible(bool visible) override;
    bool set_subtitles_visible(bool visible) override;
    // 对话 Flow Tick 冻结（1.0.74 方案 A）：只操作已收编的 FlowDialog 控件，
    // paused=true → Collapsed(1) 停止 Tick；paused=false → Hidden(2) 恢复 Tick。
    int set_dialogue_flow_tick_paused(bool paused) override;

    bool unlock_aspect_ratio() override;
    void restore_aspect_ratio() override;

    double get_time_dilation() override;
    bool set_time_dilation(double dilation) override;
    // 只回写本 Mod 确实改过的 WorldSettings 字段；世界换代后旧快照作废，直接返回 true
    bool restore_time_dilation(double original) override;
    bool set_paused(bool paused) override;
    bool world_paused() override;
    SequenceFreezeReport pause_sequences(bool paused) override;
    int wwise_component_count() override;
    int pause_wwise_voices(bool pause) override;
    bool suspend_wwise_engine(bool suspend) override;
    void pov_watch_tick(bool camera_idle) override;

    CameraShakeFreezeReport freeze_camera_shakes(bool frozen) override;
    WindFreezeReport freeze_wind(bool frozen) override;
    int mpc_take_snapshot() override;
    int mpc_hold_snapshot() override;
    void mpc_release_snapshot() override;
    std::string freeze_diag_sample() override;

    // 尽早挂上 Post 钩子截获 PlayingID（mod_app 引擎 tick 里每 2 秒重试直到成功）
    bool install_voice_hooks();

    // ---- 钩子注销（H-5）----
    // 按 hooks_ 记账表逐条注销脚本钩子与全局回调。可重复调用：已注销的不会重复处理，
    // 上次注销失败的条目会重试。返回仍处于 UnregisterFailed 的条目数（0 = 全部清理干净）。
    int unregister_all_hooks();
    // 引擎 Tick 之类由 mod_app 注册的全局回调：把 GlobalCallbackId 交给同一张记账表，
    // 卸载时由 unregister_all_hooks 一并注销。返回 false 表示注册失败或该 target 已登记。
    bool track_global_callback(const std::string& target, std::uint64_t callback_id);
    // 全局回调注册失败（ERROR_ID）也要留痕：记账表里记 Failed，便于排障与后续重试。
    void track_global_failure(const std::string& target);

    // 一次性修复：旧版本（≤1.0.68）可能把字幕/场景交互提示开关写坏并让游戏存盘
    //（1.0.30 时代就有先例）。首次运行本版时把两个开关强制打开并回读校验，
    // 成功后写 ui_settings_repair_done.flag 不再执行。返回 true=已完成（含之前已修过）。
    bool recover_ui_settings_once();

    // M-6：按 config.log_level 过滤日志（级别判定在 core/engine_port.h 的纯函数里）
    void set_log_level(LogLevel level) override;
    // 运行中清除 *_missing_ 缓存并重新探测（配置重载/重新进入摄影模式时调用）
    void refresh_function_caches() override;
    // M-1：attach_view 的 ViewTarget/相机位姿回读是否已连续失败到退出阈值
    bool view_confirmation_failed() const override;

    void log(LogLevel level, const std::string& message) override;

    // 进模式时把关键引擎对象打一遍（PC/Pawn/HUD/相机/ViewTarget），真机排障用
    void dump_engine_state();
    // 引擎侧鼠标输入：hook PlayerController::InputAxis，拿到引擎算好的真实增量
    using AxisSink = std::function<void(const std::string& key_name, double amount)>;
    bool hook_input_axis(AxisSink sink);
    // 引擎这一帧的鼠标增量（PlayerController::GetInputMouseDelta）
    bool read_mouse_delta(double& dx, double& dy);
    void on_axis_event(RC::Unreal::UnrealScriptFunctionCallableContext& context);
    // ViewTarget 是否已确认切到摄影相机（false 时上层会周期性重试接管）
    bool view_verified() const { return view_verified_; }
    // 每秒诊断：相机管理器真正在用的视角 vs 我们的相机
    std::string camera_state_summary();
    // 时间设置诊断：WorldSettings 的 bAllowTimeDilation / TimeDilation / 上下限
    std::string time_settings_summary();

private:
    RC::Unreal::UObject* world_settings();
    RC::Unreal::UObject* controller() const;
    RC::Unreal::UObject* world();
    RC::Unreal::UObject* camera_manager() const;
    RC::Unreal::UObject* camera_component();
    RC::Unreal::UObject* player_pawn() const;
    // 不做世界身份同步的原始查找（world()/world_settings() 内部用，避免递归）
    RC::Unreal::UObject* raw_world() const;
    RC::Unreal::UObject* raw_world_settings(RC::Unreal::UObject* scene) const;
    // 观察当前世界；换代时作废旧一代的全部世界级缓存与原始值快照（并尽力回滚全局设置）
    void observe_world(RC::Unreal::UObject* scene, RC::Unreal::UObject* settings);
    void invalidate_world_scoped_state();

    std::filesystem::path mod_dir_;
    RC::Unreal::UObject* camera_actor_{nullptr};
    RC::Unreal::UObject* camera_component_cache_{nullptr};
    // 进入摄影模式前的 ViewTarget（过场动画期间是过场相机）：退出时还原到它，而不是
    // 玩家 Pawn——否则过场会被踢回玩家视角，正在播放的语句也会被打断。
    RC::Unreal::UObject* original_view_target_{nullptr};
    bool original_view_target_saved_{false};
    // 视角接管是否仍由本 port 持有：attach_view 成功置位、restore_view 清位；
    // destroy_camera 只在仍持有时才还原，避免 leave() 已还原后 destroy 又还原成 Pawn。
    bool view_attached_by_port_{false};
    double saved_dilation_{1.0};

    // 超宽屏画幅解锁：进入时保存的相机管理器原值（aspect_saved_=true 时有效）
    bool aspect_saved_{false};
    bool aspect_original_constrain_{false};
    double aspect_original_value_{16.0 / 9.0};
    RC::Unreal::UObject* aspect_manager_{nullptr};

    // 世界身份令牌：UWorld/AWorldSettings 一变就换代，旧一代的原始值快照全部作废。
    WorldIdentity world_identity_;

    // 时间冻结：直接写 AWorldSettings 的字段。UGameplayStatics::SetGlobalTimeDilation
    // 会被 bAllowTimeDilation / MinGlobalTimeDilation / MaxGlobalTimeDilation 拦住，
    // 所以在第一次改时间前把原始值存下来，解冻/退出时只写回本 Mod 确实改过的字段。
    TimeSettingsSnapshot time_snapshot_;

    // 暂停模式：暂停时要让 PlayerController 继续完整 Tick，否则相机管理器会停住，
    // 摄影相机无法移动/转向；同时临时把 GameMode 的 bPauseable 打开。
    // 读不到原值就不写，恢复只写本 Mod 确实写过的项。
    PauseSnapshot pause_snapshot_;

    // M-2：游戏输入屏蔽的目标对象身份。只在确实写成功时记录；恢复前确认对象仍是同一个
    //（指针有效且全名一致），不一致就跳过写回，避免把输入屏蔽写到重生后的新 PC/Pawn 上。
    struct BlockedObject {
        RC::Unreal::UObject* object{nullptr};
        // 组件记录所属的 PC/Pawn；owner 记录填自身（用于确认恢复时仍是同一对象）
        RC::Unreal::UObject* owner{nullptr};
        std::string name{};
        // 位图：0x1=对象 bBlockInput，0x4=SetIgnoreLookInput，0x8=Disable/EnableInput。
        // 组件记录的 bBlockInput 也记为 0x1；按位恢复，没写过的字段不碰。
        std::uint8_t written_bits{0};
    };
    BlockedObject input_block_pc_;
    BlockedObject input_block_pawn_;
    BlockedObject input_block_pc_component_;
    BlockedObject input_block_pawn_component_;
    void remember_blocked_object(BlockedObject& record, RC::Unreal::UObject* object,
                                 RC::Unreal::UObject* owner, std::uint8_t bits);
    bool restore_blocked_object(BlockedObject& record, RC::Unreal::UObject* current_owner, const char* label);

    // M-1：attach_view 之后回读 ViewTarget.Target 与相机位置；连续失败到阈值才让 core 退出。
    // 返回 true=确认指向摄影相机；observed/readable 用于失败时输出实际观测结果。
    bool read_view_target_target(RC::Unreal::UObject*& observed, bool& readable) const;
    bool camera_pose_matches_last_apply() const;
    int view_verify_fail_streak_{0};
    CameraPose last_applied_pose_{};
    bool last_applied_pose_known_{false};

    // M-6：日志级别阈值；Ue4ssPort::log 用它过滤（Error 永不过滤）
    LogLevel log_level_{LogLevel::Info};

    // 保护运行期函数查找缓存（mouse_delta_fn_ / set_view_target_fn_）与 playing_ids_。
    // 线程归属：函数缓存只在游戏线程读写；playing_ids_/playing_ids_seen_ 由 UE4SS
    // Post 钩子线程写、引擎 Tick 线程读，统一用这把锁串行化（取舍见 .cpp 注释）。
    mutable std::mutex mutex_;

    // Wwise 组件可用函数只探测一次，避免每次冻结都重复打日志
    bool wwise_probe_done_{false};
    // Wwise 整引擎 Suspend/Wakeup 的函数指针（优先主程序静态导出，回退 AkSoundEngineDLL.dll）
    using AkSuspendFn = int (*)(bool, bool);
    using AkWakeupFn = int (*)(unsigned int);
    using AkIsInitializedFn = bool (*)();
    bool wwise_engine_resolved_{false};
    bool wwise_engine_suspended_{false};
    AkSuspendFn ak_suspend_{nullptr};
    AkWakeupFn ak_wakeup_{nullptr};
    AkIsInitializedFn ak_is_initialized_{nullptr};
    bool resolve_wwise_engine();
    std::string wwise_engine_module_{};
    bool wwise_engine_failure_logged_{false};
    void probe_wwise_components(const std::vector<RC::Unreal::UObject*>& components);
    // 全部钩子/全局回调的 CallbackId 记账（H-5）。安装成功与否都登记，卸载按它逐条注销；
    // 幂等判定也查这里（installed(target) 为 true 时不再重复注册）。
    HookRegistry hooks_;

    // 对话语音 PlayingID 截获：钩住 PBZSoundFXSubsystem 的 Post 系列函数，
    // 冻结/解冻用游戏自有 PBZPausePlayingID / PBZResumePlayingID 真暂停（原位置续播）
    // 注：这个布尔只表示「已经至少挂上一个」，具体 id 全在 hooks_ 里；注销时一并复位。
    bool voice_hooks_installed_{false};
    bool pause_signature_logged_{false};  // 暂停接口签名只打一次
    // 线程归属：Post 钩子线程写、引擎 Tick 线程读；一律在 mutex_ 内访问（见上面的说明）
    std::vector<int32_t> playing_ids_{};
    std::set<int32_t> playing_ids_seen_{};
    void on_voice_post(void* result_decl);
    // 语音位置校验（只读诊断，1.0.73）：暂停时记录 (PlayingID, 位置ms)，恢复前再读一次比对。
    // 只观察不改变播放行为；接口缺失/签名不符时保持空，不影响暂停逻辑。
    std::vector<std::pair<int32_t, double>> voice_positions_at_pause_;
    bool voice_position_probe_logged_{false};  // 位置接口不可用/签名不符只提示一次
    // 对话 Flow 只读诊断钩子（1.0.74）：冻结期若 PlayAudio_C.ForceFinishAudio /
    // StopAudio_C.ExecuteUbergraph_StopAudio 被调用，只记日志，不改播放行为
    bool dialogue_debug_hooks_installed_{false};
    bool dialogue_flow_diag_active_{false};
    int dialogue_flow_event_count_{0};
    void on_dialogue_flow_event(const char* label);
    bool install_dialogue_debug_hooks();
    // 一次性音频对象普查：全对象数组里认 Ak/Audio/Sound/Voice/Dialog 相关类，
    // 为“对话语音走 Wwise 直接 post、组件 Stop 够不着”找下一版的目标
    bool audio_dump_done_{false};
    void dump_audio_objects_once();

    // ---- 冻结期残余微动治理的状态（进入/退出摄影模式之外不持有，冻结还原即清空）----
    // 相机修饰器冻结前的 bDisabled 状态，恢复时按原值写回
    std::vector<std::pair<RC::Unreal::UObject*, bool>> camera_modifier_states_;
    // 风源组件冻结前的参数值（读不到的项 has_=false，还原时跳过）
    struct WindParamBackup {
        RC::Unreal::UObject* component{nullptr};
        bool has_strength{false};
        double strength{0.0};
        bool has_speed{false};
        double speed{0.0};
        bool has_min_gust{false};
        double min_gust{0.0};
        bool has_max_gust{false};
        double max_gust{0.0};
    };
    std::vector<WindParamBackup> wind_backups_;
    // MPC 快照：冻结时抓的各集合参数值，冻结期每帧写回
    struct MpcParamValue {
        std::string name;
        bool is_vector{false};
        // 标量只用 value[0]；向量 = RGBA。连续数组方便整段传给 Kismet 的 FLinearColor 读写
        float value[4]{0.0f, 0.0f, 0.0f, 0.0f};
    };
    struct MpcCollectionSnapshot {
        RC::Unreal::UObject* collection{nullptr};
        std::string name;
        std::vector<MpcParamValue> params;
    };
    std::vector<MpcCollectionSnapshot> mpc_snapshots_;
    // 写回限频时间戳：进入快照后首帧立即写，此后 4Hz（原为每帧，347 次 ProcessEvent/帧）
    std::chrono::steady_clock::time_point mpc_hold_last_{};
    // 冻结期内已确认会触发游戏访问违例的参数（集合名+参数名）与类名不符的集合，隔离不再写
    std::set<std::string> mpc_bad_params_;
    std::set<std::string> mpc_bad_collections_;
    // 冻结诊断：MPC 上一拍样本（两次诊断之间比对参数变化）
    std::vector<MpcCollectionSnapshot> mpc_diag_last_;
    bool mpc_diag_has_last_{false};
    bool wind_missing_logged_{false};   // 风源组件属性缺失只告警一次
    bool mpc_unavailable_logged_{false}; // KismetMaterialLibrary 不可用只告警一次
    // POV 微动监测状态（pov_watch_tick 累积，freeze_diag_sample 报告并清零）
    bool pov_watch_has_last_{false};
    double pov_last_loc_[3]{0.0, 0.0, 0.0};
    double pov_last_rot_[3]{0.0, 0.0, 0.0};
    double pov_max_loc_delta_{0.0};
    double pov_max_rot_delta_{0.0};

    mutable RC::Unreal::UObject* controller_cache_{nullptr};
    // 每帧高频反射查找的缓存：UFunction 挂在类上、进程内稳定，不会失效；
    // miss 也记下来，类上没有的函数就不必每帧重找
    RC::Unreal::UFunction* mouse_delta_fn_{nullptr};
    bool mouse_delta_fn_missing_{false};
    RC::Unreal::UFunction* set_view_target_fn_{nullptr};
    bool set_view_target_fn_missing_{false};
    bool set_view_target_cached(RC::Unreal::UObject* pc, RC::Unreal::UObject* target);
    bool view_verified_{false};
    bool pose_apply_failure_logged_{false};
    // 被隐藏的 UMG HUD 根控件（控件指针 + 原始 ESlateVisibility 值），退出时原样恢复
    std::vector<std::pair<RC::Unreal::UObject*, int>> hud_widgets_;
    bool hud_widget_missing_logged_{false};
    bool pv_hide_active_{false};  // 本次隐藏是否走了 UIManager.SetHideUIForPV

    // 交互提示（E 键图标）：被隐藏的控件组件（指针 + 原始 bVisible），退出时原样恢复
    std::vector<std::pair<RC::Unreal::UObject*, bool>> prompt_components_;
    // 游戏设置的“场景交互提示”开关（bSceneInteractPromptVisible）进入时保存的状态
    bool prompt_setting_saved_{false};
    bool prompt_setting_original_{true};
    RC::Unreal::UObject* prompt_setting_owner_{nullptr};
    bool prompt_setting_missing_logged_{false};
    bool prompt_component_missing_logged_{false};

    // 敌人血条设置（MonsterHealthBarMode）：进入时保存的原模式值（枚举序数），退出时写回
    bool bars_setting_saved_{false};
    double bars_setting_original_{0.0};
    RC::Unreal::UObject* bars_setting_owner_{nullptr};
    bool bars_setting_missing_logged_{false};

    // 锁定标记（锁定敌人身上的白点）：被隐藏的 UMG 控件（指针 + 原 ESlateVisibility）与
    // 可视组件（指针 + 原 bVisible），退出摄影模式时按原值还原。
    // 渲染载体离线不可证（SetHideUIForPV 订阅者、HUD 布局、血条设置都不覆盖它），运行时按
    // 类名/资产名关键词全量扫描锁定与目标家族；零命中时确认完成并打一次性诊断清单，
    // 真实类名靠实机日志收敛（同字幕控件的做法）。
    std::vector<std::pair<RC::Unreal::UObject*, int>> lockon_widgets_;
    std::vector<std::pair<RC::Unreal::UObject*, bool>> lockon_components_;
    bool lockon_absent_{false};         // 本会话已确认场上没有可隐藏的锁定对象（进模式后玩家无法再锁定）
    bool lockon_absent_logged_{false};  // “未找到锁定标记”只告警一次
    bool lockon_diag_dumped_{false};    // 一次性诊断（可见控件/可视组件/玩家组件/锁定系统清单）每会话只打一次
    bool ui_repair_missing_logged_{false};  // UI 设置一次性修复：设置对象未就绪只提示一次
    bool ui_repair_wait_logged_{false};     // UI 设置一次性修复：等待玩家世界就绪只提示一次
    // 游戏锁定系统（TargetingComponent，1.0.68 exe 取证）：调过 SetShowLockIndicator(false)
    // 的组件与 StopLockTarget 清掉的锁定目标——退出时都要恢复（指示器开关 + 重新锁定）
    std::vector<RC::Unreal::UObject*> lockon_indicator_suppressed_;
    struct LockonClearedTarget {
        RC::Unreal::UObject* component{nullptr};
        RC::Unreal::UObject* target{nullptr};
    };
    std::vector<LockonClearedTarget> lockon_cleared_targets_;
    void dump_lockon_diagnostics();

    // 字幕设置（bSubtitlesEnabled）：进入时保存的原开关状态，退出时写回。
    // touched 与 saved 分离：设置类上可能只有 setter 没有 property（1.0.30 实机：
    // PBZSettingsShared 只有函数，属性在引擎 GlobalUserParam 上）——只要动过开关就必须还原，
    // 原值读不到时按 true 还原（字幕默认开的语义）。
    bool subtitle_setting_touched_{false};
    bool subtitle_setting_saved_{false};
    bool subtitle_setting_original_{true};
    RC::Unreal::UObject* subtitle_setting_owner_{nullptr};
    bool subtitle_setting_missing_logged_{false};
    bool subtitle_restore_failed_logged_{false};
    // 被隐藏的字幕显示控件（GameSubtitles 插件 USubtitleDisplay 实例），退出时按原可见性恢复
    std::vector<std::pair<RC::Unreal::UObject*, int>> subtitle_widgets_;
    bool subtitle_widget_missing_logged_{false};
    bool subtitle_diag_dumped_{false};
    bool subtitle_flow_dialog_logged_{false};  // 检测到 FlowDialog（对话/过场）跳过设置写关闭只提示一次
    bool flow_dialog_tick_paused_{false};      // 对话 Flow Tick 冻结：true=FlowDialog Collapsed(1)，false=Hidden(2)
    bool subtitle_scanned_{false};  // 本次摄影模式会话是否做过全量扫描（每秒重申只用快速路径）
    bool subtitle_visible_dumped_{false};  // 可见 UserWidget 清单只打一次
    int subtitle_reassert_count_{0};  // 重申计数（每 30 次做一次全量扫描，收编对话中途新出现的控件类）
    // 实机发现过的字幕控件类名（进入时全量扫描记录；每秒重申合并成一次全数组遍历按这些名字匹配）
    std::vector<std::string> subtitle_widget_class_names_;
    void collect_subtitle_widgets_impl(std::vector<RC::Unreal::UObject*>& out, bool full_scan);
    void dump_subtitle_setting_diagnostics(RC::Unreal::UObject* owner);
    void dump_visible_widgets_once();

    bool axis_probe_logged_{false};
    bool axis_hook_installed_{false};  // InputAxis 钩子是否已挂（id 在 hooks_ 里）
    RC::Unreal::UFunction* axis_function_{nullptr};
    AxisSink axis_sink_;
};

}  // namespace pbzpm
