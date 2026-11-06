#include "core/photo_mode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/tuning.h"
#include "core/version.h"

namespace pbzpm {
namespace {

// 成组调参常量统一放在 core/tuning.h，避免与 mod_app / 引擎层数值分叉。
using tuning::kFreezeDiagIntervalSec;
using tuning::kMaxFrameDt;
using tuning::kMaxRecoveryAttempts;
using tuning::kMaxVisibilityRetries;
using tuning::kPitchSpeedDegPerSec;
using tuning::kRecoveryMaxDelaySec;

auto recovery_delay_seconds(int attempt) -> double {
    double delay = 1.0;
    for (int i = 0; i < attempt && delay < kRecoveryMaxDelaySec; ++i) delay *= 2.0;
    return delay > kRecoveryMaxDelaySec ? kRecoveryMaxDelaySec : delay;
}

auto join_items(const std::vector<std::string>& items) -> std::string {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) text += (i == 0 ? "" : "、") + items[i];
    return text;
}

}  // namespace

PhotoMode::PhotoMode(EnginePort& engine, Config cfg)
    : camera_(engine), input_(engine), hud_(engine), time_pause_(engine), audio_(engine),
      freeze_motion_(engine), world_(engine), log_(engine), cfg_(cfg) {
    // M-6：把配置里的日志阈值立刻交给引擎侧，避免用户设 log_level=warn 仍被 Info 刷屏
    log_.set_log_level(cfg_.log_level);
}

void PhotoMode::log(LogLevel level, const std::string& message) {
    // 配置热重载后 cfg_ 会变，这里每次都同步一次，保证运行中切换级别立即生效
    log_.set_log_level(cfg_.log_level);
    log_.log(level, std::string("[PhotoMode] ") + message);
}

void PhotoMode::set_config(const Config& cfg) {
    // 运行期改动由 tick/enter 读取 cfg_ 生效（画面效果由 apply_look_if_needed 比较后下发）。
    cfg_ = cfg;
}

// 运行时换相机后端（PBZFM 菜单热切换）。
// 两条后端都是 enter() 时建立的，所以改这一项不能只改配置：必须把旧后端完整拆掉再建新的，
// 中途任何一步失败都回滚到原后端——绝不能让用户点了开关之后没有相机可用。
bool PhotoMode::set_camera_backend_live(bool use_takeover) {
    cfg_.takeover_current_camera = use_takeover;
    if (!active_) return true;                          // 未激活：下次 enter() 按新配置建立
    if (use_takeover == takeover_active_) return true;  // 已经是目标后端（含 enter 时回退过的情况）

    if (use_takeover) {
        // 独立相机 → 接管：先让游戏拿回视角（失败就保持现状，否则会对着已销毁的相机渲染），
        // 再销毁我们建的 Actor，最后接管当前 POV。
        if (!camera_.restore_view()) {
            log(LogLevel::Warn, "切换到接管失败：无法还原游戏视角，保持独立相机");
            cfg_.takeover_current_camera = takeover_active_;  // 失败回滚：配置必须跟实际后端一致
            return false;
        }
        camera_.destroy_camera();
        spawned_ = false;
        view_attached_ = false;

        if (!camera_.takeover_current_camera(pose_)) {
            log(LogLevel::Warn, "切换到接管失败：回退到独立相机");
            if (camera_.spawn_camera(pose_)) {
                spawned_ = true;
                view_attached_ = camera_.attach_view();
            }
            cfg_.takeover_current_camera = takeover_active_;  // 失败回滚：配置必须跟实际后端一致
            reapply_visuals_live();
            return false;
        }
        takeover_active_ = true;
        log(LogLevel::Info, "已切换相机后端：接管当前相机（UUU 式，不 spawn、不换 ViewTarget）");
    } else {
        // 接管 → 独立相机：退出接管会顺手把 POV 上的后处理快照还原，再 spawn 新相机。
        if (!camera_.leave_camera_takeover()) {
            log(LogLevel::Warn, "退出接管时后处理快照未完全还原，继续切换相机后端");
        }
        takeover_active_ = false;

        if (!camera_.spawn_camera(pose_)) {
            log(LogLevel::Warn, "切换到独立相机失败：回退到接管当前相机");
            if (camera_.takeover_current_camera(pose_)) takeover_active_ = true;
            cfg_.takeover_current_camera = takeover_active_;  // 失败回滚：配置必须跟实际后端一致
            reapply_visuals_live();
            return false;
        }
        spawned_ = true;
        if (!camera_.attach_view()) {
            camera_.destroy_camera();
            spawned_ = false;
            log(LogLevel::Warn, "切换后设置 ViewTarget 失败：回退到接管当前相机");
            if (camera_.takeover_current_camera(pose_)) takeover_active_ = true;
            cfg_.takeover_current_camera = takeover_active_;  // 失败回滚：配置必须跟实际后端一致
            reapply_visuals_live();
            return false;
        }
        view_attached_ = true;
        log(LogLevel::Info, "已切换相机后端：独立相机（CineCameraActor，FOV 会同步真实焦距）");
    }

    // 新后端要重新吃到当前 FOV：旧后端上的 FOV 不会跟着搬家。
    reapply_visuals_live();
    return true;
}

void PhotoMode::reapply_visuals_live() {
    (void)camera_.apply_fov(fov_);
}

bool PhotoMode::enter() {
    if (active_) {
        log(LogLevel::Warn, "已经在摄影模式中，忽略重复进入");
        return true;
    }
    last_error_.clear();
    pov_fallback_used_ = false;
    // 前一批收尾：清掉运行期函数查找缓存，进入时重新探测（避免函数暂时没就绪就永久缺失）
    camera_.refresh_function_caches();

    // 上一次退出的残留改动还没还原：先补一次，仍失败就拒绝进入。
    // 否则 enter() 会把 frozen_ 等标志清零，而世界其实还冻结着，之后再退出也还原不回来。
    if (has_pending_restore()) {
        std::vector<std::string> failed;
        if (!attempt_restore_all(&failed)) {
            last_error_ = "上次退出的残留改动仍未还原（" + join_items(failed) + "），本次进入已取消";
            log(LogLevel::Error, last_error_);
            return false;
        }
        recovery_pending_ = false;
        recovery_attempts_ = 0;
        recovery_accum_ = 0.0;
        log(LogLevel::Info, "上次退出的残留改动已补还原，继续进入摄影模式");
    }

    if (!world_.world_valid()) {
        last_error_ = "世界未加载（UWorld 不可用）";
        log(LogLevel::Error, last_error_);
        return false;
    }

    CameraPose pov{};
    if (camera_.read_pov(pov)) {
        pose_ = pov;
    } else {
        pov_fallback_used_ = true;
        pose_ = CameraPose{};
        pose_.fov = cfg_.fov_default;
        log(LogLevel::Warn, "读不到当前视角，使用默认位姿与默认 FOV");
    }
    fov_ = std::clamp(pose_.fov > 1.0 ? pose_.fov : cfg_.fov_default, cfg_.fov_min, cfg_.fov_max);
    pose_.fov = fov_;
    reset_pose_ = pose_;
    reset_fov_ = fov_;
    has_reset_pose_ = true;

    // 默认先尝试 UUU 式「接管当前相机」：不 spawn、不换 ViewTarget，直接每帧覆盖当前相机 POV。
    // 接管失败必须在同一次 enter 内自动回退到独立相机——绝不能让「无法进入摄影模式」重现。
    bool takeover_failed = false;
    if (cfg_.takeover_current_camera) {
        if (camera_.takeover_current_camera(pose_)) {
            takeover_active_ = true;
            log(LogLevel::Info, "已接管当前相机（不生成独立 CameraActor、不切换 ViewTarget）");
        } else {
            takeover_failed = true;
            log(LogLevel::Warn,
                "接管当前相机失败（拿不到当前相机 POV 或写不进变换），本次进入自动回退到独立相机"
                "（spawn + SetViewTargetWithBlend）");
        }
    }

    if (!takeover_active_) {
        if (!camera_.spawn_camera(pose_)) {
            last_error_ = takeover_failed ? "接管当前相机失败，且独立 CameraActor 创建也失败"
                                          : "创建 CameraActor 失败";
            log(LogLevel::Error, last_error_);
            return false;
        }
        spawned_ = true;

        if (!camera_.attach_view()) {
            last_error_ = takeover_failed ? "接管当前相机失败，且独立相机的 ViewTarget 设置也失败"
                                          : "设置 ViewTarget 失败";
            log(LogLevel::Error, last_error_);
            camera_.destroy_camera();
            spawned_ = false;
            return false;
        }
        view_attached_ = true;
    }

    // 以下步骤失败都不致命：记 warning，继续进模式
    if (cfg_.block_game_input) {
        ignore_move_set_ = input_.set_ignore_move_input(true);
        if (!ignore_move_set_) log(LogLevel::Warn, "屏蔽角色移动输入失败，游玩输入可能仍在生效");

        // 光挡移动轴不够：左右键攻击/交互走的是动作绑定，必须让整个输入栈在 PC 处中断
        game_input_blocked_ = input_.set_game_input_blocked(true);
        if (!game_input_blocked_) {
            log(LogLevel::Warn, "屏蔽游戏键鼠输入失败，鼠标左右键可能仍会触发游戏动作");
        } else {
            log(LogLevel::Info, "已屏蔽游戏键鼠输入（角色不会跟着 WASD 走）");
        }
    }
    if (cfg_.hide_player) {
        player_hidden_ = hud_.set_player_hidden(true);
        if (!player_hidden_) log(LogLevel::Warn, "隐藏角色失败");
    }
    if (cfg_.hide_game_hud) {
        hud_hidden_ = hud_.set_hud_visible(false);
        if (!hud_hidden_) log(LogLevel::Warn, "隐藏 HUD 失败");
    }
    if (cfg_.hide_interact_prompt) {
        prompt_hidden_ = hud_.set_interact_prompts_visible(false);
        if (!prompt_hidden_) log(LogLevel::Warn, "隐藏交互提示失败");
    }
    if (cfg_.hide_enemy_health_bars) {
        bars_hidden_ = hud_.set_enemy_health_bars_visible(false);
        if (!bars_hidden_) log(LogLevel::Warn, "隐藏敌人血条失败");
    }
    if (cfg_.hide_lockon_marker) {
        lockon_hidden_ = hud_.set_lockon_marker_visible(false);
        if (!lockon_hidden_) log(LogLevel::Warn, "隐藏锁定标记失败");
    }
    if (cfg_.hide_subtitles) {
        subtitles_hidden_ = hud_.set_subtitles_visible(false);
        if (!subtitles_hidden_) log(LogLevel::Warn, "隐藏对话字幕失败");
    }
    if (cfg_.ultrawide_unlock) {
        aspect_unlocked_ = camera_.unlock_aspect_ratio();
        if (!aspect_unlocked_) log(LogLevel::Warn, "画幅解锁失败：超宽屏（21:9/32:9）两侧黑边可能不会消除");
    }

    prev_dilation_ = time_pause_.get_time_dilation();
    // 异常值兜底：处决/处刑/命中停顿会把全局倍率压到接近 0（0、1e-4、0.05 都见过）。
    // 若把这种“演出瞬时值”当成进入前的原始倍率，退出摄影模式后会把它写回去，角色就永久停在
    // 慢动作/定格（实测必须重启）。只接受正常可玩区间；低于 0.1 一律按 1.0 还原。
    if (!(prev_dilation_ >= 0.1) || prev_dilation_ > 10.0) prev_dilation_ = 1.0;

    camera_.apply_fov(fov_);
    camera_.apply_pose(pose_);

    active_ = true;
    frozen_ = false;
    // 冻结模式必须按本次进入重新决定：旧会话的 paused/dilation 选择不能带进新会话
    frozen_with_pause_ = false;
    sequences_paused_ = false;
    flow_dialog_frozen_ = false;
    wwise_paused_ = false;
    wwise_engine_suspended_ = false;
    shakes_frozen_ = false;
    wind_frozen_ = false;
    mpc_hold_active_ = false;
    cloth_frozen_ = false;
    custom_cloth_frozen_ = false;
    wpo_frozen_ = false;
    animation_frozen_ = false;
    motion_blur_frozen_ = false;
    render_tuning_active_ = false;
    lighting_tuning_active_ = false;
    lighting_ab_cycle_active_ = false;
    shadow_tuning_active_ = false;
    freeze_check_accum_ = 0.0;
    freeze_reassert_count_ = 0;
    freeze_reassert_gave_up_ = false;
    freeze_lost_ = false;
    freeze_diag_seconds_ = 0.0;
    freeze_diag_has_last_ = false;
    pose_dirty_ = false;
    reassert_accum_ = 0.0;
    hud_retry_accum_ = 0.0;
    prompt_retry_accum_ = 0.0;
    bars_retry_accum_ = 0.0;
    lockon_retry_accum_ = 0.0;
    subtitles_retry_accum_ = 0.0;
    hud_retry_count_ = 0;
    prompt_retry_count_ = 0;
    bars_retry_count_ = 0;
    lockon_retry_count_ = 0;

    char buf[160]{};
    std::snprintf(buf, sizeof(buf), "进入摄影模式 fov=%.1f%s", fov_, pov_fallback_used_ ? "（默认位姿）" : "");
    log(LogLevel::Info, buf);

    // 配置默认开启：进入摄影模式即自动冻结时间，不用再按 F。
    if (cfg_.auto_pause_time) {
        log(LogLevel::Info, "自动暂停时间：进入即冻结");
        toggle_freeze();
    }
    return true;
}

bool PhotoMode::has_pending_restore() const {
    // 幂等判断必须覆盖所有“进入时可能改动过”的标志（旧版漏了 frozen_ / sequences_paused_ /
    // shakes_frozen_ / wind_frozen_ / mpc_hold_active_，导致这些状态下 leave 早退不还原）
    return active_ || spawned_ || view_attached_ || takeover_active_ || ignore_move_set_ ||
           game_input_blocked_ || player_hidden_ ||
           hud_hidden_ || prompt_hidden_ || bars_hidden_ || lockon_hidden_ || subtitles_hidden_ || aspect_unlocked_ ||
           frozen_ || sequences_paused_ || flow_dialog_frozen_ || wwise_paused_ ||
           wwise_engine_suspended_ || shakes_frozen_ || wind_frozen_ || mpc_hold_active_ || cloth_frozen_ ||
           custom_cloth_frozen_ || wpo_frozen_ || animation_frozen_ || motion_blur_frozen_ || render_tuning_active_ ||
           lighting_tuning_active_ ||
           lighting_ab_cycle_active_ || shadow_tuning_active_ || recovery_pending_;
}

bool PhotoMode::attempt_restore_all(std::vector<std::string>* failed_items) {
    auto fail = [failed_items](const char* what) {
        if (failed_items != nullptr) failed_items->push_back(what);
    };

    if (takeover_active_) {
        if (camera_.leave_camera_takeover()) {
            takeover_active_ = false;
        } else {
            fail("接管当前相机未退出（游戏相机仍被每帧覆盖）");
        }
    }

    // 冻结期压住的变化源：严格逆序释放（阴影对照 → 光照对照 → 渲染对照 → 动态模糊 → 骨骼动画 → 布料 → MPC → 风 → 相机），再恢复时间/剧情/音频。
    // 每一项成功才清对应标志；失败保留，由 tick 退避重试，不在同一帧连打。
    if (shadow_tuning_active_) {
        const RenderTuningReport shadow_report = freeze_motion_.freeze_shadow_ab(false);
        if (shadow_report.ok) {
            shadow_tuning_active_ = false;
        } else {
            fail("阴影/后处理对照 CVar 未还原");
        }
    }
    if (lighting_tuning_active_) {
        const RenderTuningReport lighting_report = freeze_motion_.freeze_lighting(false, LightingTuningOptions{});
        if (lighting_report.ok) {
            lighting_tuning_active_ = false;
            lighting_ab_cycle_active_ = false;
        } else {
            fail("光照对照 CVar 未还原（Lumen/屏幕空间光照可能保持关闭）");
        }
    }
    if (render_tuning_active_) {
        if (freeze_motion_.freeze_render_tuning(false, false, false, false, false).ok) {
            render_tuning_active_ = false;
        } else {
            fail("渲染对照 CVar 未还原（AA/速度输出/FG 可能保持关闭）");
        }
    }
    if (motion_blur_frozen_) {
        if (freeze_motion_.freeze_motion_blur(false)) {
            motion_blur_frozen_ = false;
        } else {
            fail("动态模糊未恢复");
        }
    }
    if (animation_frozen_) {
        if (freeze_motion_.freeze_skeletal_animation(false).ok) {
            animation_frozen_ = false;
        } else {
            fail("骨骼动画未恢复（角色可能保持静止）");
        }
    }
    if (wpo_frozen_) {
        if (freeze_motion_.freeze_wpo(false).ok) {
            wpo_frozen_ = false;
        } else {
            fail("WPO 评估未恢复（材质顶点动画可能保持关闭）");
        }
    }
    if (custom_cloth_frozen_) {
        if (freeze_motion_.freeze_custom_cloth(false).ok) {
            custom_cloth_frozen_ = false;
        } else {
            fail("自定义布料/摆动未恢复（Sway/全局 Cloth 可能保持冻结）");
        }
    }
    if (cloth_frozen_) {
        if (freeze_motion_.freeze_cloth(false).ok) {
            cloth_frozen_ = false;
        } else {
            fail("布料模拟未恢复（裙摆/披风可能保持冻结姿态）");
        }
    }
    if (mpc_hold_active_) {
        freeze_motion_.mpc_release_snapshot();  // 无返回值：释放是幂等的
        mpc_hold_active_ = false;
    }
    if (wind_frozen_) {
        if (freeze_motion_.freeze_wind(false).ok) {
            wind_frozen_ = false;
        } else {
            fail("风源参数未还原（布料/植被仍完全静止）");
        }
    }
    if (shakes_frozen_) {
        if (freeze_motion_.freeze_camera_shakes(false).ok) {
            shakes_frozen_ = false;
        } else {
            fail("相机修饰器未还原（画面可能缺少镜头抖动/后处理修饰）");
        }
    }
    if (sequences_paused_) {
        if (time_pause_.pause_sequences(false).ok) {
            sequences_paused_ = false;
        } else {
            fail("剧情播放器仍暂停（过场动画不会继续播放）");
        }
    }

    // 时间/暂停：只有确实恢复成功才清 frozen_（失败时保持冻结状态，后续 tick 继续重试）
    if (frozen_) {
        if (frozen_with_pause_) {
            if (time_pause_.set_paused(false)) {
                frozen_ = false;
                frozen_with_pause_ = false;
                freeze_lost_ = false;
            } else {
                fail("游戏暂停未解除（世界仍停住）");
            }
        } else if (time_pause_.restore_time_dilation(prev_dilation_)) {
            frozen_ = false;
            freeze_lost_ = false;
        } else {
            fail("时间缩放未还原（世界仍是慢动作/冻结）");
        }
    }

    // Wwise：先唤醒整引擎再恢复逐语音（顺序不能反）。Wake 失败保留标志以便重试。
    if (wwise_engine_suspended_) {
        if (audio_.suspend_wwise_engine(false)) {
            wwise_engine_suspended_ = false;
        } else {
            fail("Wwise 音频引擎仍处于挂起（全场无声）");
        }
    }
    if (wwise_paused_) {
        // 返回 PlayingID 数（0=没有截获到 ID，组件音量兜底已执行）；该接口没有失败信号
        audio_.pause_wwise_voices(false);
        wwise_paused_ = false;
    }
    if (flow_dialog_frozen_) {
        // 返回实际操作控件数；0=场上已没有 FlowDialog（随世界失效），无需还原
        hud_.set_dialogue_flow_tick_paused(false);
        flow_dialog_frozen_ = false;
    }

    // 处决/过场演出会把玩家角色的 CustomTimeDilation 或骨骼动画暂停标志留在演出状态；
    // 冻结结束后演出可能已经不会再跑“收尾”，必须在退出时把玩家这一份显式恢复，
    // 否则表现为「退出摄影模式后角色模型卡住，只能重启游戏」。
    time_pause_.recover_character_after_photo_mode();

    // 进入模式时做的改动，逆序还原
    if (aspect_unlocked_) {
        camera_.restore_aspect_ratio();  // 无返回值：引擎侧无失败信号
        aspect_unlocked_ = false;
    }
    if (hud_hidden_) {
        if (hud_.set_hud_visible(true)) {
            hud_hidden_ = false;
        } else {
            fail("HUD 仍处于隐藏（游戏界面不会出现）");
        }
    }
    if (prompt_hidden_) {
        if (hud_.set_interact_prompts_visible(true)) {
            prompt_hidden_ = false;
        } else {
            fail("交互提示仍隐藏（可互物体旁的按键提示不会出现）");
        }
    }
    if (bars_hidden_) {
        if (hud_.set_enemy_health_bars_visible(true)) {
            bars_hidden_ = false;
        } else {
            fail("敌人血条仍隐藏");
        }
    }
    if (lockon_hidden_) {
        if (hud_.set_lockon_marker_visible(true)) {
            lockon_hidden_ = false;
        } else {
            fail("锁定标记仍隐藏或锁定目标未恢复");
        }
    }
    if (subtitles_hidden_) {
        if (hud_.set_subtitles_visible(true)) {
            subtitles_hidden_ = false;
        } else {
            fail("对话字幕仍隐藏");
        }
    }
    if (player_hidden_) {
        if (hud_.set_player_hidden(false)) {
            player_hidden_ = false;
        } else {
            fail("角色仍隐藏（看不见自己）");
        }
    }
    if (game_input_blocked_) {
        if (input_.set_game_input_blocked(false)) {
            game_input_blocked_ = false;
        } else {
            fail("游戏键鼠输入仍被屏蔽（按键不会响应）");
        }
    }
    if (ignore_move_set_) {
        if (input_.set_ignore_move_input(false)) {
            ignore_move_set_ = false;
        } else {
            fail("角色移动输入仍被屏蔽");
        }
    }
    if (view_attached_) {
        if (camera_.restore_view()) {
            view_attached_ = false;
        } else {
            fail("视角仍被摄影相机接管（镜头不跟随角色）");
        }
    }
    if (spawned_) {
        // 该接口返回“原本是否有相机”，不是成功与否；销毁是幂等的
        camera_.destroy_camera();
        spawned_ = false;
    }
    return failed_items == nullptr || failed_items->empty();
}

void PhotoMode::leave(const char* reason) {
    (void)camera_.set_look(LookOptions{});
    look_applied_valid_ = false;
    if (!has_pending_restore()) {
        return;  // 幂等：没进过模式、也没有残留改动
    }

    std::vector<std::string> failed;
    const bool done = attempt_restore_all(&failed);
    active_ = false;
    pose_dirty_ = false;

    if (done) {
        recovery_pending_ = false;
        recovery_attempts_ = 0;
        recovery_accum_ = 0.0;
        log(LogLevel::Info, std::string("退出摄影模式（") + (reason ? reason : "user") + "）");
        return;
    }

    // 恢复未完成：状态如实保留（失败项的标志不清、frozen_ 仍为 true），
    // 由 tick 按 1s/2s/4s/8s 退避重试；Error 日志列出玩家会看到的现象。
    recovery_pending_ = true;
    recovery_attempts_ = 0;
    recovery_accum_ = 0.0;
    log(LogLevel::Error, std::string("退出摄影模式（") + (reason ? reason : "user") + "）仍有 " +
                              std::to_string(failed.size()) + " 项未能还原：" + join_items(failed) +
                              "。将在后台按 1s/2s/4s/8s 退避重试（最多 " +
                              std::to_string(kMaxRecoveryAttempts) + " 次）");
}

void PhotoMode::tick_recovery(double dt) {
    if (!recovery_pending_) return;
    if (!(dt >= 0.0)) dt = 0.0;
    if (dt > kMaxFrameDt) dt = kMaxFrameDt;  // 与 tick 一致：切窗口回来别把多次重试压成一帧

    recovery_accum_ += dt;
    if (recovery_accum_ < recovery_delay_seconds(recovery_attempts_)) return;
    recovery_accum_ = 0.0;

    std::vector<std::string> failed;
    const bool done = attempt_restore_all(&failed);
    if (done) {
        recovery_pending_ = false;
        recovery_attempts_ = 0;
        log(LogLevel::Info, "退出后的残留改动已全部还原（后台重试成功）");
        return;
    }

    ++recovery_attempts_;
    if (recovery_attempts_ >= kMaxRecoveryAttempts) {
        recovery_pending_ = false;
        log(LogLevel::Error, "恢复重试 " + std::to_string(kMaxRecoveryAttempts) + " 次仍失败，停止重试；仍然保持：" +
                                  join_items(failed) + "。请重启游戏（或手动重进摄影模式后退出）恢复这些状态");
        return;
    }
    log(LogLevel::Warn, "恢复重试 " + std::to_string(recovery_attempts_) + "/" +
                            std::to_string(kMaxRecoveryAttempts) + " 未完成：" + join_items(failed) +
                            "；下次在 " + std::to_string(static_cast<int>(recovery_delay_seconds(recovery_attempts_))) +
                            " 秒后再试");
}

bool PhotoMode::set_fov(double fov) {
    const double clamped = std::clamp(fov, cfg_.fov_min, cfg_.fov_max);
    if (std::abs(clamped - fov_) < 1e-6) {
        return false;
    }
    fov_ = clamped;
    pose_.fov = clamped;
    if (active_) {
        if (!camera_.apply_fov(clamped)) {
            log(LogLevel::Warn, "应用 FOV 失败");
        }
    }
    return true;
}

void PhotoMode::toggle_freeze() {
    if (!active_) return;

    // 冻结已失效（自检放弃）时按 F 视为「重新冻结」，而不是解除一个本来就没生效的冻结
    const bool want = !frozen() || freeze_lost_;
    bool ok = false;
    bool use_pause = frozen_with_pause_;
    if (want) {
        use_pause = cfg_.freeze_mode == FreezeMode::Pause;
        if (use_pause) {
            ok = time_pause_.set_paused(true);
            if (!ok) {
                // 剧情等流程里 SetPause 可能被游戏拒绝：回退时间缩放，冻结总得有一个能生效
                log(LogLevel::Warn, "游戏暂停失败，回退为时间缩放冻结");
                use_pause = false;
            }
        }
        if (!use_pause) {
            const double prev = time_pause_.get_time_dilation();
            // 与 enter() 同一套异常值兜底：处决/命中停顿的接近零倍率不能当原值保存。
            if (prev >= 0.1 && prev <= 10.0) prev_dilation_ = prev;
            ok = time_pause_.set_time_dilation(cfg_.freeze_dilation);
        }
    } else if (use_pause) {
        ok = time_pause_.set_paused(false);
    } else {
        // 显式恢复：只回写本 Mod 确实改过的 WorldSettings 字段（不再靠数值是否接近来猜）
        ok = time_pause_.restore_time_dilation(prev_dilation_);
    }

    if (!ok) {
        log(LogLevel::Warn, want ? "冻结时间失败" : "恢复时间失败");
        return;
    }
    frozen_ = want;
    frozen_with_pause_ = want && use_pause;
    freeze_check_accum_ = 0.0;
    freeze_reassert_count_ = 0;
    freeze_reassert_gave_up_ = false;
    freeze_lost_ = false;
    freeze_diag_seconds_ = 0.0;
    freeze_diag_has_last_ = false;

    // 冻结期残余微动治理：世界暂停拦不住的“每帧变化源”。冻结顺序 = 相机 → 风 → MPC → 布料 → 骨骼动画 → 动态模糊 → 渲染对照 → 光照对照 → 阴影对照，
    // 解冻严格逆序（阴影对照 → 光照对照 → 渲染对照 → 动态模糊 → 骨骼动画 → 布料 → MPC → 风 → 相机），与 leave() 的还原顺序一致。
    if (shadow_tuning_active_) {
        const RenderTuningReport shadow_report = freeze_motion_.freeze_shadow_ab(false);
        if (shadow_report.ok) {
            shadow_tuning_active_ = false;
        } else {
            log(LogLevel::Warn, "阴影/后处理对照 CVar 还原失败，退出摄影模式时会重试");
        }
    }
    if (lighting_tuning_active_) {
        const RenderTuningReport lighting_report = freeze_motion_.freeze_lighting(false, LightingTuningOptions{});
        if (lighting_report.ok) {
            lighting_tuning_active_ = false;
            lighting_ab_cycle_active_ = false;
        } else {
            log(LogLevel::Warn, "光照对照 CVar 还原失败，退出摄影模式时会重试");
        }
    }
    if (render_tuning_active_) {
        if (freeze_motion_.freeze_render_tuning(false, false, false, false, false).ok) {
            render_tuning_active_ = false;
        } else {
            log(LogLevel::Warn, "渲染对照 CVar 还原失败，退出摄影模式时会重试");
        }
    }
    if (motion_blur_frozen_) {
        if (freeze_motion_.freeze_motion_blur(false)) {
            motion_blur_frozen_ = false;
        } else {
            log(LogLevel::Warn, "动态模糊恢复失败，退出摄影模式时会重试");
        }
    }
    if (animation_frozen_) {
        if (freeze_motion_.freeze_skeletal_animation(false).ok) {
            animation_frozen_ = false;
        } else {
            log(LogLevel::Warn, "骨骼动画恢复失败，退出摄影模式时会重试");
        }
    }
    if (wpo_frozen_) {
        if (freeze_motion_.freeze_wpo(false).ok) {
            wpo_frozen_ = false;
        } else {
            log(LogLevel::Warn, "WPO 评估恢复失败，退出摄影模式时会重试");
        }
    }
    if (custom_cloth_frozen_) {
        if (freeze_motion_.freeze_custom_cloth(false).ok) {
            custom_cloth_frozen_ = false;
        } else {
            log(LogLevel::Warn, "自定义布料/摆动恢复失败，退出摄影模式时会重试");
        }
    }
    if (cloth_frozen_) {
        if (freeze_motion_.freeze_cloth(false).ok) {
            cloth_frozen_ = false;
        } else {
            log(LogLevel::Warn, "布料模拟恢复失败，退出摄影模式时会重试");
        }
    }
    if (mpc_hold_active_) {
        freeze_motion_.mpc_release_snapshot();
        mpc_hold_active_ = false;
    }
    // 恢复失败必须保留标志：退出摄影模式时的级联还会再试一次。
    // 这里曾经无条件清标志（与上面 wpo/cloth 三段不一致），导致恢复失败后永不重试、
    // 布料/植被永久停在冻结态。
    if (wind_frozen_) {
        if (freeze_motion_.freeze_wind(false).ok) {
            wind_frozen_ = false;
        } else {
            log(LogLevel::Warn, "风源恢复失败，退出摄影模式时会重试");
        }
    }
    if (shakes_frozen_) {
        if (freeze_motion_.freeze_camera_shakes(false).ok) {
            shakes_frozen_ = false;
        } else {
            log(LogLevel::Warn, "相机晃动恢复失败，退出摄影模式时会重试");
        }
    }
    if (want && cfg_.stop_camera_shakes_on_freeze) {
        // shake/修饰器用真实时间驱动，暂停期间照常推视角：冻结画面边缘抖动（脸部阴影爬动）的主变化源
        const CameraShakeFreezeReport report = freeze_motion_.freeze_camera_shakes(true);
        shakes_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_wind) {
        // 风强/风速归零：布料/植被 WPO 摆动的幅度源（钟照走、幅度归零 = 位移恒零）
        const WindFreezeReport report = freeze_motion_.freeze_wind(true);
        wind_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_mpc) {
        const int held = freeze_motion_.mpc_take_snapshot();
        mpc_hold_active_ = held > 0;
    }
    if (want && cfg_.freeze_cloth) {
        // 布料/裙摆/披风模拟有独立 tick，世界暂停与 TimeDilation 都拦不住；
        // 冻结时挂起并保留最后姿态，解冻恢复。
        const ClothFreezeReport report = freeze_motion_.freeze_cloth(true);
        cloth_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_custom_cloth) {
        // PBZ 自己的 SwayComponent / 全局 Cloth 开关：引擎 Cloth.Enable 与组件 Suspend 之外的第三条路径。
        const CustomClothFreezeReport report = freeze_motion_.freeze_custom_cloth(true);
        custom_cloth_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_wpo) {
        // 材质顶点动画（WPO）在暂停/TimeDilation 下仍可能求值：逐 SkeletalMesh 关闭评估，解冻恢复。
        const WpoFreezeReport report = freeze_motion_.freeze_wpo(true);
        wpo_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_skeletal_animation) {
        // 部分角色/敌人的 SkeletalMesh 会在暂停下继续跑 AnimBP；骨骼驱动的 cape 会继续动。
        const SkeletalAnimationFreezeReport report = freeze_motion_.freeze_skeletal_animation(true);
        animation_frozen_ = report.ok;
    }
    if (want && cfg_.freeze_motion_blur_off) {
        // 残留速度缓冲 + motion blur 会把布料细纹理抹成“流动式”拖影。
        motion_blur_frozen_ = freeze_motion_.freeze_motion_blur(true);
    }
    if (want) {
        // A/B 渲染对照：默认两项都 false，只读取并记录当前渲染状态；
        // 需要定位 TSR/TAA 或 WPO 假速度时再在 config.ini 开启。
        const RenderTuningReport render_report = freeze_motion_.freeze_render_tuning(
            true, cfg_.freeze_aa_off, cfg_.freeze_wpo_velocity_off, cfg_.freeze_framegen_off, cfg_.freeze_dlss_off,
            cfg_.freeze_tonemapper_dither_off);
        render_tuning_active_ = render_report.applied > 0;

        // 光照 A/B（Lumen/屏幕空间光照）：单项开关或自动轮换任一开启就进入本阶段。
        LightingTuningOptions lighting_options{};
        lighting_options.screen_traces_off = cfg_.freeze_lumen_screen_traces_off;
        lighting_options.short_range_ao_off = cfg_.freeze_lumen_short_range_ao_off;
        lighting_options.radiance_cache_off = cfg_.freeze_lumen_radiance_cache_off;
        lighting_options.fixed_jitter = cfg_.freeze_lumen_fixed_jitter;
        lighting_options.lumen_off = cfg_.freeze_lumen_off;
        const bool any_lighting = lighting_options.screen_traces_off || lighting_options.short_range_ao_off ||
                                  lighting_options.radiance_cache_off || lighting_options.fixed_jitter ||
                                  lighting_options.lumen_off;
        if (any_lighting || cfg_.freeze_lighting_ab_cycle) {
            const RenderTuningReport lighting_report = freeze_motion_.freeze_lighting(true, lighting_options);
            lighting_tuning_active_ = lighting_report.ok;
            lighting_ab_cycle_active_ = cfg_.freeze_lighting_ab_cycle;
        }

        // 阴影/后处理 A/B：Lumen 六档全部无效后的第二层诊断（VSM/SMRT/接触阴影/体积雾/后处理）。
        if (cfg_.freeze_shadow_ab_cycle) {
            const RenderTuningReport shadow_report = freeze_motion_.freeze_shadow_ab(true);
            shadow_tuning_active_ = shadow_report.ok;
        }
    }

    // 剧情播放器一并处理：剧情系统常绕过世界暂停与时间缩放（画面像冻住、动作声音还在走）
    if (cfg_.freeze_sequences) {
        const SequenceFreezeReport report = time_pause_.pause_sequences(want);
        sequences_paused_ = want && report.ok;
    }

    // 对话 Flow 控件：冻结时改为 Collapsed 停止 Slate Tick，避免对话 Flow 在冻结期继续推进、
    // 提前结束当前语音；解冻恢复 Hidden（仍隐藏但继续 Tick）。仅在隐藏字幕时才有意义。
    if (want && cfg_.freeze_dialogue_flow && cfg_.hide_subtitles) {
        const int flow_count = hud_.set_dialogue_flow_tick_paused(true);
        flow_dialog_frozen_ = flow_count > 0;
        if (flow_count > 0) {
            char buf[160]{};
            std::snprintf(buf, sizeof(buf), "对话 Flow 冻结：FlowDialog=%d 个改为 Collapsed（停止 Tick）", flow_count);
            log(LogLevel::Info, buf);
        }
    }

    // 对话语音真暂停：截获的 PlayingID 走游戏自有 PBZPausePlayingID（Wwise 原生暂停），
    // 解冻 PBZResumePlayingID 后当前句从原位置继续；无 ID 时回落组件音量兜底。
    // 关键：有逐语音 PlayingID 时绝不调用整引擎 Suspend——2024.1 的 Suspend(true,false)
    // 是 render-anyway，音频时间轴继续推进，当前句会在冻结期间无声播完，解冻无法原位续播。
    // 整引擎只在没有截获到 PlayingID 时作为静音兜底。
    if (want && cfg_.stop_wwise_on_freeze) {
        const int voice_ids = audio_.pause_wwise_voices(true);
        wwise_paused_ = true;
        if (voice_ids > 0) {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf),
                          "逐语音真暂停已执行 %d 个 PlayingID，跳过整引擎 Suspend（整引擎 render-anyway 会让语音时间轴继续走）",
                          voice_ids);
            log(LogLevel::Info, buf);
        } else if (cfg_.wwise_engine_suspend && !wwise_engine_suspended_) {
            wwise_engine_suspended_ = audio_.suspend_wwise_engine(true);
            if (wwise_engine_suspended_) {
                log(LogLevel::Info, "无逐语音 PlayingID，整引擎 Suspend 作为静音兜底");
            }
        }
    } else if (!want) {
        // 解冻逆序：先唤醒整引擎恢复输出，再逐语音恢复（先恢复输出再恢复语音，不能反）
        if (wwise_engine_suspended_) {
            if (audio_.suspend_wwise_engine(false)) {
                wwise_engine_suspended_ = false;
            } else {
                log(LogLevel::Warn, "Wwise 音频引擎唤醒失败，保持挂起状态");
            }
        }
        if (wwise_paused_) {
            audio_.pause_wwise_voices(false);
            wwise_paused_ = false;
        }
    }

    // 对话 Flow 解冻：恢复输出/语音之后再把 FlowDialog 从 Collapsed 恢复 Hidden（保 Tick）
    if (!want && flow_dialog_frozen_) {
        hud_.set_dialogue_flow_tick_paused(false);
        flow_dialog_frozen_ = false;
        log(LogLevel::Info, "对话 Flow 解冻：FlowDialog 恢复 Hidden 保 Tick");
    }

    // 解冻时也清一次玩家角色的演出残留：处决动画里进入摄影模式后按 F 恢复时，
    // 角色应当立刻能动，而不是等到退出模式才恢复。
    if (!want) time_pause_.recover_character_after_photo_mode();

    log(LogLevel::Info, frozen_ ? "时间已冻结" : "时间已恢复");
}

void PhotoMode::reassert_time_dilation_if_needed() {
    if (!active_ || !frozen_ || frozen_with_pause_) return;
    time_pause_.reassert_time_dilation(cfg_.freeze_dilation);
}

void PhotoMode::reassert_aspect_ratio_if_needed() {
    // 只在本次进入确实做过画幅解锁时重申；配置热重载关闭开关不影响本次会话的还原语义。
    if (!active_ || !aspect_unlocked_) return;
    camera_.reassert_aspect_ratio();
}

void PhotoMode::reassert_render_tuning_if_needed() {
    if (!active_ || !frozen_ || !render_tuning_active_) return;
    freeze_motion_.render_reassert_tick();
}

void PhotoMode::reassert_mpc_hold_if_needed() {
    if (!active_ || !frozen_ || !mpc_hold_active_) return;
    freeze_motion_.mpc_hold_snapshot();
}

// ---- 图形菜单实时开关：改配置 + 立即生效，内部记账与 enter()/leave() 共用 ----

void PhotoMode::set_hide_player_live(bool visible) {
    cfg_.hide_player = visible;
    if (!active_) return;
    if (visible && !player_hidden_) {
        player_hidden_ = hud_.set_player_hidden(true);
        if (!player_hidden_) log(LogLevel::Warn, "隐藏角色失败");
    } else if (!visible && player_hidden_) {
        // 恢复失败就保留标志：退出时 leave() 还会再试一次
        if (hud_.set_player_hidden(false)) player_hidden_ = false;
        else log(LogLevel::Warn, "恢复角色显示失败，退出时再试");
    }
}

void PhotoMode::set_block_game_input_live(bool blocked) {
    cfg_.block_game_input = blocked;
    if (!active_) return;
    if (blocked) {
        if (!ignore_move_set_) ignore_move_set_ = input_.set_ignore_move_input(true);
        if (!game_input_blocked_) game_input_blocked_ = input_.set_game_input_blocked(true);
    } else {
        if (ignore_move_set_) {
            if (input_.set_ignore_move_input(false)) ignore_move_set_ = false;
            else log(LogLevel::Warn, "恢复角色移动输入失败，退出时再试");
        }
        if (game_input_blocked_) {
            if (input_.set_game_input_blocked(false)) game_input_blocked_ = false;
            else log(LogLevel::Warn, "恢复游戏键鼠输入失败，退出时再试");
        }
    }
}

void PhotoMode::set_hide_game_hud_live(bool hidden) {
    cfg_.hide_game_hud = hidden;
    if (!active_) return;
    if (hidden && !hud_hidden_) {
        hud_hidden_ = hud_.set_hud_visible(false);
    } else if (!hidden && hud_hidden_) {
        if (hud_.set_hud_visible(true)) hud_hidden_ = false;
        else log(LogLevel::Warn, "恢复 HUD 显示失败，退出时再试");
    }
}


void PhotoMode::apply_look_if_needed() {
    LookOptions want{};
    want.vignette = cfg_.vignette > 0.0 ? cfg_.vignette : 0.0;
    want.saturation = cfg_.saturation;
    want.contrast = cfg_.contrast;
    want.bloom = cfg_.bloom > 0.0 ? cfg_.bloom : 0.0;
    want.temperature = cfg_.temperature;

    if (look_applied_valid_ && look_options_equal(want, look_applied_)) return;
    if (!camera_.set_look(want)) {
        // 失败不锁死：下一帧还会再试（体积要等世界就绪），日志只告警一次
        look_applied_valid_ = false;
        if (!look_warn_logged_) {
            look_warn_logged_ = true;
            log(LogLevel::Warn, "画面效果：自建后处理体积不可用，暗角/饱和/对比/泛光/冷暖未生效（其余摄影功能不受影响）");
        }
        return;
    }
    look_applied_ = want;
    look_applied_valid_ = true;
    if (want.any_active() && !look_logged_) {
        look_logged_ = true;
        char buf[220]{};
        std::snprintf(buf, sizeof(buf),
                      "画面效果：暗角=%.2f 饱和=%.2f 对比=%.2f 泛光=%.2f 冷暖=%.2f"
                      "（写进自建后处理体积，退出还原）",
                      want.vignette, want.saturation, want.contrast, want.bloom, want.temperature);
        log(LogLevel::Info, buf);
    }
}


bool PhotoMode::apply_pose_if_dirty() {
    if (!pose_dirty_) return true;
    pose_.fov = fov_;
    if (!camera_.apply_pose(pose_)) {
        return false;
    }
    pose_dirty_ = false;
    return true;
}

bool PhotoMode::fallback_from_takeover() {
    log(LogLevel::Warn,
        "接管当前相机未生效（写入的 POV 没有赶上本帧渲染，镜头不会跟随输入），"
        "已自动回退到独立相机（spawn + SetViewTargetWithBlend）");

    const bool restored = camera_.leave_camera_takeover();
    takeover_active_ = false;
    if (!restored) {
        log(LogLevel::Warn, "回退独立相机：退出接管未完全成功（游戏相机状态可能残留）");
    }

    if (!camera_.spawn_camera(pose_)) {
        log(LogLevel::Error, "接管失效后回退独立相机失败：创建 CameraActor 失败");
        return false;
    }
    spawned_ = true;

    if (!camera_.attach_view()) {
        log(LogLevel::Error, "接管失效后回退独立相机失败：设置 ViewTarget 失败");
        camera_.destroy_camera();
        spawned_ = false;
        return false;
    }
    view_attached_ = true;

    pose_dirty_ = false;
    if (!camera_.apply_fov(fov_)) log(LogLevel::Warn, "回退独立相机后应用 FOV 失败");
    if (!camera_.apply_pose(pose_)) log(LogLevel::Warn, "回退独立相机后应用相机位姿失败");

    log(LogLevel::Info, "已回退到独立相机：WASD/鼠标应恢复移动");
    return true;
}

void PhotoMode::tick(const Intent& intent, double dt) {
    apply_look_if_needed();
    if (!active_) {
        // 退出后恢复未完成：仍然需要引擎 tick 驱动退避重试
        tick_recovery(dt);
        return;
    }

    if (!(dt >= 0.0)) dt = 0.0;
    if (dt > kMaxFrameDt) dt = kMaxFrameDt;  // 掉帧/切窗口回来时别让相机瞬移

    if (!world_.world_valid()) {
        leave("世界失效");
        return;
    }

    // 引擎层在渲染结束后回读确认：写进去的 POV 没赶上当帧渲染（连续多帧被游戏覆盖）。
    // 这时镜头完全不会跟随输入，必须立刻回退独立相机；回退也失败才退出摄影模式，
    // 绝不能让玩家停留在「能进模式但动不了」的状态。
    if (takeover_active_ && camera_.takeover_view_lost()) {
        if (!fallback_from_takeover()) {
            leave("接管当前相机未生效，且自动回退独立相机也失败");
            return;
        }
    }

    if (intent.toggle_freeze) toggle_freeze();

    if (intent.reset_view && has_reset_pose_) {
        pose_ = reset_pose_;
        fov_ = reset_fov_;
        pose_.fov = fov_;
        pose_dirty_ = true;
        if (!camera_.apply_fov(fov_)) {
            log(LogLevel::Warn, "重置相机视角时应用 FOV 失败");
        }
        log(LogLevel::Info, "相机视角已重置（位置/朝向/FOV）");
    }

    // 滚转归零（水平校正）：只动 roll，位置/俯仰/偏航/FOV 保留。
    // 与 R 同帧时 R 先整体还原快照，这里再把滚转归零。
    if (intent.reset_roll) {
        pose_.rot.roll = 0.0;
        pose_dirty_ = true;
        log(LogLevel::Info, "滚转已归零（位置/俯仰/偏航/FOV 不变）");
    }

    const double sens = cfg_.look_sensitivity;
    // 按住 Ctrl（键盘）/按下右摇杆 RS（手柄）时视角精调：鼠标与右摇杆转速一起乘 slow_look_mult
    const double look_mult = intent.slow ? cfg_.slow_look_mult : 1.0;
    if (intent.look_dx != 0.0) {
        pose_.rot.yaw = wrap_yaw(pose_.rot.yaw + intent.look_dx * sens * look_mult);
        pose_dirty_ = true;
    }
    if (intent.look_dy != 0.0) {
        const double dy = cfg_.invert_y ? -intent.look_dy : intent.look_dy;
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch - dy * sens * look_mult, cfg_.pitch_limit);
        pose_dirty_ = true;
    }

    // 手柄右摇杆：pad_look_* 已是度/秒（含灵敏度与死区），乘 dt 转成角度。
    // 不受 free_look_requires_alt 限制——手柄上没有 Alt 可按；RS 按下（slow）时同样乘 slow_look_mult。
    if (intent.pad_look_dx != 0.0) {
        pose_.rot.yaw = wrap_yaw(pose_.rot.yaw + intent.pad_look_dx * dt * look_mult);
        pose_dirty_ = true;
    }
    if (intent.pad_look_dy != 0.0) {
        const double pad_dy = cfg_.invert_y ? -intent.pad_look_dy : intent.pad_look_dy;
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch - pad_dy * dt * look_mult, cfg_.pitch_limit);
        pose_dirty_ = true;
    }

    // look 模式下升降键改俯仰（持续按住持续转）
    if (intent.pitch_up_click) {
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch + kPitchSpeedDegPerSec * dt, cfg_.pitch_limit);
        pose_dirty_ = true;
    }
    if (intent.pitch_down_click) {
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch - kPitchSpeedDegPerSec * dt, cfg_.pitch_limit);
        pose_dirty_ = true;
    }

    if (intent.wheel != 0.0 && cfg_.wheel_fov) {
        // Intent.wheel 正值 = 滚轮向上：默认向上缩小 FOV（拉近）、向下放大；
        // wheel_fov_invert=true 时方向互换。滚轮是否参与由 [control] wheel_fov 控制。
        const double direction = cfg_.wheel_fov_invert ? -1.0 : 1.0;
        set_fov(fov_ - direction * intent.wheel * cfg_.fov_step);
    }

    // FOV 键/手柄十字键：按下沿走一格，按住时 InputLogic 连发（延迟后按固定节奏补格）；正值 = 增大 FOV（拉远）
    if (intent.fov_nudge != 0) {
        set_fov(fov_ + intent.fov_nudge * cfg_.fov_step);
    }

    // Shift/Ctrl 变速对「步进类」操作同样生效：Shift 粗调、Ctrl 微调（与移动/滚转同规则）。
    const double mult = (intent.boost ? cfg_.boost_mult : 1.0) * (intent.slow ? cfg_.slow_mult : 1.0);

    const double step = cfg_.move_speed * mult * dt;

    const double fwd_in = intent.move_forward;
    const double right_in = intent.move_right;
    if (fwd_in != 0.0 || right_in != 0.0) {
        const Vec3 fwd = forward_from_angles(pose_.rot.pitch, pose_.rot.yaw);
        const Vec3 right = right_from_yaw(pose_.rot.yaw);
        double dx = fwd.x * fwd_in + right.x * right_in;
        double dy = fwd.y * fwd_in + right.y * right_in;
        double dz = fwd.z * fwd_in;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > 1e-9) {
            // 键盘对角（len≈1.41）归一化到全速；手柄摇杆行程 <1 时按行程减速
            const double k = len > 1.0 ? 1.0 / len : 1.0;
            pose_.loc.x += dx * k * step;
            pose_.loc.y += dy * k * step;
            pose_.loc.z += dz * k * step;
            pose_dirty_ = true;
        }
    }

    if (intent.move_up != 0.0) {
        pose_.loc.z += intent.move_up * cfg_.vertical_speed * mult * dt;
        pose_dirty_ = true;
    }

    // 方向键上/下与十字键上/下俯仰：pitch_speed 度/秒，正 = 仰视；Shift/Ctrl 变速与滚转同规则。
    // 方向是明确的按钮语义，不做 invert_y（那只管鼠标/摇杆的 Y 轴）。
    if (intent.pitch_axis != 0.0) {
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch + intent.pitch_axis * cfg_.pitch_speed * mult * dt, cfg_.pitch_limit);
        pose_dirty_ = true;
    }

    // 方向键左/右与十字键左/右偏航（1.0.46 起从左右平移改来）：yaw_speed 度/秒，正 = 右转；Shift/Ctrl 变速同规则。
    if (intent.yaw_axis != 0.0) {
        pose_.rot.yaw = wrap_yaw(pose_.rot.yaw + intent.yaw_axis * cfg_.yaw_speed * mult * dt);
        pose_dirty_ = true;
    }

    // Q/E 滚转：roll_speed 度/秒，正 = 顺时针；Shift 加速 / Ctrl 减速照常生效
    if (intent.roll != 0.0) {
        pose_.rot.roll = wrap_angle_180(pose_.rot.roll + intent.roll * cfg_.roll_speed * mult * dt);
        pose_dirty_ = true;
    }

    // 本帧是否动了相机（位姿有写入）：POV 微动监测只统计“没人动相机”的连续帧
    const bool camera_moved = pose_dirty_;

    if (!apply_pose_if_dirty()) {
        log(LogLevel::Warn, "应用相机位姿失败");
    }

    // HUD 是 UMG，可能比摄影模式晚建好；隐藏失败时每秒重试一次，
    // 成功后由 hud_hidden_ 记住，退出时才会正确恢复。重试有上限：
    // 每次重试都要按候选类名全对象扫描，一直失败就一直拖帧
    if (cfg_.hide_game_hud && !hud_hidden_ && hud_retry_count_ < kMaxVisibilityRetries) {
        hud_retry_accum_ += dt;
        if (hud_retry_accum_ >= 1.0) {
            hud_retry_accum_ = 0.0;
            hud_hidden_ = hud_.set_hud_visible(false);
            if (hud_hidden_) log(LogLevel::Info, "HUD 延迟隐藏成功");
            else if (++hud_retry_count_ >= kMaxVisibilityRetries) {
                log(LogLevel::Warn, "HUD 隐藏重试多次未成功，停止重试（重试会全对象扫描拖帧）；重进摄影模式可再试");
            }
        }
    }

    // 交互提示同理：进入时可能还没挂出（或第一次没找到），每秒重试直到成功
    if (cfg_.hide_interact_prompt && !prompt_hidden_ && prompt_retry_count_ < kMaxVisibilityRetries) {
        prompt_retry_accum_ += dt;
        if (prompt_retry_accum_ >= 1.0) {
            prompt_retry_accum_ = 0.0;
            prompt_hidden_ = hud_.set_interact_prompts_visible(false);
            if (prompt_hidden_) log(LogLevel::Info, "交互提示延迟隐藏成功");
            else if (++prompt_retry_count_ >= kMaxVisibilityRetries) {
                log(LogLevel::Warn, "交互提示隐藏重试多次未成功，停止重试（重试会全对象扫描拖帧）；重进摄影模式可再试");
            }
        }
    }

    // 敌人血条同理：设置对象/枚举可能比摄影模式晚就绪，每秒重试直到成功
    if (cfg_.hide_enemy_health_bars && !bars_hidden_ && bars_retry_count_ < kMaxVisibilityRetries) {
        bars_retry_accum_ += dt;
        if (bars_retry_accum_ >= 1.0) {
            bars_retry_accum_ = 0.0;
            bars_hidden_ = hud_.set_enemy_health_bars_visible(false);
            if (bars_hidden_) log(LogLevel::Info, "敌人血条延迟隐藏成功");
            else if (++bars_retry_count_ >= kMaxVisibilityRetries) {
                log(LogLevel::Warn, "敌人血条隐藏重试多次未成功，停止重试（重试会全对象扫描拖帧）；重进摄影模式可再试");
            }
        }
    }

    // 锁定标记同理：扫描要全对象遍历，隐藏失败（控件藏不住）才重试；场上确认无可识别的
    // 锁定对象时引擎侧直接确认完成，不会走到这里
    if (cfg_.hide_lockon_marker && !lockon_hidden_ && lockon_retry_count_ < kMaxVisibilityRetries) {
        lockon_retry_accum_ += dt;
        if (lockon_retry_accum_ >= 1.0) {
            lockon_retry_accum_ = 0.0;
            lockon_hidden_ = hud_.set_lockon_marker_visible(false);
            if (lockon_hidden_) log(LogLevel::Info, "锁定标记延迟隐藏成功");
            else if (++lockon_retry_count_ >= kMaxVisibilityRetries) {
                log(LogLevel::Warn, "锁定标记隐藏重试多次未成功，停止重试（重试会全对象扫描拖帧）；重进摄影模式可再试");
            }
        }
    }

    // 字幕：进入时藏一次，之后每秒重申——对话 UI 创建时才读设置，运行中的对话由游戏
    // 逐条把控件重新显示，一次性隐藏会被盖回去（1.0.32 实机：进模式后字幕不消失）。
    // 引擎侧幂等：设置开关只在首跳写一次（有未知副作用），控件每秒收编新实例/再压被改回的。
    if (cfg_.hide_subtitles) {
        subtitles_retry_accum_ += dt;
        if (subtitles_retry_accum_ >= 1.0) {
            subtitles_retry_accum_ = 0.0;
            const bool hidden = hud_.set_subtitles_visible(false);
            if (!subtitles_hidden_ && hidden) {
                subtitles_hidden_ = true;
                log(LogLevel::Info, "对话字幕延迟隐藏成功");
            }
        }
    }

    // 每 1 秒重新确认一次 ViewTarget：防止游戏里某些流程（过场、重生）把视角抢回去。
    // M-1：ProcessEvent 成功不代表真的接管成功，但“回读暂未生效”（同帧 Target=null/
    // 属性不可读）也不能算失效——只有回读确凿证明视角被抢或相机位姿被搬走，
    // 连续多次才 leave。重试本身由 attach_view 幂等发出。
    // 接管模式没有 ViewTarget 可确认：位姿覆盖由引擎侧在 post tick 每帧重写，不做回读判死。
    reassert_accum_ += dt;
    if (!takeover_active_ && reassert_accum_ >= 1.0) {
        reassert_accum_ = 0.0;
        camera_.attach_view();
        if (camera_.view_confirmation_failed()) {
            log(LogLevel::Error,
                "相机接管已失效：ViewTarget 未指向摄影相机或相机位姿被改写，连续多次校验失败，"
                "退出摄影模式（不再假装仍在摄影模式）");
            leave("ViewTarget/相机位姿连续校验失败");
        }
    }

    // 冻结期每秒校验：剧情/游戏系统可能擅自解除暂停或改写时间缩放（画面像冻住但世界还在走）。
    // 连续 3 次重新冻结都压不住就停止对抗，只留告警——继续每秒硬写会和剧情系统打摆。
    if (frozen_) {
        // MPC 参数钉住：冻结期每帧把快照写回（实时驱动代码每帧改写，必须每帧赢回写入权）
        if (mpc_hold_active_) freeze_motion_.mpc_hold_snapshot();
        // POV 微动监测：冻结期每帧采样相机管理器最终 POV（诊断用，受 freeze_diag 门控；
        // 只统计没人动相机的帧，用户转相机时清样本不累计）
        if (cfg_.freeze_diag) freeze_motion_.pov_watch_tick(!camera_moved);
        // 光照 A/B 自动轮换：每 phase_seconds 切一档，日志带阶段名（诊断用）
        if (lighting_ab_cycle_active_) {
            freeze_motion_.lighting_ab_tick(dt, true, cfg_.freeze_lighting_ab_phase_seconds);
        }
        // 阴影/后处理 A/B 自动轮换（与光照轮换二选一；预设包只开这个）
        if (shadow_tuning_active_) {
            freeze_motion_.shadow_ab_tick(dt, true, cfg_.freeze_shadow_ab_phase_seconds);
        }
        freeze_check_accum_ += dt;
        if (freeze_check_accum_ >= 1.0) {
            freeze_check_accum_ = 0.0;
            const bool lost = frozen_with_pause_ ? !time_pause_.world_paused()
                                                 : time_pause_.get_time_dilation() > cfg_.freeze_dilation + 0.001;
            if (!lost) {
                freeze_reassert_count_ = 0;
                freeze_reassert_gave_up_ = false;
                if (freeze_lost_) {
                    // 游戏侧又不再改写时间状态，冻结重新生效：恢复对外状态并再打一次
                    freeze_lost_ = false;
                    log(LogLevel::Info, "冻结状态已恢复（游戏侧不再改写时间状态）");
                }
            } else if (!freeze_reassert_gave_up_) {
                const bool reok = frozen_with_pause_ ? time_pause_.set_paused(true)
                                                     : time_pause_.set_time_dilation(cfg_.freeze_dilation);
                ++freeze_reassert_count_;
                if (freeze_reassert_count_ >= 3) {
                    freeze_reassert_gave_up_ = true;
                    // 如实标记：内部 frozen_ 仍保留（退出时还要还原），但对外 frozen() 变 false，
                    // UI/调用方不能再显示「已冻结」。一次性 Error 说明，不再每秒刷屏。
                    freeze_lost_ = true;
                    log(LogLevel::Error,
                        "游戏/剧情系统持续改写时间状态，冻结已失效（画面可能仍在动），已停止自动重试；"
                        "按 F 重新冻结，或退出摄影模式恢复");
                } else {
                    log(reok ? LogLevel::Info : LogLevel::Warn,
                        frozen_with_pause_ ? "检测到暂停被游戏解除，已重新暂停" : "检测到时间缩放被游戏改写，已重新冻结");
                }
            }

            // 冻结诊断：默认关闭；开启后状态翻转立即输出，持续期每 kFreezeDiagIntervalSec 秒最多一条。
            // freeze_diag_sample() 是反射扫描，只在真正要输出时才调用。
            if (cfg_.freeze_diag) {
                const bool flipped = !freeze_diag_has_last_ || freeze_diag_last_paused_ != frozen_with_pause_ ||
                                     freeze_diag_last_lost_ != lost;
                freeze_diag_seconds_ = flipped ? 0.0 : freeze_diag_seconds_ + 1.0;
                if (flipped || freeze_diag_seconds_ >= kFreezeDiagIntervalSec) {
                    freeze_diag_seconds_ = 0.0;
                    freeze_diag_has_last_ = true;
                    freeze_diag_last_paused_ = frozen_with_pause_;
                    freeze_diag_last_lost_ = lost;
                    log(LogLevel::Info, freeze_motion_.freeze_diag_sample() + (frozen_with_pause_ ? " [暂停]" : " [缩放]") +
                                            (freeze_lost_ ? " [冻结已失效]" : ""));
                }
            }
        }
    }
}

}  // namespace pbzpm
