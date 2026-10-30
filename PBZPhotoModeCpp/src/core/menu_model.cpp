#include "core/menu_model.h"

#include <cstdio>
#include <sstream>

#include "core/gamepad.h"
#include "core/version.h"
#include "core/vk.h"

namespace pbzpm {
namespace {

std::string fmt(const char* pattern, double a) {
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), pattern, a);
    return buf;
}

std::string fmt2(const char* pattern, double a, double b) {
    char buf[96]{};
    std::snprintf(buf, sizeof(buf), pattern, a, b);
    return buf;
}

}  // namespace

MenuModel build_menu_model(const PhotoMode& mode, const KeyMap& keys) {
    MenuModel model;
    model.title = std::string(PBZPM_DISPLAY_NAME) + "  " + PBZPM_VERSION;
    model.status_frozen = mode.frozen();
    model.status = !mode.active() ? "OFF" : (mode.frozen() ? "FROZEN" : "LIVE");

    const CameraPose& pose = mode.pose();

    MenuRow fov;
    fov.label = "FOV";
    fov.label_ascii = "FOV";
    fov.value = fmt("%.1f", mode.fov());
    fov.value_ascii = fov.value;
    fov.emphasis = true;
    model.rows.push_back(fov);

    MenuRow speed;
    speed.label = "移动速度";
    speed.label_ascii = "SPEED";
    speed.value = fmt("%.0f /s", mode.config().move_speed);
    speed.value_ascii = speed.value;
    model.rows.push_back(speed);

    MenuRow pos;
    pos.label = "位置";
    pos.label_ascii = "POS";
    pos.value = fmt("%.0f", pose.loc.x) + ", " + fmt("%.0f", pose.loc.y) + ", " + fmt("%.0f", pose.loc.z);
    pos.value_ascii = pos.value;
    model.rows.push_back(pos);

    MenuRow view;
    view.label = "朝向";
    view.label_ascii = "VIEW";
    view.value = "俯仰 " + fmt("%.1f", pose.rot.pitch) + "  偏航 " + fmt("%.1f", pose.rot.yaw);
    view.value_ascii = fmt2("pitch %.1f  yaw %.1f", pose.rot.pitch, pose.rot.yaw);
    model.rows.push_back(view);

    MenuRow block;
    block.label = "屏蔽移动输入";
    block.label_ascii = "BLOCK INPUT";
    block.value = mode.config().block_game_input ? "开" : "关";
    block.value_ascii = mode.config().block_game_input ? "on" : "off";
    model.rows.push_back(block);

    MenuRow freeze;
    freeze.label = "冻结方式";
    freeze.label_ascii = "FREEZE";
    const bool pause_mode = mode.config().freeze_mode == FreezeMode::Pause;
    freeze.value = pause_mode ? "pause（全局暂停）" : "dilation（时间变慢）";
    freeze.value_ascii = pause_mode ? "pause" : "dilation";
    model.rows.push_back(freeze);

    MenuRow auto_pause;
    auto_pause.label = "进入时暂停";
    auto_pause.label_ascii = "AUTO PAUSE";
    auto_pause.value = mode.config().auto_pause_time ? "开" : "关";
    auto_pause.value_ascii = mode.config().auto_pause_time ? "on" : "off";
    model.rows.push_back(auto_pause);

    model.hints.push_back("ALT + 鼠标  自由转视角");
    model.hints.push_back("WASD 平移    Q/E 升降    Shift/Ctrl 变速");
    model.hints.push_back("滚轮 FOV    左键上移  右键下移");
    model.hints.push_back(vk_to_name(keys.reset) + " 重置相机视角（回到进入时）");
    model.hints.push_back(vk_to_name(keys.freeze) + " 冻结时间    " + vk_to_name(keys.menu) + " 隐藏菜单    " +
                          vk_to_name(keys.toggle) + " 退出");

    model.hints_ascii.push_back("ALT + MOUSE  free look");
    model.hints_ascii.push_back("WASD move    Q/E up-down    Shift/Ctrl speed");
    model.hints_ascii.push_back("WHEEL fov    LMB up   RMB down");
    model.hints_ascii.push_back(vk_to_name(keys.reset) + " reset view");
    model.hints_ascii.push_back(vk_to_name(keys.freeze) + " freeze    " + vk_to_name(keys.menu) + " hide menu    " +
                                vk_to_name(keys.toggle) + " exit");

    if (mode.config().pad_enabled) {
        model.hints.push_back("手柄 " + pad_combo_name(keys.pad_toggle) + " 进/出    左摇杆移动  右摇杆视角");
        model.hints.push_back("RT/LT 升降    LB/RB 变速    " + pad_combo_name(keys.pad_fov_up) + "/" +
                              pad_combo_name(keys.pad_fov_down) + " FOV    " + pad_combo_name(keys.pad_freeze) +
                              " 冻结  " + pad_combo_name(keys.pad_reset) + " 重置  " +
                              pad_combo_name(keys.pad_menu) + " 菜单");
        model.hints_ascii.push_back("PAD " + pad_combo_name(keys.pad_toggle) + " enter/exit   L-stick move  R-stick look");
        model.hints_ascii.push_back("RT/LT up-dn   LB/RB speed   " + pad_combo_name(keys.pad_fov_up) + "/" +
                                     pad_combo_name(keys.pad_fov_down) + " fov   " + pad_combo_name(keys.pad_freeze) +
                                     " freeze  " + pad_combo_name(keys.pad_reset) + " reset  " +
                                     pad_combo_name(keys.pad_menu) + " menu");
    }
    return model;
}

std::string render_menu_text(const MenuModel& model) {
    std::ostringstream out;
    out << model.title << "\n";
    out << "状态 : " << model.status << "\n";
    out << "--------------------------\n";
    for (const MenuRow& row : model.rows) {
        out << row.label << " : " << row.value << "\n";
    }
    out << "--------------------------\n";
    for (const std::string& hint : model.hints) {
        out << hint << "\n";
    }
    return out.str();
}

std::string render_menu_text_ascii(const MenuModel& model) {
    std::ostringstream out;
    out << model.title << "\n";
    out << "STATE : " << model.status << "\n";
    out << "--------------------------\n";
    for (const MenuRow& row : model.rows) {
        out << row.label_ascii << " : " << row.value_ascii << "\n";
    }
    out << "--------------------------\n";
    for (const std::string& hint : model.hints_ascii) {
        out << hint << "\n";
    }
    return out.str();
}

}  // namespace pbzpm
