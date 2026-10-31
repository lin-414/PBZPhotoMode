#ifdef PBZPM_WITH_UE4SS

#include "game_menu.h"

#include <imgui.h>

namespace pbzpm {
namespace {

// 字体没加载出中文时整体退英文
const char* zh(bool cjk, const char* text_zh, const char* text_en) {
    return cjk ? text_zh : text_en;
}

}  // namespace

void render_game_menu(const MenuView& view, MenuActions& actions) {
    const bool cjk = view.font_cjk_ok;
    bool open = true;
    if (!ImGui::Begin(zh(cjk, "影之刃零 · 摄影模式", "PBZ Photo Mode"), &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!open) actions.close_menu = true;
        ImGui::End();
        return;
    }
    if (!open) actions.close_menu = true;

    if (ImGui::BeginTabBar("pbzpm_tabs")) {
        // ---------------- 相机页：滑条 + 只读信息 + 动作按钮
        if (ImGui::BeginTabItem(zh(cjk, "相机", "Camera"))) {
            if (view.status == "FROZEN") {
                ImGui::TextColored(ImVec4(1.0f, 0.60f, 0.25f, 1.0f), "%s: %s",
                                   zh(cjk, "状态", "State"), zh(cjk, "时间已冻结", "FROZEN"));
            } else if (view.status == "LIVE") {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s: %s",
                                   zh(cjk, "状态", "State"), zh(cjk, "运行中", "LIVE"));
            } else {
                ImGui::TextDisabled("%s: %s", zh(cjk, "状态", "State"), zh(cjk, "未启用", "OFF"));
            }
            ImGui::Separator();

            float fov = static_cast<float>(view.fov);
            if (ImGui::SliderFloat(zh(cjk, "视场角 (FOV)", "FOV"), &fov,
                                   static_cast<float>(view.fov_min), static_cast<float>(view.fov_max), "%.1f")) {
                actions.fov_changed = true;
                actions.fov = fov;
            }
            ImGui::SetItemTooltip("%s", zh(cjk, "也可以用鼠标滚轮或手柄十字键调整",
                                   "Mouse wheel or D-pad also adjusts FOV"));

            float speed = static_cast<float>(view.move_speed);
            if (ImGui::SliderFloat(zh(cjk, "移动速度", "Move speed"), &speed, 50.0f, 2000.0f, "%.0f")) {
                actions.speed_changed = true;
                actions.speed = speed;
            }

            float sens = static_cast<float>(view.look_sensitivity);
            if (ImGui::SliderFloat(zh(cjk, "视角灵敏度", "Look sensitivity"), &sens, 0.01f, 1.0f, "%.2f")) {
                actions.sens_changed = true;
                actions.sens = sens;
            }

            ImGui::Separator();
            ImGui::Text("%s: %.0f, %.0f, %.0f", zh(cjk, "位置", "Pos"), view.pos[0], view.pos[1], view.pos[2]);
            ImGui::Text("%s: %s %.1f   %s %.1f", zh(cjk, "朝向", "Rot"),
                        zh(cjk, "俯仰", "pitch"), view.rot[0], zh(cjk, "偏航", "yaw"), view.rot[1]);
            ImGui::Separator();

            if (ImGui::Button(view.status_frozen ? zh(cjk, "恢复时间", "Resume time")
                                                 : zh(cjk, "冻结时间", "Freeze time"))) {
                actions.toggle_freeze = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(zh(cjk, "重置视角", "Reset view"))) {
                actions.reset_view = true;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            if (ImGui::Button(zh(cjk, "退出模式", "Exit"))) {
                actions.exit_mode = true;
            }
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        // ---------------- 快捷键页
        if (ImGui::BeginTabItem(zh(cjk, "快捷键", "Keys"))) {
            const std::vector<std::string>& hints = cjk ? view.hints : view.hints_ascii;
            if (hints.empty()) {
                ImGui::TextDisabled("%s", zh(cjk, "（无）", "(none)"));
            }
            for (const std::string& hint : hints) {
                ImGui::Bullet();
                ImGui::TextUnformatted(hint.c_str());
            }
            ImGui::EndTabItem();
        }

        // ---------------- 选项页
        if (ImGui::BeginTabItem(zh(cjk, "选项", "Options"))) {
            bool value = view.hide_player;
            if (ImGui::Checkbox(zh(cjk, "隐藏角色", "Hide player"), &value)) {
                actions.flag_hide_player = true;
                actions.v_hide_player = value;
            }
            ImGui::SameLine();
            value = view.block_game_input;
            if (ImGui::Checkbox(zh(cjk, "屏蔽游戏输入", "Block game input"), &value)) {
                actions.flag_block_input = true;
                actions.v_block_input = value;
            }

            value = view.hide_game_hud;
            if (ImGui::Checkbox(zh(cjk, "隐藏游戏 HUD", "Hide game HUD"), &value)) {
                actions.flag_hide_hud = true;
                actions.v_hide_hud = value;
            }
            ImGui::SameLine();
            value = view.auto_pause_time;
            if (ImGui::Checkbox(zh(cjk, "进入时冻结时间", "Auto-freeze on enter"), &value)) {
                actions.flag_auto_pause = true;
                actions.v_auto_pause = value;
            }

            ImGui::Separator();
            value = view.pad_enabled;
            if (ImGui::Checkbox(zh(cjk, "手柄支持", "Gamepad"), &value)) {
                actions.flag_pad_enabled = true;
                actions.v_pad_enabled = value;
            }
            ImGui::SameLine();
            float pad_speed = static_cast<float>(view.pad_look_speed);
            if (ImGui::SliderFloat(zh(cjk, "手柄视角", "Pad look"), &pad_speed, 30.0f, 540.0f, "%.0f")) {
                actions.pad_speed_changed = true;
                actions.pad_speed = pad_speed;
            }
            float deadzone = static_cast<float>(view.pad_deadzone);
            if (ImGui::SliderFloat(zh(cjk, "手柄死区", "Pad deadzone"), &deadzone, 0.0f, 0.5f, "%.2f")) {
                actions.pad_deadzone_changed = true;
                actions.pad_deadzone = deadzone;
            }

            ImGui::Separator();
            ImGui::TextDisabled("%s", zh(cjk,
                "改键位与更多选项：编辑 config.ini（改键需重启游戏）",
                "Edit config.ini for key bindings (restart to apply)"));
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    actions.rect_valid = true;
    actions.rect[0] = window_pos.x;
    actions.rect[1] = window_pos.y;
    actions.rect[2] = window_size.x;
    actions.rect[3] = window_size.y;
    ImGui::End();
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
