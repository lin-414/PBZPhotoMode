#include "core/menu_bridge.h"

#include <cmath>

namespace pbzpm {

namespace {
// 滑条是浮点量，比较留一点噪声容差（拖动中的最小可感变化远大于这些值）
constexpr double kFovEps = 0.05;
constexpr double kSpeedEps = 0.5;
constexpr double kSensEps = 0.0005;

double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void WbpBridge::on_opened(double fov, double speed, double sens) {
    last_fov_ = fov;
    last_speed_ = speed;
    last_sens_ = sens;
    pushed_fov_ = fov;
    pushed_speed_ = speed;
    pushed_sens_ = sens;
    push_pending_ = true;  // 下一步把真值推给 BP（滑条/显示就位）
    last_command_ = 0;
    opened_ = true;
}

void WbpBridge::reset() {
    last_fov_ = last_speed_ = last_sens_ = 0.0;
    pushed_fov_ = pushed_speed_ = pushed_sens_ = 0.0;
    push_pending_ = false;
    last_command_ = 0;
    opened_ = false;
}

WbpCommands WbpBridge::step(const WbpVars& vars,
                            double ext_fov, double ext_speed, double ext_sens, bool ext_frozen,
                            double fov_min, double fov_max,
                            double sens_min, double sens_max,
                            WbpWriteBack& out) {
    WbpCommands cmds;
    out = WbpWriteBack{};

    if (!vars.valid) return cmds;  // 控件不可用：这帧什么都不做（含写回，防误写别的对象）

    // Frozen/Status 每帧推：BP 不回写它们，无回环，代价是两次直写换免状态跟踪
    out.write_frozen = true;
    out.frozen = ext_frozen;
    out.write_status = true;
    out.status = ext_frozen ? 2 : 1;

    if (!opened_) {
        // 没经过 on_opened（例如控件被重建后桥还没重新初始化）：以外部真值为基线自举
        on_opened(ext_fov, ext_speed, ext_sens);
    }

    // ---- 真值回显：只在外部真值变化时推（滚轮/快捷键/夹紧），拖动期间完全静默 ----
    // 不能每帧推：拖动结束那帧推送会把过期的 ext 写进变量，下一帧被当成"用户新输入"再
    // 下发一次回旧值的命令——滑条和相机会互相拉扯（自激振荡）。
    if (!vars.dragging) {
        if (push_pending_ || std::fabs(ext_fov - pushed_fov_) > kFovEps) {
            out.write_fov = true;
            out.fov = static_cast<float>(ext_fov);
            pushed_fov_ = ext_fov;
        }
        if (push_pending_ || std::fabs(ext_speed - pushed_speed_) > kSpeedEps) {
            out.write_speed = true;
            out.speed = static_cast<float>(ext_speed);
            pushed_speed_ = ext_speed;
        }
        if (push_pending_ || std::fabs(ext_sens - pushed_sens_) > kSensEps) {
            out.write_sens = true;
            out.sens = static_cast<float>(ext_sens);
            pushed_sens_ = ext_sens;
        }
        push_pending_ = false;
    }

    // ---- 滑条 → 命令：与上次应用值比较，变了就是用户拖了 ----
    if (std::fabs(static_cast<double>(vars.fov) - last_fov_) > kFovEps) {
        cmds.fov_changed = true;
        cmds.fov = clamp(static_cast<double>(vars.fov), fov_min, fov_max);
        last_fov_ = cmds.fov;
        // 夹紧导致命令值 ≠ 滑条值时回写（拖动中不写，避免和拖拽打架；松手后残余差会再触发）
        if (!vars.dragging && std::fabs(cmds.fov - static_cast<double>(vars.fov)) > kFovEps) {
            out.write_fov = true;
            out.fov = static_cast<float>(cmds.fov);
            pushed_fov_ = cmds.fov;
        }
    }
    if (std::fabs(static_cast<double>(vars.speed) - last_speed_) > kSpeedEps) {
        cmds.speed_changed = true;
        cmds.speed = clamp(static_cast<double>(vars.speed), 0.0, kWbpSpeedMax);
        last_speed_ = cmds.speed;
        if (!vars.dragging && std::fabs(cmds.speed - static_cast<double>(vars.speed)) > kSpeedEps) {
            out.write_speed = true;
            out.speed = static_cast<float>(cmds.speed);
            pushed_speed_ = cmds.speed;
        }
    }
    if (std::fabs(static_cast<double>(vars.sens) - last_sens_) > kSensEps) {
        cmds.sens_changed = true;
        cmds.sens = clamp(static_cast<double>(vars.sens), sens_min, sens_max);
        last_sens_ = cmds.sens;
        if (!vars.dragging && std::fabs(cmds.sens - static_cast<double>(vars.sens)) > kSensEps) {
            out.write_sens = true;
            out.sens = static_cast<float>(cmds.sens);
            pushed_sens_ = cmds.sens;
        }
    }

    // ---- 按钮 → 命令：沿语义。C++ 执行后把 Command 写回 0，BP 下次点击再置新值 ----
    if (vars.command != 0 && vars.command != last_command_) {
        switch (vars.command) {
            case 1: cmds.toggle_freeze = true; break;
            case 2: cmds.reset_view = true; break;
            case 3: cmds.exit_mode = true; break;
            default: break;  // 未知命令忽略（资产版本超前/落后时不至于误动作）
        }
        last_command_ = vars.command;
    }
    if (vars.command != 0) {
        out.write_command = true;
        out.command = 0;
    }
    // Command 回到 0 后允许同号命令再次触发
    if (vars.command == 0) last_command_ = 0;

    return cmds;
}

}  // namespace pbzpm
