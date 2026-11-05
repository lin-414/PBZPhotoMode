#pragma once

// ImGui 面板：只在真实 DLL 构建里参与编译。
#ifdef PBZPM_WITH_UE4SS

#include "core/config.h"
#include "core/input_logic.h"
#include "core/photo_mode.h"

namespace pbzpm {

class PbzmI18n;

// 面板上的按钮通过这些输出参数回传给 mod_app，由它统一执行，避免 UI 直接碰引擎。
struct UiCommands {
    bool toggle_freeze{false};
    bool exit_mode{false};
    // apply_fov/new_fov：面板收敛为只读后不再置位，但 mod_app.cpp:547 仍在读这两个字段，
    // 删除需两侧同步（mod_app.cpp 不归本区改）。
    bool apply_fov{false};
    double new_fov{0.0};
};

// 画一帧只读诊断面板（不修改 cfg，也不改 mode）。可写设置项的唯一入口是 PBZFM 页。
// 文案取自 lang（外部 languages/*.json；与 PBZFM 自绘页共用同一份词典实例）。
void render_photo_mode_tab(PhotoMode& mode, Config& cfg, const KeyMap& keys, bool& request_reload_config,
                           UiCommands& commands, PbzmI18n& lang);

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
