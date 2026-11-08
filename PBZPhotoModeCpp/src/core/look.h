#pragma once

#include "tuning.h"

// 画面效果（[look] 段）参数集合。
//
// 为什么集中成一个结构：这些字段全部走同一个通道——自建的无界高优先级 PostProcessVolume。
// 本作 gameplay 视图里，实测只有「体积的后处理」能出画面（暗角探针证明），相机 POV 的
// PostProcessSettings 写了没反应。所以每一项都只是往那个体积里写一组 bOverride_* + 值。
// （2026-11 起暗角/泛光另有相机 POV 每帧重申通道，因为游戏镜头的 PostProcessSettings 会覆盖
// 这两项；饱和/对比/冷暖与调色·进阶三件仍走体积。）
//
// 中性值（= 不覆盖该项，写回时把 bOverride_ 置回 false，等于「这一项交还给游戏自己的设置」）：
//   vignette/bloom/temperature/color_offset → 0
//   saturation/contrast/color_gain/color_gamma → 1
//
// 调色·进阶三件（亮度/伽马/黑位）2026-11-08 实机验证存活——本 build 的 ColorGrading LUT 只
// 消费每通道经典五件（饱和/对比/伽马/增益/偏移）；当日试过的其余十项（分区调色/胶片曲线/
// 白平衡/泛光阈值软膝）判死移除，取证结论在 tuning.h。三件没有独立启用开关（沿用色差先例：
// 中性值即关闭），每次进入摄影模式时数值一起恢复默认。

namespace pbzpm {

struct LookOptions {
    double vignette{0.0};     // 暗角 0~4
    double saturation{1.0};   // 饱和度倍率 0~2（1=原样）
    double contrast{1.0};     // 对比度倍率 0~2（1=原样）
    double bloom{0.0};        // 泛光强度 0~20（0=不覆盖）
    double temperature{0.0};  // 冷暖 -1~1（0=不覆盖）

    // 五项各自独立的启用开关：关掉只停用该项，滑块值保留（供下次打开或重新启用）。
    // Config 里对应 vignette_enabled / saturation_enabled / contrast_enabled /
    // bloom_enabled / temperature_enabled；每次进入摄影模式时连同数值一起恢复默认。
    bool vignette_enabled{true};
    bool saturation_enabled{true};
    bool contrast_enabled{true};
    bool bloom_enabled{true};
    bool temperature_enabled{true};

    // 调色·进阶三件（中性值见 tuning.h；无独立开关，值回到中性=停用）。
    double color_gain{tuning::kColorGainDefault};      // 亮度增益 0~4（1=原样）
    double color_gamma{tuning::kColorGammaDefault};    // 伽马 0.25~4（1=原样）
    double color_offset{tuning::kColorOffsetDefault};  // 黑位 -0.2~0.2（0=原样）

    bool vignette_active() const { return vignette_enabled && vignette > 1e-6; }
    bool saturation_active() const {
        return saturation_enabled && (saturation < 1.0 - 1e-6 || saturation > 1.0 + 1e-6);
    }
    bool contrast_active() const {
        return contrast_enabled && (contrast < 1.0 - 1e-6 || contrast > 1.0 + 1e-6);
    }
    bool bloom_active() const { return bloom_enabled && bloom > 1e-6; }
    bool temperature_active() const { return temperature_enabled && (temperature < -1e-6 || temperature > 1e-6); }

    // 调色·进阶三件：偏离中性值超 eps 即活跃（中性即恒等值，无「非恒等中性」的特例）。
    bool color_gain_active() const {
        return color_gain < tuning::kColorGainDefault - 1e-6 || color_gain > tuning::kColorGainDefault + 1e-6;
    }
    bool color_gamma_active() const {
        return color_gamma < tuning::kColorGammaDefault - 1e-6 || color_gamma > tuning::kColorGammaDefault + 1e-6;
    }
    bool color_offset_active() const { return color_offset < -1e-6 || color_offset > 1e-6; }

    // ColorGrading LUT 家族任一活跃——引擎侧写入时据此把 ColorGradingIntensity 钉成 1
    //（LUT 不参与合成则这些项全部无效；2026-11-08 实机：增益/伽马随饱和/对比同一 LUT 生效）。
    bool color_grading_active() const {
        return saturation_active() || contrast_active() || color_gain_active() || color_gamma_active() ||
               color_offset_active();
    }

    // 调色·进阶三件（不含老五项）任一活跃：photo_mode 首启日志与引擎 POV 诊断日志据此门控。
    bool color_pro_active() const {
        return color_gain_active() || color_gamma_active() || color_offset_active();
    }

    bool any_active() const {
        return vignette_active() || saturation_active() || contrast_active() || bloom_active() ||
               temperature_active() || color_pro_active();
    }
};

// 两份参数是否等价（用于「值没变就不下发」的判定）。
inline bool look_options_equal(const LookOptions& a, const LookOptions& b) {
    const double eps = 1e-6;
    return a.vignette_enabled == b.vignette_enabled && a.saturation_enabled == b.saturation_enabled &&
           a.contrast_enabled == b.contrast_enabled && a.bloom_enabled == b.bloom_enabled &&
           a.temperature_enabled == b.temperature_enabled &&
           (a.vignette - b.vignette) < eps && (b.vignette - a.vignette) < eps &&
           (a.saturation - b.saturation) < eps && (b.saturation - a.saturation) < eps &&
           (a.contrast - b.contrast) < eps && (b.contrast - a.contrast) < eps &&
           (a.bloom - b.bloom) < eps && (b.bloom - a.bloom) < eps &&
           (a.temperature - b.temperature) < eps && (b.temperature - a.temperature) < eps &&
           (a.color_gain - b.color_gain) < eps && (b.color_gain - a.color_gain) < eps &&
           (a.color_gamma - b.color_gamma) < eps && (b.color_gamma - a.color_gamma) < eps &&
           (a.color_offset - b.color_offset) < eps && (b.color_offset - a.color_offset) < eps;
}

}  // namespace pbzpm
