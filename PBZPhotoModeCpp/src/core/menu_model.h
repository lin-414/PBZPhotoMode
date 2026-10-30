#pragma once

#include <string>
#include <vector>

#include "core/input_logic.h"
#include "core/photo_mode.h"

namespace pbzpm {

struct MenuRow {
    std::string label;
    std::string label_ascii;  // 引擎内置字体没有中文，右下角文字菜单用这份
    std::string value;
    std::string value_ascii;
    bool emphasis{false};
};

struct MenuModel {
    std::string title;
    std::string status;      // "OFF" / "LIVE" / "FROZEN"
    bool status_frozen{false};
    std::vector<MenuRow> rows;
    std::vector<std::string> hints;
    std::vector<std::string> hints_ascii;
};

// 菜单内容与绘制技术无关：ImGui 面板和文字兜底共用同一份数据
MenuModel build_menu_model(const PhotoMode& mode, const KeyMap& keys);

// 多行纯文本（右下角文字菜单、日志、README 截图都用它）
std::string render_menu_text(const MenuModel& model);

// 纯 ASCII 版本：UE 内置调试字体没有中文字形，PrintString 只能用这个
std::string render_menu_text_ascii(const MenuModel& model);

}  // namespace pbzpm
