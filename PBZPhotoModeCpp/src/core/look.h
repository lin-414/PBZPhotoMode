#pragma once

// 画面效果（[look] 段）参数集合。
//
// 为什么集中成一个结构：这些字段全部走同一个通道——自建的无界高优先级 PostProcessVolume。
// 本作 gameplay 视图里，实测只有「体积的后处理」能出画面（暗角探针证明），相机 POV 的
// PostProcessSettings 写了没反应。所以每一项都只是往那个体积里写一组 bOverride_* + 值。
//
// 中性值（= 不覆盖该项）：
//   vignette/bloom/temperature → 0
//   saturation/contrast → 1
// 写回中性值时会把 bOverride_ 置回 false，等于「这一项交还给游戏自己的设置」。
//
// 只剩五项。色差与胶片颗粒在 hotfix33 判死（两个 pass 都被 r.Tonemapper.Quality=2 这一档整段
// 跳过，而本作把该 cvar 钉在比任何游戏设置级写入都高的优先级上：抬档回读不变、补噪声纹理后
// 强度拉到 6.85 仍无颗粒），hotfix34 按景深那套做法把滑条与配置键整条删除。
// 取证结论保留在 core/tuning.h 顶部。

namespace pbzpm {

struct LookOptions {
    double vignette{0.0};     // 暗角 0~4
    double saturation{1.0};   // 饱和度倍率 0~2（1=原样）
    double contrast{1.0};     // 对比度倍率 0~2（1=原样）
    double bloom{0.0};        // 泛光强度 0~20（0=不覆盖）
    double temperature{0.0};  // 冷暖 -1~1（0=不覆盖）

    bool vignette_active() const { return vignette > 1e-6; }
    bool saturation_active() const { return saturation < 1.0 - 1e-6 || saturation > 1.0 + 1e-6; }
    bool contrast_active() const { return contrast < 1.0 - 1e-6 || contrast > 1.0 + 1e-6; }
    bool bloom_active() const { return bloom > 1e-6; }
    bool temperature_active() const { return temperature < -1e-6 || temperature > 1e-6; }
    bool any_active() const {
        return vignette_active() || saturation_active() || contrast_active() || bloom_active() ||
               temperature_active();
    }
};

// 两份参数是否等价（用于「值没变就不下发」的判定）。
inline bool look_options_equal(const LookOptions& a, const LookOptions& b) {
    const double eps = 1e-6;
    return (a.vignette - b.vignette) < eps && (b.vignette - a.vignette) < eps &&
           (a.saturation - b.saturation) < eps && (b.saturation - a.saturation) < eps &&
           (a.contrast - b.contrast) < eps && (b.contrast - a.contrast) < eps &&
           (a.bloom - b.bloom) < eps && (b.bloom - a.bloom) < eps &&
           (a.temperature - b.temperature) < eps && (b.temperature - a.temperature) < eps;
}

}  // namespace pbzpm
