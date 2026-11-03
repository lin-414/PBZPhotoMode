#pragma once

#include <string>
#include <vector>

#include "core/engine_port.h"

namespace pbzpm::test {

// 假引擎：记录调用顺序，可按需注入故障。用来验证状态机的顺序与回滚。
class FakeEngine final : public EnginePort {
public:
    std::vector<std::string> calls;
    std::vector<std::string> logs;

    CameraPose pov{};
    bool read_pov_ok{true};
    bool world_ok{true};
    double dilation{1.0};
    bool paused{false};

    bool camera_exists{false};
    bool view_attached{false};
    bool ignore_move{false};
    bool input_blocked{false};
    bool player_hidden{false};
    bool hud_visible{true};
    bool prompts_visible{true};
    bool bars_visible{true};
    bool lockon_visible{true};
    bool subtitles_visible{true};
    int prompt_restore_count{0};
    CameraPose last_pose{};
    double last_fov{0.0};

    // 超宽屏画幅解锁
    bool aspect_unlocked{false};
    bool aspect_lock_active{true};  // 模拟游戏是否真的锁着 16:9
    int aspect_unlock_count{0};
    int aspect_restore_count{0};

    // 故障注入（隐藏/开启方向）
    bool fail_spawn{false};
    bool fail_attach{false};
    // M-1 部分成功语义：ProcessEvent“成功”但回读确认 ViewTarget 没切到摄影相机。
    // attach_view 仍返回 false，并累计连续失败次数供 view_confirmation_failed() 判定。
    bool attach_reports_mismatch{false};
    int attach_verify_fail_streak{0};
    // 日志阈值（PhotoMode 构造/每次 log 都会同步；测试可检查是否传入）
    LogLevel configured_log_level{LogLevel::Info};
    int refresh_cache_calls{0};
    bool fail_apply_pose{false};
    bool fail_apply_fov{false};
    bool fail_ignore_move{false};
    bool fail_block_input{false};
    bool fail_hide_player{false};
    bool fail_hud{false};
    bool fail_prompts{false};
    bool fail_bars{false};
    bool fail_lockon{false};
    bool fail_subtitles{false};
    bool fail_pause{false};
    bool fail_time_dilation{false};
    bool fail_aspect_unlock{false};

    // 故障注入（还原方向）：只让 leave()/重试里的恢复调用失败，用于验证“失败项不被吞”
    bool fail_view_restore{false};
    bool fail_ignore_move_restore{false};
    bool fail_block_input_restore{false};
    bool fail_player_restore{false};
    bool fail_hud_restore{false};
    bool fail_prompts_restore{false};
    bool fail_bars_restore{false};
    bool fail_lockon_restore{false};
    bool fail_subtitles_restore{false};
    bool fail_pause_restore{false};
    bool fail_time_dilation_restore{false};
    bool fail_sequences_restore{false};
    bool fail_shakes_restore{false};
    bool fail_wind_restore{false};

    bool world_valid() override {
        calls.push_back("world_valid");
        return world_ok;
    }
    bool read_pov(CameraPose& out) override {
        calls.push_back("read_pov");
        if (!read_pov_ok) return false;
        out = pov;
        return true;
    }
    bool spawn_camera(const CameraPose& pose) override {
        calls.push_back("spawn_camera");
        if (fail_spawn) return false;
        camera_exists = true;
        last_pose = pose;
        return true;
    }
    bool attach_view() override {
        calls.push_back("attach_view");
        if (fail_attach) {
            ++attach_verify_fail_streak;
            return false;
        }
        if (attach_reports_mismatch) {
            // 部分成功：函数调用成功、内部 view_attached 已置位，但回读确认失败
            view_attached = true;
            ++attach_verify_fail_streak;
            return false;
        }
        attach_verify_fail_streak = 0;
        view_attached = true;
        return true;
    }
    bool view_confirmation_failed() const override { return attach_verify_fail_streak >= 3; }
    void set_log_level(LogLevel level) override { configured_log_level = level; }
    void refresh_function_caches() override { ++refresh_cache_calls; }
    bool restore_view() override {
        calls.push_back("restore_view");
        if (fail_view_restore) return false;
        view_attached = false;
        return true;
    }
    bool apply_pose(const CameraPose& pose) override {
        calls.push_back("apply_pose");
        if (fail_apply_pose) return false;
        last_pose = pose;
        return true;
    }
    bool apply_fov(double fov) override {
        calls.push_back("apply_fov");
        if (fail_apply_fov) return false;
        last_fov = fov;
        return true;
    }
    bool destroy_camera() override {
        calls.push_back("destroy_camera");
        camera_exists = false;
        return true;
    }
    bool set_ignore_move_input(bool ignore) override {
        calls.push_back(ignore ? "ignore_move_on" : "ignore_move_off");
        if (fail_ignore_move && ignore) return false;
        if (fail_ignore_move_restore && !ignore) return false;
        ignore_move = ignore;
        return true;
    }
    bool set_game_input_blocked(bool blocked) override {
        calls.push_back(blocked ? "block_input_on" : "block_input_off");
        if (fail_block_input && blocked) return false;
        if (fail_block_input_restore && !blocked) return false;
        input_blocked = blocked;
        return true;
    }
    bool set_player_hidden(bool hidden) override {
        calls.push_back(hidden ? "hide_player_on" : "hide_player_off");
        if (fail_hide_player && hidden) return false;
        if (fail_player_restore && !hidden) return false;
        player_hidden = hidden;
        return true;
    }
    bool set_hud_visible(bool visible) override {
        calls.push_back(visible ? "hud_on" : "hud_off");
        if (fail_hud && !visible) return false;
        if (fail_hud_restore && visible) return false;
        hud_visible = visible;
        return true;
    }
    bool set_interact_prompts_visible(bool visible) override {
        calls.push_back(visible ? "prompt_on" : "prompt_off");
        if (fail_prompts && !visible) return false;
        if (fail_prompts_restore && visible) return false;
        prompts_visible = visible;
        if (visible) ++prompt_restore_count;
        return true;
    }
    bool set_enemy_health_bars_visible(bool visible) override {
        calls.push_back(visible ? "bars_on" : "bars_off");
        if (fail_bars && !visible) return false;
        if (fail_bars_restore && visible) return false;
        bars_visible = visible;
        return true;
    }
    bool set_lockon_marker_visible(bool visible) override {
        calls.push_back(visible ? "lockon_on" : "lockon_off");
        if (fail_lockon && !visible) return false;
        if (fail_lockon_restore && visible) return false;
        lockon_visible = visible;
        return true;
    }
    bool set_subtitles_visible(bool visible) override {
        calls.push_back(visible ? "subs_on" : "subs_off");
        if (fail_subtitles && !visible) return false;
        if (fail_subtitles_restore && visible) return false;
        subtitles_visible = visible;
        return true;
    }
    double get_time_dilation() override {
        calls.push_back("get_time_dilation");
        return dilation;
    }
    bool set_time_dilation(double value) override {
        calls.push_back("set_time_dilation");
        if (fail_time_dilation) return false;
        dilation = value;
        return true;
    }
    bool restore_time_dilation(double original) override {
        calls.push_back("restore_time_dilation");
        if (fail_time_dilation_restore) return false;
        dilation = original;
        return true;
    }
    bool set_paused(bool value) override {
        calls.push_back(value ? "pause_on" : "pause_off");
        if (fail_pause && value) return false;
        if (fail_pause_restore && !value) return false;
        paused = value;
        return true;
    }
    bool world_paused() override {
        calls.push_back("world_paused");
        return paused;
    }

    // 剧情播放器模拟：sequences_playing 同时表示“暂停时正在播放的数量”与“恢复时处于暂停态的数量”
    int sequences_found{0};
    int sequences_playing{0};
    bool fail_sequences{false};
    int sequence_calls{0};
    SequenceFreezeReport pause_sequences(bool pause) override {
        calls.push_back(pause ? "sequences_pause" : "sequences_resume");
        ++sequence_calls;
        SequenceFreezeReport report{};
        report.ok = pause ? !fail_sequences : !fail_sequences_restore;
        report.found = sequences_found;
        report.matched = report.ok ? sequences_playing : 0;
        report.acted = report.matched;
        return report;
    }

    // 对话 Flow 控件模拟：flow_dialog_widgets 是场上 FlowDialog 控件数；
    // 暂停=Collapsed（停 Tick），恢复=Hidden（继续 Tick）；0 个控件时不算失败、不记调用序列
    int flow_dialog_widgets{0};
    bool flow_dialog_paused{false};
    int flow_freeze_calls{0};
    int set_dialogue_flow_tick_paused(bool pause) override {
        ++flow_freeze_calls;
        if (flow_dialog_widgets <= 0) {
            flow_dialog_paused = false;
            return 0;
        }
        flow_dialog_paused = pause;
        calls.push_back(pause ? "flow_freeze_on" : "flow_freeze_off");
        return flow_dialog_widgets;
    }

    // Wwise 组件模拟：wwise_components 只表示组件数量（诊断/音量兜底语义）
    int wwise_components{0};
    // 逐 PlayingID 模拟：pause_wwise_voices 返回实际执行暂停/恢复的 PlayingID 数
    int wwise_playing_ids{0};
    int wwise_component_count() override {
        calls.push_back("wwise_count");
        return wwise_components;
    }
    int pause_wwise_voices(bool pause) override {
        calls.push_back(pause ? "wwise_pause" : "wwise_resume");
        return wwise_playing_ids;
    }

    // Wwise 整引擎 Suspend 模拟：默认不可用，让原有测试继续覆盖逐语音兜底路径
    bool wwise_engine_suspend_ok{false};
    // Wake 单独可失败：验证“唤醒失败保留挂起标志、后续 tick 再重试”
    bool wwise_engine_wake_ok{true};
    bool wwise_engine_suspended{false};
    int wwise_engine_suspend_calls{0};
    bool suspend_wwise_engine(bool suspend) override {
        calls.push_back(suspend ? "wwise_engine_suspend" : "wwise_engine_wake");
        ++wwise_engine_suspend_calls;
        if (suspend) {
            if (!wwise_engine_suspend_ok) return false;
        } else if (!wwise_engine_wake_ok) {
            return false;  // 唤醒失败：引擎仍保持挂起
        }
        wwise_engine_suspended = suspend;
        return true;
    }

    // ---- 冻结期残余微动治理模拟 ----
    int modifiers_found{0};      // CameraModifiers 数量
    int modifiers_disabled{0};   // 冻结时本次停用的数量
    int wind_sources{0};         // 风源数量（同时是冻结归零/还原成功数）
    int mpc_params{0};           // MPC 快照参数数（>0 才进入每帧写回）
    bool fail_camera_shakes{false};
    bool fail_wind{false};
    bool fail_mpc{false};
    int mpc_snapshot_calls{0};
    int mpc_hold_calls{0};
    int mpc_release_calls{0};
    CameraShakeFreezeReport freeze_camera_shakes(bool frozen) override {
        calls.push_back(frozen ? "shakes_freeze" : "shakes_restore");
        CameraShakeFreezeReport report{};
        report.ok = frozen ? !fail_camera_shakes : !fail_shakes_restore;
        report.found = modifiers_found;
        report.acted = report.ok ? (frozen ? modifiers_disabled : modifiers_found) : 0;
        return report;
    }
    WindFreezeReport freeze_wind(bool frozen) override {
        calls.push_back(frozen ? "wind_freeze" : "wind_restore");
        WindFreezeReport report{};
        report.ok = frozen ? !fail_wind : !fail_wind_restore;
        report.found = wind_sources;
        report.acted = report.ok ? wind_sources : 0;
        return report;
    }
    int mpc_take_snapshot() override {
        calls.push_back("mpc_snapshot");
        ++mpc_snapshot_calls;
        return fail_mpc ? 0 : mpc_params;
    }
    int mpc_hold_snapshot() override {
        calls.push_back("mpc_hold");
        ++mpc_hold_calls;
        return mpc_params;
    }
    void mpc_release_snapshot() override {
        calls.push_back("mpc_release");
        ++mpc_release_calls;
    }
    std::string freeze_diag_sample() override {
        calls.push_back("freeze_diag");
        return "冻结诊断(模拟)";
    }
    int pov_watch_calls{0};
    int pov_watch_idle_calls{0};
    void pov_watch_tick(bool camera_idle) override {
        calls.push_back("pov_watch_tick");
        ++pov_watch_calls;
        if (camera_idle) ++pov_watch_idle_calls;
    }

    bool unlock_aspect_ratio() override {
        calls.push_back("aspect_unlock");
        if (fail_aspect_unlock) return false;
        aspect_unlocked = aspect_lock_active;
        ++aspect_unlock_count;
        return true;
    }
    void restore_aspect_ratio() override {
        calls.push_back("restore_aspect");
        aspect_unlocked = false;
        ++aspect_restore_count;
    }
    void log(LogLevel level, const std::string& message) override {
        const char* tag = "?";
        switch (level) {
            case LogLevel::Error: tag = "E"; break;
            case LogLevel::Warn: tag = "W"; break;
            case LogLevel::Info: tag = "I"; break;
            case LogLevel::Debug: tag = "D"; break;
        }
        logs.push_back(std::string(tag) + " " + message);
    }

    void reset_calls() { calls.clear(); }
    int count(const std::string& name) const {
        int n = 0;
        for (const std::string& c : calls) {
            if (c == name) ++n;
        }
        return n;
    }
    int index_of(const std::string& name) const {
        for (size_t i = 0; i < calls.size(); ++i) {
            if (calls[i] == name) return static_cast<int>(i);
        }
        return -1;
    }
    bool has(const std::string& name) const { return index_of(name) >= 0; }
    bool logs_contain(const std::string& needle) const {
        for (const std::string& line : logs) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }
    // 日志出现次数（验证「一次性告警 / 限流」用）
    int count_logs_contain(const std::string& needle) const {
        int n = 0;
        for (const std::string& line : logs) {
            if (line.find(needle) != std::string::npos) ++n;
        }
        return n;
    }
};

}  // namespace pbzpm::test
