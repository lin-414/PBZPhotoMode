#ifdef PBZPM_WITH_UE4SS

#include "ui_tab.h"

#include <imgui.h>

#include <string>

#include "core/gamepad.h"
#include "core/version.h"
#include "pbzfm_client_lang.h"

// 本面板只读诊断：状态行 / 位姿 / 键位速查 / 冻结与恢复 / Reload / 退出。
// 可写设置的唯一入口是 PBZFM 页（src/pbzfm_client.cpp 自绘页）。这里曾有同一批 11 个
// 设置项的第二份可写实现，且提交语义不同（本地立刻 set_config，PBZFM 页走排队写盘 +
// submit_config），已删除——避免同一项在两个界面行为不同（审查报告 R-B-03）。
namespace pbzpm {

void render_photo_mode_tab(PhotoMode& mode, Config& cfg, const KeyMap& keys, bool& request_reload_config,
                           UiCommands& commands, PbzmI18n& lang) {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(PBZPM_DISPLAY_NAME " " PBZPM_VERSION_FULL, nullptr,
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
    ImGui::Text(lang.T("#console_pos_fmt").c_str(), mode.pose().loc.x, mode.pose().loc.y, mode.pose().loc.z);
    ImGui::Text(lang.T("#console_rot_fmt").c_str(), mode.pose().rot.pitch, mode.pose().rot.yaw, mode.pose().rot.roll);

    ImGui::Separator();

    const std::string freeze_label = mode.frozen() ? lang.T("#btn_resume") : lang.T("#btn_freeze");
    if (ImGui::Button(freeze_label.c_str())) {
        commands.toggle_freeze = true;
    }

    ImGui::Separator();

    // 键位提示（游戏内无菜单，这里就是唯一的操作速查）。cfg 只被读取：
    // pad_enabled 决定是否列出「手柄」那几行，本面板不修改它。
    ImGui::TextDisabled(lang.T("#console_help_look").c_str(), vk_to_name(keys.free_look).c_str());
    ImGui::TextDisabled(lang.T("#console_help_move").c_str(), vk_to_name(keys.move_forward).c_str(),
                        vk_to_name(keys.move_back).c_str(), vk_to_name(keys.move_left).c_str(),
                        vk_to_name(keys.move_right).c_str(), vk_to_name(keys.roll_left).c_str(),
                        vk_to_name(keys.roll_right).c_str(), vk_to_name(keys.boost).c_str(),
                        vk_to_name(keys.slow).c_str());
    if (cfg.wheel_fov) ImGui::TextUnformatted(lang.T("#console_help_wheel").c_str());
    ImGui::TextDisabled(lang.T("#console_help_vertical").c_str(), vk_to_name(keys.vertical_up).c_str(),
                        vk_to_name(keys.vertical_down).c_str());
    ImGui::TextDisabled(lang.T("#console_help_reset").c_str(), vk_to_name(keys.reset).c_str());
    ImGui::TextDisabled(lang.T("#console_help_roll").c_str(), vk_to_name(keys.roll_reset).c_str());
    ImGui::TextDisabled(lang.T("#console_help_keys").c_str(), vk_to_name(keys.freeze).c_str(),
                        vk_to_name(keys.toggle).c_str());
    if (cfg.pad_enabled) {
        ImGui::TextDisabled(lang.T("#console_help_pad1").c_str(), pad_combo_name(keys.pad_toggle).c_str());
        ImGui::TextDisabled(lang.T("#console_help_pad2").c_str(), pad_combo_name(keys.pad_vertical_up).c_str(),
                            pad_combo_name(keys.pad_vertical_down).c_str(), pad_combo_name(keys.pad_boost).c_str(),
                            pad_combo_name(keys.pad_slow).c_str(), pad_combo_name(keys.pad_pitch_up).c_str(),
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
