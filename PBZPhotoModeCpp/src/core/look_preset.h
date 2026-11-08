#pragma once

// 「画面效果」风格预设（2026-11-08 批次）：当前风格快照的保存与套用。
//
// 入口只有 PBZFM「画面效果」组页的「保存预设」按钮 + 预设下拉框 + 删除按钮（pbzfm_client.cpp
// 渲染，不经 schema/命令队列）。保存 = 把当前 9 个风格字段 + 5 个开关写进
// <mod>/look_preset_<编号>.ini（编号 = 现有最大编号 + 1，绝不覆盖已有预设；原子替换）；下拉
// 选中 = 按文件原样整表套用（走 commit_settings 既有管线）；删除 = 删文件本体（两击确认）。
// header-only：预设文本的生成/解析都是纯函数，核心层测试直接覆盖，也避开新建 .cpp 时
// CMakeLists 与 tools/build_tests.bat 的两处源列表手工同步。
//
// 语义约定：
//   · 预设只碰 9 个风格字段 + 5 个开关；**不碰** sharpness / screen_percentage——它们是
//     渲染管线设置，不是风格。
//   · 套用按文件原样（含开关），与保存严格往返；文件缺失/为空/无 [look] 段返回 false
//    （防「静默套默认值」），out 不动。
//   · 文件键名/取值与 config.ini 的 [look] 段一致，parse_config 可直接解析；删除后的编号
//     不回收（保存永远取最大编号 + 1），避免外部残留同名文件被新预设覆盖。
//   · （历史）2026-11-08 同批 UI 演进：5 个写死初值按钮（黑白/电影感等）→ 3 固定槽位导入
//     导出 → 本形态（保存 + 下拉 + 删除），均为用户决策；本文件随之只留文本生成/解析。
#include <sstream>
#include <string>

#include "config.h"

namespace pbzpm {

// 导出文本：只含 [look] 段的 9 个风格字段 + 5 个开关（键名/取值与 config.ini 的 [look] 一致，
// parse_config 可直接解析）。不含锐化/超采样——渲染管线设置不进风格槽位。
inline std::string look_preset_ini_text(const Config& c) {
    std::ostringstream out;
    out << "[look]\n"
        << "; 风格槽位（PBZFM「画面效果」页导出）：导入 = 整表套用（含开关）。\n"
        << "; 手改请保持键名与取值范围（与 config.ini 的 [look] 段一致）；不含锐化/超采样。\n"
        << "vignette = " << c.vignette << "\n"
        << "saturation = " << c.saturation << "\n"
        << "contrast = " << c.contrast << "\n"
        << "bloom = " << c.bloom << "\n"
        << "temperature = " << c.temperature << "\n"
        << "chromatic_aberration = " << c.chromatic_aberration << "\n"
        << "color_gain = " << c.color_gain << "\n"
        << "color_gamma = " << c.color_gamma << "\n"
        << "color_offset = " << c.color_offset << "\n"
        << "vignette_enabled = " << (c.vignette_enabled ? "true" : "false") << "\n"
        << "saturation_enabled = " << (c.saturation_enabled ? "true" : "false") << "\n"
        << "contrast_enabled = " << (c.contrast_enabled ? "true" : "false") << "\n"
        << "bloom_enabled = " << (c.bloom_enabled ? "true" : "false") << "\n"
        << "temperature_enabled = " << (c.temperature_enabled ? "true" : "false") << "\n";
    return out.str();
}

// 导入解析：从文本里取回 14 个风格字段（原样套用，含开关）。空文本 / 无 [look] 段返回
// false（out 不动）。parse_config 对未知键只告警不报错，所以必须先拦下「空文件静默套用
// 默认值」的路径。
inline bool parse_look_preset_ini(const std::string& text, Config& out) {
    if (text.find("[look]") == std::string::npos) return false;
    const ConfigLoadResult result = parse_config(text, "look_preset");
    out.vignette = result.cfg.vignette;
    out.saturation = result.cfg.saturation;
    out.contrast = result.cfg.contrast;
    out.bloom = result.cfg.bloom;
    out.temperature = result.cfg.temperature;
    out.chromatic_aberration = result.cfg.chromatic_aberration;
    out.color_gain = result.cfg.color_gain;
    out.color_gamma = result.cfg.color_gamma;
    out.color_offset = result.cfg.color_offset;
    out.vignette_enabled = result.cfg.vignette_enabled;
    out.saturation_enabled = result.cfg.saturation_enabled;
    out.contrast_enabled = result.cfg.contrast_enabled;
    out.bloom_enabled = result.cfg.bloom_enabled;
    out.temperature_enabled = result.cfg.temperature_enabled;
    return true;
}

}  // namespace pbzpm
