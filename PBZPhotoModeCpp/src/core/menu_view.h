#pragma once

// 游戏内图形菜单的数据桥。
// 纯数据：引擎线程写 MenuView、渲染线程读；MenuActions 相反。
// 渲染层（game_menu/imgui_overlay）与逻辑层（mod_app）只通过这两份结构对话，
// 彼此不共享指针、不互相调用，跨线程安全靠 imgui_overlay 里的一把锁交换。

#include <string>
#include <vector>

namespace pbzpm {

// 引擎线程 → 渲染线程：菜单这一帧要显示什么
struct MenuView {
    bool active{false};      // 摄影模式进行中
    bool menu_open{false};   // 图形菜单要求显示
    bool font_cjk_ok{true};  // 渲染层回报：中文字体是否可用（决定显示中文还是英文）

    std::string status;      // "OFF" / "LIVE" / "FROZEN"
    bool status_frozen{false};

    double fov{90.0};
    double fov_min{15.0};
    double fov_max{150.0};
    double move_speed{400.0};
    double look_sensitivity{0.10};
    double pad_look_speed{180.0};
    double pad_deadzone{0.15};
    bool pad_enabled{true};

    // 选项页的开关（写回走 MenuActions，这里只是展示）
    bool hide_player{false};
    bool block_game_input{true};
    bool hide_game_hud{true};
    bool auto_pause_time{true};

    // 相机信息（只读展示）
    double pos[3]{0.0, 0.0, 0.0};
    double rot[2]{0.0, 0.0};  // pitch, yaw

    // 快捷键说明（快捷键页；中英两份，字体加载失败时用 ascii）
    std::vector<std::string> hints;
    std::vector<std::string> hints_ascii;
};

// 渲染线程 → 引擎线程：用户在菜单里做的操作
struct MenuActions {
    bool fov_changed{false};          double fov{0.0};
    bool speed_changed{false};        double speed{0.0};
    bool sens_changed{false};         double sens{0.0};
    bool pad_speed_changed{false};    double pad_speed{0.0};
    bool pad_deadzone_changed{false}; double pad_deadzone{0.0};

    bool flag_hide_player{false};   bool v_hide_player{false};
    bool flag_block_input{false};   bool v_block_input{false};
    bool flag_hide_hud{false};      bool v_hide_hud{false};
    bool flag_auto_pause{false};    bool v_auto_pause{false};
    bool flag_pad_enabled{false};   bool v_pad_enabled{false};

    bool toggle_freeze{false};
    bool reset_view{false};
    bool close_menu{false};
    bool exit_mode{false};

    // 菜单窗口矩形（渲染线程写，引擎线程用来判断滚轮/鼠标是否落在 UI 上）
    bool rect_valid{false};
    float rect[4]{0.0f, 0.0f, 0.0f, 0.0f};  // x, y, w, h

    // 渲染线程内部合并用：布尔沿求或，数值最新优先
    void merge(const MenuActions& o) {
        if (o.fov_changed) { fov_changed = true; fov = o.fov; }
        if (o.speed_changed) { speed_changed = true; speed = o.speed; }
        if (o.sens_changed) { sens_changed = true; sens = o.sens; }
        if (o.pad_speed_changed) { pad_speed_changed = true; pad_speed = o.pad_speed; }
        if (o.pad_deadzone_changed) { pad_deadzone_changed = true; pad_deadzone = o.pad_deadzone; }
        if (o.flag_hide_player) { flag_hide_player = true; v_hide_player = o.v_hide_player; }
        if (o.flag_block_input) { flag_block_input = true; v_block_input = o.v_block_input; }
        if (o.flag_hide_hud) { flag_hide_hud = true; v_hide_hud = o.v_hide_hud; }
        if (o.flag_auto_pause) { flag_auto_pause = true; v_auto_pause = o.v_auto_pause; }
        if (o.flag_pad_enabled) { flag_pad_enabled = true; v_pad_enabled = o.v_pad_enabled; }
        if (o.toggle_freeze) toggle_freeze = true;
        if (o.reset_view) reset_view = true;
        if (o.close_menu) close_menu = true;
        if (o.exit_mode) exit_mode = true;
        if (o.rect_valid) { rect_valid = true; for (int i = 0; i < 4; ++i) rect[i] = o.rect[i]; }
    }
    void clear() { *this = MenuActions{}; }
};

}  // namespace pbzpm
