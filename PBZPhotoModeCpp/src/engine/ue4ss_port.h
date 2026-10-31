#pragma once

#include <filesystem>
#include <functional>
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

    bool unlock_aspect_ratio() override;
    void restore_aspect_ratio() override;

    double get_time_dilation() override;
    bool set_time_dilation(double dilation) override;
    bool set_paused(bool paused) override;

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

    bool axis_probe_logged_{false};
    bool axis_hook_installed_{false};
    RC::Unreal::UFunction* axis_function_{nullptr};
    AxisSink axis_sink_;
};

}  // namespace pbzpm
