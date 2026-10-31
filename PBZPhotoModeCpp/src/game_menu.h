#pragma once

#ifdef PBZPM_WITH_UE4SS

#include "core/menu_view.h"

namespace pbzpm {

// 画一帧游戏内菜单。调用方保证已处于有效的 ImGui 帧上下文中
// （imgui_overlay 的 NewFrame 与 Render 之间）。只读 view、只写 actions，
// 不碰任何引擎对象。
void render_game_menu(const MenuView& view, MenuActions& actions);

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
