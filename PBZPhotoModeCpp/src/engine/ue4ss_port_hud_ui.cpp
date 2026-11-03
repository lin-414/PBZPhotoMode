// =============================================================================
//  Ue4ssPort 成员函数实现：HUD / 交互提示 / 血条 / 字幕 / 对话 Flow / UI 设置修复 / 锁定标记
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

// 不走 controller_cache_ 的实时玩家 Pawn 查询：读档/换图期间缓存的 PlayerController 可能已经被
// 销毁（1.0.69 crash_2026_11_02_15_36_45 的根因），而且此时世界往往还没 BeginPlay，写设置会被
// 随后载入的存档覆盖。这里每次从对象数组重新枚举：只要求 Pawn 可读且对象有效；世界若可读且
// bBegunPlay 明确为 false 才继续等，属性读不到时不拦截（1.0.70 实机就是被过严的属性要求卡死）。
auto find_live_player_pawn() -> UObject* {
    std::vector<UObject*> candidates;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("PlayerController"), candidates);
    for (UObject* candidate : candidates) {
        if (!is_ok(candidate)) continue;
        UObject* pawn = nullptr;
        // 不强制要求 Player 属性可读：不同版本/蓝图里它可能是 FObjectPtrProperty 或读不到；
        // 能拿到真实 Pawn 就说明这是玩家控制器（观察者/过场 PC 通常没有 Pawn）。
        if (!read_object(find_property(candidate, "Pawn"), candidate, pawn)) continue;
        if (!is_ok(pawn)) continue;
        // 世界已经 BeginPlay 才写设置，避免读档期间写入被随后载入的存档覆盖；
        // 属性读不到时不拦截（不同引擎版本可能没有/不可读），只拦明确 false。
        if (UObject* scene = safe_get_world(pawn); is_ok(scene)) {
            double begun = 1.0;
            if (read_number(find_property(scene, "bBegunPlay"), scene, begun) && begun == 0.0) continue;
        }
        return pawn;
    }
    return nullptr;
}

bool Ue4ssPort::set_player_hidden(bool hidden) {
    UObject* pawn = player_pawn();
    if (!is_ok(pawn)) return false;
    static_cast<AActor*>(pawn)->SetActorHiddenInGame(hidden);
    return true;
}

namespace {

// UMG 的 UWidget::Visibility 是 TEnumAsByte<ESlateVisibility>：
// Visible=0 / Collapsed=1 / Hidden=2 / HitTestInvisible=3 / SelfHitTestInvisible=4。
// 必须走 UWidget::SetVisibility（而不是直接改属性），否则 Slate 控件不会立即刷新。
auto set_widget_visibility(UObject* widget, int visibility) -> bool {
    if (!is_ok(widget)) return false;
    if (UFunction* function = find_function(widget, "SetVisibility"); function != nullptr) {
        ParamBlock params(function);
        if (!params.valid()) return false;
        if (!params.set_number_any({"InVisibility"}, static_cast<double>(visibility))) {
            // 参数名不符时不按位置猜（M-7），但可以直接写语义相同的 Visibility 属性
            return write_number(find_property(widget, "Visibility"), widget, static_cast<double>(visibility));
        }
        widget->ProcessEvent(function, params.data());
        return true;
    }
    // 退路：个别版本把 SetVisibility 裁掉时，直接写属性（可能要到下一帧才刷新）
    return write_number(find_property(widget, "Visibility"), widget, static_cast<double>(visibility));
}

// get_widget_visibility / find_hud_layout_widgets 被诊断文件共用，已提到
// ue4ss_port_internal.h（namespace pbzpm::detail），此处不再保留副本。

auto pointer_in(const std::vector<UObject*>& list, UObject* target) -> bool {
    return std::find(list.begin(), list.end(), target) != list.end();
}

auto tracked_hud_widget(const std::vector<std::pair<UObject*, int>>& list, UObject* target) -> bool {
    for (const auto& entry : list) {
        if (entry.first == target) return true;
    }
    return false;
}

// 游戏自带的“为拍照/PV 隐藏 UI”总开关（UPBZUIManagerSubsystem::SetHideUIForPV）。
// 它会广播 OnHideUIForPVChanged，Boss 血条、教程提示、世界空间 HUD 等订阅者会一起隐藏；
// 这是最完整的隐藏路径，纯 UMG 根控件 Collapsed 只是它的补充。
auto set_ui_manager_hide_for_pv(bool hidden) -> bool {
    UObject* manager = RC::Unreal::UObjectGlobals::FindFirstOf(STR("PBZUIManagerSubsystem"));
    if (!is_ok(manager)) return false;
    UFunction* function = find_function(manager, "SetHideUIForPV");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_bool("bHidden", hidden)) {
        FProperty* property = params.prop({"bHidden"});
        if (property == nullptr || !write_bool(property, params.data(), hidden)) return false;
    }
    manager->ProcessEvent(function, params.data());
    return true;
}

}  // namespace

bool Ue4ssPort::set_hud_visible(bool visible) {
    bool touched = false;

    std::vector<UObject*> widgets;
    find_hud_layout_widgets(widgets);

    if (!visible) {
        // 先记录原始可见性，再调用游戏自己的 PV 开关——它内部可能先把控件改成 Collapsed。
        for (UObject* widget : widgets) {
            if (!is_ok(widget)) continue;
            if (!tracked_hud_widget(hud_widgets_, widget)) {
                int original = 0;
                if (!get_widget_visibility(widget, original)) original = 0;  // 默认 Visible
                hud_widgets_.emplace_back(widget, original);
            }
        }

        // 主路径：游戏 UIManager 的“为拍照/PV 隐藏 UI”总开关。
        const bool pv_ok = set_ui_manager_hide_for_pv(true);
        pv_hide_active_ = pv_ok;
        if (pv_ok) {
            log(LogLevel::Info, "UIManager.SetHideUIForPV(true) 已请求隐藏游戏 UI/HUD");
        }

        // 补充路径：HUD 布局根控件也设为 Collapsed，覆盖没有订阅 PV 事件的纯 UMG 控件。
        int hidden_count = 0;
        for (UObject* widget : widgets) {
            if (!is_ok(widget)) continue;
            if (set_widget_visibility(widget, 1)) {  // Collapsed
                touched = true;
                ++hidden_count;
            }
        }
        if (hidden_count > 0) {
            hud_widget_missing_logged_ = false;
            log(LogLevel::Info, "UMG HUD 已隐藏：控件数=" + std::to_string(hidden_count) +
                                    "（退出摄影模式时恢复原可见性）");
        } else if (widgets.empty() && !pv_ok) {
            if (!hud_widget_missing_logged_) {
                hud_widget_missing_logged_ = true;
                log(LogLevel::Warn, "找不到 UMG HUD 根控件（W_FrontEndHUDLayout_C），也未找到 UIManager，退回隐藏 AHUD");
            }
        } else if (!widgets.empty()) {
            hud_widget_missing_logged_ = false;
        }

        // 最后一道兜底：AHUD Actor（纯 UMG 的 HUD 对 SetActorHiddenInGame 不敏感）。
        if (widgets.empty() && !pv_ok) {
            UObject* pc = controller();
            if (is_ok(pc)) {
                UObject* hud = nullptr;
                if (read_object(find_property(pc, "MyHUD"), pc, hud) && is_ok(hud)) {
                    static_cast<AActor*>(hud)->SetActorHiddenInGame(true);
                    touched = true;
                }
            }
        }
        return pv_ok || touched;
    }

    // 恢复：先关掉游戏 UIManager 的 PV 隐藏开关，再按记录的原值恢复 UMG 控件。
    // 任一项失败都保留记账并返回 false，由 core 在后续 tick 退避重试（H-1）。
    bool ok = true;
    if (pv_hide_active_) {
        if (set_ui_manager_hide_for_pv(false)) {
            pv_hide_active_ = false;
            log(LogLevel::Info, "UIManager.SetHideUIForPV(false) 已恢复游戏 UI/HUD");
        } else {
            ok = false;  // 保留 pv_hide_active_，下次重试
        }
    }

    // 重新枚举一次做存活校验：关卡切换后旧控件可能已销毁，不能直接解引用旧指针。
    // 已随旧世界销毁的控件无需回写（世界换代时 invalidate 已清空缓存），只保留写失败的项。
    int restored_count = 0;
    std::vector<std::pair<UObject*, int>> pending;
    for (const auto& entry : hud_widgets_) {
        if (!pointer_in(widgets, entry.first)) continue;
        if (set_widget_visibility(entry.first, entry.second)) {
            ++restored_count;
        } else {
            pending.push_back(entry);
            ok = false;
        }
    }
    hud_widgets_.swap(pending);
    if (restored_count > 0) {
        log(LogLevel::Info, "UMG HUD 已恢复：控件数=" + std::to_string(restored_count));
    }

    // AHUD 兜底恢复（即使 PV 开关没找到，也把可能被隐藏的 AHUD 恢复）
    UObject* pc = controller();
    if (is_ok(pc)) {
        UObject* hud = nullptr;
        if (read_object(find_property(pc, "MyHUD"), pc, hud) && is_ok(hud)) {
            static_cast<AActor*>(hud)->SetActorHiddenInGame(false);
        }
    }
    return ok;
}

// ------------------------------------------------------------------ 交互提示（E 键图标）

namespace {

// 场景互动物（铃铛/拾取点/机关）旁的“E”提示是 PBZInteractComponent
// （游戏源码 System/Character/Components/PBZInteractComponent.cpp）挂的世界空间 UMG：
//   ① 控件组件实例：组件上的 InteractWidgetComponent 属性（UWidgetComponent）；
//   ② 游戏自带设置开关：bSceneInteractPromptVisible（SetSceneInteractPromptVisible/Get…）。
// SetHideUIForPV 不覆盖它，而且暂停（freeze_mode=pause）下游戏 UI 不再刷新，
// 已经画出来的提示必须把组件本身藏掉才会消失——所以两条路都用：
// 设置开关管住之后新出现的提示，控件组件管住此刻已经画出来的。

// 设置开关挂在哪个类离线确定不了（exe 里只有属性/函数名），运行时按候选探测：
// 第一个带 bSceneInteractPromptVisible 属性的实例就是它（跳过 CDO，优先活对象）。
auto find_scene_prompt_setting_owner() -> UObject* {
    static const char* candidates[] = {
        "PBZUIManagerSubsystem", "BP_GameInstance_C", "PBZGameUserSettings", "PBZCommonUserSettings",
        "PBZSettingsSubsystem", "PBZUserSettings", "PBZSettingsShared", "PBZSettings", "GameUserSettings",
    };
    UObject* cdo_fallback = nullptr;
    for (const char* name : candidates) {
        std::vector<UObject*> found;
        RC::Unreal::UObjectGlobals::FindAllOf(widen(name), found);
        for (UObject* object : found) {
            if (!is_ok(object)) continue;
            if (find_property(object, "bSceneInteractPromptVisible") == nullptr) continue;
            if (narrow(object->GetFullName()).find("Default__") == std::string::npos) return object;
            if (cdo_fallback == nullptr) cdo_fallback = object;
        }
    }
    return cdo_fallback;
}

// UWidgetComponent 继承 USceneComponent：SetVisibility(false) 后渲染器不再画它。
// UFunction 不可用时直写 bVisible 属性（暂停下没有“下一帧”，但恢复发生在退出后的活世界，没有影响）。
auto set_component_visibility(UObject* component, bool visible) -> bool {
    if (invoke(component, "SetVisibility",
               [&](ParamBlock& params) { return params.set_bool("bNewVisibility", visible); })) {
        return true;
    }
    return write_bool(find_property(component, "bVisible"), component, visible);
}

}  // namespace

bool Ue4ssPort::set_interact_prompts_visible(bool visible) {
    bool touched = false;
    bool ok = true;  // 恢复方向的成败；失败项保留记账由 core 重试（H-1）

    // ---- 路线 ①：游戏设置开关（管住之后新出现的提示）----
    if (!visible) {
        // 已保存过就直接用缓存的 owner：find_scene_prompt_setting_owner 要按 9 个候选类名
        // 逐个 FindAllOf（每个都是一遍全对象数组扫描），失败重试期间是纯浪费
        UObject* owner = prompt_setting_saved_ ? prompt_setting_owner_ : find_scene_prompt_setting_owner();
        if (is_ok(owner)) {
            bool original = true;
            std::string via = "默认(true)";
            read_bool_setting(owner, kPromptSettingSpec, original, &via);
            prompt_setting_saved_ = true;  // 只要动过开关，退出就必须还原（原值读不到按 true 还原）
            prompt_setting_original_ = original;
            prompt_setting_owner_ = owner;
            log(LogLevel::Info, "场景交互提示开关原值=" + std::string(original ? "开" : "关") + "（读取=" + via +
                                    "，owner=" + narrow(owner->GetFullName()).substr(0, 140) + "）");
            const bool wrote = write_bool_setting(owner, kPromptSettingSpec, false);
            bool readback = true;
            const bool verified = read_bool_setting(owner, kPromptSettingSpec, readback) && !readback;
            if (wrote) {
                touched = true;
                log(LogLevel::Info, std::string("场景交互提示开关已关闭（回读=") + (verified ? "关" : "开-未生效!") + "）");
            }
        } else if (!prompt_setting_missing_logged_) {
            prompt_setting_missing_logged_ = true;
            log(LogLevel::Warn, "候选对象里找不到场景交互提示开关（bSceneInteractPromptVisible）");
        }
    } else if (prompt_setting_saved_) {
        bool restored = false;
        bool verified = false;
        if (is_ok(prompt_setting_owner_)) {
            restored = write_bool_setting(prompt_setting_owner_, kPromptSettingSpec, prompt_setting_original_);
            bool readback = !prompt_setting_original_;
            verified = read_bool_setting(prompt_setting_owner_, kPromptSettingSpec, readback) &&
                       readback == prompt_setting_original_;
            if (restored) {
                log(LogLevel::Info, std::string("场景交互提示开关已还原（原值=") +
                                        (prompt_setting_original_ ? "开" : "关") + "，回读=" +
                                        (verified ? (readback ? "开" : "关") : "未生效!") + "）");
            } else {
                log(LogLevel::Warn, "场景交互提示开关还原失败（setter/pending/属性都写不进去）——保留记账，退出时重试");
            }
        } else {
            log(LogLevel::Warn, "场景交互提示开关的 owner 已失效（换世界/GC），保留记账等待重试");
        }
        if (restored) {
            prompt_setting_saved_ = false;
            prompt_setting_owner_ = nullptr;
            touched = true;
        } else {
            ok = false;  // 失败不清记账：后续 tick 还会再写一次
        }
    }

    // ---- 路线 ②：控件组件（管住此刻已经画出来的）----
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZInteractComponent"), components);

    if (!visible) {
        int hidden_count = 0;
        for (UObject* interact : components) {
            if (!is_ok(interact)) continue;
            UObject* widget_component = nullptr;
            for (const char* property_name : {"InteractWidgetComponent", "WidgetComponent", "InteractWidget"}) {
                if (read_object(find_property(interact, property_name), interact, widget_component) &&
                    is_ok(widget_component)) {
                    break;
                }
            }
            if (!is_ok(widget_component)) continue;
            bool tracked = false;
            for (const auto& entry : prompt_components_) {
                if (entry.first == widget_component) {
                    tracked = true;
                    break;
                }
            }
            if (tracked) continue;
            double original_visible = 1.0;
            if (!read_number(find_property(widget_component, "bVisible"), widget_component, original_visible)) {
                original_visible = 1.0;
            }
            if (set_component_visibility(widget_component, false)) {
                prompt_components_.emplace_back(widget_component, original_visible != 0.0);
                ++hidden_count;
            }
        }
        if (hidden_count > 0) {
            touched = true;
            log(LogLevel::Info, "已隐藏交互提示控件：数量=" + std::to_string(hidden_count) +
                                    "（退出摄影模式时恢复原可见性）");
        } else if (components.empty() && !touched && !prompt_component_missing_logged_) {
            prompt_component_missing_logged_ = true;
            log(LogLevel::Warn, "场景里没有 PBZInteractComponent（交互提示若仍在屏上，把这条日志发回来）");
        }
    } else {
        // 重新枚举做存活校验：换图/GC 后旧组件可能已销毁，不能直接解引用旧指针
        int restored_count = 0;
        std::vector<std::pair<UObject*, bool>> pending;
        for (const auto& entry : prompt_components_) {
            if (!pointer_in(components, entry.first)) continue;
            if (set_component_visibility(entry.first, entry.second)) {
                ++restored_count;
            } else {
                pending.push_back(entry);  // 写失败保留，下次重试
                ok = false;
            }
        }
        prompt_components_.swap(pending);
        if (restored_count > 0) {
            touched = true;
            log(LogLevel::Info, "已恢复交互提示控件：数量=" + std::to_string(restored_count));
        }
    }

    // 恢复方向：没有待还原项即成功（世界换代已清空缓存时不能报失败）
    return visible ? ok : touched;
}

// ------------------------------------------------------------------ 敌人（怪物）血条

namespace {

// 头顶血条不在 W_FrontEndHUDLayout 子树下（1.0.12 的 dump 证据只覆盖屏幕 HUD），
// SetHideUIForPV 也管不到它们——实机截图确认进摄影模式后小怪血条仍在。
// exe 反射字符串扫描（1.6.0 同款取证）找到游戏侧唯一的总开关：用户设置
// MonsterHealthBarMode（EPBZMonsterHealthBarMode：HideFullHealth/AlwaysShow/OnlyLockedTarget，
// 带 Set/Get/SetPending/GetPending/OnChanged 一族，挂在游戏设置类上）。
// 三档里 OnlyLockedTarget 压制最强：摄影模式下没有锁定目标 → 血条全部隐藏。

// 枚举序数离线定不了（exe 字符串只有名字没有值）：运行时按名找 UEnum 对象。
// 模块包名先按源码路径里的模块名猜，猜不中再全量扫 UEnum 按对象名过滤。
auto find_bar_mode_enum() -> UObject* {
    for (const char* path :
         {"/Script/ProjectPBZ.EPBZMonsterHealthBarMode", "/Script/PBZ.EPBZMonsterHealthBarMode"}) {
        if (UObject* enum_object = find_object(path); is_ok(enum_object)) return enum_object;
    }
    std::vector<UObject*> enums;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("UEnum"), enums);
    for (UObject* enum_object : enums) {
        if (!is_ok(enum_object)) continue;
        if (narrow(enum_object->GetName()) == "EPBZMonsterHealthBarMode") return enum_object;
    }
    return nullptr;
}

// 名字表 → 序数：条目名可能是全名（EPBZMonsterHealthBarMode::OnlyLockedTarget）或裸名
auto resolve_bar_mode_value(UObject* enum_object, const char* entry, int64_t& out_value) -> bool {
    if (!is_ok(enum_object)) return false;
    std::vector<std::pair<RC::Unreal::FName, int64_t>> names;
    static_cast<RC::Unreal::UEnum*>(enum_object)->GetEnumNamesAsVector(names);
    const std::string suffix = std::string("::") + entry;
    for (const auto& item : names) {
        const std::string name = narrow(item.first.ToString());
        if (name == entry ||
            (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            out_value = item.second;
            return true;
        }
    }
    return false;
}

// 序数 → 名字（诊断日志用；查不到就打数字）
auto bar_mode_display_name(UObject* enum_object, double value) -> std::string {
    if (is_ok(enum_object)) {
        std::vector<std::pair<RC::Unreal::FName, int64_t>> names;
        static_cast<RC::Unreal::UEnum*>(enum_object)->GetEnumNamesAsVector(names);
        for (const auto& item : names) {
            if (static_cast<double>(item.second) == value) {
                std::string name = narrow(item.first.ToString());
                const size_t sep = name.rfind("::");
                if (sep != std::string::npos) name = name.substr(sep + 2);
                return name;
            }
        }
    }
    return std::to_string(static_cast<long long>(value));
}

// 设置项挂在哪个类离线确定不了，运行时按候选探测（1.6.0 交互提示开关同款）：
// 第一个带 MonsterHealthBarMode 属性的实例就是它（跳过 CDO，优先活对象）。
auto find_bar_mode_setting_owner() -> UObject* {
    static const char* candidates[] = {
        "PBZSettingsShared", "PBZGameUserSettings", "PBZCommonUserSettings", "PBZSettingsSubsystem",
        "PBZUserSettings", "PBZSettings", "PBZUIManagerSubsystem", "BP_GameInstance_C", "GameUserSettings",
    };
    UObject* cdo_fallback = nullptr;
    for (const char* name : candidates) {
        std::vector<UObject*> found;
        RC::Unreal::UObjectGlobals::FindAllOf(widen(name), found);
        for (UObject* object : found) {
            if (!is_ok(object)) continue;
            if (find_property(object, "MonsterHealthBarMode") == nullptr) continue;
            if (narrow(object->GetFullName()).find("Default__") == std::string::npos) return object;
            if (cdo_fallback == nullptr) cdo_fallback = object;
        }
    }
    return cdo_fallback;
}

// fill_bar_mode_param 被世界换代清理共用，已提到 ue4ss_port_internal.h（namespace pbzpm::detail）。

}  // namespace

bool Ue4ssPort::set_enemy_health_bars_visible(bool visible) {
    bool touched = false;

    if (!visible) {
        // 同交互提示：设置对象找到过就不再按 9 个候选类名全对象扫描
        UObject* owner = bars_setting_saved_ ? bars_setting_owner_ : find_bar_mode_setting_owner();
        if (!is_ok(owner)) {
            if (!bars_setting_missing_logged_) {
                bars_setting_missing_logged_ = true;
                log(LogLevel::Warn, "候选对象里找不到怪物血条模式设置（MonsterHealthBarMode），敌人血条保持可见（把这条日志发回来）");
            }
            return false;
        }

        FProperty* property = find_property(owner, "MonsterHealthBarMode");
        UObject* enum_object = find_bar_mode_enum();
        int64_t target_value = 0;
        if (!resolve_bar_mode_value(enum_object, "OnlyLockedTarget", target_value)) {
            if (!bars_setting_missing_logged_) {
                bars_setting_missing_logged_ = true;
                log(LogLevel::Warn, "EPBZMonsterHealthBarMode 枚举表里没有 OnlyLockedTarget，敌人血条保持可见（把这条日志发回来）");
            }
            return false;
        }

        double original = 0.0;
        if (read_number(property, owner, original)) {
            bars_setting_saved_ = true;
            bars_setting_original_ = original;
            bars_setting_owner_ = owner;
        }

        bool set_ok = invoke(owner, "SetMonsterHealthBarMode",
                             [&](ParamBlock& params) { return fill_bar_mode_param(params, static_cast<double>(target_value)); });
        // setter 参数名对不上时直写属性（值本身一定在）；广播缺失只影响实时性，
        // 血条系统若每帧读设置也能生效。
        if (!set_ok) set_ok = write_number(property, owner, static_cast<double>(target_value));
        if (set_ok) {
            touched = true;
            log(LogLevel::Info, "敌人血条显示模式已切到 OnlyLockedTarget（原模式=" +
                                    bar_mode_display_name(enum_object, original) +
                                    "，owner=" + narrow(owner->GetFullName()).substr(0, 140) + "）");
        }
        return touched;
    }

    if (bars_setting_saved_) {
        bool restored = false;
        if (is_ok(bars_setting_owner_)) {
            restored = invoke(bars_setting_owner_, "SetMonsterHealthBarMode",
                              [&](ParamBlock& params) { return fill_bar_mode_param(params, bars_setting_original_); });
            if (!restored) {
                restored = write_number(find_property(bars_setting_owner_, "MonsterHealthBarMode"),
                                        bars_setting_owner_, bars_setting_original_);
            }
        }
        if (restored) {
            bars_setting_saved_ = false;
            bars_setting_owner_ = nullptr;
            touched = true;
            log(LogLevel::Info, "敌人血条显示模式已还原");
        } else {
            // 失败不清记账：退出后的退避重试还会再写一次（H-1）
            log(LogLevel::Warn, "敌人血条显示模式还原失败，保留记账等待重试");
            return false;
        }
    }
    // 没有待还原项（世界换代已清空记账）时视为成功，恢复是幂等的
    return true;
}

// ------------------------------------------------------------------ 对话字幕

namespace {

// 字幕显示走游戏内置的 GameSubtitles 插件（exe 源码路径
// `Plugins/GameSubtitles/Source/Private/Widgets/SubtitleDisplay.cpp`，包名 /Script/GameSubtitles）：
//   ① 游戏设置开关 bSubtitlesEnabled（SetSubtitlesEnabled/AreSubtitlesEnabled + Pending 对，
//      名字表邻居是 RunHardwareBenchmark/MouseCaptureMode 等 GameUserSettings 家族函数）；
//   ② 字幕显示控件 USubtitleDisplay（UMG）——freeze_mode=pause 下控件 Tick 冻住，
//      光改设置对已经画出来的字幕不会立即生效，必须把控件本身 Collapsed。
// SetSubtitlesEnabled 是设置总开关，只影响之后的新字幕；两路都用，退出都还原。

// 类继承链里是否有指定名字的类（覆盖原生实例与 BP 子类两种情况）。
// 用 FName 池索引比较（O(1)、零分配）：全量扫描对每个对象走两遍继承链，
// 之前逐节点 GetName() 转 FString，一次入场扫描就是几十万次小分配（实机 ~270ms 卡顿）
auto class_chain_contains(UClass* cls, const char* base_name) -> bool {
    if (cls == nullptr) return false;
    static std::map<std::string, RC::Unreal::FName> fname_cache;
    const RC::Unreal::FName& target =
        fname_cache.emplace(base_name, RC::Unreal::FName(widen(base_name))).first->second;
    for (UStruct* node = cls; node != nullptr; node = node->GetSuperStruct()) {
        if (node->GetNamePrivate().Equals(target)) return true;
    }
    return false;
}



// 设置开关挂在哪个类离线确定不了（exe 里只有函数名），运行时按候选探测：
// 第一个带 SetSubtitlesEnabled 函数或 bSubtitlesEnabled 属性的实例就是它（跳过 CDO，优先活对象）。
// 1.0.30 实机：PBZSettingsShared 命中（只有 setter，属性在引擎 GlobalUserParam 上）。
auto find_subtitle_setting_owner() -> UObject* {
    static const char* candidates[] = {
        "PBZGameUserSettings", "PBZSettingsShared", "PBZCommonUserSettings", "PBZSettingsSubsystem",
        "PBZUserSettings", "PBZSettings", "GameUserSettings", "PBZUIManagerSubsystem", "BP_GameInstance_C",
    };
    UObject* cdo_fallback = nullptr;
    for (const char* name : candidates) {
        std::vector<UObject*> found;
        RC::Unreal::UObjectGlobals::FindAllOf(widen(name), found);
        for (UObject* object : found) {
            if (!is_ok(object)) continue;
            if (find_function(object, "SetSubtitlesEnabled") == nullptr &&
                find_property(object, "bSubtitlesEnabled") == nullptr) {
                continue;
            }
            if (narrow(object->GetFullName()).find("Default__") == std::string::npos) return object;
            if (cdo_fallback == nullptr) cdo_fallback = object;
        }
    }
    // 设置对象真不在时才退到引擎实例：bSubtitlesEnabled 是 UEngine 的 GlobalUserParam，
    // 字幕显示控件读的就是它（bSubtitlesForcedOff 是它的强制关闭位）。
    // 注意：必须在候选都没命中时才走这里，不能覆盖上面已经找到的设置对象。
    if (cdo_fallback == nullptr) {
        for (const char* name : {"GameEngine", "PBZGameEngine", "Engine"}) {
            UObject* engine = RC::Unreal::UObjectGlobals::FindFirstOf(widen(name));
            if (is_ok(engine) && find_property(engine, "bSubtitlesEnabled") != nullptr) return engine;
        }
    }
    return cdo_fallback;
}

}  // namespace

// 字幕控件全量扫描的类级判定：继承链 + 类名匹配只依赖类，按类缓存一次算完。
// 对象几十万、类 ~1 万，此前逐对象 narrow+lower 是进入/退出卡顿的另一大来源
struct SubtitleClassVerdict {
    bool matched{false};
    // 对话/过场 UI（WBP_FlowDialog_C）：文本画在它身上，且必须保持 Tick 才能推进对话；
    // 隐藏时用 ESlateVisibility=2（Hidden）而不是 1（Collapsed）。
    bool flow_dialog{false};
    std::string class_name;
};
static auto subtitle_class_verdict(UClass* cls) -> const SubtitleClassVerdict& {
    static std::unordered_map<UClass*, SubtitleClassVerdict> cache;
    auto it = cache.find(cls);
    if (it == cache.end()) {
        SubtitleClassVerdict verdict;
        const bool is_subtitle_display = class_chain_contains(cls, "SubtitleDisplay");
        const bool is_user_widget = class_chain_contains(cls, "UserWidget");
        if (is_subtitle_display || is_user_widget) {
            verdict.class_name = narrow(cls->GetName());
            const std::string lower_name = lower_ascii(verdict.class_name);
            verdict.flow_dialog = lower_name.find("flowdialog") != std::string::npos;
            verdict.matched =
                is_subtitle_display ||
                (is_user_widget && (lower_name.find("subtitle") != std::string::npos || verdict.flow_dialog));
        }
        it = cache.emplace(cls, std::move(verdict)).first;
    }
    return it->second;
}

// 字幕显示控件收集。full_scan：全对象扫描（进入/退出各一次；顺带把发现的类名记进缓存，
// 1.0.32 实机发现游戏自制控件 WBP_FlowSubTitle_C）；否则只按缓存类名 FindAllOf
//（每秒重申用，类哈希查找微秒级，不拖帧）。匹配规则：USubtitleDisplay 继承链，
// 或 UserWidget 派生且类名含 subtitle（游戏自制字幕控件）。
void Ue4ssPort::collect_subtitle_widgets_impl(std::vector<UObject*>& out, bool full_scan) {
    if (!full_scan) {
        // 单次全数组遍历匹配全部缓存类名：原来是每个类名一次 FindAllOf（各扫一遍对象数组），
        // 类名一多每秒就是 N 次全扫描拖帧；合并成一遍，FName 比较与 FindAllOf 的匹配语义一致
        if (subtitle_widget_class_names_.empty()) return;
        std::vector<RC::Unreal::FName> targets;
        targets.reserve(subtitle_widget_class_names_.size());
        for (const std::string& name : subtitle_widget_class_names_) {
            targets.push_back(RC::Unreal::FName(widen(name)));
        }
        RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
            if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
            UClass* cls = object->GetClassPrivate();
            if (cls == nullptr) return RC::LoopAction::Continue;
            const RC::Unreal::FName class_name = cls->GetNamePrivate();
            bool matched = false;
            for (const RC::Unreal::FName& target : targets) {
                if (class_name.Equals(target)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) return RC::LoopAction::Continue;
            if (narrow(object->GetFullName()).find("Default__") != std::string::npos) return RC::LoopAction::Continue;
            out.push_back(object);
            return RC::LoopAction::Continue;
        });
        return;
    }
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        // 1.0.35 实机：bEnableSubtitles 回读=关 且 WBP_FlowSubTitle_C 已 Collapsed，字幕仍显示——
        // 文本画在对话 UI 本体上（音频普查见 WBP_FlowDialog_C），flowdialog 一并纳入压制。
        // 判定细节在 subtitle_class_verdict 里按类缓存
        const SubtitleClassVerdict& verdict = subtitle_class_verdict(cls);
        if (!verdict.matched) return RC::LoopAction::Continue;
        if (narrow(object->GetName()).rfind("Default__", 0) == 0) return RC::LoopAction::Continue;
        if (std::find(subtitle_widget_class_names_.begin(), subtitle_widget_class_names_.end(),
                      verdict.class_name) == subtitle_widget_class_names_.end()) {
            subtitle_widget_class_names_.push_back(verdict.class_name);
        }
        out.push_back(object);
        return RC::LoopAction::Continue;
    });
}

// 诊断（每会话一次）：SetSubtitlesEnabled 的真实参数签名 + owner 上所有字幕相关成员。
// 1.0.32 实机证明设置开关的行为与预期不符（写 false 对运行中的对话不生效），把它的真面目
// 记下来，下一步才能换成正确的读回/写入通道。
void Ue4ssPort::dump_subtitle_setting_diagnostics(RC::Unreal::UObject* owner) {
    if (subtitle_diag_dumped_ || !is_ok(owner)) return;
    subtitle_diag_dumped_ = true;
    UFunction* setter = find_function(owner, "SetSubtitlesEnabled");
    if (setter != nullptr) {
        std::string params;
        for (TFieldIterator<FProperty> it(setter); it; ++it) {
            FProperty* p = *it;
            if (p == nullptr) continue;
            const bool bool_like = p->IsA<FBoolProperty>() || p->IsA<FEnumProperty>() || p->IsA<FByteProperty>();
            params += (params.empty() ? "" : ", ") + narrow(p->GetName()) + (bool_like ? "(bool)" : "(非bool)");
        }
        log(LogLevel::Info, "SetSubtitlesEnabled 参数签名：" + (params.empty() ? std::string("（无参数）") : params));
    }
    std::string members;
    for (TFieldIterator<FProperty> it(owner->GetClassPrivate()); it; ++it) {
        FProperty* p = *it;
        if (p == nullptr) continue;
        const std::string name = narrow(p->GetName());
        if (lower_ascii(name).find("subtitle") == std::string::npos) continue;
        members += (members.empty() ? "" : ", ") + name;
    }
    for (TFieldIterator<UFunction> it(owner->GetClassPrivate()); it; ++it) {
        UFunction* f = *it;
        if (f == nullptr) continue;
        const std::string name = narrow(f->GetName());
        if (lower_ascii(name).find("subtitle") == std::string::npos) continue;
        members += (members.empty() ? "" : ", ") + name + "()";
    }
    log(LogLevel::Info, "owner 字幕相关成员：" + (members.empty() ? std::string("（无）") : members));
}

// 诊断（每会话一次，压不住字幕时给下一轮指认元凶）：列出场上所有「可见的」UserWidget
// 派生控件类（Visibility≠Collapsed，含数量）。字幕文本若不在已压制的控件里，真身必然在
// 这份名单里。
void Ue4ssPort::dump_visible_widgets_once() {
    if (subtitle_visible_dumped_) return;
    subtitle_visible_dumped_ = true;
    std::map<std::string, int> visible;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        if (!class_chain_contains(cls, "UserWidget")) return RC::LoopAction::Continue;
        if (narrow(object->GetFullName()).find("Default__") != std::string::npos) return RC::LoopAction::Continue;
        int visibility = 1;
        if (!get_widget_visibility(object, visibility)) return RC::LoopAction::Continue;
        if (visibility == 1) return RC::LoopAction::Continue;  // Collapsed = 不可见
        ++visible[narrow(cls->GetName())];
        return RC::LoopAction::Continue;
    });
    std::string names;
    int listed = 0;
    for (const auto& entry : visible) {
        if (listed++ >= 40) { names += ", …"; break; }
        names += (names.empty() ? "" : ", ") + entry.first + " x" + std::to_string(entry.second);
    }
    log(LogLevel::Info, "可见 UserWidget 清单：" + (names.empty() ? std::string("（无）") : names));
}

bool Ue4ssPort::set_subtitles_visible(bool visible) {
    bool touched = false;
    bool ok = true;  // 恢复方向：失败项保留记账，由 core 重试（H-1）

    // ---- 路线 ②前置：先收集本轮字幕控件 ----
    // 必须先收集再决定是否写设置开关：本轮若发现 FlowDialog 对话 UI，说明正处于对话/过场中，
    // 关闭 bEnableSubtitles 会打断当前语句（对话/语音控制器读的是这个开关），只能隐藏控件。
    // 扫描收敛（1.0.39）：收集控件要遍历整个对象数组，大场景一次几十毫秒，1.0.38 实机
    // 摄影模式里每秒一次、转视角时肉眼可见。世界硬暂停时控件 Tick 冻住，游戏根本不会
    // 重新显示控件——暂停期完全跳过扫描；未暂停（dilation 冻结/暂停被剧情解除）才按秒
    // 收编，另留 30 秒一次的例行收编兜底新对话 UI。
    bool full_scan = visible ? true : false;
    bool discovery = true;
    if (!visible) {
        ++subtitle_reassert_count_;
        const bool paused = world_paused();
        full_scan = !subtitle_scanned_ || (!paused && subtitle_reassert_count_ % 30 == 0);
        discovery = full_scan || (!paused && subtitle_widgets_.empty()) || !paused;
    }
    std::vector<UObject*> widgets;
    if (discovery) {
        collect_subtitle_widgets_impl(widgets, full_scan);
        if (!visible && full_scan) subtitle_scanned_ = true;
        if (!visible) dump_visible_widgets_once();
    }

    bool flow_dialog_present = false;
    for (UObject* widget : widgets) {
        if (!is_ok(widget)) continue;
        UClass* cls = widget->GetClassPrivate();
        if (cls != nullptr && subtitle_class_verdict(cls).flow_dialog) {
            flow_dialog_present = true;
            break;
        }
    }

    // 暂停期跳过了收集：控件 Tick 冻住、游戏不会重新显示控件，本轮无事可做。
    // （1.0.39 实机：空 widgets 落到下面“没找到控件”告警，白跑一次候选全扫描）
    if (!visible && !discovery) return touched;

    // ---- 路线 ①：游戏设置开关（管住之后新出的字幕）----
    // 1.0.33 首版的结构 bug（实机日志钉死）：!visible && !touched_ / else if touched_ 的写法，
    // 让每秒重申（visible=false 且 touched_=true）掉进还原分支 → 设置开关每秒 关/开 打摆。
    // 必须把"隐藏"与"还原"按 visible 彻底分开；已在关状态时重申只跳过，不碰开关。
    if (!visible) {
        if (!subtitle_setting_touched_) {
            if (flow_dialog_present) {
                // 对话/过场中：只藏控件，保留 bEnableSubtitles 原值，避免打断正在播放的语句。
                if (!subtitle_flow_dialog_logged_) {
                    subtitle_flow_dialog_logged_ = true;
                    log(LogLevel::Info,
                        "检测到对话 UI（FlowDialog），保留字幕设置开关不写（避免打断当前语句）；仅隐藏控件");
                }
            } else {
                UObject* owner = find_subtitle_setting_owner();
                if (is_ok(owner)) {
                    dump_subtitle_setting_diagnostics(owner);
                    bool original = true;
                    std::string via = "默认(true)";
                    read_bool_setting(owner, kSubtitleSettingSpec, original, &via);
                    subtitle_setting_saved_ = true;  // 只要动过开关，退出就必须还原（原值读不到按 true 还原）
                    subtitle_setting_touched_ = true;
                    subtitle_setting_original_ = original;
                    subtitle_setting_owner_ = owner;
                    log(LogLevel::Info, "字幕设置开关原值=" + std::string(original ? "开" : "关") + "（读取=" + via +
                                            "，owner=" + narrow(owner->GetFullName()).substr(0, 140) + "）");
                    const bool wrote = write_bool_setting(owner, kSubtitleSettingSpec, false);
                    bool readback = true;
                    const bool verified = read_bool_setting(owner, kSubtitleSettingSpec, readback) && !readback;
                    if (wrote) {
                        touched = true;
                        log(LogLevel::Info, std::string("字幕设置开关已关闭（回读=") + (verified ? "关" : "开-未生效!") + "）");
                    }
                } else if (!subtitle_setting_missing_logged_) {
                    subtitle_setting_missing_logged_ = true;
                    log(LogLevel::Warn, "候选对象里找不到字幕设置开关（SetSubtitlesEnabled/bEnableSubtitles），退回只藏字幕控件");
                }
            }
        }
    } else if (subtitle_setting_touched_) {
        bool restored = false;
        bool verified = false;
        bool readback = !subtitle_setting_original_;
        if (is_ok(subtitle_setting_owner_)) {
            restored = write_bool_setting(subtitle_setting_owner_, kSubtitleSettingSpec, subtitle_setting_original_);
            verified = read_bool_setting(subtitle_setting_owner_, kSubtitleSettingSpec, readback) &&
                       readback == subtitle_setting_original_;
            if (restored) {
                log(LogLevel::Info, std::string("字幕设置开关已还原（原值=") + (subtitle_setting_original_ ? "开" : "关") +
                                        "，回读=" + (verified ? (readback ? "开" : "关") : "未生效!") + "）");
            } else if (!subtitle_restore_failed_logged_) {
                subtitle_restore_failed_logged_ = true;
                log(LogLevel::Warn, "字幕设置开关还原失败（setter/pending/属性都写不进去），字幕可能保持隐藏——保留记账，退出时重试");
            }
        } else {
            log(LogLevel::Warn, "字幕设置开关的 owner 已失效（换世界/GC），保留记账等待重试");
        }
        if (restored) {
            subtitle_setting_touched_ = false;
            subtitle_setting_saved_ = false;
            subtitle_setting_owner_ = nullptr;
            touched = true;
        } else {
            ok = false;  // 失败不清记账
        }
    }

    // ---- 路线 ②：字幕显示控件（管住此刻已经画出来的）----
    // 进入后重申：对话 UI 创建时才读设置，运行中的对话由游戏逐条把控件重新显示，
    // 一次性的隐藏会被盖回去（1.0.32 实机：进模式后字幕不消失）。重申是幂等的：
    // 已管的控件若被游戏改回可见就再压回去，新实例收编，设置开关不重复写。
    if (!visible) {
        int hidden_count = 0;
        int reasserted_count = 0;
        int flow_dialog_count = 0;
        std::string hidden_names;
        for (UObject* widget : widgets) {
            if (!is_ok(widget)) continue;
            UClass* cls = widget->GetClassPrivate();
            const bool is_flow_dialog = cls != nullptr && subtitle_class_verdict(cls).flow_dialog;
            // FlowDialog 默认用 Hidden(2)：控件仍参与 Tick；对话 Flow 冻结时改用 Collapsed(1) 停 Tick。
            // 其他字幕控件始终用 Collapsed(1)：彻底不画，同时停止 Tick。
            const int target_visibility = is_flow_dialog ? (flow_dialog_tick_paused_ ? 1 : 2) : 1;
            if (tracked_hud_widget(subtitle_widgets_, widget)) {
                // 已在管：游戏每条字幕可能把控件重新 SetVisibility(Visible)，重申目标可见性。
                // 不重复记录原值——原值在首次收编时已存，退出时按它恢复。
                int current = 1;
                if (get_widget_visibility(widget, current) && current != target_visibility) {
                    if (set_widget_visibility(widget, target_visibility)) ++reasserted_count;
                }
                continue;
            }
            int original = 0;
            if (!get_widget_visibility(widget, original)) original = 0;  // 默认 Visible
            if (set_widget_visibility(widget, target_visibility)) {
                subtitle_widgets_.emplace_back(widget, original);
                ++hidden_count;
                if (is_flow_dialog) ++flow_dialog_count;
                if (is_ok(widget)) {
                    hidden_names += (hidden_names.empty() ? "" : ", ") + narrow(widget->GetClassPrivate()->GetName());
                }
            }
        }
        if (hidden_count > 0) {
            touched = true;
            const std::string flow_note =
                flow_dialog_count > 0
                    ? std::string("，FlowDialog=") + std::to_string(flow_dialog_count) +
                          (flow_dialog_tick_paused_ ? " 个用 Collapsed 停 Tick（对话 Flow 冻结）" : " 个用 Hidden 保 Tick")
                    : std::string();
            log(LogLevel::Info, "已隐藏字幕显示控件：数量=" + std::to_string(hidden_count) + "（" + hidden_names + "）" +
                                    flow_note + "，退出摄影模式时恢复原可见性");
        } else if (reasserted_count > 0) {
            touched = true;  // 每秒重申压制，保持返回值语义
        } else if (widgets.empty() && !subtitle_widget_missing_logged_) {
            subtitle_widget_missing_logged_ = true;
            // 诊断：列出场内类名带 Subtitle 的控件类，游戏若用自制字幕控件，按这份名单改下一版
            std::map<std::string, int> candidates;
            RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
                if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
                UClass* cls = object->GetClassPrivate();
                if (cls == nullptr) return RC::LoopAction::Continue;
                std::string class_name = narrow(cls->GetName());
                std::string lower = lower_ascii(class_name);
                if (lower.find("subtitle") == std::string::npos) return RC::LoopAction::Continue;
                candidates[class_name] += 1;
                return RC::LoopAction::Continue;
            });
            std::string names;
            for (const auto& entry : candidates) {
                names += (names.empty() ? "" : ", ") + entry.first + " x" + std::to_string(entry.second);
            }
            log(LogLevel::Warn, "场上没有 USubtitleDisplay 控件" + (names.empty() ? std::string("，也没找到类名带 Subtitle 的控件") :
                                                                         std::string("，类名带 Subtitle 的控件：") + names));
        }
    } else {
        // 重新枚举做存活校验：换图/GC 后旧控件可能已销毁，不能直接解引用旧指针
        int restored_count = 0;
        std::vector<std::pair<UObject*, int>> pending;
        for (const auto& entry : subtitle_widgets_) {
            if (!pointer_in(widgets, entry.first)) continue;
            if (set_widget_visibility(entry.first, entry.second)) {
                ++restored_count;
            } else {
                pending.push_back(entry);  // 写失败保留，下次重试
                ok = false;
            }
        }
        subtitle_widgets_.swap(pending);
        if (restored_count > 0) {
            touched = true;
            log(LogLevel::Info, "已恢复字幕显示控件：数量=" + std::to_string(restored_count));
        }
        // 仍有未恢复的控件时不要重置扫描状态，下次重试还要用这份清单
        if (subtitle_widgets_.empty()) {
            subtitle_scanned_ = false;  // 下次进入重新做一次全量扫描（新对话控件类可能不同）
            subtitle_flow_dialog_logged_ = false;
            flow_dialog_tick_paused_ = false;  // 退出/恢复后回到默认 Hidden(2) 语义
        }
    }

    return visible ? ok : touched;
}

// 对话 Flow Tick 冻结（1.0.74 方案 A）：不挂线程、不动 Wwise，只把已收编的
// WBP_FlowDialog_C 控件在 Collapsed(1) 与 Hidden(2) 之间切换——Collapsed 会停止 Slate Tick，
// 用于冻结对话 Flow 的推进；Hidden 保留 Tick，恢复后对话照常继续。
int Ue4ssPort::set_dialogue_flow_tick_paused(bool paused) {
    if (subtitle_widgets_.empty()) {
        flow_dialog_tick_paused_ = false;  // 本会话未收编任何字幕控件：不留下“已冻结”的脏标记
        return 0;
    }

    const int target_visibility = paused ? 1 : 2;  // Collapsed 停 Tick / Hidden 保 Tick
    int acted = 0;
    bool has_flow_widget = false;
    for (const auto& entry : subtitle_widgets_) {
        UObject* widget = entry.first;
        if (!is_ok(widget)) continue;
        UClass* cls = widget->GetClassPrivate();
        if (cls == nullptr || !subtitle_class_verdict(cls).flow_dialog) continue;
        has_flow_widget = true;
        int current = -1;
        if (get_widget_visibility(widget, current) && current == target_visibility) continue;  // 已是目标值
        if (set_widget_visibility(widget, target_visibility)) ++acted;
    }
    // 没有 FlowDialog 时不能把 flow_dialog_tick_paused_ 留成 true，否则后续 set_subtitles_visible
    // 的每秒重申会把新出现的对话控件误判成“冻结中”。有 FlowDialog 才记录状态。
    flow_dialog_tick_paused_ = paused && has_flow_widget;
    // 日志由 core 的 PhotoMode 统一输出（避免同一条「对话 Flow 冻结/解冻」打两遍）
    return acted;
}

// ------------------------------------------------------------------ 旧版卡掉的 UI 设置一次性修复（1.0.69）

// 1.0.30 时代起，字幕/场景交互提示开关就曾被旧版写坏过（还原静默失败 + 游戏存盘，
// 玩家只能进游戏设置手动开回来）。1.0.69 首次运行做一次修复：把两个开关强制打开、
// 回读校验、立即存盘，成功后写 ui_settings_repair_done.flag 不再执行。
// 只做一次；若你本来就关闭字幕，把 config.ini 的 repair_ui_settings_once 设为 false。
bool Ue4ssPort::recover_ui_settings_once() {
    const std::filesystem::path marker = mod_dir_ / "ui_settings_repair_done.flag";
    std::error_code ec;
    if (std::filesystem::exists(marker, ec)) return true;  // 已修复过（或用户关掉了自动修复后再运行）

    // 等真正的游戏世界就绪再做：设置对象可能比存档加载先存在，过早写会被随后载入的存档覆盖。
    // 注意不要用 controller()/player_pawn()：读档/换图时 controller_cache_ 可能指向已销毁的旧 PC
    // （1.0.69 crash_2026_11_02_15_36_45 即此），这里每次重新枚举并要求世界已 BeginPlay。
    if (find_live_player_pawn() == nullptr) {
        if (!ui_repair_wait_logged_) {
            ui_repair_wait_logged_ = true;
            log(LogLevel::Info, "UI 设置修复：等待玩家进入游戏世界（Player/Pawn/bBegunPlay）后执行");
        }
        return false;
    }
    UObject* subtitle_owner = find_subtitle_setting_owner();
    UObject* prompt_owner = find_scene_prompt_setting_owner();
    if (!is_ok(subtitle_owner) || !is_ok(prompt_owner)) {
        if (!ui_repair_missing_logged_) {
            ui_repair_missing_logged_ = true;
            log(LogLevel::Info, "UI 设置修复：设置对象暂未就绪，稍后重试");
        }
        return false;
    }

    bool subtitle_before = true;
    bool prompt_before = true;
    std::string subtitle_via = "默认";
    std::string prompt_via = "默认";
    read_bool_setting(subtitle_owner, kSubtitleSettingSpec, subtitle_before, &subtitle_via);
    read_bool_setting(prompt_owner, kPromptSettingSpec, prompt_before, &prompt_via);
    log(LogLevel::Info, std::string("UI 设置修复：字幕开关=") + (subtitle_before ? "开" : "关") +
                            "（读取=" + subtitle_via + "），场景交互提示=" + (prompt_before ? "开" : "关") +
                            "（读取=" + prompt_via + "）");

    write_bool_setting(subtitle_owner, kSubtitleSettingSpec, true);
    write_bool_setting(prompt_owner, kPromptSettingSpec, true);
    bool subtitle_after = false;
    bool prompt_after = false;
    const bool subtitle_ok = read_bool_setting(subtitle_owner, kSubtitleSettingSpec, subtitle_after) && subtitle_after;
    const bool prompt_ok = read_bool_setting(prompt_owner, kPromptSettingSpec, prompt_after) && prompt_after;
    if (!subtitle_ok || !prompt_ok) {
        log(LogLevel::Warn, std::string("UI 设置修复未完全生效（字幕回读=") + (subtitle_ok ? "开" : "关") +
                                "，场景交互提示回读=" + (prompt_ok ? "开" : "关") + "），引擎 Tick 稍后重试");
        return false;
    }

    // 立即存盘：设置类自带 SaveSettings（exe 确认），否则修复值只在内存、游戏退出可能被旧档覆盖
    if (find_function(subtitle_owner, "SaveSettings") != nullptr) {
        invoke(subtitle_owner, "SaveSettings", [](ParamBlock&) { return true; });
    }

    std::ofstream marker_file(marker);
    marker_file << "repaired by " << PBZPM_VERSION << " (subtitles=on, scene_interact_prompt=on)\n";
    marker_file.close();
    log(LogLevel::Info, std::string("UI 设置修复完成：字幕开关与场景交互提示已打开并存盘（只执行一次；")
                            + "若你本来关闭字幕，可在 config.ini 把 repair_ui_settings_once 设为 false 并删除 "
                            + "ui_settings_repair_done.flag）");
    return true;
}

// ------------------------------------------------------------------ 锁定标记（锁定敌人身上的白点）

namespace {

// 锁定时敌人身上出现的白点：SetHideUIForPV 的订阅者、W_FrontEndHUDLayout 子树、
// MonsterHealthBarMode（只管血条）都不覆盖它，渲染载体离线也证不了——只能运行时按
// 名字广撒网。宁可多收（原可见性记录在案、退出还原、逐条记日志），也不能漏掉真身；
// 实机日志把名单收敛成精确类名后，再收窄关键词。

// 名字归一：去掉 _ / - 再匹配，让 LockOn / lock_on / LOCK-ON 落到同一个词上
auto lockon_key(const std::string& lower) -> std::string {
    std::string key;
    key.reserve(lower.size());
    for (char c : lower) {
        if (c == '_' || c == '-') continue;
        key.push_back(c);
    }
    return key;
}

auto name_lockish(const std::string& key, const std::string& lower) -> bool {
    if (key.find("lockon") != std::string::npos) return true;
    if (lower.find("lock") != std::string::npos && lower.find("block") == std::string::npos) return true;
    if (key.find("targetmark") != std::string::npos) return true;
    if (key.find("reticle") != std::string::npos) return true;
    if (key.find("crosshair") != std::string::npos) return true;
    if (key.find("mark") != std::string::npos) return true;
    // 1.0.67 实机诊断补充：目标的锁定系统在 PBZ 里叫 Focus（exe 证据 CharacterState.Targeting.
    // Forbid.FocusControl / PBZAbilityTask_FocusControlTick），锁定标记控件类是 WG_PBZFocus_C；
    // 指示器/血条家族（LockIndicator、MonsterHpBar 等）一并收网。误收的都是战斗 UI，
    // 摄影模式本来就要藏，而且全部按原值还原、逐条记日志。
    if (key.find("focus") != std::string::npos) return true;
    if (key.find("indicator") != std::string::npos) return true;
    if (key.find("monsterhp") != std::string::npos) return true;
    if (key.find("enemyhp") != std::string::npos) return true;
    if (key.find("hpbar") != std::string::npos) return true;
    if (key.find("healthbar") != std::string::npos) return true;
    return false;
}

// 第二张网：目标/准星家族。误收的战斗 UI 摄影模式本来就要藏（HUD 默认隐藏），
// 且全部按原值还原、隐藏清单逐条记日志，误伤可从日志里看见。
auto name_targetish(const std::string& key) -> bool {
    return key.find("target") != std::string::npos || key.find("aim") != std::string::npos;
}

auto name_matches_lockon(const std::string& lower) -> bool {
    const std::string key = lockon_key(lower);
    return name_lockish(key, lower) || name_targetish(key);
}

// 白点可能的渲染载体：世界空间 UMG / 粒子 / 贴花 / 小面片。按继承链认（覆盖 BP 子类）；
// 玩法组件（TargetingComponent 之类）不是可视物，不在其列——藏它们没有意义。
auto is_visual_component_class(UClass* cls) -> bool {
    return class_chain_contains(cls, "WidgetComponent") || class_chain_contains(cls, "NiagaraComponent") ||
           class_chain_contains(cls, "ParticleSystemComponent") || class_chain_contains(cls, "DecalComponent") ||
           class_chain_contains(cls, "SpriteComponent") || class_chain_contains(cls, "MeshComponent");
}

// 组件引用的资产（控件类/粒子系统/网格）：泛型组件类名（WidgetComponent/NiagaraComponent…）
// 认不出标记，真身常在资产名上（NS_LockOnMarker、SM_LockOnDot…）。属性按类缓存——
// find_property 走类属性链，同一个类的几百个实例不必各走一遍。
auto visual_asset_properties(UClass* cls) -> const std::vector<FProperty*>& {
    static std::map<UClass*, std::vector<FProperty*>> cache;
    auto it = cache.find(cls);
    if (it == cache.end()) {
        std::vector<FProperty*> props;
        for (const char* name :
             {"WidgetClass", "NiagaraSystem", "Template", "StaticMesh", "SkeletalMesh", "DecalMaterial"}) {
            if (FProperty* p = find_property(cls, widen(name)); p != nullptr) props.push_back(p);
        }
        it = cache.emplace(cls, std::move(props)).first;
    }
    return it->second;
}

// ---- 游戏锁定系统（TargetingComponent）——1.0.68 真机+exe 取证 ----
// exe 字符串确认（PBZ.exe）：锁定系统有自己的一套指示器与目标状态——
//   LockWidget / SecondaryLockIndicators（锁定/副目标指示器控件）、
//   SetShowLockIndicator(bool)（游戏自带的指示器显隐接口）、
//   HasLockTarget / GetLockTarget / SetLockTarget / StopLockTarget（锁定目标状态）、
//   TargetingHandlers / CurrentHandler / TargetingSubsystem（内部结构）。
// 关键：MonsterHealthBarMode=OnlyLockedTarget 只为「当前锁定目标」显示血条——只要锁定
// 目标还在，那条血条就永远不隐藏（1.0.67 实机日志：原模式=OnlyLockedTarget，切档无效）。
// 所以进摄影模式要按游戏自己的接口把锁定目标清掉（退出恢复），指示器控件一并 Collapsed。

auto name_targetingish(const std::string& lower) -> bool {
    if (lower.find("targeting") != std::string::npos) return true;
    if (lower.find("locktarget") != std::string::npos) return true;
    // 类名没证死：锁定系统也可能叫 LockOn/Focus 家族（exe：FocusControl 属锁定体系）
    if (lower.find("lock") != std::string::npos && lower.find("block") == std::string::npos) return true;
    if (lower.find("focus") != std::string::npos) return true;
    return false;
}

// 找锁定系统对象：类名带 Targeting/LockTarget，且具备任一锁定系统特征
//（TargetingHandlers 属性 / LockWidget 属性 / SetShowLockIndicator 函数 / GetLockTarget 函数）。
// 只在类名命中后做属性/函数探测——这类对象全场至多个位数，成本可忽略。
auto find_targeting_roots(std::vector<UObject*>& out) -> void {
    static std::unordered_map<UClass*, bool> class_cache;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        // CDO 跳过（对象名固定以 Default__ 开头）
        if (narrow(object->GetName()).rfind("Default__", 0) == 0) return RC::LoopAction::Continue;
        auto it = class_cache.find(cls);
        if (it == class_cache.end()) {
            it = class_cache.emplace(cls, name_targetingish(lower_ascii(narrow(cls->GetName())))).first;
        }
        if (!it->second) return RC::LoopAction::Continue;
        if (find_property(object, "TargetingHandlers") == nullptr &&
            find_property(object, "LockWidget") == nullptr && find_function(object, "SetShowLockIndicator") == nullptr &&
            find_function(object, "GetLockTarget") == nullptr) {
            return RC::LoopAction::Continue;
        }
        out.push_back(object);
        return RC::LoopAction::Continue;
    });
}

// 函数声明里的目标对象参数（SetLockTarget 的参数名离线未知）：优先名字带 target/actor
// 的对象参数，没有就退回第一个对象参数
auto first_object_param(UFunction* function) -> FProperty* {
    if (function == nullptr) return nullptr;
    FProperty* first_object = nullptr;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == nullptr || !prop->IsA<FObjectProperty>()) continue;
        if (first_object == nullptr) first_object = prop;
        const std::string lower = lower_ascii(narrow(prop->GetName()));
        if (lower.find("target") != std::string::npos || lower.find("actor") != std::string::npos) return prop;
    }
    return first_object;
}

// 锁定标记扫描的类级判定：继承链/类名/资产属性都只依赖类。全对象扫描时对象几十万、
// 类 ~1 万，按类缓存一次算完（lockon_absent_ 每次退出重置、进模式都要重扫，这里是
// 每次进入卡顿的主要来源）。类指针存活期与进程同序——与 visual_asset_properties
// 的按类缓存同一前提。
struct LockonClassVerdict {
    bool in_superset{false};  // UserWidget 派生或可视组件派生（扫描超集）
    bool is_widget{false};
    bool name_matched{false};
    std::string class_name;
    const std::vector<FProperty*>* asset_props{nullptr};
};

auto lockon_class_verdict(UClass* cls) -> const LockonClassVerdict& {
    static std::unordered_map<UClass*, LockonClassVerdict> cache;
    auto it = cache.find(cls);
    if (it == cache.end()) {
        LockonClassVerdict verdict;
        verdict.is_widget = class_chain_contains(cls, "UserWidget");
        verdict.in_superset = verdict.is_widget || is_visual_component_class(cls);
        if (verdict.in_superset) {
            verdict.class_name = narrow(cls->GetName());
            verdict.name_matched = name_matches_lockon(lower_ascii(verdict.class_name));
            verdict.asset_props = &visual_asset_properties(cls);
        }
        it = cache.emplace(cls, std::move(verdict)).first;
    }
    return it->second;
}

// 可能承载白点的对象超集（UserWidget 派生 + 可视组件派生，跳过 CDO）。隐藏后只记录
// 这个集合里的指针，还原时重新枚举一遍做存活校验——不能直接解引用退出前的旧指针
//（换图/GC 后可能已销毁，槽位可能被复用）。
auto collect_lockon_superset(std::vector<UObject*>& out) -> void {
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        // 用 Widget 基类（不是 UserWidget）：锁定系统的 LockWidget 可能是 UWidget 子类（UImage 之类）
        const LockonClassVerdict& verdict = lockon_class_verdict(cls);
        if (!verdict.in_superset && !class_chain_contains(cls, "Widget")) return RC::LoopAction::Continue;
        // CDO 的对象名固定以 Default__ 开头；查对象自身名比拼全名（外层链展开）便宜得多
        if (narrow(object->GetName()).rfind("Default__", 0) == 0) return RC::LoopAction::Continue;
        out.push_back(object);
        return RC::LoopAction::Continue;
    });
}

}  // namespace

bool Ue4ssPort::set_lockon_marker_visible(bool visible) {
    bool touched = false;

    if (!visible) {
        // 本会话已确认场上没有可隐藏的锁定对象。进模式后游戏输入被屏蔽、玩家无法再锁定，
        // 重复的全量扫描不会带来新对象，直接确认完成（core 不再重试）。
        if (lockon_absent_) return true;

        // ---- 扫描阶段：只收集命中对象，不动任何状态（遍历中不跑 ProcessEvent）----
        struct LockonCandidate {
            UObject* object;
            bool is_widget;
            std::string class_name;
        };
        std::vector<LockonCandidate> candidates;
        RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
            if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
            UClass* cls = object->GetClassPrivate();
            if (cls == nullptr) return RC::LoopAction::Continue;
            const LockonClassVerdict& verdict = lockon_class_verdict(cls);
            if (!verdict.in_superset) return RC::LoopAction::Continue;
            if (narrow(object->GetName()).rfind("Default__", 0) == 0) return RC::LoopAction::Continue;

            bool matched = verdict.name_matched;
            if (!matched && !verdict.is_widget && !verdict.asset_props->empty()) {
                // 泛型组件类名认不出标记时认资产名（NS_LockOnMarker 之类）
                for (FProperty* property : *verdict.asset_props) {
                    UObject* asset = nullptr;
                    if (read_object(property, object, asset) && is_ok(asset) &&
                        name_matches_lockon(lower_ascii(narrow(asset->GetName())))) {
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched && !verdict.is_widget && class_chain_contains(cls, "WidgetComponent")) {
                // 怪物身上的世界空间控件组件（敌人头顶血条/名牌/锁定标记多为此类）：
                // 全名里带 Monster/Enemy 的组件一并藏——暂停下只清锁定目标可能不刷新，
                // 控件组件直接压掉最稳（退出按原 bVisible 还原）
                const std::string full_lower = lower_ascii(narrow(object->GetFullName()));
                if (full_lower.find("monster") != std::string::npos ||
                    full_lower.find("enemy") != std::string::npos) {
                    matched = true;
                }
            }
            if (matched) candidates.push_back({object, verdict.is_widget, verdict.class_name});
            return RC::LoopAction::Continue;
        });

        // ---- 行动阶段：读原可见性 → 隐藏 → 记账（枚举已结束，过程中跑 ProcessEvent 无虞）----
        int hidden_count = 0;
        int already_hidden = 0;
        int hide_failed = 0;
        std::string hidden_names;
        for (const LockonCandidate& candidate : candidates) {
            UObject* object = candidate.object;
            if (candidate.is_widget) {
                if (tracked_hud_widget(lockon_widgets_, object)) continue;
                int original = 0;
                if (!get_widget_visibility(object, original)) continue;
                if (original == 1 || original == 2) {  // 已是 Collapsed/Hidden，无需再藏也不记账
                    ++already_hidden;
                    continue;
                }
                if (set_widget_visibility(object, 1)) {  // Collapsed
                    lockon_widgets_.emplace_back(object, original);
                    ++hidden_count;
                    hidden_names += (hidden_names.empty() ? "" : ", ") + candidate.class_name;
                } else {
                    ++hide_failed;
                }
                continue;
            }
            bool tracked = false;
            for (const auto& entry : lockon_components_) {
                if (entry.first == object) {
                    tracked = true;
                    break;
                }
            }
            if (tracked) continue;
            double original_visible = 1.0;
            if (!read_number(find_property(object, "bVisible"), object, original_visible)) {
                original_visible = 1.0;
            }
            if (original_visible == 0.0) {
                ++already_hidden;
                continue;
            }
            if (set_component_visibility(object, false)) {
                lockon_components_.emplace_back(object, true);
                ++hidden_count;
                hidden_names += (hidden_names.empty() ? "" : ", ") + candidate.class_name;
            } else {
                ++hide_failed;
            }
        }

        // ---- 路线②：游戏锁定系统（TargetingComponent）自己的指示器与锁定目标 ----
        // 只有这条路线能治「血条」：MonsterHealthBarMode=OnlyLockedTarget 只为当前锁定目标
        // 显示血条，锁定目标不清掉，血条永远在（1.0.67 实机日志证实原模式就是 OnlyLockedTarget）。
        // 所有动作都按游戏自己的接口执行，退出时成对恢复。
        std::vector<UObject*> targeting_roots;
        find_targeting_roots(targeting_roots);
        int targeting_acted = 0;
        // 藏一个“视觉对象”：可能是 UMG 控件（UWidget 家族），也可能是控件组件本体
        // （WidgetComponent 家族）。两条路都试，成功即记账。
        auto hide_visual_object = [&](UObject* object) -> int {
            if (!is_ok(object)) return 0;
            UClass* cls = object->GetClassPrivate();
            if (class_chain_contains(cls, "Widget")) {
                if (tracked_hud_widget(lockon_widgets_, object)) return 0;
                int original = 0;
                if (get_widget_visibility(object, original)) {
                    if (original == 1 || original == 2) return 0;
                    if (set_widget_visibility(object, 1)) {
                        lockon_widgets_.emplace_back(object, original);
                        return 1;
                    }
                    return 0;
                }
            }
            if (class_chain_contains(cls, "ActorComponent")) {
                for (const auto& entry : lockon_components_) {
                    if (entry.first == object) return 0;
                }
                double original_visible = 1.0;
                if (!read_number(find_property(object, "bVisible"), object, original_visible)) original_visible = 1.0;
                if (original_visible == 0.0) return 0;
                if (set_component_visibility(object, false)) {
                    lockon_components_.emplace_back(object, true);
                    return 1;
                }
            }
            return 0;
        };
        // 世界空间 UMG 的宿主 WidgetComponent 一并藏掉：暂停下控件 Tick 冻结，
        // 只改控件可见性可能不刷新世界空间渲染（与交互提示同样的坑）
        auto hide_host_widget_component = [&](UObject* widget) -> int {
            static UClass* widget_component_class = nullptr;
            if (widget_component_class == nullptr) {
                if (UObject* sample = RC::Unreal::UObjectGlobals::FindFirstOf(STR("WidgetComponent")); is_ok(sample)) {
                    widget_component_class = sample->GetClassPrivate();
                }
            }
            if (widget_component_class == nullptr) return 0;
            UObject* host = widget->GetTypedOuter(widget_component_class);
            if (!is_ok(host)) return 0;
            for (const auto& entry : lockon_components_) {
                if (entry.first == host) return 0;
            }
            double original_visible = 1.0;
            if (!read_number(find_property(host, "bVisible"), host, original_visible)) original_visible = 1.0;
            if (original_visible == 0.0) return 0;
            if (!set_component_visibility(host, false)) return 0;
            lockon_components_.emplace_back(host, true);
            return 1;
        };
        for (UObject* root : targeting_roots) {
            std::string detail;
            const std::string root_class = narrow(root->GetClassPrivate()->GetName());
            // ① 锁定指示器控件（LockWidget）：白点/目标信息很可能就是它
            FProperty* lock_widget_prop = find_property(root, "LockWidget");
            if (lock_widget_prop != nullptr) {
                UObject* lock_widget = nullptr;
                if (read_object(lock_widget_prop, root, lock_widget) && is_ok(lock_widget)) {
                    detail += " LockWidget=" + narrow(lock_widget->GetClassPrivate()->GetName());
                    targeting_acted += hide_visual_object(lock_widget);
                    targeting_acted += hide_host_widget_component(lock_widget);
                } else {
                    detail += " LockWidget不可读(类型=" + narrow(lock_widget_prop->GetClass().GetFName().ToString()) + ")";
                }
            }
            // ② 副目标指示器（SecondaryLockIndicators 数组）
            FProperty* secondary_prop = find_property(root, "SecondaryLockIndicators");
            if (auto* array_prop = as_array_property(secondary_prop)) {
                FScriptArrayHelper_InContainer helper(array_prop, static_cast<const void*>(root));
                const int32 count = helper.Num();
                detail += " 副指示器=" + std::to_string(count);
                for (int32 i = 0; i < count && i < 64; ++i) {
                    UObject* indicator = nullptr;
                    if (!read_object(array_prop->GetInner(), helper.GetRawPtr(i), indicator) || !is_ok(indicator)) continue;
                    targeting_acted += hide_visual_object(indicator);
                    targeting_acted += hide_host_widget_component(indicator);
                }
            } else if (secondary_prop != nullptr) {
                detail += " 副指示器属性类型=" + narrow(secondary_prop->GetClass().GetFName().ToString());
            }
            // ③ 游戏自带的指示器开关（暂停下控件 Tick 冻结，所以上面还用 Collapsed 兜底）
            if (UFunction* show_fn = find_function(root, "SetShowLockIndicator")) {
                bool tracked = false;
                for (UObject* suppressed : lockon_indicator_suppressed_) {
                    if (suppressed == root) tracked = true;
                }
                if (!tracked) {
                    const bool ok = invoke(root, "SetShowLockIndicator", [&](ParamBlock& params) {
                        return params.set_bool("bShow", false) || params.set_bool("Show", false) ||
                               params.set_bool("bNewShow", false) || params.set_bool("bVisible", false) ||
                               params.set_bool("bEnable", false) || params.set_bool("Value", false);
                    });
                    if (ok) {
                        lockon_indicator_suppressed_.push_back(root);
                        ++targeting_acted;
                    } else {
                        // 参数名不符时回落写 Show 属性（若存在）
                        if (write_bool(find_property(root, "Show"), root, false)) {
                            lockon_indicator_suppressed_.push_back(root);
                            ++targeting_acted;
                        } else {
                            detail += " SetShowLockIndicator参数不符(" + describe_params(show_fn) + ")";
                        }
                    }
                }
            }
            // ④ 锁定目标：先存后清（血条只为锁定目标显示，不清血条永远在）
            bool has_lock = false;
            invoke_bool(root, "HasLockTarget", [&](ParamBlock&) { return true; }, has_lock);
            if (has_lock) {
                UFunction* getter = find_function(root, "GetLockTarget");
                FProperty* ret = getter != nullptr ? getter->GetReturnProperty() : nullptr;
                UObject* target = nullptr;
                bool got_target = false;
                if (getter != nullptr && ret != nullptr && ret->IsA<FObjectProperty>()) {
                    ParamBlock params(getter);
                    if (params.valid()) {
                        root->ProcessEvent(getter, params.data());
                        got_target = read_object(params.property("ReturnValue"), params.data(), target) && is_ok(target);
                    }
                }
                if (got_target) {
                    detail += " 锁定目标=" + narrow(target->GetName());
                    bool already = false;
                    for (const auto& entry : lockon_cleared_targets_) {
                        if (entry.component == root) already = true;
                    }
                    if (!already) {
                        if (UFunction* stop_fn = find_function(root, "StopLockTarget")) {
                            ParamBlock stop_params(stop_fn);
                            if (stop_params.valid()) {
                                root->ProcessEvent(stop_fn, stop_params.data());
                                lockon_cleared_targets_.push_back({root, target});
                                ++targeting_acted;
                                detail += "（已清）";
                            }
                        } else {
                            detail += "（无StopLockTarget）";
                        }
                    }
                } else {
                    detail += " 有锁定但GetLockTarget不可读(" +
                              (getter != nullptr ? describe_params(getter) : std::string("无函数")) + ")";
                }
            }
            if (UFunction* set_fn = find_function(root, "SetLockTarget")) {
                detail += " SetLockTarget(" + describe_params(set_fn) + ")";
            }
            log(LogLevel::Info, "锁定系统：类=" + root_class + " 对象=" + narrow(root->GetName()) + detail);
        }
        if (targeting_acted > 0) {
            touched = true;
            log(LogLevel::Info, "锁定系统指示器/锁定目标已处理：动作数=" + std::to_string(targeting_acted) +
                                    "，退出摄影模式时恢复");
            return true;
        }

        if (hidden_count > 0) {
            touched = true;
            log(LogLevel::Info, "已隐藏锁定标记：数量=" + std::to_string(hidden_count) + "（" + hidden_names +
                                    "），退出摄影模式时还原原可见性");
            return true;
        }
        if (hide_failed > 0) return false;  // 有目标但藏不住：按秒重试

        // 全量扫描零可隐藏对象：白点要么此刻不在（没锁定），要么类名不在关键词网里。
        // 打一次性诊断清单（可见控件/可视组件/玩家组件/锁定系统），真实类名靠这份日志收敛。
        lockon_absent_ = true;
        dump_lockon_diagnostics();
        if (!lockon_absent_logged_) {
            lockon_absent_logged_ = true;
            log(LogLevel::Warn,
                "场上未找到可见的锁定标记对象（锁定/目标/标记关键词零命中，已按隐藏=" + std::to_string(already_hidden) +
                    " 个同名家族对象，锁定系统对象=" + std::to_string(targeting_roots.size()) +
                    " 个）——若白点仍在屏上，把上面的诊断清单发回来");
        }
        return true;
    }

    // 恢复：重新枚举白点可能所在的超集做存活校验（换图/GC 后旧对象可能已销毁，
    // 不能直接解引用旧指针）。任何一项写失败都保留记账，由 core 退避重试（H-1）。
    std::vector<UObject*> alive;
    collect_lockon_superset(alive);
    bool ok = true;
    int restored_count = 0;

    std::vector<std::pair<UObject*, int>> pending_widgets;
    for (const auto& entry : lockon_widgets_) {
        if (!pointer_in(alive, entry.first)) continue;
        if (set_widget_visibility(entry.first, entry.second)) {
            ++restored_count;
        } else {
            pending_widgets.push_back(entry);
            ok = false;
        }
    }
    lockon_widgets_.swap(pending_widgets);

    std::vector<std::pair<UObject*, bool>> pending_components;
    for (const auto& entry : lockon_components_) {
        if (!pointer_in(alive, entry.first)) continue;
        if (set_component_visibility(entry.first, entry.second)) {
            ++restored_count;
        } else {
            pending_components.push_back(entry);
            ok = false;
        }
    }
    lockon_components_.swap(pending_components);

    // 游戏锁定系统的指示器开关恢复（失败项保留，下次重试）
    std::vector<UObject*> pending_indicators;
    for (UObject* component : lockon_indicator_suppressed_) {
        if (!is_ok(component)) continue;
        if (invoke(component, "SetShowLockIndicator", [&](ParamBlock& params) {
                return params.set_bool("bShow", true) || params.set_bool("Show", true) ||
                       params.set_bool("bNewShow", true) || params.set_bool("bVisible", true) ||
                       params.set_bool("bEnable", true) || params.set_bool("Value", true);
            })) {
            ++restored_count;
        } else if (write_bool(find_property(component, "Show"), component, true)) {
            ++restored_count;
        } else {
            pending_indicators.push_back(component);
            ok = false;
        }
    }
    lockon_indicator_suppressed_.swap(pending_indicators);

    // 锁定目标恢复：SetLockTarget 的参数名离线未知——按声明顺序第一个对象参数填保存的目标
    const size_t cleared_count = lockon_cleared_targets_.size();
    int relocked_count = 0;
    std::string relock_failed_detail;
    std::vector<LockonClearedTarget> pending_targets;
    for (const auto& entry : lockon_cleared_targets_) {
        if (!is_ok(entry.component) || !is_ok(entry.target)) {
            pending_targets.push_back(entry);
            ok = false;
            continue;
        }
        UFunction* setter = find_function(entry.component, "SetLockTarget");
        if (setter == nullptr) {
            relock_failed_detail = "无 SetLockTarget 函数";
            pending_targets.push_back(entry);
            ok = false;
            continue;
        }
        FProperty* target_param = first_object_param(setter);
        if (target_param == nullptr) {
            relock_failed_detail = "SetLockTarget(" + describe_params(setter) + ") 无对象参数";
            pending_targets.push_back(entry);
            ok = false;
            continue;
        }
        ParamBlock params(setter);
        if (!params.valid()) {
            pending_targets.push_back(entry);
            ok = false;
            continue;
        }
        if (write_object(target_param, params.data(), entry.target)) {
            entry.component->ProcessEvent(setter, params.data());
            ++relocked_count;
            ++restored_count;
        } else {
            relock_failed_detail = "SetLockTarget(" + describe_params(setter) + ") 参数写不进去";
            pending_targets.push_back(entry);
            ok = false;
        }
    }
    lockon_cleared_targets_.swap(pending_targets);
    if (cleared_count > 0) {
        if (relocked_count == static_cast<int>(cleared_count)) {
            log(LogLevel::Info, "锁定目标已恢复：数量=" + std::to_string(relocked_count));
        } else {
            log(LogLevel::Warn, "锁定目标恢复失败 " + std::to_string(cleared_count - relocked_count) + "/" +
                                    std::to_string(cleared_count) + "（" +
                                    (relock_failed_detail.empty() ? std::string("目标已失效") : relock_failed_detail) +
                                    "）——保留记账重试；退出后若未锁定，重新按锁定键即可");
        }
    }
    if (restored_count > 0) {
        touched = true;
        log(LogLevel::Info, "已恢复锁定标记：数量=" + std::to_string(restored_count));
    }
    lockon_absent_ = false;  // 下次进入重新扫描（不同场景的锁定标记可能不同）
    if (!ok) log(LogLevel::Warn, "锁定标记/锁定目标仍有未恢复项，保留记账等待重试");
    // 没有待恢复项（世界换代已清空缓存）时也返回 true：恢复是幂等的
    return ok;
}

// 一次性诊断（每会话一次，关键词没接住白点时给下一轮指认真身）：
//   ① 可见 UserWidget 类——白点若是屏幕/世界 UMG，必在其中；
//   ② 场上可视组件类——白点若是粒子/贴花/小面片，类名在这里；
//   ③ 玩家 pawn 名下类名带锁定/目标语义的组件；
//   ④ 锁定系统（TargetingComponent）对象、锁定状态与关键成员（1.0.68 起）。
void Ue4ssPort::dump_lockon_diagnostics() {
    if (lockon_diag_dumped_) return;
    lockon_diag_dumped_ = true;

    std::map<std::string, int> visible_widgets;
    std::map<std::string, int> visual_components;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        if (narrow(object->GetFullName()).find("Default__") != std::string::npos) return RC::LoopAction::Continue;
        if (class_chain_contains(cls, "UserWidget")) {
            int visibility = 1;
            if (!get_widget_visibility(object, visibility) || visibility == 1 || visibility == 2) {
                return RC::LoopAction::Continue;
            }
            ++visible_widgets[narrow(cls->GetName())];
            return RC::LoopAction::Continue;
        }
        if (is_visual_component_class(cls)) ++visual_components[narrow(cls->GetName())];
        return RC::LoopAction::Continue;
    });
    auto join_counts = [](const std::map<std::string, int>& table, size_t cap) {
        std::string names;
        size_t listed = 0;
        for (const auto& entry : table) {
            if (listed++ >= cap) {
                names += ", …";
                break;
            }
            names += (names.empty() ? "" : ", ") + entry.first + " x" + std::to_string(entry.second);
        }
        return names;
    };
    log(LogLevel::Info, "锁定标记诊断① 可见 UserWidget：" +
                            (visible_widgets.empty() ? std::string("（无）") : join_counts(visible_widgets, 120)));
    log(LogLevel::Info, "锁定标记诊断② 可视组件：" +
                            (visual_components.empty() ? std::string("（无）") : join_counts(visual_components, 120)));

    UObject* pawn = player_pawn();
    if (is_ok(pawn)) {
        // 组件名下判定走全名：组件的完整名里含 ":<pawn 对象名>." 即为 pawn 名下（含子对象）。
        const std::string pawn_name = narrow(pawn->GetName());
        const std::string pawn_marker = ":" + pawn_name + ".";
        std::string pawn_components;
        RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
            if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
            UClass* cls = object->GetClassPrivate();
            if (cls == nullptr || !class_chain_contains(cls, "ActorComponent")) return RC::LoopAction::Continue;
            const std::string full_name = narrow(object->GetFullName());
            if (full_name.find(pawn_marker) == std::string::npos) return RC::LoopAction::Continue;
            const std::string lower = lower_ascii(narrow(cls->GetName()));
            if (lower.find("lock") == std::string::npos && lower.find("target") == std::string::npos &&
                lower.find("aim") == std::string::npos) {
                return RC::LoopAction::Continue;
            }
            pawn_components += (pawn_components.empty() ? "" : ", ") + narrow(cls->GetName());
            return RC::LoopAction::Continue;
        });
        log(LogLevel::Info, "锁定标记诊断③ 玩家锁定/目标组件：" +
                                (pawn_components.empty() ? std::string("（未发现）") : pawn_components));
    }

    // ④ 锁定系统对象（1.0.68 起）：类/关键成员/锁定状态——白点与锁定血条的真身在这里
    std::vector<UObject*> roots;
    find_targeting_roots(roots);
    if (roots.empty()) {
        // 类名像锁定系统但特征不符的对象也列出来（组件可能换了名字/成员）
        std::map<std::string, int> name_hits;
        RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
            if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
            UClass* cls = object->GetClassPrivate();
            if (cls == nullptr) return RC::LoopAction::Continue;
            if (narrow(object->GetName()).rfind("Default__", 0) == 0) return RC::LoopAction::Continue;
            if (name_targetingish(lower_ascii(narrow(cls->GetName())))) ++name_hits[narrow(cls->GetName())];
            return RC::LoopAction::Continue;
        });
        std::string names;
        for (const auto& entry : name_hits) names += (names.empty() ? "" : ", ") + entry.first + " x" + std::to_string(entry.second);
        log(LogLevel::Info, "锁定标记诊断④ 锁定系统对象：（未发现）" +
                                (names.empty() ? std::string("") : "；类名疑似：" + names));
    } else {
        for (UObject* root : roots) {
            std::string members;
            for (TFieldIterator<FProperty> it(root->GetClassPrivate()); it; ++it) {
                FProperty* prop = *it;
                if (prop == nullptr) continue;
                const std::string name = narrow(prop->GetName());
                const std::string lower = lower_ascii(name);
                if (lower.find("lock") == std::string::npos && lower.find("show") == std::string::npos &&
                    lower.find("target") == std::string::npos && lower.find("indicator") == std::string::npos) {
                    continue;
                }
                members += (members.empty() ? "" : ", ") + name + "(" +
                           narrow(prop->GetClass().GetFName().ToString()) + ")";
            }
            bool has_lock = false;
            invoke_bool(root, "HasLockTarget", [&](ParamBlock&) { return true; }, has_lock);
            std::string detail = "锁定标记诊断④ 锁定系统：类=" + narrow(root->GetClassPrivate()->GetName()) +
                                 " 对象=" + narrow(root->GetName()) + " 有锁定=" + (has_lock ? "是" : "否");
            if (has_lock) {
                UFunction* getter = find_function(root, "GetLockTarget");
                FProperty* ret = getter != nullptr ? getter->GetReturnProperty() : nullptr;
                UObject* target = nullptr;
                if (getter != nullptr && ret != nullptr && ret->IsA<FObjectProperty>()) {
                    ParamBlock params(getter);
                    if (params.valid()) {
                        root->ProcessEvent(getter, params.data());
                        if (read_object(params.property("ReturnValue"), params.data(), target) && is_ok(target)) {
                            detail += " 目标=" + narrow(target->GetName()) + "(" +
                                      narrow(target->GetClassPrivate()->GetName()) + ")";
                        }
                    }
                } else {
                    detail += " GetLockTarget(" + (getter != nullptr ? describe_params(getter) : std::string("无")) + ")";
                }
            }
            log(LogLevel::Info, detail + " 成员：" + (members.empty() ? std::string("（无）") : members));
        }
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
