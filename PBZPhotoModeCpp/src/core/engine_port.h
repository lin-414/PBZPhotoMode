#pragma once

#include <string>

#include "core/config.h"
#include "core/math3.h"

namespace pbzpm {

struct CameraPose {
    Vec3 loc{};
    Rot rot{};
    double fov{90.0};
    bool operator==(const CameraPose& o) const {
        return loc == o.loc && rot == o.rot && fov == o.fov;
    }
};

// 引擎侧的抽象端口。core 只通过它说话，因此状态机可以在没有游戏的情况下单测。
// 实现在 src/engine/ue4ss_port.cpp（C++/UE4SS 反射），测试里用 tests/fake_engine.h。
class EnginePort {
public:
    virtual ~EnginePort() = default;

    virtual bool world_valid() = 0;
    virtual bool read_pov(CameraPose& out) = 0;

    virtual bool spawn_camera(const CameraPose& pose) = 0;
    virtual bool attach_view() = 0;
    virtual bool restore_view() = 0;
    virtual bool apply_pose(const CameraPose& pose) = 0;
    virtual bool apply_fov(double fov) = 0;
    virtual bool destroy_camera() = 0;

    // 输入屏蔽：只动“移动输入”，不动“视角输入”（否则会连带屏蔽鼠标轴）
    virtual bool set_ignore_move_input(bool ignore) = 0;

    // 彻底吞掉游戏自己的键鼠输入：左右键攻击、交互、技能都不要触发。
    // 做法是给 PC/Pawn 的 InputComponent 打上 bBlockInput，让输入栈在最高优先级处中断；
    // 与 set_ignore_move_input 互补（那个只挡移动轴，挡不住动作绑定）。
    virtual bool set_game_input_blocked(bool blocked) = 0;

    virtual bool set_player_hidden(bool hidden) = 0;
    virtual bool set_hud_visible(bool visible) = 0;

    virtual double get_time_dilation() = 0;
    virtual bool set_time_dilation(double dilation) = 0;
    virtual bool set_paused(bool paused) = 0;

    virtual bool save_postfx() = 0;
    virtual void restore_postfx() = 0;

    virtual bool run_command(const std::string& command) = 0;

    virtual void log(LogLevel level, const std::string& message) = 0;
};

}  // namespace pbzpm
