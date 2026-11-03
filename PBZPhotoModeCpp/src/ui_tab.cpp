#ifdef PBZPM_WITH_UE4SS

#include "ui_tab.h"

#include <imgui.h>

#include <string>

#include "core/gamepad.h"
#include "core/version.h"
#include "pbzfm_client_lang.h"

namespace pbzpm {

void render_photo_mode_tab(PhotoMode& mode, Config& cfg, const KeyMap& keys, bool& request_reload_config,
                           UiCommands& commands, PbzmI18n& lang) {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(PBZPM_DISPLAY_NAME " " PBZPM_VERSION, nullptr,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // 状态行：OFF / LIVE / FROZEN（与 core 的机器值一致，tests 锁定）
    const char* status = !mode.active() ? "OFF" : (mode.frozen() ? "FROZEN" : "LIVE");
    const std::string status_fmt = lang.T("#console_status_fmt");
    if (mode.frozen()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), status_fmt.c_str(), status);
    } else if (mode.active()) {
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), status_fmt.c_str(), status);
    } else {
        ImGui::TextDisabled(status_fmt.c_str(), status);
    }

    ImGui::Separator();

    // FOV：滑块 + 直接输入
    float fov = static_cast<float>(mode.fov());
    if (ImGui::SliderFloat("FOV", &fov, static_cast<float>(cfg.fov_min), static_cast<float>(cfg.fov_max), "%.1f")) {
        commands.apply_fov = true;
        commands.new_fov = static_cast<double>(fov);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", lang.T("#console_fov_tip").c_str());
    }

    // 以下滑条与 PBZFM 菜单是同一批设置项，label 复用同一批键（#move_speed 等）
    float speed = static_cast<float>(cfg.move_speed);
    if (ImGui::SliderFloat(lang.T("#move_speed").c_str(), &speed, 50.0f, 2000.0f, "%.0f")) {
        cfg.move_speed = static_cast<double>(speed);
        mode.set_config(cfg);
    }

    float sens = static_cast<float>(cfg.look_sensitivity);
    if (ImGui::SliderFloat(lang.T("#look_sensitivity").c_str(), &sens, 0.01f, 1.0f, "%.2f")) {
        cfg.look_sensitivity = static_cast<double>(sens);
        mode.set_config(cfg);
    }

    float slow_look = static_cast<float>(cfg.slow_look_mult);
    if (ImGui::SliderFloat(lang.T("#slow_look_mult").c_str(), &slow_look, 0.01f, 1.0f, "%.2f")) {
        cfg.slow_look_mult = static_cast<double>(slow_look);
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", lang.T("#console_ctrl_look_tip").c_str());
    }

    float yaw_speed = static_cast<float>(cfg.yaw_speed);
    if (ImGui::SliderFloat(lang.T("#yaw_speed").c_str(), &yaw_speed, 5.0f, 270.0f, "%.0f")) {
        cfg.yaw_speed = static_cast<double>(yaw_speed);
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", lang.T("#console_yaw_speed_tip").c_str());
    }

    ImGui::Separator();
    ImGui::Text(lang.T("#console_pos_fmt").c_str(), mode.pose().loc.x, mode.pose().loc.y, mode.pose().loc.z);
    ImGui::Text(lang.T("#console_rot_fmt").c_str(), mode.pose().rot.pitch, mode.pose().rot.yaw, mode.pose().rot.roll);

    ImGui::Separator();

    const std::string freeze_label = mode.frozen() ? lang.T("#btn_resume") : lang.T("#btn_freeze");
    if (ImGui::Button(freeze_label.c_str())) {
        commands.toggle_freeze = true;
    }

    ImGui::Separator();

    bool hide_player = cfg.hide_player;
    if (ImGui::Checkbox(lang.T("#hide_player").c_str(), &hide_player)) {
        cfg.hide_player = hide_player;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool block_input = cfg.block_game_input;
    if (ImGui::Checkbox(lang.T("#console_block_input").c_str(), &block_input)) {
        cfg.block_game_input = block_input;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool hide_hud = cfg.hide_game_hud;
    if (ImGui::Checkbox(lang.T("#console_hide_hud").c_str(), &hide_hud)) {
        cfg.hide_game_hud = hide_hud;
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    bool auto_pause = cfg.auto_pause_time;
    if (ImGui::Checkbox(lang.T("#console_auto_pause").c_str(), &auto_pause)) {
        cfg.auto_pause_time = auto_pause;
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(lang.T("#console_auto_pause_tip").c_str(), vk_to_name(keys.freeze).c_str());
    }

    ImGui::Separator();

    bool pad_enabled = cfg.pad_enabled;
    if (ImGui::Checkbox(lang.T("#pad_enabled").c_str(), &pad_enabled)) {
        cfg.pad_enabled = pad_enabled;
        mode.set_config(cfg);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(lang.T("#console_pad_tip").c_str(), pad_combo_name(keys.pad_toggle).c_str());
    }
    ImGui::SameLine();
    float pad_look = static_cast<float>(cfg.pad_look_speed);
    if (ImGui::SliderFloat(lang.T("#pad_look_speed").c_str(), &pad_look, 30.0f, 540.0f, "%.0f")) {
        cfg.pad_look_speed = static_cast<double>(pad_look);
        mode.set_config(cfg);
    }
    ImGui::SameLine();
    float pad_deadzone = static_cast<float>(cfg.pad_deadzone);
    if (ImGui::SliderFloat(lang.T("#pad_deadzone").c_str(), &pad_deadzone, 0.0f, 0.5f, "%.2f")) {
        cfg.pad_deadzone = static_cast<double>(pad_deadzone);
        mode.set_config(cfg);
    }

    ImGui::Separator();

    // 键位提示（游戏内无菜单，这里就是唯一的操作速查）
    ImGui::TextUnformatted(lang.T("#console_help_look").c_str());
    ImGui::TextDisabled(lang.T("#console_help_move").c_str(), vk_to_name(keys.roll_left).c_str(),
                        vk_to_name(keys.roll_right).c_str());
    ImGui::TextUnformatted(lang.T("#console_help_wheel").c_str());
    ImGui::TextDisabled(lang.T("#console_help_reset").c_str(), vk_to_name(keys.reset).c_str());
    ImGui::TextDisabled(lang.T("#console_help_roll").c_str(), vk_to_name(keys.roll_reset).c_str());
    ImGui::TextDisabled(lang.T("#console_help_keys").c_str(), vk_to_name(keys.freeze).c_str(),
                        vk_to_name(keys.toggle).c_str());
    if (cfg.pad_enabled) {
        ImGui::TextDisabled(lang.T("#console_help_pad1").c_str(), pad_combo_name(keys.pad_toggle).c_str());
        ImGui::TextDisabled(lang.T("#console_help_pad2").c_str(), pad_combo_name(keys.pad_pitch_up).c_str(),
                            pad_combo_name(keys.pad_pitch_down).c_str(), pad_combo_name(keys.pad_yaw_left).c_str(),
                            pad_combo_name(keys.pad_yaw_right).c_str());
        ImGui::TextDisabled(lang.T("#console_help_pad3").c_str(), pad_combo_name(keys.pad_fov_up).c_str(),
                            pad_combo_name(keys.pad_fov_down).c_str(), pad_combo_name(keys.pad_freeze).c_str(),
                            pad_combo_name(keys.pad_reset).c_str(), pad_combo_name(keys.pad_roll_reset).c_str());
    }

    ImGui::Separator();
    if (ImGui::Button(lang.T("#btn_reload").c_str())) {
        request_reload_config = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(lang.T("#btn_exit").c_str())) {
        commands.exit_mode = true;
    }

    ImGui::End();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
