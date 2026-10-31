#ifdef PBZPM_WITH_UE4SS

#include "ui_tab.h"

#include <imgui.h>

#include <string>

#include "core/gamepad.h"
#include "core/version.h"

namespace pbzpm {

void render_photo_mode_tab(PhotoMode& mode, Config& cfg, const KeyMap& keys, bool& request_reload_config,
                           UiCommands& commands) {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(PBZPM_DISPLAY_NAME " " PBZPM_VERSION, nullptr,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // 状态行：OFF / LIVE / FROZEN（与 core 的机器值一致，tests 锁定）
    const char* status = !mode.active() ? "OFF" : (mode.frozen() ? "FROZEN" : "LIVE");
    if (mode.frozen()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "状态：%s", status);
    } else if (mode.active()) {
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), "状态：%s", status);
    } else {
        ImGui::TextDisabled("状态：%s", status);
    }

    ImGui::Separator();

    // FOV：滑块 + 直接输入
    float fov = static_cast<float>(mode.fov());
    if (ImGui::SliderFloat("FOV", &fov, static_cast<float>(cfg.fov_min), static_cast<float>(cfg.fov_max), "%.1f")) {
        commands.apply_fov = true;
        commands.new_fov = static_cast<double>(fov);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("也可以用鼠标滚轮调整（需按住 Alt）");
    }

    float speed = static_cast<float>(cfg.move_speed);
    if (ImGui::SliderFloat("移动速度", &speed, 50.0f, 2000.0f, "%.0f /s")) {
        cfg.move_speed = static_cast<double>(speed);
        mode.set_config(cfg);
    }

    float sens = static_cast<float>(cfg.look_sensitivity);
    if (ImGui::SliderFloat("视角灵敏度", &sens, 0.01f, 1.0f, "%.2f")) {
        cfg.look_sensitivity = static_cast<double>(sens);
        mode.set_config(cfg);
    }

    ImGui::Separator();
    ImGui::Text("位置：%.0f, %.0f, %.0f", mode.pose().loc.x, mode.pose().loc.y, mode.pose().loc.z);
    ImGui::Text("朝向：俯仰 %.1f   偏航 %.1f", mode.pose().rot.pitch, mode.pose().rot.yaw);

    ImGui::Separator();

    if (ImGui::Button(mode.frozen() ? "恢复时间" : "冻结时间")) {
        commands.toggle_freeze = true;
    }

    ImGui::Separator();

    bool hide_player = cfg.hide_player;
    if (ImGui::Checkbox("隐藏角色", &hide_player)) {
        cfg.hide_player = hide_player;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool block_input = cfg.block_game_input;
    if (ImGui::Checkbox("屏蔽角色输入", &block_input)) {
        cfg.block_game_input = block_input;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool hide_hud = cfg.hide_game_hud;
    if (ImGui::Checkbox("隐藏 HUD", &hide_hud)) {
        cfg.hide_game_hud = hide_hud;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool auto_pause = cfg.auto_pause_time;
    if (ImGui::Checkbox("进入时暂停", &auto_pause)) {
        cfg.auto_pause_time = auto_pause;
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("进入摄影模式时自动冻结时间；关闭后可按 %s 手动冻结。", vk_to_name(keys.freeze).c_str());
    }

    ImGui::Separator();

    bool pad_enabled = cfg.pad_enabled;
    if (ImGui::Checkbox("手柄支持", &pad_enabled)) {
        cfg.pad_enabled = pad_enabled;
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("手柄 %s 进入/退出摄影模式。\n改按钮绑定请编辑 config.ini 的 [gamepad] 节后点「重新载入」。",
                          pad_combo_name(keys.pad_toggle).c_str());
    }
    ImGui::SameLine();
    float pad_look = static_cast<float>(cfg.pad_look_speed);
    if (ImGui::SliderFloat("手柄视角速度", &pad_look, 30.0f, 540.0f, "%.0f 度/s")) {
        cfg.pad_look_speed = static_cast<double>(pad_look);
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    float pad_deadzone = static_cast<float>(cfg.pad_deadzone);
    if (ImGui::SliderFloat("手柄死区", &pad_deadzone, 0.0f, 0.5f, "%.2f")) {
        cfg.pad_deadzone = static_cast<double>(pad_deadzone);
        mode.set_config(cfg);
    }

    ImGui::Separator();

    // 键位提示（游戏内无菜单，这里就是唯一的操作速查）
    ImGui::TextDisabled("ALT + 鼠标  自由转视角");
    ImGui::TextDisabled("WASD 平移    Q/E 滚转    Shift/Ctrl 变速");
    ImGui::TextDisabled("滚轮 FOV    左键上移  右键下移");
    ImGui::TextDisabled("%s 重置相机视角（回到进入时）", vk_to_name(keys.reset).c_str());
    ImGui::TextDisabled("%s 冻结时间    %s 退出", vk_to_name(keys.freeze).c_str(), vk_to_name(keys.toggle).c_str());
    if (cfg.pad_enabled) {
        ImGui::TextDisabled("手柄 %s 进/出    左摇杆移动  右摇杆视角", pad_combo_name(keys.pad_toggle).c_str());
        ImGui::TextDisabled("RT/LT 升降    L3/R3 变速    %s/%s FOV（按住连调）    %s 冻结  %s 重置",
                            pad_combo_name(keys.pad_fov_up).c_str(), pad_combo_name(keys.pad_fov_down).c_str(),
                            pad_combo_name(keys.pad_freeze).c_str(), pad_combo_name(keys.pad_reset).c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("重新载入 config.ini")) {
        request_reload_config = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("退出摄影模式")) {
        commands.exit_mode = true;
    }

    ImGui::End();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
