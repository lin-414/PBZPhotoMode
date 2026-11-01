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
    bool postfx_saved{false};
    CameraPose last_pose{};
    double last_fov{0.0};
    int postfx_save_count{0};
    int postfx_restore_count{0};

    // 超宽屏画幅解锁
    bool aspect_unlocked{false};
    bool aspect_lock_active{true};  // 模拟游戏是否真的锁着 16:9
    int aspect_unlock_count{0};
    int aspect_restore_count{0};

    // 故障注入
    bool fail_spawn{false};
    bool fail_attach{false};
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
    bool fail_postfx{false};
    bool fail_pause{false};
    bool fail_time_dilation{false};
    bool fail_aspect_unlock{false};

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
        if (fail_attach) return false;
        view_attached = true;
        return true;
    }
    bool restore_view() override {
        calls.push_back("restore_view");
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
        if (fail_ignore_move) return false;
        ignore_move = ignore;
        return true;
    }
    bool set_game_input_blocked(bool blocked) override {
        calls.push_back(blocked ? "block_input_on" : "block_input_off");
        if (fail_block_input) return false;
        input_blocked = blocked;
        return true;
    }
    bool set_player_hidden(bool hidden) override {
        calls.push_back(hidden ? "hide_player_on" : "hide_player_off");
        if (fail_hide_player && hidden) return false;
        player_hidden = hidden;
        return true;
    }
    bool set_hud_visible(bool visible) override {
        calls.push_back(visible ? "hud_on" : "hud_off");
        if (fail_hud && !visible) return false;
        hud_visible = visible;
        return true;
    }
    bool set_interact_prompts_visible(bool visible) override {
        calls.push_back(visible ? "prompt_on" : "prompt_off");
        if (fail_prompts && !visible) return false;
        prompts_visible = visible;
        if (visible) ++prompt_restore_count;
        return true;
    }
    bool set_enemy_health_bars_visible(bool visible) override {
        calls.push_back(visible ? "bars_on" : "bars_off");
        if (fail_bars && !visible) return false;
        bars_visible = visible;
        return true;
    }
    bool set_lockon_marker_visible(bool visible) override {
        calls.push_back(visible ? "lockon_on" : "lockon_off");
        if (fail_lockon && !visible) return false;
        lockon_visible = visible;
        return true;
    }
    bool set_subtitles_visible(bool visible) override {
        calls.push_back(visible ? "subs_on" : "subs_off");
        if (fail_subtitles && !visible) return false;
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
    bool set_paused(bool value) override {
        calls.push_back(value ? "pause_on" : "pause_off");
        if (fail_pause) return false;
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
        report.ok = !fail_sequences;
        report.found = sequences_found;
        report.matched = fail_sequences ? 0 : sequences_playing;
        report.acted = report.matched;
        return report;
    }

    // Wwise 组件模拟：wwise_components 同时是“数量”与“暂停/恢复成功”的执行数
    int wwise_components{0};
    int wwise_component_count() override {
        calls.push_back("wwise_count");
        return wwise_components;
    }
    int pause_wwise_voices(bool pause) override {
        calls.push_back(pause ? "wwise_pause" : "wwise_resume");
        return wwise_components;
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
        report.ok = !fail_camera_shakes;
        report.found = modifiers_found;
        report.acted = report.ok ? (frozen ? modifiers_disabled : modifiers_found) : 0;
        return report;
    }
    WindFreezeReport freeze_wind(bool frozen) override {
        calls.push_back(frozen ? "wind_freeze" : "wind_restore");
        WindFreezeReport report{};
        report.ok = !fail_wind;
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

    bool save_postfx() override {
        calls.push_back("save_postfx");
        if (fail_postfx) return false;
        postfx_saved = true;
        ++postfx_save_count;
        return true;
    }
    void restore_postfx() override {
        calls.push_back("restore_postfx");
        postfx_saved = false;
        ++postfx_restore_count;
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
};

}  // namespace pbzpm::test
