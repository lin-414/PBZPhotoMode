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

// UMG 交互菜单：动作输出与点击输入（命名空间层：mod_app 也要用，不能嵌在未完整声明的类里）
struct Umg2Commands {
    int fov_step{0};           // -1/+1 个 fov_step（配置里默认 2.0）
    double speed_delta{0.0};   // ±20 单位
    double sens_delta{0.0};    // ±0.01
    bool toggle_freeze{false};
    bool reset_view{false};
    bool exit_mode{false};
};
struct Umg2Clicks {
    bool fov_dec{false}, fov_inc{false};
    bool speed_dec{false}, speed_inc{false};
    bool sens_dec{false}, sens_inc{false};
    bool freeze{false}, reset{false}, exit{false};
};

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

    double get_time_dilation() override;
    bool set_time_dilation(double dilation) override;
    bool set_paused(bool paused) override;

    bool save_postfx() override;
    void restore_postfx() override;

    void log(LogLevel level, const std::string& message) override;

    // ---- 仅 DLL 版使用：游戏内右下角文字菜单（UMG 控件）----
    // 动态拼一个 UserWidget（CanvasPanel 根 + TextBlock）AddToViewport 常驻，显示/隐藏走
    // SetVisibility。旧方案两条路在本作 Shipping 包里都不通：HUD 是纯 UMG，AHUD::DrawHUD 不被
    // 调用；PrintString 的函数体被 #if !UE_BUILD_SHIPPING 整体裁掉（详见 PROGRESS 1.0.19）。
    bool show_overlay(const std::string& text);
    void hide_overlay();
    // 诊断：UMG 菜单当前状态（未创建/显示/隐藏），随每秒诊断行输出
    std::string umg_menu_state() const;
    // 进模式时把关键引擎对象打一遍（PC/Pawn/HUD/相机/ViewTarget），真机排障用
    void dump_engine_state();

    // ---- UMG 交互菜单（v1.2.0：帧生成环境下用游戏原生控件做图形菜单）----
    // 游戏自己的 UI 系统：帧生成把它当普通画面合成，零冲突。交互全部走轮询
    // （滑条每帧读 Value、按钮轮询 IsPressed 上升沿），不碰 Shipping 下不可控的委托绑定。
    // 构建成功且未标记不可用（构建失败会永久落回文字菜单）
    bool umg2_available() const;
    // 菜单可见时的每帧调用：同步状态/标签，轮询滑条与按钮，动作写入 out。返回 false=不可用
    bool umg2_tick(const std::string& status, double fov, double speed, double sens,
                   const Umg2Clicks& clicks, Umg2Commands& out);
    // 隐藏菜单（沿触发；顺带恢复鼠标指针状态）
    void umg2_hide();
    std::string umg2_state() const;
    // Slate（逻辑）视口尺寸：UMG 锚点/偏移都按这个坐标系算，物理像素 = Slate × UI缩放。
    // 走 KismetSystemLibrary.GetViewportSize（与 KismetTextLibrary 同族的纯引擎静态工具）。
    bool umg2_viewport_size(float& w, float& h);
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
    // UMG 菜单：构建 / 写文本。控件可能被换图/GC 清掉，用 umg_live() 按全名查活。
    bool ensure_umg_menu();
    bool umg_set_text(const std::string& text);
    RC::Unreal::UObject* umg_live() const;
    void umg_drop();
    // UMG 交互菜单
    bool ensure_umg2_menu();
    RC::Unreal::UObject* umg2_live() const;
    void umg2_drop();
    bool umg2_set_text(RC::Unreal::UObject* widget, const std::string& text);
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
    bool overlay_visible_{false};
    // 被隐藏的 UMG HUD 根控件（控件指针 + 原始 ESlateVisibility 值），退出时原样恢复
    std::vector<std::pair<RC::Unreal::UObject*, int>> hud_widgets_;
    bool hud_widget_missing_logged_{false};
    bool pv_hide_active_{false};  // 本次隐藏是否走了 UIManager.SetHideUIForPV
    // UMG 菜单控件（UserWidget → WidgetTree → CanvasPanel → TextBlock）
    RC::Unreal::UObject* umg_widget_{nullptr};
    RC::Unreal::UObject* umg_tree_{nullptr};
    RC::Unreal::UObject* umg_canvas_{nullptr};
    RC::Unreal::UObject* umg_text_{nullptr};
    std::string umg_widget_path_;    // 创建时的 GetFullName()；查活用，不解引用缓存指针
    std::string umg_last_text_;
    int umg_visibility_state_{-1};   // 最近一次 SetVisibility 的值（-1=未知），避免每帧重复调用
    bool umg_shown_{false};          // 菜单可见→隐藏的沿检测（隐藏路径只在沿上碰引擎）
    bool umg_failed_logged_{false};  // 同类失败只打一次日志
    bool umg_ready_logged_{false};
    bool slot_props_logged_{false};  // 槽位类属性名 dump 只打一次
    double umg_last_vp_check_{0.0};  // IsInViewport 周期校验
    double umg_last_rebuild_{0.0};   // 重建节流：查活失败时最多 0.5 秒重建一次
    bool axis_probe_logged_{false};
    bool axis_hook_installed_{false};
    RC::Unreal::UFunction* axis_function_{nullptr};
    AxisSink axis_sink_;
    std::string overlay_text_;

    // UMG 交互菜单控件缓存（查活走 umg2_path_，与文字菜单同款防 GC 策略）
    RC::Unreal::UObject* umg2_widget_{nullptr};
    RC::Unreal::UObject* umg2_tree_{nullptr};
    RC::Unreal::UObject* umg2_status_{nullptr};
    RC::Unreal::UObject* umg2_fov_value_{nullptr};
    RC::Unreal::UObject* umg2_speed_value_{nullptr};
    RC::Unreal::UObject* umg2_sens_value_{nullptr};
    std::string umg2_path_;
    bool umg2_unavailable_{false};
    bool umg2_shown_{false};
    bool umg2_ready_logged_{false};
    bool umg2_failed_logged_{false};
    bool umg2_cursor_shown_{false};
    double umg2_last_rebuild_{0.0};
    double umg2_vp_check_{0.0};
    double umg2_last_fov_{-1.0};
    double umg2_last_speed_{-1.0};
    double umg2_last_sens_{-1.0};

    std::string umg2_last_status_;
    std::string umg2_last_fov_text_;
    std::string umg2_last_speed_text_;
    std::string umg2_last_sens_text_;
    bool umg2_prev_freeze_{false};
    bool umg2_prev_reset_{false};
    bool umg2_prev_exit_{false};
    int umg2_visibility_state_{-1};

};

}  // namespace pbzpm
