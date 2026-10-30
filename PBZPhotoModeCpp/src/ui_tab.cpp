#ifdef PBZPM_WITH_UE4SS

#include "ui_tab.h"

#include <imgui.h>

#include <string>

#include "core/menu_model.h"
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

    const MenuModel model = build_menu_model(mode, keys);

    if (model.status_frozen) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "状态：%s", model.status.c_str());
    } else if (model.status == "LIVE") {
        ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.5f, 1.0f), "状态：%s", model.status.c_str());
    } else {
        ImGui::TextDisabled("状态：%s", model.status.c_str());
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
    ImGui::SameLine();
    if (ImGui::Button("隐藏菜单")) {
        commands.hide_menu = true;
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

    for (const std::string& hint : model.hints) {
        ImGui::TextDisabled("%s", hint.c_str());
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
