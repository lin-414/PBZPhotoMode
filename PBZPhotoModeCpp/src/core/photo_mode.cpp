#include "core/photo_mode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/version.h"

namespace pbzpm {
namespace {

constexpr double kPitchSpeedDegPerSec = 30.0;
constexpr double kMaxFrameDt = 0.25;

}  // namespace

PhotoMode::PhotoMode(EnginePort& engine, Config cfg) : engine_(engine), cfg_(cfg) {}

void PhotoMode::log(LogLevel level, const std::string& message) {
    engine_.log(level, std::string("[PhotoMode] ") + message);
}

bool PhotoMode::enter() {
    if (active_) {
        log(LogLevel::Warn, "已经在摄影模式中，忽略重复进入");
        return true;
    }
    last_error_.clear();
    pov_fallback_used_ = false;

    if (!engine_.world_valid()) {
        last_error_ = "世界未加载（UWorld 不可用）";
        log(LogLevel::Error, last_error_);
        return false;
    }

    CameraPose pov{};
    if (engine_.read_pov(pov)) {
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

    if (!engine_.spawn_camera(pose_)) {
        last_error_ = "创建 CameraActor 失败";
        log(LogLevel::Error, last_error_);
        return false;
    }
    spawned_ = true;

    if (!engine_.attach_view()) {
        last_error_ = "设置 ViewTarget 失败";
        log(LogLevel::Error, last_error_);
        engine_.destroy_camera();
        spawned_ = false;
        return false;
    }
    view_attached_ = true;

    // 以下步骤失败都不致命：记 warning，继续进模式
    if (cfg_.block_game_input) {
        ignore_move_set_ = engine_.set_ignore_move_input(true);
        if (!ignore_move_set_) log(LogLevel::Warn, "屏蔽角色移动输入失败，游玩输入可能仍在生效");

        // 光挡移动轴不够：左右键攻击/交互走的是动作绑定，必须让整个输入栈在 PC 处中断
        game_input_blocked_ = engine_.set_game_input_blocked(true);
        if (!game_input_blocked_) {
            log(LogLevel::Warn, "屏蔽游戏键鼠输入失败，鼠标左右键可能仍会触发游戏动作");
        } else {
            log(LogLevel::Info, "已屏蔽游戏键鼠输入（角色不会跟着 WASD 走）");
        }
    }
    if (cfg_.hide_player) {
        player_hidden_ = engine_.set_player_hidden(true);
        if (!player_hidden_) log(LogLevel::Warn, "隐藏角色失败");
    }
    if (cfg_.hide_game_hud) {
        hud_hidden_ = engine_.set_hud_visible(false);
        if (!hud_hidden_) log(LogLevel::Warn, "隐藏 HUD 失败");
    }
    if (cfg_.hide_interact_prompt) {
        prompt_hidden_ = engine_.set_interact_prompts_visible(false);
        if (!prompt_hidden_) log(LogLevel::Warn, "隐藏交互提示失败");
    }
    if (cfg_.hide_enemy_health_bars) {
        bars_hidden_ = engine_.set_enemy_health_bars_visible(false);
        if (!bars_hidden_) log(LogLevel::Warn, "隐藏敌人血条失败");
    }
    if (cfg_.hide_subtitles) {
        subtitles_hidden_ = engine_.set_subtitles_visible(false);
        if (!subtitles_hidden_) log(LogLevel::Warn, "隐藏对话字幕失败");
    }
    if (cfg_.ultrawide_unlock) {
        aspect_unlocked_ = engine_.unlock_aspect_ratio();
        if (!aspect_unlocked_) log(LogLevel::Warn, "画幅解锁失败：超宽屏（21:9/32:9）两侧黑边可能不会消除");
    }

    postfx_saved_ = engine_.save_postfx();
    if (!postfx_saved_) log(LogLevel::Warn, "PostProcess 快照失败，退出时不会还原画质设置");

    prev_dilation_ = engine_.get_time_dilation();
    if (!(prev_dilation_ > 0.0) || prev_dilation_ > 10.0) prev_dilation_ = 1.0;  // 异常值兜底

    engine_.apply_fov(fov_);
    engine_.apply_pose(pose_);

    active_ = true;
    frozen_ = false;
    sequences_paused_ = false;
    freeze_check_accum_ = 0.0;
    freeze_reassert_count_ = 0;
    freeze_reassert_gave_up_ = false;
    pose_dirty_ = false;
    reassert_accum_ = 0.0;
    hud_retry_accum_ = 0.0;
    prompt_retry_accum_ = 0.0;
    bars_retry_accum_ = 0.0;
    subtitles_retry_accum_ = 0.0;

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

void PhotoMode::leave(const char* reason) {
    if (!active_ && !spawned_ && !view_attached_ && !ignore_move_set_ && !game_input_blocked_ && !player_hidden_ &&
        !hud_hidden_ && !prompt_hidden_ && !bars_hidden_ && !subtitles_hidden_ && !aspect_unlocked_ &&
        !postfx_saved_) {
        return;  // 幂等：没进过模式就没有可还原的东西
    }

    if (frozen_) {
        if (sequences_paused_) {
            engine_.pause_sequences(false);
            sequences_paused_ = false;
        }
        if (frozen_with_pause_) {
            engine_.set_paused(false);
        } else {
            engine_.set_time_dilation(prev_dilation_);
        }
        if (wwise_paused_) {
            engine_.pause_wwise_voices(false);
            wwise_paused_ = false;
        }
        frozen_ = false;
    }
    if (postfx_saved_) {
        engine_.restore_postfx();
        postfx_saved_ = false;
    }
    if (aspect_unlocked_) {
        engine_.restore_aspect_ratio();
        aspect_unlocked_ = false;
    }
    if (hud_hidden_) {
        engine_.set_hud_visible(true);
        hud_hidden_ = false;
    }
    if (prompt_hidden_) {
        engine_.set_interact_prompts_visible(true);
        prompt_hidden_ = false;
    }
    if (bars_hidden_) {
        engine_.set_enemy_health_bars_visible(true);
        bars_hidden_ = false;
    }
    if (subtitles_hidden_) {
        engine_.set_subtitles_visible(true);
        subtitles_hidden_ = false;
    }
    if (player_hidden_) {
        engine_.set_player_hidden(false);
        player_hidden_ = false;
    }
    if (game_input_blocked_) {
        engine_.set_game_input_blocked(false);
        game_input_blocked_ = false;
    }
    if (ignore_move_set_) {
        engine_.set_ignore_move_input(false);
        ignore_move_set_ = false;
    }
    if (view_attached_) {
        engine_.restore_view();
        view_attached_ = false;
    }
    if (spawned_) {
        engine_.destroy_camera();
        spawned_ = false;
    }

    active_ = false;
    pose_dirty_ = false;

    log(LogLevel::Info, std::string("退出摄影模式（") + (reason ? reason : "user") + "）");
}

bool PhotoMode::set_fov(double fov) {
    const double clamped = std::clamp(fov, cfg_.fov_min, cfg_.fov_max);
    if (std::abs(clamped - fov_) < 1e-6) {
        return false;
    }
    fov_ = clamped;
    pose_.fov = clamped;
    if (active_) {
        if (!engine_.apply_fov(clamped)) {
            log(LogLevel::Warn, "应用 FOV 失败");
        }
    }
    return true;
}

void PhotoMode::toggle_freeze() {
    if (!active_) return;

    const bool want = !frozen_;
    bool ok = false;
    bool use_pause = frozen_with_pause_;
    if (want) {
        use_pause = cfg_.freeze_mode == FreezeMode::Pause;
        if (use_pause) {
            ok = engine_.set_paused(true);
            if (!ok) {
                // 剧情等流程里 SetPause 可能被游戏拒绝：回退时间缩放，冻结总得有一个能生效
                log(LogLevel::Warn, "游戏暂停失败，回退为时间缩放冻结");
                use_pause = false;
            }
        }
        if (!use_pause) {
            const double prev = engine_.get_time_dilation();
            if (prev > 0.0 && prev <= 10.0) prev_dilation_ = prev;
            ok = engine_.set_time_dilation(cfg_.freeze_dilation);
        }
    } else if (use_pause) {
        ok = engine_.set_paused(false);
    } else {
        ok = engine_.set_time_dilation(prev_dilation_);
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

    // 剧情播放器一并处理：剧情系统常绕过世界暂停与时间缩放（画面像冻住、动作声音还在走）
    if (cfg_.freeze_sequences) {
        const SequenceFreezeReport report = engine_.pause_sequences(want);
        sequences_paused_ = want && report.ok;
    }

    // 对话语音真暂停：截获的 PlayingID 走游戏自有 PBZPausePlayingID（Wwise 原生暂停），
    // 解冻 PBZResumePlayingID 后当前句从原位置继续；无 ID 时回落组件音量兜底
    if (want && cfg_.stop_wwise_on_freeze) {
        engine_.pause_wwise_voices(true);
        wwise_paused_ = true;
    } else if (!want && wwise_paused_) {
        engine_.pause_wwise_voices(false);
        wwise_paused_ = false;
    }

    log(LogLevel::Info, frozen_ ? "时间已冻结" : "时间已恢复");
}

// ---- 图形菜单实时开关：改配置 + 立即生效，内部记账与 enter()/leave() 共用 ----

void PhotoMode::set_hide_player_live(bool visible) {
    cfg_.hide_player = visible;
    if (!active_) return;
    if (visible && !player_hidden_) {
        player_hidden_ = engine_.set_player_hidden(true);
        if (!player_hidden_) log(LogLevel::Warn, "隐藏角色失败");
    } else if (!visible && player_hidden_) {
        engine_.set_player_hidden(false);
        player_hidden_ = false;
    }
}

void PhotoMode::set_block_game_input_live(bool blocked) {
    cfg_.block_game_input = blocked;
    if (!active_) return;
    if (blocked) {
        if (!ignore_move_set_) ignore_move_set_ = engine_.set_ignore_move_input(true);
        if (!game_input_blocked_) game_input_blocked_ = engine_.set_game_input_blocked(true);
    } else {
        if (ignore_move_set_) {
            engine_.set_ignore_move_input(false);
            ignore_move_set_ = false;
        }
        if (game_input_blocked_) {
            engine_.set_game_input_blocked(false);
            game_input_blocked_ = false;
        }
    }
}

void PhotoMode::set_hide_game_hud_live(bool hidden) {
    cfg_.hide_game_hud = hidden;
    if (!active_) return;
    if (hidden && !hud_hidden_) {
        hud_hidden_ = engine_.set_hud_visible(false);
    } else if (!hidden && hud_hidden_) {
        engine_.set_hud_visible(true);
        hud_hidden_ = false;
    }
}

bool PhotoMode::apply_pose_if_dirty() {
    if (!pose_dirty_) return true;
    pose_.fov = fov_;
    if (!engine_.apply_pose(pose_)) {
        return false;
    }
    pose_dirty_ = false;
    return true;
}

void PhotoMode::tick(const Intent& intent, double dt) {
    if (!active_) return;

    if (!(dt >= 0.0)) dt = 0.0;
    if (dt > kMaxFrameDt) dt = kMaxFrameDt;  // 掉帧/切窗口回来时别让相机瞬移

    if (!engine_.world_valid()) {
        leave("世界失效");
        return;
    }

    if (intent.toggle_freeze) toggle_freeze();

    if (intent.reset_view && has_reset_pose_) {
        pose_ = reset_pose_;
        fov_ = reset_fov_;
        pose_.fov = fov_;
        pose_dirty_ = true;
        if (!engine_.apply_fov(fov_)) {
            log(LogLevel::Warn, "重置相机视角时应用 FOV 失败");
        }
        log(LogLevel::Info, "相机视角已重置（位置/朝向/FOV）");
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

    // look 模式下左右键改俯仰（持续按住持续转）
    if (intent.pitch_up_click) {
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch + kPitchSpeedDegPerSec * dt, cfg_.pitch_limit);
        pose_dirty_ = true;
    }
    if (intent.pitch_down_click) {
        pose_.rot.pitch = clamp_pitch(pose_.rot.pitch - kPitchSpeedDegPerSec * dt, cfg_.pitch_limit);
        pose_dirty_ = true;
    }

    if (intent.wheel != 0.0) {
        // Intent.wheel 正值 = 滚轮向上：向上应是缩小 FOV（拉近），向下放大 FOV。
        set_fov(fov_ - intent.wheel * cfg_.fov_step);
    }

    // FOV 键/手柄十字键：按下沿走一格，按住时 InputLogic 连发（延迟后按固定节奏补格）；正值 = 增大 FOV（拉远）
    if (intent.fov_nudge != 0) {
        set_fov(fov_ + intent.fov_nudge * cfg_.fov_step);
    }

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

    // Q/E 滚转：roll_speed 度/秒，正 = 顺时针；Shift 加速 / Ctrl 减速照常生效
    if (intent.roll != 0.0) {
        pose_.rot.roll = wrap_angle_180(pose_.rot.roll + intent.roll * cfg_.roll_speed * mult * dt);
        pose_dirty_ = true;
    }

    if (!apply_pose_if_dirty()) {
        log(LogLevel::Warn, "应用相机位姿失败");
    }

    // HUD 是 UMG，可能比摄影模式晚建好；隐藏失败时每秒重试一次，
    // 成功后由 hud_hidden_ 记住，退出时才会正确恢复。
    if (cfg_.hide_game_hud && !hud_hidden_) {
        hud_retry_accum_ += dt;
        if (hud_retry_accum_ >= 1.0) {
            hud_retry_accum_ = 0.0;
            hud_hidden_ = engine_.set_hud_visible(false);
            if (hud_hidden_) log(LogLevel::Info, "HUD 延迟隐藏成功");
        }
    }

    // 交互提示同理：进入时可能还没挂出（或第一次没找到），每秒重试直到成功
    if (cfg_.hide_interact_prompt && !prompt_hidden_) {
        prompt_retry_accum_ += dt;
        if (prompt_retry_accum_ >= 1.0) {
            prompt_retry_accum_ = 0.0;
            prompt_hidden_ = engine_.set_interact_prompts_visible(false);
            if (prompt_hidden_) log(LogLevel::Info, "交互提示延迟隐藏成功");
        }
    }

    // 敌人血条同理：设置对象/枚举可能比摄影模式晚就绪，每秒重试直到成功
    if (cfg_.hide_enemy_health_bars && !bars_hidden_) {
        bars_retry_accum_ += dt;
        if (bars_retry_accum_ >= 1.0) {
            bars_retry_accum_ = 0.0;
            bars_hidden_ = engine_.set_enemy_health_bars_visible(false);
            if (bars_hidden_) log(LogLevel::Info, "敌人血条延迟隐藏成功");
        }
    }

    // 字幕：进入时藏一次，之后每秒重申——对话 UI 创建时才读设置，运行中的对话由游戏
    // 逐条把控件重新显示，一次性隐藏会被盖回去（1.0.32 实机：进模式后字幕不消失）。
    // 引擎侧幂等：设置开关只在首跳写一次（有未知副作用），控件每秒收编新实例/再压被改回的。
    if (cfg_.hide_subtitles) {
        subtitles_retry_accum_ += dt;
        if (subtitles_retry_accum_ >= 1.0) {
            subtitles_retry_accum_ = 0.0;
            const bool hidden = engine_.set_subtitles_visible(false);
            if (!subtitles_hidden_ && hidden) {
                subtitles_hidden_ = true;
                log(LogLevel::Info, "对话字幕延迟隐藏成功");
            }
        }
    }

    // 每 1 秒重新确认一次 ViewTarget：防止游戏里某些流程（过场、重生）把视角抢回去
    reassert_accum_ += dt;
    if (reassert_accum_ >= 1.0) {
        reassert_accum_ = 0.0;
        if (!engine_.attach_view()) {
            leave("ViewTarget 失效");
        }
    }

    // 冻结期每秒校验：剧情/游戏系统可能擅自解除暂停或改写时间缩放（画面像冻住但世界还在走）。
    // 连续 3 次重新冻结都压不住就停止对抗，只留告警——继续每秒硬写会和剧情系统打摆。
    if (frozen_) {
        freeze_check_accum_ += dt;
        if (freeze_check_accum_ >= 1.0) {
            freeze_check_accum_ = 0.0;
            const bool lost = frozen_with_pause_ ? !engine_.world_paused()
                                                 : engine_.get_time_dilation() > cfg_.freeze_dilation + 0.001;
            if (!lost) {
                freeze_reassert_count_ = 0;
                freeze_reassert_gave_up_ = false;
            } else if (!freeze_reassert_gave_up_) {
                const bool reok = frozen_with_pause_ ? engine_.set_paused(true)
                                                     : engine_.set_time_dilation(cfg_.freeze_dilation);
                ++freeze_reassert_count_;
                if (freeze_reassert_count_ >= 3) {
                    freeze_reassert_gave_up_ = true;
                    log(LogLevel::Warn, "游戏/剧情系统持续改写时间状态，冻结压不住，已停止自动重试（可退出后重进或按 F 重新冻结）");
                } else {
                    log(reok ? LogLevel::Info : LogLevel::Warn,
                        frozen_with_pause_ ? "检测到暂停被游戏解除，已重新暂停" : "检测到时间缩放被游戏改写，已重新冻结");
                }
            }
        }
    }
}

}  // namespace pbzpm
