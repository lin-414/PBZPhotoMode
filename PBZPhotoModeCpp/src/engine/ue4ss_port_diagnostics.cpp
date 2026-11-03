// =============================================================================
//  Ue4ssPort 成员函数实现：冻结诊断与引擎状态转储
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

// 每秒一条的冻结诊断：风源现状 + MPC 参数与上一拍的变化 + 相机修饰器/shake 状态。
// freeze_mpc 生效时 MPC 变化应为“无”（变化仍在 = 有驱动源在每帧和我们抢写入权）。
std::string Ue4ssPort::freeze_diag_sample() {
    std::string out = "冻结诊断:";

    // ① 风源现状（归零后应显示 强度0/速度0；仍非零 = 游戏侧在改回）
    {
        std::vector<UObject*> sources;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("WindDirectionalSource"), sources);
        int live = 0;
        std::string detail;
        for (UObject* source : sources) {
            if (!is_ok(source)) continue;
            if (narrow(source->GetFullName()).find("Default__") != std::string::npos) continue;
            ++live;
            if (live > 3 || detail.size() >= 96) continue;
            UObject* component = nullptr;
            double strength = 0.0;
            double speed = 0.0;
            if (read_object(find_property(source, "Component"), source, component) && is_ok(component)) {
                read_number(find_property(component, "Strength"), component, strength);
                read_number(find_property(component, "Speed"), component, speed);
                char item[64]{};
                std::snprintf(item, sizeof(item), " 强度%.2f/速度%.2f;", strength, speed);
                detail += item;
            }
        }
        out += " 风源=" + std::to_string(live) + (detail.empty() ? "" : "[" + detail + "]");
    }

    // ② MPC 参数与上一拍的变化（冻结期参数还在变 = 布料/植被摆动的驱动源找到了）
    {
        UObject* statics = kismet_material_library();
        UObject* scene = world();
        std::vector<MpcCollectionSnapshot> current;
        if (is_ok(statics) && is_ok(scene)) {
            std::vector<UObject*> collections;
            RC::Unreal::UObjectGlobals::FindAllOf(STR("MaterialParameterCollection"), collections);
            int total_params = 0;
            std::string changes;
            int change_count = 0;
            for (UObject* collection : collections) {
                if (!is_ok(collection)) continue;
                if (narrow(collection->GetFullName()).find("Default__") != std::string::npos) continue;
                MpcCollectionSnapshot snapshot;
                snapshot.collection = collection;
                snapshot.name = narrow(collection->GetName());
                std::vector<std::string> scalar_names;
                mpc_read_param_names(collection, "ScalarParameters", scalar_names);
                for (const std::string& name : scalar_names) {
                    double value = 0.0;
                    if (!mpc_get_scalar(statics, scene, collection, name, value)) continue;
                    MpcParamValue param;
                    param.name = name;
                    param.value[0] = static_cast<float>(value);
                    snapshot.params.push_back(param);
                    ++total_params;
                }
                std::vector<std::string> vector_names;
                mpc_read_param_names(collection, "VectorParameters", vector_names);
                for (const std::string& name : vector_names) {
                    float rgba[4]{};
                    if (!mpc_get_vector(statics, scene, collection, name, rgba)) continue;
                    MpcParamValue param;
                    param.name = name;
                    param.is_vector = true;
                    for (int k = 0; k < 4; ++k) param.value[k] = rgba[k];
                    snapshot.params.push_back(param);
                    ++total_params;
                }
                current.push_back(std::move(snapshot));
            }
            if (mpc_diag_has_last_) {
                for (const MpcCollectionSnapshot& snapshot : current) {
                    const MpcCollectionSnapshot* last = nullptr;
                    for (const MpcCollectionSnapshot& candidate : mpc_diag_last_) {
                        if (candidate.name == snapshot.name) {
                            last = &candidate;
                            break;
                        }
                    }
                    if (last == nullptr) continue;
                    for (const MpcParamValue& param : snapshot.params) {
                        const MpcParamValue* last_param = nullptr;
                        for (const MpcParamValue& candidate : last->params) {
                            if (candidate.name == param.name && candidate.is_vector == param.is_vector) {
                                last_param = &candidate;
                                break;
                            }
                        }
                        if (last_param == nullptr) continue;
                        bool differs = false;
                        for (int k = 0; k < 4; ++k) {
                            if (std::fabs(param.value[k] - last_param->value[k]) > 1e-6f) {
                                differs = true;
                                break;
                            }
                        }
                        if (!differs) continue;
                        ++change_count;
                        if (change_count <= 3 && changes.size() < 110) {
                            char item[96]{};
                            if (param.is_vector) {
                                std::snprintf(item, sizeof(item), "%s.%s(向量)", snapshot.name.c_str(),
                                              param.name.c_str());
                            } else {
                                std::snprintf(item, sizeof(item), "%s.%s %.3f→%.3f", snapshot.name.c_str(),
                                              param.name.c_str(), last_param->value[0], param.value[0]);
                            }
                            changes += (changes.empty() ? "" : " | ") + std::string(item);
                        }
                    }
                }
            }
            out += " MPC=" + std::to_string(current.size()) + "集/" + std::to_string(total_params) + "参" +
                   (mpc_diag_has_last_ ? (" 变化:" + (changes.empty() ? std::string("无") : changes))
                                       : std::string(" 基线"));
        } else {
            out += " MPC=不可用";
        }
        mpc_diag_last_ = std::move(current);
        mpc_diag_has_last_ = true;
    }

    // ③ 相机修饰器与活动 shake
    {
        UObject* manager = camera_manager();
        if (is_ok(manager)) {
            int modifier_count = 0;
            int disabled_count = 0;
            int shake_count = -1;
            FProperty* modifiers_prop = camera_modifiers_property(manager);
            auto* array_prop = as_array_property(modifiers_prop);
            if (array_prop != nullptr) {
                const void* container = manager;
                FScriptArrayHelper_InContainer helper(array_prop, container);
                modifier_count = helper.Num();
                for (int32 i = 0; i < modifier_count; ++i) {
                    UObject* modifier = nullptr;
                    if (!read_object(array_prop->GetInner(), helper.GetRawPtr(i), modifier) || !is_ok(modifier)) continue;
                    double value = 0.0;
                    if (read_number(find_property(modifier, "bDisabled"), modifier, value) && value != 0.0) {
                        ++disabled_count;
                    }
                }
            }
            FProperty* shakes_prop = find_property(manager, "CameraShakes");
            auto* shakes_array = as_array_property(shakes_prop);
            if (shakes_array != nullptr) {
                const void* container = manager;
                FScriptArrayHelper_InContainer helper(shakes_array, container);
                shake_count = helper.Num();
            }
            char buf[96]{};
            if (shake_count >= 0) {
                std::snprintf(buf, sizeof(buf), " 修饰器=%d(停用%d) shake=%d", modifier_count, disabled_count,
                              shake_count);
            } else {
                std::snprintf(buf, sizeof(buf), " 修饰器=%d(停用%d)", modifier_count, disabled_count);
            }
            out += buf;
        } else {
            out += " 修饰器=不可达";
        }
    }

    // ④ 视角微动（上一诊断周期内的最大逐帧偏差）：非零 = 有东西在暂停期继续推视角
    {
        char buf[96]{};
        std::snprintf(buf, sizeof(buf), " 视角微动:位移%.3f 旋转%.2f°", pov_max_loc_delta_, pov_max_rot_delta_);
        out += buf;
        pov_watch_has_last_ = false;
        pov_max_loc_delta_ = 0.0;
        pov_max_rot_delta_ = 0.0;
    }
    return out;
}

// ------------------------------------------------------------------ 游戏内文字菜单

std::string Ue4ssPort::camera_state_summary() {
    std::string out;

    // 相机管理器真正在用的视角：不是我们的相机就说明 ViewTarget 没切过去
    if (UObject* manager = camera_manager(); is_ok(manager)) {
        if (UFunction* get_location = find_function(manager, "GetCameraLocation"); get_location != nullptr) {
            ParamBlock params(get_location);
            if (params.valid()) {
                manager->ProcessEvent(get_location, params.data());
                FProperty* result = params.prop({"ReturnValue"});
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                if (read_struct_number(result, params.data(), "X", x) &&
                    read_struct_number(result, params.data(), "Y", y) &&
                    read_struct_number(result, params.data(), "Z", z)) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "管理器视角=(%.0f,%.0f,%.0f) ", x, y, z);
                    out += buf;
                }
            }
        }
        // 管理器这一帧算出来的朝向：它跟着我们的相机走，说明相机变换真的生效了
        if (UFunction* get_rotation = find_function(manager, "GetCameraRotation"); get_rotation != nullptr) {
            ParamBlock params(get_rotation);
            if (params.valid()) {
                manager->ProcessEvent(get_rotation, params.data());
                FProperty* result = params.prop({"ReturnValue"});
                double pitch = 0.0;
                double yaw = 0.0;
                double roll = 0.0;
                if (read_struct_number(result, params.data(), "Pitch", pitch) &&
                    read_struct_number(result, params.data(), "Yaw", yaw) &&
                    read_struct_number(result, params.data(), "Roll", roll)) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "管理器朝向=(%.1f,%.1f) ", pitch, yaw);
                    out += buf;
                }
            }
        }
        // 画幅锁状态：真机验证超宽屏解锁（1.5.0）
        {
            double constrain = -1.0;
            if (read_number(find_property(manager, "bDefaultConstrainAspectRatio"), manager, constrain)) {
                double value = 0.0;
                read_number(find_property(manager, "DefaultAspectRatio"), manager, value);
                char buf[64]{};
                std::snprintf(buf, sizeof(buf), "画幅锁=%s(%.3f) ", constrain != 0.0 ? "开" : "关", value);
                out += buf;
            }
        }
    }

    if (is_ok(camera_actor_)) {
        auto* actor = static_cast<AActor*>(camera_actor_);
        const FVector location = actor->K2_GetActorLocation();
        const FRotator rotation = actor->K2_GetActorRotation();
        char buf[200]{};
        std::snprintf(buf, sizeof(buf), "我们的相机=(%.0f,%.0f,%.0f) 朝向=(%.0f,%.0f) ", location.X(), location.Y(),
                      location.Z(), rotation.GetPitch(), rotation.GetYaw());
        out += buf;
    } else {
        out += "我们的相机=无 ";
    }

    if (UObject* component = camera_component(); is_ok(component)) {
        double fov = 0.0;
        if (read_number(find_property(component, "FieldOfView"), component, fov)) {
            char buf[48]{};
            std::snprintf(buf, sizeof(buf), "组件FOV=%.1f ", fov);
            out += buf;
        }
    }

    UObject* view_target = nullptr;
    if (UObject* pc = controller(); is_ok(pc) && read_struct_object(find_property(pc, "ViewTarget"), pc, "Actor", view_target)) {
        out += view_target == camera_actor_ ? "ViewTarget=我们的相机" : "ViewTarget=其他Actor";
    }
    return out;
}

void Ue4ssPort::dump_engine_state() {
    log(LogLevel::Info, "---- 引擎对象 ----");

    log(LogLevel::Info, "时间设置：" + time_settings_summary());

    UObject* pc = controller();
    log(LogLevel::Info, std::string("PC=") + (is_ok(pc) ? narrow(pc->GetFullName()) : "null"));

    UObject* pawn = player_pawn();
    log(LogLevel::Info, std::string("Pawn=") + (is_ok(pawn) ? narrow(pawn->GetFullName()) : "null"));

    UObject* hud = nullptr;
    if (is_ok(pc) && read_object(find_property(pc, "MyHUD"), pc, hud) && is_ok(hud)) {
        log(LogLevel::Info, std::string("HUD=") + narrow(hud->GetFullName()));
        std::string names;
        int count = 0;
        for (TFieldIterator<UFunction> it(static_cast<UClass*>(hud->GetClassPrivate())); it && count < 30; ++it, ++count) {
            names += narrow((*it)->GetName());
            names += ' ';
        }
        log(LogLevel::Info, "HUD函数: " + names);
    } else {
        log(LogLevel::Warn, "MyHUD 读取失败（本作可能没有 AHUD 实例）");
    }

    // UIManager 的 PV 隐藏开关诊断：这是隐藏游戏全局 UI/HUD 的主路径
    UObject* ui_manager = RC::Unreal::UObjectGlobals::FindFirstOf(STR("PBZUIManagerSubsystem"));
    if (is_ok(ui_manager)) {
        log(LogLevel::Info, "UIManager=" + narrow(ui_manager->GetFullName()) +
                                " SetHideUIForPV=" +
                                (find_function(ui_manager, "SetHideUIForPV") != nullptr ? "有" : "无"));
    } else {
        log(LogLevel::Warn, "找不到 PBZUIManagerSubsystem（UIManager 的 PV 隐藏开关不可用）");
    }

    // UMG HUD 根控件诊断：进入摄影模式时必须能找到它，否则 hide_game_hud 只能退回 AHUD
    std::vector<UObject*> hud_widgets;
    find_hud_layout_widgets(hud_widgets);
    if (!hud_widgets.empty()) {
        for (UObject* widget : hud_widgets) {
            int visibility = -1;
            get_widget_visibility(widget, visibility);
            log(LogLevel::Info, "HUD控件=" + narrow(widget->GetFullName()) +
                                    " Visibility=" + std::to_string(visibility));
        }
    } else {
        log(LogLevel::Warn, "找不到 UMG HUD 根控件（W_FrontEndHUDLayout_C）");
        // 兜底诊断：把运行时类名里带 HUD/Hud 的 UserWidget 列出来，方便下个版本改类名
        std::vector<UObject*> all_widgets;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("UserWidget"), all_widgets);
        std::string candidates;
        for (UObject* widget : all_widgets) {
            if (!is_ok(widget)) continue;
            const std::string class_name = narrow(widget->GetClassPrivate()->GetName());
            if (class_name.find("HUD") == std::string::npos && class_name.find("Hud") == std::string::npos) continue;
            if (!candidates.empty()) candidates += ", ";
            candidates += class_name;
        }
        if (!candidates.empty()) {
            log(LogLevel::Info, "含 HUD 的 Widget 类：" + candidates);
        }
    }

    if (is_ok(camera_actor_)) {
        log(LogLevel::Info, std::string("相机Actor=") + narrow(camera_actor_->GetFullName()));
        if (UObject* component = camera_component(); is_ok(component)) {
            log(LogLevel::Info, std::string("相机组件=") + narrow(component->GetFullName()));
        } else {
            log(LogLevel::Warn, "相机 Actor 上找不到 CameraComponent（FOV/视角都不会生效）");
        }
    }

    if (is_ok(pc)) {
        std::string names;
        int count = 0;
        if (UObject* pc_class = pc->GetClassPrivate(); is_ok(pc_class)) {
            for (TFieldIterator<UFunction> it(static_cast<UClass*>(pc_class)); it && count < 20; ++it) {
                const std::string name = narrow((*it)->GetName());
                if (name.find("Input") == std::string::npos && name.find("Look") == std::string::npos &&
                    name.find("Turn") == std::string::npos) {
                    continue;
                }
                names += name;
                names += ' ';
                ++count;
            }
        }
        log(LogLevel::Info, "PC 输入相关函数: " + (names.empty() ? std::string("（无）") : names));
    }

    if (UObject* manager = camera_manager(); is_ok(manager)) {
        UObject* current = nullptr;
        if (read_struct_object(find_property(manager, "ViewTarget"), manager, "Target", current)) {
            log(LogLevel::Info, std::string("管理器 ViewTarget=") +
                                    (is_ok(current) ? narrow(current->GetFullName()) : "null"));
        } else {
            log(LogLevel::Warn, "读不到管理器 ViewTarget.Target");
        }
    }

    UObject* view_target = nullptr;
    if (is_ok(pc) && read_struct_object(find_property(pc, "ViewTarget"), pc, "Actor", view_target)) {
        log(LogLevel::Info, std::string("ViewTarget.Actor=") + (is_ok(view_target) ? narrow(view_target->GetFullName()) : "null"));
    } else {
        log(LogLevel::Warn, "读不到 PlayerController.ViewTarget.Actor（校验跳过）");
    }

    const std::string summary = camera_state_summary();
    if (!summary.empty()) log(LogLevel::Info, summary);
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
