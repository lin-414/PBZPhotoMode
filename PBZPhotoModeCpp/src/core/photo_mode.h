#pragma once

#include <string>
#include <vector>

#include "core/config.h"
#include "core/engine_port.h"
#include "core/input_logic.h"

namespace pbzpm {

// 摄影模式状态机。所有与游戏引擎的交互都经过按职责拆分的子接口
//（EnginePort 只是构造时传入的聚合接口），因此 enter/leave/tick 的顺序、回滚、
// 幂等性都能在没有游戏的情况下测出来。
class PhotoMode {
public:
    PhotoMode(EnginePort& engine, Config cfg);

    // 进入失败会自动回滚已做的改动，并把原因写进 last_error()
    bool enter();
    void leave(const char* reason = "user");
    void tick(const Intent& intent, double dt);

    bool active() const { return active_; }
    // 景深/胶片活动时的每帧钩子（GameViewportClient Tick 之后调用）：原生镜头/视图刷新依赖
    // 暂停期常规相机更新（ForceUpdateCameraView 在本环境已停用，见 ue4ss_port_dof.cpp），
    // 门控延迟回读探针也在这里消化。
    void dof_frame_tick();
    // 「已冻结」= 冻结生效且没有被游戏侧改写夺走。freeze_lost_ 为 true 时内部 frozen_ 仍保留
    // （退出时仍要还原），但 UI 必须显示「冻结已失效」，不能再假装画面是停住的。
    bool frozen() const { return frozen_ && !freeze_lost_; }
    // 冻结自检连续重压失败、已停止自动重试：冻结实际失效，等待用户按 F 重新冻结或退出
    bool freeze_lost() const { return freeze_lost_; }
    // 退出时仍有未还原项：调用方（mod_app）需要在 inactive 状态下继续驱动 tick 重试
    bool recovery_pending() const { return recovery_pending_; }

    const CameraPose& pose() const { return pose_; }
    double fov() const { return fov_; }
    // 本次进入是否走了 UUU 式「接管当前相机」（true 时不 spawn、不切 ViewTarget）
    bool takeover_active() const { return takeover_active_; }
    // 夹紧到 [fov_min, fov_max]；返回是否真的改变了
    bool set_fov(double fov);
    void toggle_freeze();
    // dilation 模式每帧重申：只在 frozen 且非 pause 时有动作（需要引擎 tick 后调用）
    void reassert_time_dilation_if_needed();
    // 超宽屏画幅每帧重申：游戏在暂停/处决演出时会重新打开 16:9 锁，必须在渲染前压回。
    void reassert_aspect_ratio_if_needed();
    // 冻结期每帧重申渲染对照目标值（游戏可能把 FG/DLSS/AA/tonemapper 改回去）
    void reassert_render_tuning_if_needed();
    // 冻结期在 World Tick 之后、渲染之前再写一次 MPC 快照（游戏在 World Tick 里会覆盖）
    void reassert_mpc_hold_if_needed();
    // 应用当前配置（配置文件热重载/PBZFM 菜单改值后用）。
    void set_config(const Config& cfg);
    // 运行时换相机后端（接管当前相机 ↔ 独立相机），供 PBZFM 菜单热切换。
    // 未激活时只记下配置；激活时拆旧后端、建新后端并重新下发 FOV。
    // 任何一步失败都会回滚到原后端并返回 false（用户不会落到"没有相机"的状态）。
    bool set_camera_backend_live(bool use_takeover);
    const Config& config() const { return cfg_; }

    // ---- 图形菜单的实时开关：更新配置 + 立即作用于引擎，退出时按最终状态还原 ----
    // 与 enter() 里的同一套 *_hidden_ 记账联动，保证 leave() 的回滚仍然正确
    void set_hide_player_live(bool visible);
    void set_block_game_input_live(bool blocked);
    void set_hide_game_hud_live(bool hidden);

    const std::string& last_error() const { return last_error_; }
    bool pov_fallback_used() const { return pov_fallback_used_; }

private:
    bool apply_pose_if_dirty();

    // 画面效果（暗角/饱和/对比/泛光/冷暖，走后处理体积通道）：值变化时才下发。
    void apply_look_if_needed();
    LookOptions look_applied_{};
    bool look_applied_valid_{false};
    bool look_logged_{false};
    bool look_warn_logged_{false};
    // 景深/胶片（门控 + CineCamera 组件通道，见 dof_math.h）：值变化才下发。
    // 色差的配置键在 [look]，但下发走 DofParams（与景深共用组件通道与胶片闸门）。
    void apply_dof_if_needed(double dt);
    // 对焦/光圈键位的处理（mult 为 Shift/Ctrl 变速倍率）
    void handle_focus_intent(const Intent& intent, double mult);
    // 景深/胶片启用前置：必要时就地切独立相机后端（CineCamera 组件通道的前提）+ 打开门控。
    // 返回 false = 门控不可用（本次会话置 dof_unavailable_，不再尝试）。
    bool ensure_dof_ready(bool need_dof, bool need_film);
    // ensure_dof_ready 的实际建控过程（外层包一层解冻/复冻，理由见 .cpp）
    bool ensure_dof_ready_impl(bool need_dof, bool need_film);
    DofParams dof_applied_{};
    bool dof_applied_valid_{false};
    bool dof_logged_{false};
    bool dof_warn_logged_{false};
    // apply_dof 失败后的重试退避累计（秒；>0 期间不下发，见 tuning.h kDofApplyRetryBackoffSec）
    double dof_apply_retry_accum_{0.0};
    // NLP 档释放失败后的重试退避累计（秒，同 kDofApplyRetryBackoffSec）：
    // 景深主开关关掉后每帧尝试释放 NLP 档（游戏原生过场景深随档位渲染，参数管不到），
    // 失败不每帧重入（每次释放都要走解冻窗口），等退避到期再试
    double nlp_release_retry_accum_{0.0};
    bool nlp_release_warn_logged_{false};
    // 门控已打开（NLP 档 + tagged cvar）；leave 时逆序释放（胶片闸门 → 景深闸门 → NLP 档）
    bool dof_gate_active_{false};
    // 本会话已确认门控/CineCamera 不可用：不再重试（重进摄影模式会重新探测）
    bool dof_unavailable_{false};
    bool dof_unavailable_logged_{false};
    // 后端热切换/自动回退后重新下发 FOV：成功路径与回滚路径都必须走这里。
    void reapply_visuals_live();
    void log(LogLevel level, const std::string& message);
    // 退出/重试共用的还原实现：逐项调用、成功才清标志，失败项写进 failed_items
    bool attempt_restore_all(std::vector<std::string>* failed_items);
    // 是否还有任何需要还原的状态（幂等判断；包含 recovery_pending_）
    bool has_pending_restore() const;
    // inactive 状态下的恢复重试（有限次数 + 退避）
    void tick_recovery(double dt);
    // 接管被证实没生效（写入的 POV 没赶上渲染）时，退出接管并原地重建独立相机。
    // 返回 true 表示已回退成功（spawn + attach 都完成），false 表示回退也失败。
    bool fallback_from_takeover();

    // 只持有实际用到的子接口：构造时从 EnginePort 聚合接口一次性接线，
    // 让状态机看不到与摄影模式无关的引擎细节。
    CameraViewPort& camera_;
    DofPort& dof_;
    InputPort& input_;
    HudPort& hud_;
    TimePausePort& time_pause_;
    AudioPort& audio_;
    FreezeMotionPort& freeze_motion_;
    WorldPort& world_;
    LogPort& log_;
    Config cfg_;

    CameraPose pose_{};
    double fov_{90.0};
    // 进入摄影模式时的原始位姿/FOV，R 键回到这里
    CameraPose reset_pose_{};
    double reset_fov_{90.0};
    bool has_reset_pose_{false};

    bool active_{false};
    bool frozen_{false};
    // 冻结被游戏侧改写夺走且重压放弃：frozen_ 保留用于退出还原，但对外 frozen() 返回 false
    bool freeze_lost_{false};

    // 进入时成功执行了哪些步骤 —— leave 严格按这些标记回滚，不多做也不少做
    bool spawned_{false};
    bool view_attached_{false};
    // UUU 式接管：不生成独立 CameraActor、不切 ViewTarget；退出时停止写 POV。
    bool takeover_active_{false};
    bool ignore_move_set_{false};
    bool game_input_blocked_{false};
    bool player_hidden_{false};
    bool hud_hidden_{false};
    bool prompt_hidden_{false};
    bool bars_hidden_{false};
    bool lockon_hidden_{false};
    bool subtitles_hidden_{false};
    bool aspect_unlocked_{false};

    double prev_dilation_{1.0};
    // 冻结时实际使用的模式：配置在冻结中途被热重载改掉时，解冻仍按进入时的模式还原
    bool frozen_with_pause_{false};
    // 冻结时一并暂停了剧情播放器（解冻/退出时要恢复）
    bool sequences_paused_{false};
    // 冻结时把对话 Flow 控件（WBP_FlowDialog_C）改为 Collapsed 停 Tick（解冻/退出时要恢复 Hidden）
    bool flow_dialog_frozen_{false};
    // 冻结时逐 PlayingID 真暂停已生效（无 ID 时组件音量兜底；解冻/退出时要恢复）
    bool wwise_paused_{false};
    // 无逐语音 PlayingID 时整引擎 Suspend 作为静音兜底（解冻/退出时要 WakeupFromSuspend）
    bool wwise_engine_suspended_{false};
    // 冻结时停用了相机修饰器/结束 shake（解冻/退出时按原状态还原）
    bool shakes_frozen_{false};
    // 冻结时风源参数归零了（解冻/退出时要还原原值）
    bool wind_frozen_{false};
    // MPC 快照钉住生效中（冻结期每帧写回，解冻/退出时释放）
    bool mpc_hold_active_{false};
    // 冻结时挂起了布料模拟（SkeletalMesh/ChaosCloth，解冻/退出时要恢复）
    bool cloth_frozen_{false};
    // 冻结时处理了 PBZ 自定义 Sway/Cloth 全局开关（解冻/退出时要恢复）
    bool custom_cloth_frozen_{false};
    // 冻结时关闭了 SkeletalMesh 的 WPO 评估（解冻/退出时要恢复）
    bool wpo_frozen_{false};
    // 冻结时暂停了骨骼动画（bPauseAnims/bNoSkeletonUpdate）
    bool animation_frozen_{false};
    // 冻结时关闭了动态模糊（r.MotionBlurQuality）
    bool motion_blur_frozen_{false};
    // 渲染对照 CVar 已被本 Mod 改写（解冻/退出时必须还原）
    bool render_tuning_active_{false};
    // 光照 A/B CVar 已被本 Mod 改写（与渲染对照分开记账；解冻/退出时必须还原）
    bool lighting_tuning_active_{false};
    // 冻结期光照阶段自动轮换进行中（每 phase_seconds 切一档）
    bool lighting_ab_cycle_active_{false};
    // 阴影/后处理 A/B CVar 已被本 Mod 改写（解冻/退出时必须还原）
    bool shadow_tuning_active_{false};
    // 冻结期每秒校验暂停/时间缩放是否被游戏改写；连续 3 次失守后停止对抗并告警
    double freeze_check_accum_{0.0};
    int freeze_reassert_count_{0};
    bool freeze_reassert_gave_up_{false};
    // 冻结期布料闸门累加器：每秒复核登记的布料组件是否仍处于挂起态（见 engine_port.h
    // cloth_gate_tick 注释）。游戏侧布料管理可能在冻结期按可见性/距离把布料翻回活模拟。
    double cloth_gate_accum_{0.0};
    // 冻结诊断日志节流：状态翻转立即输出，持续期每 kFreezeDiagIntervalSec 秒最多一条
    double freeze_diag_seconds_{0.0};
    bool freeze_diag_has_last_{false};
    bool freeze_diag_last_paused_{false};
    bool freeze_diag_last_lost_{false};
    double reassert_accum_{0.0};
    double hud_retry_accum_{0.0};
    double prompt_retry_accum_{0.0};
    double bars_retry_accum_{0.0};
    double lockon_retry_accum_{0.0};
    double subtitles_retry_accum_{0.0};
    // 每秒重试的可见性设置（HUD/交互提示/血条）失败计数：每次重试在引擎侧都要按候选
    // 类名做全对象数组扫描（一次几十毫秒），无限重试等于周期性卡顿，连续失败就放弃
    int hud_retry_count_{0};
    int prompt_retry_count_{0};
    int bars_retry_count_{0};
    int lockon_retry_count_{0};
    bool pose_dirty_{false};
    bool pov_fallback_used_{false};
    // 退出后的恢复重试：失败项已保留在各自标志里，这里只记节奏
    bool recovery_pending_{false};
    int recovery_attempts_{0};
    double recovery_accum_{0.0};
    std::string last_error_{};
};

}  // namespace pbzpm
