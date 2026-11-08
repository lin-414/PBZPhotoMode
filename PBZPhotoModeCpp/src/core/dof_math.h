#pragma once

// 景深 / 对焦 / 色差（[dof] 段 + [look] 的 chromatic_aberration）的纯逻辑。
//
// 通道说明（2.1 轮复活，背景见 tuning.h 顶部）：这组参数不走 [look] 的后处理体积通道，
// 而是走「渲染门控（NLP 过场渲染档 + tagged cvar）+ spawn 出的 CineCameraActor 组件通道」。
// 引擎实现见 engine/ue4ss_port_dof.cpp；这里只放可单测的参数集合与换算，
// 与 look.h 的分野：色差的配置键在 [look]（沿用旧版键名），但下发走 DofParams。

#include <algorithm>
#include <cmath>

#include "core/tuning.h"

namespace pbzpm {

// 景深/色差参数集合（引擎下发单位：距离 cm、传感器 mm、F 值、色差强度）。
// 色差与景深共用同一条组件通道与胶片闸门，所以合在一个结构里下发。
struct DofParams {
    bool dof_enabled{false};
    double focus_distance_cm{tuning::kDofDefaultFocalCm};
    double fstop{tuning::kDofFstopDefault};
    double sensor_width_mm{tuning::kDofSensorDefaultMm};
    double chromatic_aberration{0.0};

    bool dof_active() const { return dof_enabled; }
    bool film_active() const { return chromatic_aberration > 1e-6; }
    bool any_active() const { return dof_active() || film_active(); }
};

// 两份参数是否等价（「值没变就不下发」的判定，与 look_options_equal 同风格）。
inline bool dof_params_equal(const DofParams& a, const DofParams& b) {
    const double eps = 1e-6;
    return a.dof_enabled == b.dof_enabled &&
           std::fabs(a.focus_distance_cm - b.focus_distance_cm) < eps &&
           std::fabs(a.fstop - b.fstop) < eps &&
           std::fabs(a.sensor_width_mm - b.sensor_width_mm) < eps &&
           std::fabs(a.chromatic_aberration - b.chromatic_aberration) < eps;
}

// 对焦比例步进（hotfix31 语义）：实际步进 = max(配置步进, 当前距离 × 10%)。
// 配置步进是「最小步进」：近距离保持精确，远距离 10 步左右就能追上目标。
inline double focus_step_scaled(double current_distance_cm, double config_step_cm) {
    if (!(config_step_cm > 0.0)) config_step_cm = tuning::kDofFocusStepDefaultCm;
    const double distance = std::max(0.0, current_distance_cm);
    return std::max(config_step_cm, distance * tuning::kDofFocusStepProportion);
}

inline double clamp_focus_distance(double cm) {
    return std::clamp(cm, tuning::kDofFocalMinCm, tuning::kDofFocalMaxCm);
}

inline double clamp_fstop(double fstop) {
    return std::clamp(fstop, tuning::kDofFstopMin, tuning::kDofFstopMax);
}

inline double clamp_sensor_width(double mm) {
    return std::clamp(mm, tuning::kDofSensorMinMm, tuning::kDofSensorMaxMm);
}

}  // namespace pbzpm
