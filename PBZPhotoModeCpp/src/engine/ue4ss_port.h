#pragma once

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/engine_port.h"

// 说明：本文件只在 PBZPM_WITH_UE4SS 下参与编译。
// 所有 UE4SS/UE 反射调用都集中在 ue4ss_port.cpp，上层逻辑（core/）不接触引擎类型。
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
    bool set_subtitles_visible(bool visible) override;

    bool unlock_aspect_ratio() override;
    void restore_aspect_ratio() override;

    double get_time_dilation() override;
    bool set_time_dilation(double dilation) override;
    bool set_paused(bool paused) override;
    bool world_paused() override;
    SequenceFreezeReport pause_sequences(bool paused) override;
    int wwise_component_count() override;
    int pause_wwise_voices(bool pause) override;
    // 尽早挂上 Post 钩子截获 PlayingID（mod_app 引擎 tick 里每 2 秒重试直到成功）
    bool install_voice_hooks();

    bool save_postfx() override;
    void restore_postfx() override;

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
    RC::Unreal::UObject* world_settings() const;
    RC::Unreal::UObject* controller() const;
    RC::Unreal::UObject* world() const;
    RC::Unreal::UObject* camera_manager() const;
    RC::Unreal::UObject* camera_component();
    RC::Unreal::UObject* player_pawn() const;

    std::filesystem::path mod_dir_;
    RC::Unreal::UObject* camera_actor_{nullptr};
    RC::Unreal::UObject* camera_component_cache_{nullptr};
    bool postfx_saved_{false};
    double saved_dilation_{1.0};

    // 超宽屏画幅解锁：进入时保存的相机管理器原值（aspect_saved_=true 时有效）
    bool aspect_saved_{false};
    bool aspect_original_constrain_{false};
    double aspect_original_value_{16.0 / 9.0};
    RC::Unreal::UObject* aspect_manager_{nullptr};

    // 时间冻结：直接写 AWorldSettings 的字段。UGameplayStatics::SetGlobalTimeDilation
    // 会被 bAllowTimeDilation / MinGlobalTimeDilation / MaxGlobalTimeDilation 拦住，
    // 所以在第一次改时间前把原始值存下来，解冻/退出时原样还原。
    bool time_settings_saved_{false};
    bool time_override_active_{false};
    bool time_original_allow_{true};
    double time_original_dilation_{1.0};
    double time_original_min_{0.0001};
    double time_original_max_{20.0};
    double time_original_effective_{1.0};

    // 暂停模式：暂停时要让 PlayerController 继续完整 Tick，否则相机管理器会停住，
    // 摄影相机无法移动/转向；同时临时把 GameMode 的 bPauseable 打开。
    bool pause_flags_saved_{false};
    bool pause_original_full_tick_{false};
    bool pause_original_game_mode_pauseable_{true};

    // Wwise 组件可用函数只探测一次，避免每次冻结都重复打日志
    bool wwise_probe_done_{false};
    void probe_wwise_components(const std::vector<RC::Unreal::UObject*>& components);
    // 对话语音 PlayingID 截获：钩住 PBZSoundFXSubsystem 的 Post 系列函数，
    // 冻结/解冻用游戏自有 PBZPausePlayingID / PBZResumePlayingID 真暂停（原位置续播）
    bool voice_hooks_installed_{false};
    bool pause_signature_logged_{false};  // 暂停接口签名只打一次
    std::vector<int32_t> playing_ids_{};
    std::set<int32_t> playing_ids_seen_{};
    void on_voice_post(void* result_decl);
    // 一次性音频对象普查：全对象数组里认 Ak/Audio/Sound/Voice/Dialog 相关类，
    // 为“对话语音走 Wwise 直接 post、组件 Stop 够不着”找下一版的目标
    bool audio_dump_done_{false};
    void dump_audio_objects_once();

    mutable RC::Unreal::UObject* controller_cache_{nullptr};
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
    bool subtitle_scanned_{false};  // 本次摄影模式会话是否做过全量扫描（每秒重申只用快速路径）
    bool subtitle_visible_dumped_{false};  // 可见 UserWidget 清单只打一次
    int subtitle_reassert_count_{0};  // 重申计数（每 10 次做一次全量扫描，收编对话中途新出现的控件类）
    // 实机发现过的字幕控件类名（进入时全量扫描记录；每秒重申只按这些名字 FindAllOf，避免全对象扫描拖帧）
    std::vector<std::string> subtitle_widget_class_names_;
    void collect_subtitle_widgets_impl(std::vector<RC::Unreal::UObject*>& out, bool full_scan);
    void dump_subtitle_setting_diagnostics(RC::Unreal::UObject* owner);
    void dump_visible_widgets_once();

    bool axis_probe_logged_{false};
    bool axis_hook_installed_{false};
    RC::Unreal::UFunction* axis_function_{nullptr};
    AxisSink axis_sink_;
};

}  // namespace pbzpm
