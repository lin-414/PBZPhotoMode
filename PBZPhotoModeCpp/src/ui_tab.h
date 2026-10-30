#pragma once

// ImGui 面板：只在真实 DLL 构建里参与编译。
#ifdef PBZPM_WITH_UE4SS

#include "core/config.h"
#include "core/input_logic.h"
#include "core/photo_mode.h"

namespace pbzpm {

// 面板上的按钮通过这些输出参数回传给 mod_app，由它统一执行，避免 UI 直接碰引擎。
struct UiCommands {
    bool toggle_freeze{false};
    bool hide_menu{false};
    bool shutter{false};
    bool exit_mode{false};
    bool apply_fov{false};
    double new_fov{0.0};
};

// 画一帧面板。config 的改动会写回 cfg（下次进入生效）并同步给 mode。
void render_photo_mode_tab(PhotoMode& mode, Config& cfg, const KeyMap& keys, bool& request_reload_config,
                           UiCommands& commands);

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
