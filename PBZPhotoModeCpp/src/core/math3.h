// 纯数学层：不依赖任何引擎头，可单独编译与单测。
// 约定（与 UE 一致）：
//   yaw   0 = 朝 +X（前），正方向向右转；right = (-sin yaw, cos yaw, 0)
//   pitch 正 = 抬头；forward = (cos p cos y, cos p sin y, sin p)
//   鼠标向右 -> yaw 增大；鼠标向下 -> pitch 减小（保持默认手感）
#pragma once

#include <algorithm>
#include <cmath>

namespace pbzpm {

struct Vec3 {
    double x{};
    double y{};
    double z{};

    Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    bool operator==(const Vec3& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct Rot {
    double pitch{};
    double yaw{};
    double roll{};

    bool operator==(const Rot& o) const {
        return pitch == o.pitch && yaw == o.yaw && roll == o.roll;
    }
};

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

inline Vec3 forward_from_angles(double pitch_deg, double yaw_deg) {
    const double p = pitch_deg * kDegToRad;
    const double y = yaw_deg * kDegToRad;
    return Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

inline Vec3 right_from_yaw(double yaw_deg) {
    const double y = yaw_deg * kDegToRad;
    return Vec3{-std::sin(y), std::cos(y), 0.0};
}

inline double clamp_pitch(double pitch_deg, double limit_deg = 89.0) {
    return std::clamp(pitch_deg, -limit_deg, limit_deg);
}

inline double wrap_yaw(double yaw_deg) {
    double y = std::fmod(yaw_deg, 360.0);
    if (y < 0.0) y += 360.0;
    return y;
}

// 滚转归一化到 [-180, 180)：Q/E 连续滚时不无界增长；roll 关于水平对称，用对称区间更直观
inline double wrap_angle_180(double angle_deg) {
    double a = std::fmod(angle_deg + 180.0, 360.0);
    if (a < 0.0) a += 360.0;
    return a - 180.0;
}

// 取景角度滑条的归一（PBZFM 相机组 pitch/yaw/roll；单位度）：
//   0=pitch -> 夹到 ±pitch_limit（与鼠标俯仰同一上限，避免菜单滑条把镜头翻过顶）
//   1=yaw   -> [-180, 180)（内部仍是 0..360，等价角；对称区间便于滑条显示）
//   2=roll  -> [-180, 180)
// 非法轴原样返回。纯函数，菜单写入与相机姿态读取共用，单测覆盖。
inline double normalize_pose_angle(int axis, double angle_deg, double pitch_limit_deg = 89.0) {
    switch (axis) {
        case 0:
            return clamp_pitch(angle_deg, pitch_limit_deg);
        case 1:
        case 2:
            return wrap_angle_180(angle_deg);
        default:
            return angle_deg;
    }
}

inline Vec3 offset_by_local(const Vec3& loc, const Rot& rot, const Vec3& local_dir, double scale) {
    const Vec3 fwd = forward_from_angles(rot.pitch, rot.yaw);
    const Vec3 right = right_from_yaw(rot.yaw);
    return Vec3{
        loc.x + (fwd.x * local_dir.x + right.x * local_dir.y) * scale,
        loc.y + (fwd.y * local_dir.x + right.y * local_dir.y) * scale,
        loc.z + (fwd.z * local_dir.x) * scale + local_dir.z * scale,
    };
}

}  // namespace pbzpm
