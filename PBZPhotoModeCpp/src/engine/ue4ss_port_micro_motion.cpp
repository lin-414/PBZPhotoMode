// =============================================================================
//  Ue4ssPort 成员函数实现：冻结期残余微动治理（相机 shake/修饰器、风源、MPC、POV 微动）
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

// 背景：冻结=世界暂停/时间缩放，但渲染与相机管理层每帧照跑，冻结画面并非逐位一致。
// 每帧变化源与对策：
//   ① 相机修饰器/shake（真实时间驱动）→ 视角亚像素微动 → 阴影贴图每帧重新对齐 → 脸部阴影爬动
//   ② 风源推的材质 WPO（布料/植被摆动）→ 走材质时间，暂停与时间缩放都拦不住
//   ③ 实时驱动代码每帧改写材质参数集合（MPC）推 WPO
// ①停用 ②幅度归零（钟照走、位移恒零）③每帧写回快照；变化源掐掉后 TSR/TAA 自己收敛。

CameraShakeFreezeReport Ue4ssPort::freeze_camera_shakes(bool frozen) {
    CameraShakeFreezeReport report{};
    UObject* manager = camera_manager();
    if (!is_ok(manager)) {
        if (frozen) log(LogLevel::Warn, "相机管理器不可达，shake/修饰器治理跳过");
        return report;
    }
    report.ok = true;
    bool shakes_stopped = false;
    if (frozen) {
        shakes_stopped = invoke_set_bool(manager, "StopAllCameraShakes", true);
    }

    FProperty* modifiers_prop = camera_modifiers_property(manager);
    auto* array_prop = as_array_property(modifiers_prop);
    if (array_prop != nullptr) {
        const void* container = manager;
        FScriptArrayHelper_InContainer helper(array_prop, container);
        const int32 count = helper.Num();
        report.found = count;
        if (frozen) {
            camera_modifier_states_.clear();
            for (int32 i = 0; i < count; ++i) {
                UObject* modifier = nullptr;
                if (!read_object(array_prop->GetInner(), helper.GetRawPtr(i), modifier) || !is_ok(modifier)) continue;
                if (narrow(modifier->GetFullName()).find("Default__") != std::string::npos) continue;
                double disabled_value = 0.0;
                const bool disabled =
                    read_number(find_property(modifier, "bDisabled"), modifier, disabled_value) &&
                    disabled_value != 0.0;
                camera_modifier_states_.push_back({modifier, disabled});
                if (disabled) continue;  // 原本就停用的不动，还原时也不碰
                if (invoke_set_bool(modifier, "DisableModifier", true)) ++report.acted;
            }
            char buf[200]{};
            std::snprintf(buf, sizeof(buf), "相机治理（冻结）：shake停用=%s，修饰器 %d 个、本次停用 %d 个",
                          shakes_stopped ? "成功" : "不可用", report.found, report.acted);
            log(LogLevel::Info, buf);
        } else {
            for (const auto& entry : camera_modifier_states_) {
                if (!is_ok(entry.first)) continue;
                // DisableModifier(false)=启用；冻结前就是停用态的写回 DisableModifier(true)
                if (invoke_set_bool(entry.first, "DisableModifier", !entry.second)) ++report.acted;
            }
            camera_modifier_states_.clear();
            char buf[120]{};
            std::snprintf(buf, sizeof(buf), "相机治理（恢复）：修饰器按原状态写回 %d 个", report.acted);
            log(LogLevel::Info, buf);
        }
    } else if (frozen) {
        // 找不到数组时把管理器类名与属性字段类名打进日志：属性存在=IsA 失灵，属性不存在=游戏自定义管理器
        std::string detail = "（属性未找到）";
        if (modifiers_prop != nullptr) {
            detail = "（字段类=" + narrow(modifiers_prop->GetClass().GetFName().ToString()) + "）";
        }
        std::string manager_class = "-";
        if (is_ok(manager)) manager_class = narrow(manager->GetClassPrivate()->GetName());
        log(LogLevel::Info, std::string("相机管理器无 ModifierList/CameraModifiers 数组") + detail + "，管理器=" +
                                manager_class + "；shake 停用=" + (shakes_stopped ? "成功" : "不可用"));
    } else {
        const bool had_states = !camera_modifier_states_.empty();
        camera_modifier_states_.clear();
        if (had_states) log(LogLevel::Warn, "相机管理器无 ModifierList/CameraModifiers 数组，修饰器还原跳过");
    }
    return report;
}

WindFreezeReport Ue4ssPort::freeze_wind(bool frozen) {
    WindFreezeReport report{};
    report.ok = true;
    if (frozen) {
        std::vector<UObject*> sources;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("WindDirectionalSource"), sources);
        for (UObject* source : sources) {
            if (!is_ok(source)) continue;
            if (narrow(source->GetFullName()).find("Default__") != std::string::npos) continue;
            ++report.found;
            UObject* component = nullptr;
            if (!read_object(find_property(source, "Component"), source, component) || !is_ok(component)) {
                if (!wind_missing_logged_) {
                    wind_missing_logged_ = true;
                    log(LogLevel::Warn, "风源没有组件属性 Component，归零跳过");
                }
                continue;
            }
            WindParamBackup backup;
            backup.component = component;
            struct WindEntry {
                const char* name;
                bool* has;
                double* value;
            };
            WindEntry entries[4] = {
                {"Strength", &backup.has_strength, &backup.strength},
                {"Speed", &backup.has_speed, &backup.speed},
                {"MinGustAmt", &backup.has_min_gust, &backup.min_gust},
                {"MaxGustAmt", &backup.has_max_gust, &backup.max_gust},
            };
            bool touched = false;
            for (const WindEntry& entry : entries) {
                FProperty* prop = find_property(component, entry.name);
                *entry.has = read_number(prop, component, *entry.value);
                if (*entry.has) {
                    write_number(prop, component, 0.0);
                    touched = true;
                }
            }
            wind_backups_.push_back(backup);
            if (touched) ++report.acted;
        }
        char buf[160]{};
        std::snprintf(buf, sizeof(buf), "风源归零：风源 %d 个、归零 %d 个", report.found, report.acted);
        log(LogLevel::Info, buf);  // 找不到风源只记录：有的场景没风，摆动另有来源（看 MPC 诊断）
    } else {
        for (const WindParamBackup& backup : wind_backups_) {
            if (!is_ok(backup.component)) continue;
            struct WindEntry {
                const char* name;
                bool has;
                double value;
            };
            WindEntry entries[4] = {
                {"Strength", backup.has_strength, backup.strength},
                {"Speed", backup.has_speed, backup.speed},
                {"MinGustAmt", backup.has_min_gust, backup.min_gust},
                {"MaxGustAmt", backup.has_max_gust, backup.max_gust},
            };
            for (const WindEntry& entry : entries) {
                if (!entry.has) continue;
                write_number(find_property(backup.component, entry.name), backup.component, entry.value);
            }
            ++report.acted;
        }
        const int restored = report.acted;
        wind_backups_.clear();
        if (restored > 0) {
            char buf[120]{};
            std::snprintf(buf, sizeof(buf), "风源已还原：%d 个", restored);
            log(LogLevel::Info, buf);
        }
    }
    return report;
}

int Ue4ssPort::mpc_take_snapshot() {
    mpc_snapshots_.clear();
    mpc_bad_params_.clear();
    mpc_bad_collections_.clear();
    mpc_hold_last_ = {};
    UObject* statics = kismet_material_library();
    UObject* scene = world();
    if (!is_ok(statics) || !is_ok(scene)) {
        if (!mpc_unavailable_logged_) {
            mpc_unavailable_logged_ = true;
            log(LogLevel::Warn, "MPC 钉住不可用：KismetMaterialLibrary/世界不可达");
        }
        return 0;
    }
    std::vector<UObject*> collections;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("MaterialParameterCollection"), collections);
    int total = 0;
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
        }
        total += static_cast<int>(snapshot.params.size());
        mpc_snapshots_.push_back(std::move(snapshot));
    }
    char buf[160]{};
    std::snprintf(buf, sizeof(buf), "MPC 快照：%zu 个集合、%d 个参数（冻结期每帧写回）",
                  mpc_snapshots_.size(), total);
    log(LogLevel::Info, buf);
    if (total == 0 && !mpc_snapshots_.empty()) {
        // 快照 0 参数：链路上有两个环节可能断（参数名提取 / Kismet 取值），逐环节打证据。
        // ① 取值函数存在性 + 真实签名：函数缺失或签名不符 → 每个参数读值失败 → total 恒 0
        UObject* statics = kismet_material_library();
        if (is_ok(statics)) {
            for (const char* fn : {"GetScalarParameterValue", "GetVectorParameterValue"}) {
                UFunction* function = find_function(statics, fn);
                if (function == nullptr) {
                    log(LogLevel::Warn, std::string("MPC 诊断：KismetMaterialLibrary.") + fn + " 不存在");
                } else {
                    log(LogLevel::Info, std::string("MPC 诊断：") + fn + " 签名=" + describe_params(function));
                }
            }
        } else {
            log(LogLevel::Warn, "MPC 诊断：KismetMaterialLibrary CDO 不可达");
        }
        // ② 前三个集合：参数名提取数量 + 数组头原始内存（Data 指针 / +8 / +12）。
        //    原始 Num>0 而提名=0 → 数组遍历环节断；提名>0 而快照 0 → 取值环节断；两者都 0 → 集合真空参数
        for (size_t i = 0; i < mpc_snapshots_.size() && i < 3; ++i) {
            const MpcCollectionSnapshot& snap = mpc_snapshots_[i];
            std::vector<std::string> scalar_names;
            mpc_read_param_names(snap.collection, "ScalarParameters", scalar_names);
            auto* array_prop = as_array_property(find_property(snap.collection, "ScalarParameters"));
            char detail[160]{};
            if (array_prop != nullptr) {
                const void* arr = array_prop->ContainerPtrToValuePtr<void>(static_cast<const void*>(snap.collection));
                const uintptr_t data_ptr = *static_cast<const uintptr_t*>(arr);
                const int32 raw_num = *reinterpret_cast<const int32*>(static_cast<const uint8_t*>(arr) + 8);
                const int32 raw_max = *reinterpret_cast<const int32*>(static_cast<const uint8_t*>(arr) + 12);
                std::snprintf(detail, sizeof(detail), "数组头[Data=%llx Num=%d Max=%d] 提名=%zu",
                              static_cast<unsigned long long>(data_ptr), raw_num, raw_max, scalar_names.size());
            } else {
                std::snprintf(detail, sizeof(detail), "ScalarParameters 属性未找到或非数组");
            }
            log(LogLevel::Info, "MPC 诊断：" + snap.name + " " + detail);
        }
    }
    return total;
}

int Ue4ssPort::mpc_hold_snapshot() {
    if (mpc_snapshots_.empty()) return 0;
    // 限频到 4Hz：原来每帧对 34 集合/347 参数逐个 ProcessEvent，开销和崩溃面都太大。
    // 冻结诊断整场都是“变化:无”，4Hz 足够压住偶发漂移，同时把 ProcessEvent 降到约 1/15。
    const auto now = std::chrono::steady_clock::now();
    if (mpc_hold_last_.time_since_epoch().count() != 0 &&
        now - mpc_hold_last_ < std::chrono::milliseconds(250)) {
        return 0;
    }
    mpc_hold_last_ = now;

    UObject* statics = kismet_material_library();
    UObject* scene = world();
    if (!is_ok(statics) || !is_ok(scene)) return 0;
    int written = 0;
    for (const MpcCollectionSnapshot& snapshot : mpc_snapshots_) {
        if (!is_ok(snapshot.collection)) continue;
        // 复核集合类：FindAllOf 按父类匹配，冻结期间对象也可能被卸载/替换；
        // 不是 MaterialParameterCollection 就整体跳过，避免拿实例/过期指针去 ProcessEvent。
        if (!mpc_is_collection_class(snapshot.collection->GetClassPrivate())) {
            if (mpc_bad_collections_.insert(snapshot.name).second) {
                log(LogLevel::Warn, "MPC 写回跳过：集合类名不符或已失效 " + snapshot.name);
            }
            continue;
        }
        for (const MpcParamValue& param : snapshot.params) {
            const std::string key = snapshot.name + "\x1f" + param.name;
            if (mpc_bad_params_.count(key) != 0) continue;
            bool faulted = false;
            const bool ok = param.is_vector
                                ? mpc_set_vector(statics, scene, snapshot.collection, param.name, param.value, faulted)
                                : mpc_set_scalar(statics, scene, snapshot.collection, param.name,
                                                 param.value[0], faulted);
            if (ok) {
                ++written;
            } else if (faulted) {
                // SEH 抓到游戏侧访问违例：隔离该参数，后续不再写，避免反复触发
                mpc_bad_params_.insert(key);
                log(LogLevel::Warn, "MPC 写回触发游戏访问违例，已隔离参数：" + snapshot.name + "." + param.name);
            }
        }
    }
    return written;
}

void Ue4ssPort::mpc_release_snapshot() {
    mpc_snapshots_.clear();
    mpc_bad_params_.clear();
    mpc_bad_collections_.clear();
    mpc_hold_last_ = {};
    mpc_diag_last_.clear();
    mpc_diag_has_last_ = false;
}

// 每帧读相机管理器算出的最终 POV（ViewTarget.POV，含游戏自定义管理器加的一切东西）。
// 与摄影相机 Actor 的静态位姿对照：暂停期 POV 仍在动 = 有代码在继续推视角。
// camera_idle=false（该帧用户在转/移相机）不累计并清样本——监测值只来自“没人动相机”的连续帧。
void Ue4ssPort::pov_watch_tick(bool camera_idle) {
    if (!camera_idle) {
        pov_watch_has_last_ = false;
        return;
    }
    UObject* manager = camera_manager();
    if (!is_ok(manager)) { pov_watch_has_last_ = false; return; }
    FProperty* view_target_prop = find_property(manager, "ViewTarget");
    void* view_target = struct_member_base(view_target_prop, manager);
    if (view_target == nullptr) { pov_watch_has_last_ = false; return; }
    FProperty* pov_prop = struct_member(view_target_prop, "POV");
    void* pov = struct_member_base(pov_prop, view_target);
    if (pov == nullptr) { pov_watch_has_last_ = false; return; }
    FProperty* loc_prop = struct_member(pov_prop, "Location");
    void* loc_base = struct_member_base(loc_prop, pov);
    FProperty* rot_prop = struct_member(pov_prop, "Rotation");
    void* rot_base = struct_member_base(rot_prop, pov);
    if (loc_base == nullptr || rot_base == nullptr) { pov_watch_has_last_ = false; return; }
    double loc[3]{};
    double rot[3]{};
    if (!read_number(struct_member(loc_prop, "X"), loc_base, loc[0]) ||
        !read_number(struct_member(loc_prop, "Y"), loc_base, loc[1]) ||
        !read_number(struct_member(loc_prop, "Z"), loc_base, loc[2]) ||
        !read_number(struct_member(rot_prop, "Pitch"), rot_base, rot[0]) ||
        !read_number(struct_member(rot_prop, "Yaw"), rot_base, rot[1]) ||
        !read_number(struct_member(rot_prop, "Roll"), rot_base, rot[2])) {
        pov_watch_has_last_ = false;
        return;
    }
    if (pov_watch_has_last_) {
        const double dloc = std::max({std::fabs(loc[0] - pov_last_loc_[0]), std::fabs(loc[1] - pov_last_loc_[1]),
                                      std::fabs(loc[2] - pov_last_loc_[2])});
        const double drot = std::max({std::fabs(rot[0] - pov_last_rot_[0]), std::fabs(rot[1] - pov_last_rot_[1]),
                                      std::fabs(rot[2] - pov_last_rot_[2])});
        if (dloc > pov_max_loc_delta_) pov_max_loc_delta_ = dloc;
        if (drot > pov_max_rot_delta_) pov_max_rot_delta_ = drot;
    }
    pov_last_loc_[0] = loc[0];
    pov_last_loc_[1] = loc[1];
    pov_last_loc_[2] = loc[2];
    pov_last_rot_[0] = rot[0];
    pov_last_rot_[1] = rot[1];
    pov_last_rot_[2] = rot[2];
    pov_watch_has_last_ = true;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
