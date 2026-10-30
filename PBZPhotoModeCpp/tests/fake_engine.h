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
    bool postfx_saved{false};
    CameraPose last_pose{};
    double last_fov{0.0};
    int postfx_save_count{0};
    int postfx_restore_count{0};

    // 故障注入
    bool fail_spawn{false};
    bool fail_attach{false};
    bool fail_apply_pose{false};
    bool fail_apply_fov{false};
    bool fail_ignore_move{false};
    bool fail_block_input{false};
    bool fail_hide_player{false};
    bool fail_hud{false};
    bool fail_postfx{false};

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
    double get_time_dilation() override {
        calls.push_back("get_time_dilation");
        return dilation;
    }
    bool set_time_dilation(double value) override {
        calls.push_back("set_time_dilation");
        dilation = value;
        return true;
    }
    bool set_paused(bool value) override {
        calls.push_back(value ? "pause_on" : "pause_off");
        paused = value;
        return true;
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
