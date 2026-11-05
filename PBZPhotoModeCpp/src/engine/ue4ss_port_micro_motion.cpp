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

// 把场景里所有 MaterialParameterCollection 的标量/向量参数读进快照数组。
// 冻结快照（mpc_take_snapshot）与冻结诊断（freeze_diag_sample）共用这一份采集：
// 两处原来逐行同构，唯一差别是诊断侧多一步与上一拍的 diff。
int Ue4ssPort::scan_mpc_collections(UObject* statics, UObject* scene, std::vector<MpcCollectionSnapshot>& out) {
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
        out.push_back(std::move(snapshot));
    }
    return total;
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
    const int total = scan_mpc_collections(statics, scene, mpc_snapshots_);
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
    // 全量写回限频到 4Hz：原来每帧对 34 集合/347 参数逐个 ProcessEvent，开销和崩溃面都太大。
    // 但冻结诊断里反复出现的只有 UltraDynamicWeather 的 CameraFadeStartLocation / GrassInteractor0
    // 两个热点参数——它们在两轮 4Hz 写回之间仍会被游戏改写，足以让草地/天气材质持续微动。
    // 所以这两个参数逐帧写回（2 次 ProcessEvent/帧），其余仍保持 4Hz。
    const auto now = std::chrono::steady_clock::now();
    const bool full_due = mpc_hold_last_.time_since_epoch().count() == 0 ||
                          now - mpc_hold_last_ >= std::chrono::milliseconds(250);

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
        const bool weather_collection = snapshot.name.find("UltraDynamicWeather") != std::string::npos;
        const bool water_collection = snapshot.name.find("MPC_Water") != std::string::npos;
        for (const MpcParamValue& param : snapshot.params) {
            // 实机日志先后出现过 GrassInteractor0/1/2（多个角色/NPC 的草地交互子）、
            // CameraFadeStartLocation、FieldRefOrigin/FieldOriginRel（风场/树叶）以及
            // MPC_Water.PrevTime；按前缀全部纳入逐帧写回，避免白名单追不上实例编号。
            const bool hot_param =
                (weather_collection && (param.name.rfind("GrassInteractor", 0) == 0 ||
                                        param.name.rfind("CameraFade", 0) == 0 ||
                                        param.name.rfind("FieldRef", 0) == 0 ||
                                        param.name.rfind("FieldOrigin", 0) == 0)) ||
                (water_collection && param.name == "PrevTime");
            if (!full_due && !hot_param) continue;
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
    if (full_due) mpc_hold_last_ = now;
    // 全量写回本身不逐条打日志；每 10 秒汇总一次实际写入数，避免把“快照参数数”误当“写回成功数”。
    if (mpc_hold_log_last_.time_since_epoch().count() == 0 ||
        now - mpc_hold_log_last_ >= std::chrono::seconds(10)) {
        mpc_hold_log_last_ = now;
        log(written > 0 ? LogLevel::Info : LogLevel::Warn,
            "MPC 写回：本轮成功 " + std::to_string(written) + " 个参数（快照 " +
                std::to_string(mpc_snapshots_.size()) + " 个集合）");
    }
    return written;
}

void Ue4ssPort::mpc_release_snapshot() {
    mpc_snapshots_.clear();
    mpc_bad_params_.clear();
    mpc_bad_collections_.clear();
    mpc_hold_last_ = {};
    mpc_hold_log_last_ = {};
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

// =============================================================================
//  渲染对照（A/B）：临时改 CVar，定位 TSR/TAA 或 WPO 假速度
// =============================================================================
namespace {

// kismet_system_library / get_cvar_int / exec_console_command 已提到 detail（相机后端的景深自检
// 也要读 r.DepthOfFieldQuality），这里不再各留一份。

auto array_count(UObject* owner, const char* property_name) -> int {
    FProperty* property = find_property(owner, property_name);
    auto* array_property = as_array_property(property);
    if (array_property == nullptr) return -1;
    const void* container = owner;
    FScriptArrayHelper_InContainer helper(array_property, container);
    return helper.Num();
}

auto has_active_clothing_assets(UObject* component) -> bool {
    // 优先读组件上的 MeshClothingAssets（部分版本在 SkeletalMeshComponent 上），
    // 没有再问 HasActiveClothingAssets；最后回退到 SkeletalMesh 资产上的
    // MeshClothingAssets 数组（UE5 的服装资产实际挂在 SkeletalMesh 资产上）。
    const int count = array_count(component, "MeshClothingAssets");
    if (count > 0) return true;
    bool active = false;
    if (invoke_bool(component, "HasActiveClothingAssets", [](ParamBlock&) { return true; }, active) && active) {
        return true;
    }
    UObject* mesh = nullptr;
    if (read_object(find_property(component, "SkeletalMesh"), component, mesh) && is_ok(mesh)) {
        const int asset_count = array_count(mesh, "MeshClothingAssets");
        if (asset_count > 0) return true;
    }
    return false;
}

constexpr const char* kClothSuspendSkeletal = "SuspendClothingSimulation";
constexpr const char* kClothResumeSkeletal = "ResumeClothingSimulation";
constexpr const char* kClothSuspendedSkeletal = "IsClothingSimulationSuspended";
constexpr const char* kClothSuspendChaos = "SuspendSimulation";
constexpr const char* kClothResumeChaos = "ResumeSimulation";
constexpr const char* kClothSuspendedChaos = "IsSimulationSuspended";

auto pp_read(FProperty* settings, void* base, const char* name, double& out) -> bool {
    if (settings == nullptr || base == nullptr) return false;
    return read_number(struct_member(settings, name), base, out);
}

auto pp_write(FProperty* settings, void* base, const char* name, double value) -> bool {
    if (settings == nullptr || base == nullptr) return false;
    return write_number(struct_member(settings, name), base, value);
}

auto cvar_state_text(std::initializer_list<const char*> names) -> std::string {
    std::string out;
    for (const char* name : names) {
        int value = 0;
        if (!get_cvar_int(name, value)) continue;
        if (!out.empty()) out += "，";
        out += std::string(name) + "=" + std::to_string(value);
    }
    return out;
}

auto render_state_text() -> std::string {
    return cvar_state_text({"r.AntiAliasingMethod",
                            "r.Velocity.EnableVertexDeformation",
                            "r.Velocity.EnableLandscapeGrass",
                            "r.HairStrands.ForceVelocityOutput",
                            "r.HairStrands.Simulation",
                            "r.MotionBlurQuality",
                            // 景深走哪条管线由它决定：低档位时 fstop/sensor 不参与计算，
                            // 写作「景深调数值没反应」（见 hotfix7 景深自检）。
                            "r.DepthOfFieldQuality",
                            "sg.DepthOfFieldQuality",
                            "r.NGX.DLSS.Enable",
                            "r.Streamline.DLSSG.Enable",
                            "r.FidelityFX.FSR.Enabled",
                            "r.FidelityFX.FI.Enabled",
                            "r.PBZ.SuppressFrameGen",
                            "r.Nanite.SkeletalMesh.AllowWPO",
                            "r.MorphTarget.Enable",
                            "r.MorphTarget.ForceUpdate",
                            "r.XeSS.Enabled",
                            "r.XeFG.Enabled",
                            "r.Tonemapper.Quality",
                            "r.BackbufferQuantizationDitheringOverride"});
}

auto render_runtime_text_free() -> std::string {
    return cvar_state_text({"cr.PBZ.FrameGeneration.Backend",
                            "r.PBZ.FrameGeneration.Ready",
                            "r.PBZ.SuppressFrameGen",
                            "r.Streamline.DLSSG.Enable",
                            "r.Streamline.DLSSG.FramesToGenerate",
                            "r.NGX.DLSS.Enable",
                            "r.XeSS.Enabled",
                            "r.XeFG.Enabled",
                            "r.Nanite.SkeletalMesh.AllowWPO",
                            "r.MorphTarget.Enable",
                            "r.MorphTarget.ForceUpdate",
                            "r.HairStrands.Simulation",
                            "r.AntiAliasingMethod",
                            "r.Tonemapper.Quality"});
}

auto lighting_state_text() -> std::string {
    return cvar_state_text({"r.DynamicGlobalIlluminationMethod",
                            "r.ReflectionMethod",
                            "r.Lumen.DiffuseIndirect.Allow",
                            "r.Lumen.Reflections.Allow",
                            "r.Lumen.ScreenProbeGather.ScreenTraces",
                            "r.Lumen.Reflections.ScreenTraces",
                            "r.Lumen.ScreenProbeGather.ShortRangeAO",
                            "r.Lumen.DiffuseIndirect.SSAO",
                            "r.AmbientOcclusionLevels",
                            "r.Lumen.ScreenProbeGather.RadianceCache",
                            "r.Lumen.ScreenProbeGather.FixedJitterIndex",
                            "r.Lumen.ScreenProbeGather.FixedStateFrameIndex",
                            "r.Lumen.Reflections.FixedStateFrameIndex"});
}


// 材质参数诊断：列出 MaterialInstanceDynamic 上名字带 time/wind/sway/flow 等关键词的
// 标量/向量参数（只读，不写回），用于判断剩余闪烁是否来自 BP 每帧驱动的 DMI 参数。
auto material_param_diag_text() -> std::string {
    std::vector<UObject*> dmis;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("MaterialInstanceDynamic"), dmis);
    int dmi_count = 0;
    int scalar_matches = 0;
    int vector_matches = 0;
    std::string scalar_text;
    std::string vector_text;
    auto lower_copy = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    auto keyword_hit = [&lower_copy](const std::string& name) {
        const std::string lower_name = lower_copy(name);
        return lower_name.find("time") != std::string::npos || lower_name.find("wind") != std::string::npos ||
               lower_name.find("sway") != std::string::npos || lower_name.find("flow") != std::string::npos ||
               lower_name.find("wave") != std::string::npos || lower_name.find("phase") != std::string::npos ||
               lower_name.find("swing") != std::string::npos || lower_name.find("offset") != std::string::npos ||
               lower_name.find("noise") != std::string::npos || lower_name.find("strength") != std::string::npos;
    };
    for (UObject* dmi : dmis) {
        if (!is_ok(dmi)) continue;
        if (narrow(dmi->GetFullName()).find("Default__") != std::string::npos) continue;
        if (++dmi_count > 250) break;

        FProperty* scalar_array_prop = find_property(dmi, "ScalarParameterValues");
        if (auto* array_prop = as_array_property(scalar_array_prop)) {
            FScriptArrayHelper_InContainer helper(array_prop, static_cast<const void*>(dmi));
            const int32 count = helper.Num();
            for (int32 i = 0; i < count && i < 64; ++i) {
                void* element = helper.GetRawPtr(i);
                std::string name;
                double value = 0.0;
                if (!read_struct_name(array_prop->GetInner(), element, "ParameterName", name)) continue;
                if (!keyword_hit(name)) continue;
                read_struct_number(array_prop->GetInner(), element, "ParameterValue", value);
                if (!scalar_text.empty()) scalar_text += "，";
                char buf[96]{};
                std::snprintf(buf, sizeof(buf), "%s=%.3f", name.c_str(), value);
                scalar_text += buf;
                ++scalar_matches;
                if (scalar_text.size() > 700) break;
            }
        }
        FProperty* vector_array_prop = find_property(dmi, "VectorParameterValues");
        if (auto* array_prop = as_array_property(vector_array_prop)) {
            FScriptArrayHelper_InContainer helper(array_prop, static_cast<const void*>(dmi));
            const int32 count = helper.Num();
            for (int32 i = 0; i < count && i < 64; ++i) {
                void* element = helper.GetRawPtr(i);
                std::string name;
                if (!read_struct_name(array_prop->GetInner(), element, "ParameterName", name)) continue;
                if (!keyword_hit(name)) continue;
                if (!vector_text.empty()) vector_text += "，";
                vector_text += name;
                ++vector_matches;
                if (vector_text.size() > 400) break;
            }
        }
    }
    if (dmi_count == 0) return "材质参数候选：未找到 MaterialInstanceDynamic";
    char head[160]{};
    std::snprintf(head, sizeof(head), "材质参数候选：DMI=%d，标量匹配=%d，向量匹配=%d", dmi_count, scalar_matches,
                  vector_matches);
    std::string out = head;
    if (!scalar_text.empty()) out += "；标量：" + scalar_text;
    if (!vector_text.empty()) out += "；向量：" + vector_text;
    return out;
}

// 部分引擎 setter 的参数名在不同版本会变（bNewValue/NewValue/...）；名字全不中时
// 退化为“写第一个 bool 参数”，仍由反射类型校验，不按内存偏移硬写。
auto set_first_bool_param(ParamBlock& params, bool value) -> bool {
    UFunction* function = params.function();
    if (function == nullptr) return false;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* property = *it;
        if (property != nullptr && property->IsA<FBoolProperty>()) {
            return write_bool(property, params.data(), value);
        }
    }
    return false;
}

auto shadow_state_text() -> std::string {
    return cvar_state_text({"r.ShadowQuality",
                            "r.Shadow.Virtual.Enable",
                            "r.Shadow.Virtual.SMRT.RayCountDirectional",
                            "r.Shadow.Virtual.SMRT.RayCountLocal",
                            "r.Shadow.Virtual.Cache.DeformableMeshesInvalidate",
                            "r.Shadow.Denoiser",
                            "r.Shadow.Denoiser.TemporalAccumulation",
                            "r.ContactShadows",
                            "r.ContactShadows.Denoise.Enable",
                            "r.VolumetricFog",
                            "r.VolumetricFog.TemporalReprojection",
                            "r.FilmGrain",
                            "r.PostProcessing.DisableMaterials",
                            "r.DepthOfFieldQuality"});
}

}  // namespace

ClothFreezeReport Ue4ssPort::freeze_cloth(bool frozen) {
    ClothFreezeReport report{};
    if (!frozen) {
        int restored = 0;
        int failed = 0;
        std::string failures;
        std::vector<ClothComponentState> remaining;
        for (const ClothComponentState& state : cloth_states_) {
            if (!state.suspended_by_us) continue;
            if (!is_ok(state.component)) continue;  // 旧世界对象已随关卡销毁，无需恢复
            const char* resume = state.chaos ? kClothResumeChaos : kClothResumeSkeletal;
            const char* suspended = state.chaos ? kClothSuspendedChaos : kClothSuspendedSkeletal;
            bool called = invoke_void(state.component, resume, [](ParamBlock&) { return true; });
            bool still_suspended = false;
            const bool query_ok =
                invoke_bool(state.component, suspended, [](ParamBlock&) { return true; }, still_suspended);
            const bool ok = called && (!query_ok || !still_suspended);
            if (ok) {
                ++restored;
            } else {
                ++failed;
                remaining.push_back(state);
                if (failures.size() < 240) {
                    if (!failures.empty()) failures += "；";
                    failures += state.chaos ? "ChaosCloth" : "SkeletalMesh";
                    failures += " 恢复失败";
                }
            }
        }
        cloth_states_ = remaining;
        report.found = restored + failed;
        report.acted = restored;
        report.ok = failed == 0;
        if (restored > 0) {
            char buf[128]{};
            std::snprintf(buf, sizeof(buf), "布料恢复：恢复 %d 个布料模拟", restored);
            log(LogLevel::Info, buf);
        }
        if (failed > 0) log(LogLevel::Warn, "布料恢复部分失败：" + failures);
        return report;
    }

    // 冻结：重新扫描并记录每个组件冻结前的状态。先清空旧状态，避免上一轮残留指针。
    cloth_states_.clear();
    int sk_found = 0;
    int sk_acted = 0;
    int sk_skipped = 0;
    int chaos_found = 0;
    int chaos_acted = 0;
    int chaos_skipped = 0;
    std::string failures;

    auto remember_failure = [&failures](const char* kind, UObject* component, const char* why) {
        if (failures.size() >= 400) return;
        if (!failures.empty()) failures += "；";
        failures += kind;
        failures += " ";
        // 带组件全名（截断尾部）：实机日志里能直接看出挂不住的是玩家还是 NPC 的布料。
        std::string full_name = narrow(component->GetFullName());
        if (full_name.size() > 88) full_name = "…" + full_name.substr(full_name.size() - 86);
        failures += full_name;
        failures += "：";
        failures += why;
    };

    // ① SkeletalMeshComponent 的 clothing simulation（角色裙摆/披风最常见路径）
    {
        std::vector<UObject*> components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("SkeletalMeshComponent"), components);
        for (UObject* component : components) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            if (!has_active_clothing_assets(component)) continue;
            ++sk_found;

            bool was_suspended = false;
            const bool state_known = invoke_bool(component, kClothSuspendedSkeletal,
                                                 [](ParamBlock&) { return true; }, was_suspended);
            if (state_known && was_suspended) {
                ++sk_skipped;
                cloth_states_.push_back({component, false, true, false});
                continue;
            }

            const bool called = invoke_void(component, kClothSuspendSkeletal, [](ParamBlock&) { return true; });
            bool now_suspended = !state_known;  // 查询不可用时以调用成功为准
            if (called && state_known) {
                bool observed = false;
                if (invoke_bool(component, kClothSuspendedSkeletal, [](ParamBlock&) { return true; }, observed)) {
                    now_suspended = observed;
                }
            }
            if (called && now_suspended) {
                ++sk_acted;
                cloth_states_.push_back({component, false, false, true});
            } else {
                remember_failure("SkeletalMesh", component, "SuspendClothingSimulation 未生效");
            }
        }
    }

    // ② UChaosClothComponent 的独立布料模拟（部分裙摆/披风走 ChaosClothAsset）
    {
        std::vector<UObject*> components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("ChaosClothComponent"), components);
        for (UObject* component : components) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            ++chaos_found;

            bool was_suspended = false;
            const bool state_known =
                invoke_bool(component, kClothSuspendedChaos, [](ParamBlock&) { return true; }, was_suspended);
            if (state_known && was_suspended) {
                ++chaos_skipped;
                cloth_states_.push_back({component, true, true, false});
                continue;
            }

            const bool called = invoke_void(component, kClothSuspendChaos, [](ParamBlock&) { return true; });
            bool now_suspended = !state_known;
            if (called && state_known) {
                bool observed = false;
                if (invoke_bool(component, kClothSuspendedChaos, [](ParamBlock&) { return true; }, observed)) {
                    now_suspended = observed;
                }
            }
            if (called && now_suspended) {
                ++chaos_acted;
                cloth_states_.push_back({component, true, false, true});
            } else {
                remember_failure("ChaosCloth", component, "SuspendSimulation 未生效");
            }
        }
    }

    report.found = sk_found + chaos_found;
    report.acted = sk_acted + chaos_acted;
    report.ok = report.found == 0 || report.acted > 0;

    if (report.found == 0) {
        log(LogLevel::Info, "布料冻结：未发现带布料的 SkeletalMesh / ChaosCloth 组件（若裙摆/披风仍摆动，请反馈）");
    } else {
        char buf[320]{};
        std::snprintf(buf, sizeof(buf),
                      "布料冻结：SkeletalMesh 带布料 %d 个（本次挂起 %d，原本已暂停 %d）；"
                      "ChaosCloth %d 个（本次挂起 %d，原本已暂停 %d）",
                      sk_found, sk_acted, sk_skipped, chaos_found, chaos_acted, chaos_skipped);
        log(LogLevel::Info, buf);
    }
    if (!failures.empty()) log(LogLevel::Warn, "布料冻结部分失败：" + failures);
    return report;
}

SkeletalAnimationFreezeReport Ue4ssPort::freeze_skeletal_animation(bool frozen) {
    SkeletalAnimationFreezeReport report{};
    if (!frozen) {
        int restored = 0;
        int failed = 0;
        std::string failures;
        std::vector<SkeletalAnimState> remaining;
        for (const SkeletalAnimState& state : skeletal_anim_states_) {
            if (!is_ok(state.component)) continue;
            bool ok = true;
            FProperty* pause_prop = find_property(state.component, "bPauseAnims");
            FProperty* no_update_prop = find_property(state.component, "bNoSkeletonUpdate");
            if (pause_prop != nullptr && !write_bool(pause_prop, state.component, state.pause_anims)) ok = false;
            if (no_update_prop != nullptr && !write_bool(no_update_prop, state.component, state.no_skeleton_update)) ok = false;
            if (ok) {
                ++restored;
            } else {
                ++failed;
                remaining.push_back(state);
                if (failures.size() < 240) {
                    if (!failures.empty()) failures += "；";
                    failures += "SkeletalMesh 动画恢复失败";
                }
            }
        }
        skeletal_anim_states_ = remaining;
        report.found = restored + failed;
        report.acted = restored;
        report.ok = failed == 0;
        if (restored > 0) {
            char buf[128]{};
            std::snprintf(buf, sizeof(buf), "骨骼动画恢复：恢复 %d 个 SkeletalMesh", restored);
            log(LogLevel::Info, buf);
        }
        if (failed > 0) log(LogLevel::Warn, "骨骼动画恢复部分失败：" + failures);
        return report;
    }

    skeletal_anim_states_.clear();
    int found = 0;
    int acted = 0;
    int playing = 0;
    int failed = 0;
    std::string failures;

    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("SkeletalMeshComponent"), components);
    for (UObject* component : components) {
        if (!is_ok(component)) continue;
        if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
        FProperty* pause_prop = find_property(component, "bPauseAnims");
        FProperty* no_update_prop = find_property(component, "bNoSkeletonUpdate");
        if (pause_prop == nullptr && no_update_prop == nullptr) continue;
        ++found;

        double pause_value = 0.0;
        double no_update_value = 0.0;
        const bool has_pause = pause_prop != nullptr && read_number(pause_prop, component, pause_value);
        const bool has_no_update = no_update_prop != nullptr && read_number(no_update_prop, component, no_update_value);
        bool is_playing = false;
        if (invoke_bool(component, "IsPlaying", [](ParamBlock&) { return true; }, is_playing) && is_playing) {
            ++playing;
        }

        const bool already_paused = (!has_pause || pause_value != 0.0) && (!has_no_update || no_update_value != 0.0);
        if (already_paused) continue;

        bool ok = true;
        if (pause_prop != nullptr && !write_bool(pause_prop, component, true)) ok = false;
        if (no_update_prop != nullptr && !write_bool(no_update_prop, component, true)) ok = false;
        if (ok) {
            ++acted;
            skeletal_anim_states_.push_back({component, has_pause && pause_value != 0.0,
                                             has_no_update && no_update_value != 0.0});
        } else {
            ++failed;
            if (failures.size() < 240) {
                if (!failures.empty()) failures += "；";
                failures += "SkeletalMesh 动画暂停失败";
            }
        }
    }

    report.found = found;
    report.acted = acted;
    report.ok = failed == 0 || acted > 0;
    if (found == 0) {
        log(LogLevel::Info, "骨骼动画冻结：未发现 SkeletalMeshComponent");
    } else {
        char buf[240]{};
        std::snprintf(buf, sizeof(buf), "骨骼动画冻结：SkeletalMesh %d 个（播放中 %d，本次暂停 %d）", found, playing, acted);
        log(LogLevel::Info, buf);
    }
    if (failed > 0) log(LogLevel::Warn, "骨骼动画冻结部分失败：" + failures);
    return report;
}

bool Ue4ssPort::freeze_motion_blur(bool frozen) {
    if (!frozen) {
        if (!motion_blur_active_) return true;
        int current = 0;
        if (!get_cvar_int("r.MotionBlurQuality", current)) return false;
        if (!exec_console_command("r.MotionBlurQuality " + std::to_string(motion_blur_original_), world())) return false;
        if (!get_cvar_int("r.MotionBlurQuality", current) || current != motion_blur_original_) return false;
        motion_blur_active_ = false;
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "动态模糊已还原：r.MotionBlurQuality=%d", motion_blur_original_);
        log(LogLevel::Info, buf);
        return true;
    }

    int original = 0;
    if (!get_cvar_int("r.MotionBlurQuality", original)) return false;
    if (original == 0) {
        motion_blur_active_ = false;
        return true;
    }
    if (!exec_console_command("r.MotionBlurQuality 0", world())) return false;
    int current = 0;
    if (!get_cvar_int("r.MotionBlurQuality", current) || current != 0) return false;
    motion_blur_original_ = original;
    motion_blur_active_ = true;
    char buf[128]{};
    std::snprintf(buf, sizeof(buf), "动态模糊已关闭：r.MotionBlurQuality %d→0", original);
    log(LogLevel::Info, buf);
    return true;
}

RenderTuningReport Ue4ssPort::freeze_render_tuning(bool frozen, bool disable_aa, bool disable_wpo_velocity,
                                                   bool disable_framegen, bool disable_dlss,
                                                   bool disable_tonemapper_dither) {
    RenderTuningReport report{};
    if (!frozen) {
        std::vector<RenderCvarSnapshot> remaining;
        std::string restored_text;
        for (const RenderCvarSnapshot& snapshot : render_cvar_snapshots_) {
            bool restored = false;
            int current = 0;
            if (exec_console_command(snapshot.name + " " + std::to_string(snapshot.original), world()) &&
                get_cvar_int(snapshot.name, current) && current == snapshot.original) {
                restored = true;
                ++report.restored;
                if (!restored_text.empty()) restored_text += "；";
                restored_text += snapshot.name + " 恢复 " + std::to_string(snapshot.original);
            }
            if (!restored) remaining.push_back(snapshot);
        }
        render_cvar_snapshots_ = remaining;
        apply_post_process_override(false);
        report.ok = remaining.empty();
        if (report.restored > 0) log(LogLevel::Info, "渲染对照已还原：" + restored_text);
        if (!report.ok) log(LogLevel::Warn, "渲染对照还原失败：有 CVar 未能写回原值，退出时会重试");
        return report;
    }

    render_reassert_logged_ = 0;
    const std::string state = render_state_text();
    if (!state.empty()) {
        log(LogLevel::Info, "渲染状态：" + state);
    } else if (!render_cvar_probe_logged_) {
        render_cvar_probe_logged_ = true;
        log(LogLevel::Warn, "渲染状态不可读（GetConsoleVariableIntValue 不可用），渲染对照跳过");
    }

    if (!disable_aa && !disable_wpo_velocity && !disable_framegen && !disable_dlss && !disable_tonemapper_dither) {
        report.ok = !state.empty();
        return report;
    }

    struct Target {
        const char* name;
        int value;
    };
    std::vector<Target> targets;
    if (disable_aa) {
        // 关时域 AA（TSR/TAA 历史），但保留 FXAA 空间抗锯齿：树叶/布料这类细边缘在
        // “无 AA”下会因像素走样持续闪烁，FXAA 不依赖历史帧，不会产生重影，只做边缘平滑。
        targets.push_back({"r.AntiAliasingMethod", 1});
    }
    if (disable_wpo_velocity) {
        targets.push_back({"r.Velocity.EnableVertexDeformation", 0});
        targets.push_back({"r.Velocity.EnableLandscapeGrass", 0});
        targets.push_back({"r.HairStrands.ForceVelocityOutput", 0});
        // Nanite 骨骼网格的 WPO 是独立路径，SetEvaluateWorldPositionOffset(false) 管不到；
        // 这里只关 Nanite Skeletal WPO。
        // 注意：r.MorphTarget.Enable/ForceUpdate 曾一并塞进本批，但它是“全局关 morph 求值”——
        // 会让布料姿态修正/身体皮肤 morph 一次性消失（表现为布料/皮肤在进入冻结后改位置），
        // 与这里“关速度输出”的用途无关，已移除（1.0.76）。
        targets.push_back({"r.Nanite.SkeletalMesh.AllowWPO", 0});
        // 毛发 strand 模拟/渲染也有独立的每帧路径；关闭模拟，保留发丝当前姿态
        targets.push_back({"r.HairStrands.Simulation", 0});
    }
    if (disable_framegen) {
        // 顺序很重要：先关后端 DLSSG/FSR-FI，再设游戏总开关。
        // 若先设 r.PBZ.SuppressFrameGen，游戏可能立刻把 DLSSG 改成 0，导致后面的
        // DLSSG 目标原值==目标值被跳过、日志看不到实际应用；先设后端能留下清晰证据。
        targets.push_back({"r.Streamline.DLSSG.Enable", 0});
        targets.push_back({"r.FidelityFX.FI.Enabled", 0});
        targets.push_back({"r.PBZ.SuppressFrameGen", 1});
    }
    if (disable_dlss) {
        targets.push_back({"r.NGX.DLSS.Enable", 0});
        // 游戏可能走 XeSS/XeFG 而不是 DLSS；一起关掉，避免留下另一个时域超分器。
        targets.push_back({"r.XeSS.Enabled", 0});
        targets.push_back({"r.XeFG.Enabled", 0});
    }
    if (disable_tonemapper_dither) {
        // 只有原值 >=5 才降到 4（5=+GrainJitter，4=+Grain）；原值更低就保持，
        // 绝不要把 2 这种低档位反向拉高（会把 Grain 打开）。
        int quality = 0;
        if (get_cvar_int("r.Tonemapper.Quality", quality)) {
            const int quality_target = quality < 4 ? quality : 4;
            if (quality != quality_target) targets.push_back({"r.Tonemapper.Quality", quality_target});
        }
        targets.push_back({"r.BackbufferQuantizationDitheringOverride", 0});
        targets.push_back({"r.FilmGrain", 0});
    }

    std::string applied_text;
    std::string failed_text;
    for (const Target& target : targets) {
        bool already_snapshotted = false;
        for (const RenderCvarSnapshot& snapshot : render_cvar_snapshots_) {
            if (snapshot.name == target.name) {
                already_snapshotted = true;
                break;
            }
        }
        if (already_snapshotted) continue;

        int original = 0;
        if (!get_cvar_int(target.name, original)) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 读不到";
            continue;
        }
        if (original == target.value) continue;

        const auto apply_begin = std::chrono::steady_clock::now();
        if (!exec_console_command(std::string(target.name) + " " + std::to_string(target.value), world())) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 命令执行失败";
            continue;
        }
        const auto exec_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - apply_begin)
                                 .count();
        // 渲染状态类 CVar 写入可能触发渲染线程排空，是进入冻结时几十到几百 ms 卡顿的来源。
        // 只报慢项，正常项不刷日志。
        if (exec_ms >= 50) {
            log(LogLevel::Warn, "渲染 CVar 写入耗时：" + std::string(target.name) + " " + std::to_string(exec_ms) +
                                    " ms（疑似渲染线程排空）");
        }
        int current = 0;
        if (!get_cvar_int(target.name, current) || current != target.value) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 回读不是目标值";
            continue;
        }

        render_cvar_snapshots_.push_back({target.name, original, target.value});
        ++report.applied;
        if (!applied_text.empty()) applied_text += "；";
        applied_text += std::string(target.name) + " " + std::to_string(original) + "→" +
                        std::to_string(target.value);
    }

    if (disable_aa) {
        // r.AntiAliasingMethod 只是“默认值”；真正生效的 AA/Lumen 方法可能由 PPV 或相机 POV
        // 的 FPostProcessSettings override 决定，必须直接改那些结构体。
        apply_post_process_override(true);
        // 即使所有 CVar 原本就是目标值，只要 PPV/POV 覆盖确实写入，也要让 core 记成“渲染对照已激活”，
        // 否则解冻时不会调用 freeze_render_tuning(false) 还原 PPV。
        if (report.applied == 0 && !post_process_states_.empty()) report.applied = 1;
    }

    report.ok = !state.empty();
    if (report.applied > 0) {
        log(LogLevel::Info, "渲染对照已应用：" + applied_text + "（解冻/退出按原值还原）");
    }
    const std::string after_state = render_state_text();
    if (!after_state.empty()) {
        log(LogLevel::Info, "渲染对照后状态：" + after_state);
    }
    if (!failed_text.empty()) {
        log(LogLevel::Warn, "渲染对照部分项目未生效：" + failed_text);
    }
    return report;
}

std::string Ue4ssPort::render_runtime_text() {
    return render_runtime_text_free();
}

void Ue4ssPort::render_reassert_tick() {
    if (render_cvar_snapshots_.empty()) return;
    int changed = 0;
    for (const RenderCvarSnapshot& snapshot : render_cvar_snapshots_) {
        int current = 0;
        if (!get_cvar_int(snapshot.name, current)) continue;
        if (current == snapshot.target) continue;
        if (!exec_console_command(snapshot.name + " " + std::to_string(snapshot.target), world())) continue;
        int after = 0;
        if (get_cvar_int(snapshot.name, after) && after == snapshot.target) ++changed;
    }
    if (changed > 0 && render_reassert_logged_ < 8) {
        ++render_reassert_logged_;
        char buf[160]{};
        std::snprintf(buf, sizeof(buf), "渲染对照重申：游戏把 %d 个 CVar 改写，已压回目标值", changed);
        log(LogLevel::Info, buf);
    }
}

void Ue4ssPort::apply_post_process_override(bool enabled) {
    if (!enabled) {
        int restored = 0;
        int failed = 0;
        for (const PostProcessOverrideState& state : post_process_states_) {
            if (state.is_pov || state.volume == nullptr) continue;  // POV 每帧重建，无需写回
            if (!is_ok(state.volume)) continue;
            FProperty* settings = find_property(state.volume, "Settings");
            void* base = struct_member_base(settings, state.volume);
            if (settings == nullptr || base == nullptr) {
                ++failed;
                continue;
            }
            bool ok = true;
            if (state.has_aa) {
                ok = pp_write(settings, base, "bOverride_AntiAliasingMethod", state.override_aa) &&
                     pp_write(settings, base, "AntiAliasingMethod", state.aa_method) && ok;
            }
            if (state.has_mb) {
                ok = pp_write(settings, base, "bOverride_MotionBlurAmount", state.override_mb_amount) &&
                     pp_write(settings, base, "MotionBlurAmount", state.mb_amount) &&
                     pp_write(settings, base, "bOverride_MotionBlurMax", state.override_mb_max) &&
                     pp_write(settings, base, "MotionBlurMax", state.mb_max) && ok;
            }
            if (state.has_gi) {
                ok = pp_write(settings, base, "bOverride_DynamicGlobalIlluminationMethod", state.override_gi) &&
                     pp_write(settings, base, "DynamicGlobalIlluminationMethod", state.gi_method) && ok;
            }
            if (state.has_reflection) {
                ok = pp_write(settings, base, "bOverride_ReflectionMethod", state.override_reflection) &&
                     pp_write(settings, base, "ReflectionMethod", state.reflection_method) && ok;
            }
            if (state.has_grain) {
                ok = pp_write(settings, base, "bOverride_FilmGrainIntensity", state.override_grain) &&
                     pp_write(settings, base, "FilmGrainIntensity", state.grain_intensity) && ok;
            }
            if (ok) {
                ++restored;
            } else {
                ++failed;
            }
        }
        post_process_states_.clear();
        if (restored > 0 || failed > 0) {
            char buf[160]{};
            std::snprintf(buf, sizeof(buf), "后处理覆盖恢复：体积 %d 个成功、%d 个失败", restored, failed);
            log(LogLevel::Info, buf);
        }
        return;
    }

    post_process_states_.clear();
    int volume_count = 0;
    int volumes_with_override = 0;
    int aa_count = 0;
    int mb_count = 0;
    int gi_count = 0;
    int reflection_count = 0;
    int grain_count = 0;
    bool pov_has_aa = false;

    auto capture_and_apply = [&](UObject* owner, bool is_pov, FProperty* settings, void* base) -> bool {
        if (settings == nullptr || base == nullptr) return false;
        PostProcessOverrideState state{};
        state.volume = owner;
        state.is_pov = is_pov;
        state.has_aa = pp_read(settings, base, "bOverride_AntiAliasingMethod", state.override_aa) &&
                       pp_read(settings, base, "AntiAliasingMethod", state.aa_method);
        state.has_mb = pp_read(settings, base, "bOverride_MotionBlurAmount", state.override_mb_amount) &&
                       pp_read(settings, base, "MotionBlurAmount", state.mb_amount);
        if (state.has_mb) {
            state.has_mb = pp_read(settings, base, "bOverride_MotionBlurMax", state.override_mb_max) &&
                           pp_read(settings, base, "MotionBlurMax", state.mb_max);
        }
        state.has_gi = pp_read(settings, base, "bOverride_DynamicGlobalIlluminationMethod", state.override_gi) &&
                       pp_read(settings, base, "DynamicGlobalIlluminationMethod", state.gi_method);
        state.has_reflection = pp_read(settings, base, "bOverride_ReflectionMethod", state.override_reflection) &&
                               pp_read(settings, base, "ReflectionMethod", state.reflection_method);
        state.has_grain = pp_read(settings, base, "bOverride_FilmGrainIntensity", state.override_grain) &&
                          pp_read(settings, base, "FilmGrainIntensity", state.grain_intensity);
        if (!state.has_aa && !state.has_mb && !state.has_gi && !state.has_reflection && !state.has_grain) return false;

        if (state.has_aa) {
            pp_write(settings, base, "bOverride_AntiAliasingMethod", 1.0);
            pp_write(settings, base, "AntiAliasingMethod", 0.0);
            ++aa_count;
            if (is_pov) pov_has_aa = true;
        }
        if (state.has_mb) {
            pp_write(settings, base, "bOverride_MotionBlurAmount", 1.0);
            pp_write(settings, base, "MotionBlurAmount", 0.0);
            pp_write(settings, base, "bOverride_MotionBlurMax", 1.0);
            pp_write(settings, base, "MotionBlurMax", 0.0);
            ++mb_count;
        }
        if (state.has_gi) {
            pp_write(settings, base, "bOverride_DynamicGlobalIlluminationMethod", 1.0);
            pp_write(settings, base, "DynamicGlobalIlluminationMethod", 0.0);
            ++gi_count;
        }
        if (state.has_reflection) {
            pp_write(settings, base, "bOverride_ReflectionMethod", 1.0);
            pp_write(settings, base, "ReflectionMethod", 0.0);
            ++reflection_count;
        }
        if (state.has_grain) {
            pp_write(settings, base, "bOverride_FilmGrainIntensity", 1.0);
            pp_write(settings, base, "FilmGrainIntensity", 0.0);
            ++grain_count;
        }
        post_process_states_.push_back(state);
        return true;
    };

    // ① 世界里的 PostProcessVolume（含 PBZ 自定义子类）。很多项目的 AA/Lumen/MB 实际由
    //    这里的 FPostProcessSettings override 决定，改 cvar 的“默认值”对最终 view 无效。
    std::vector<UObject*> volumes;
    for (const char* class_name : {"PostProcessVolume", "PBZPostProcessVolume"}) {
        std::vector<UObject*> one_class;
        RC::Unreal::UObjectGlobals::FindAllOf(widen(class_name).c_str(), one_class);
        for (UObject* volume : one_class) {
            bool duplicate = false;
            for (UObject* existing : volumes) {
                if (existing == volume) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) volumes.push_back(volume);
        }
    }
    for (UObject* volume : volumes) {
        if (!is_ok(volume)) continue;
        if (narrow(volume->GetFullName()).find("Default__") != std::string::npos) continue;
        ++volume_count;
        FProperty* settings = find_property(volume, "Settings");
        void* base = struct_member_base(settings, volume);
        if (capture_and_apply(volume, false, settings, base)) ++volumes_with_override;
    }

    // ② 相机 CameraCache.POV 的 PostProcessSettings：POV 每帧由 UpdateCamera 重建，
    //    所以这里写一次只保底；真正的每帧重申在 post_process_override_tick()。
    {
        PovView pov;
        if (resolve_pov_view(pov)) {
            FProperty* settings = struct_member(pov.pov, "PostProcessSettings");
            void* base = struct_member_base(settings, pov.base);
            capture_and_apply(nullptr, true, settings, base);
        }
    }

    char buf[300]{};
    std::snprintf(buf, sizeof(buf),
                  "后处理覆盖：体积 %d 个（命中 %d 个；AA %d / 动态模糊 %d / GI %d / 反射 %d / 胶片颗粒 %d），相机POV AA=%s",
                  volume_count, volumes_with_override, aa_count, mb_count, gi_count, reflection_count, grain_count,
                  pov_has_aa ? "已覆盖" : "不可用");
    log(LogLevel::Info, buf);

    // 再打一条首个体积的原始覆盖值：用于确认游戏到底有没有在 PPV 上强制 Lumen/动态模糊/颗粒。
    for (const PostProcessOverrideState& state : post_process_states_) {
        if (state.is_pov) continue;
        char detail[360]{};
        std::snprintf(detail, sizeof(detail),
                      "后处理覆盖首个体积原值：AA覆盖=%.0f 方法=%.0f；MB覆盖=%.0f 量=%.2f；GI覆盖=%.0f 方法=%.0f；反射覆盖=%.0f 方法=%.0f；颗粒覆盖=%.0f 强度=%.2f",
                      state.override_aa, state.aa_method, state.override_mb_amount, state.mb_amount, state.override_gi,
                      state.gi_method, state.override_reflection, state.reflection_method, state.override_grain,
                      state.grain_intensity);
        log(LogLevel::Info, detail);
        break;
    }
}

void Ue4ssPort::post_process_override_tick() {
    bool has_pov_override = false;
    for (const PostProcessOverrideState& state : post_process_states_) {
        if (state.is_pov) {
            has_pov_override = true;
            break;
        }
    }
    if (!has_pov_override) return;
    PovView pov;
    if (!resolve_pov_view(pov)) return;
    FProperty* settings = struct_member(pov.pov, "PostProcessSettings");
    void* base = struct_member_base(settings, pov.base);
    if (settings == nullptr || base == nullptr) return;
    pp_write(settings, base, "bOverride_AntiAliasingMethod", 1.0);
    pp_write(settings, base, "AntiAliasingMethod", 0.0);
    pp_write(settings, base, "bOverride_MotionBlurAmount", 1.0);
    pp_write(settings, base, "MotionBlurAmount", 0.0);
    pp_write(settings, base, "bOverride_MotionBlurMax", 1.0);
    pp_write(settings, base, "MotionBlurMax", 0.0);
    pp_write(settings, base, "bOverride_DynamicGlobalIlluminationMethod", 1.0);
    pp_write(settings, base, "DynamicGlobalIlluminationMethod", 0.0);
    pp_write(settings, base, "bOverride_ReflectionMethod", 1.0);
    pp_write(settings, base, "ReflectionMethod", 0.0);
    pp_write(settings, base, "bOverride_FilmGrainIntensity", 1.0);
    pp_write(settings, base, "FilmGrainIntensity", 0.0);
}


// =============================================================================
//  光照 A/B（Lumen / 屏幕空间光照）：独立快照表，阶段轮换不碰 AA/DLSS/FG
// =============================================================================
namespace {

constexpr int kLightingAbPhaseCount = 6;

struct LightingAbPhase {
    const char* name;
    LightingTuningOptions options;
};

// 默认轮换顺序：先保留 Lumen 只关某个屏幕空间环节，最后才整组关掉，便于二分定位。
const LightingAbPhase kLightingAbPhases[kLightingAbPhaseCount] = {
    {"原始（Lumen/屏幕空间光照全开）", {}},
    {"屏幕空间追踪关", {true, false, false, false, false}},
    {"短距屏幕空间 AO 关", {false, true, false, false, false}},
    {"Lumen 辐射缓存关", {false, false, true, false, false}},
    {"Lumen 抖动固定（逐帧随机采样关）", {false, false, false, true, false}},
    {"Lumen GI/反射全关", {false, false, false, false, true}},
};

}  // namespace

RenderTuningReport Ue4ssPort::apply_lighting_options(const LightingTuningOptions& options, bool verbose) {
    RenderTuningReport report{};

    struct Target {
        const char* name;
        int value;
    };
    std::vector<Target> targets;
    if (options.screen_traces_off) {
        targets.push_back({"r.Lumen.ScreenProbeGather.ScreenTraces", 0});
        targets.push_back({"r.Lumen.Reflections.ScreenTraces", 0});
    }
    if (options.short_range_ao_off) {
        targets.push_back({"r.Lumen.ScreenProbeGather.ShortRangeAO", 0});
        targets.push_back({"r.Lumen.DiffuseIndirect.SSAO", 0});
        targets.push_back({"r.AmbientOcclusionLevels", 0});
    }
    if (options.radiance_cache_off) {
        targets.push_back({"r.Lumen.ScreenProbeGather.RadianceCache", 0});
    }
    if (options.fixed_jitter) {
        targets.push_back({"r.Lumen.ScreenProbeGather.FixedJitterIndex", 0});
        targets.push_back({"r.Lumen.ScreenProbeGather.FixedStateFrameIndex", 0});
        targets.push_back({"r.Lumen.Reflections.FixedStateFrameIndex", 0});
    }
    if (options.lumen_off) {
        targets.push_back({"r.Lumen.DiffuseIndirect.Allow", 0});
        targets.push_back({"r.Lumen.Reflections.Allow", 0});
        // 方法 CVar 在部分构建里是只读/启动期决定；能写就写，写不动会在日志里列为“未生效”，不影响 Allow 开关。
        targets.push_back({"r.DynamicGlobalIlluminationMethod", 0});
        targets.push_back({"r.ReflectionMethod", 0});
    }

    if (targets.empty()) {
        report.ok = true;
        return report;
    }

    std::string applied_text;
    std::string failed_text;
    for (const Target& target : targets) {
        bool already_snapshotted = false;
        for (const RenderCvarSnapshot& snapshot : lighting_cvar_snapshots_) {
            if (snapshot.name == target.name) {
                already_snapshotted = true;
                break;
            }
        }
        if (already_snapshotted) continue;

        int original = 0;
        if (!get_cvar_int(target.name, original)) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 读不到";
            continue;
        }
        if (original == target.value) continue;

        if (!exec_console_command(std::string(target.name) + " " + std::to_string(target.value), world())) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 命令执行失败";
            continue;
        }
        int current = 0;
        if (!get_cvar_int(target.name, current) || current != target.value) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.name) + " 回读不是目标值";
            continue;
        }

        lighting_cvar_snapshots_.push_back({target.name, original, target.value});
        ++report.applied;
        if (!applied_text.empty()) applied_text += "；";
        applied_text += std::string(target.name) + " " + std::to_string(original) + "→" + std::to_string(target.value);
    }

    report.ok = report.applied > 0 || failed_text.empty();
    if (verbose && report.applied > 0) {
        log(LogLevel::Info, "光照对照已应用：" + applied_text + "（解冻/退出按原值还原）");
    }
    if (!failed_text.empty()) {
        log(LogLevel::Warn, "光照对照部分项目未生效：" + failed_text);
    }
    if (verbose) {
        const std::string after_state = lighting_state_text();
        if (!after_state.empty()) log(LogLevel::Info, "光照对照后状态：" + after_state);
    }
    return report;
}

bool Ue4ssPort::restore_lighting_options(bool verbose) {
    std::vector<RenderCvarSnapshot> remaining;
    std::string restored_text;
    int restored = 0;
    for (const RenderCvarSnapshot& snapshot : lighting_cvar_snapshots_) {
        bool ok = false;
        int current = 0;
        if (exec_console_command(snapshot.name + " " + std::to_string(snapshot.original), world()) &&
            get_cvar_int(snapshot.name, current) && current == snapshot.original) {
            ok = true;
            ++restored;
            if (!restored_text.empty()) restored_text += "；";
            restored_text += snapshot.name + " 恢复 " + std::to_string(snapshot.original);
        }
        if (!ok) remaining.push_back(snapshot);
    }
    lighting_cvar_snapshots_ = remaining;
    if (verbose && restored > 0) log(LogLevel::Info, "光照对照已还原：" + restored_text);
    if (!remaining.empty()) log(LogLevel::Warn, "光照对照还原失败：有 CVar 未能写回原值，退出时会重试");
    return remaining.empty();
}

RenderTuningReport Ue4ssPort::freeze_lighting(bool frozen, const LightingTuningOptions& options) {
    RenderTuningReport report{};
    if (!frozen) {
        report.ok = restore_lighting_options(true);
        report.restored = 0;
        lighting_ab_phase_ = -1;
        lighting_ab_accum_ = 0.0;
        lighting_ab_active_ = false;
        return report;
    }

    const std::string state = lighting_state_text();
    if (!state.empty()) {
        log(LogLevel::Info, "灯光状态：" + state);
    } else if (!lighting_probe_logged_) {
        lighting_probe_logged_ = true;
        log(LogLevel::Warn, "灯光状态不可读（GetConsoleVariableIntValue 不可用），光照对照跳过");
    }

    report = apply_lighting_options(options, true);
    return report;
}

void Ue4ssPort::lighting_ab_tick(double dt, bool enabled, double phase_seconds) {
    if (!enabled) {
        if (lighting_ab_active_) {
            restore_lighting_options(true);
            lighting_ab_active_ = false;
            lighting_ab_phase_ = -1;
            lighting_ab_accum_ = 0.0;
        }
        return;
    }

    if (phase_seconds < 1.0) phase_seconds = 1.0;

    if (!lighting_ab_active_) {
        lighting_ab_active_ = true;
        lighting_ab_phase_ = 0;
        lighting_ab_accum_ = 0.0;
        char buf[256]{};
        std::snprintf(buf, sizeof(buf), "灯光A/B：阶段 0/%d %s（每 %.1f 秒自动切换）", kLightingAbPhaseCount,
                      kLightingAbPhases[0].name, phase_seconds);
        log(LogLevel::Info, buf);
        return;
    }

    lighting_ab_accum_ += dt;
    if (lighting_ab_accum_ < phase_seconds) return;
    lighting_ab_accum_ = 0.0;

    const bool restored = restore_lighting_options(false);
    lighting_ab_phase_ = (lighting_ab_phase_ + 1) % kLightingAbPhaseCount;
    const LightingAbPhase& phase = kLightingAbPhases[lighting_ab_phase_];
    const RenderTuningReport report = apply_lighting_options(phase.options, false);
    char buf[320]{};
    std::snprintf(buf, sizeof(buf), "灯光A/B：阶段 %d/%d %s（CVar 改动 %d 项）", lighting_ab_phase_,
                  kLightingAbPhaseCount, phase.name, report.applied);
    log(LogLevel::Info, buf);
    if (!restored) {
        log(LogLevel::Warn, "灯光A/B：上一阶段有 CVar 未还原，本阶段数值可能叠加");
    }
}

// =============================================================================
//  阴影/后处理 A/B：Lumen 之外的第二层（VSM SMRT / VSM 缓存 / 接触阴影 / 体积雾 / 后处理）
// =============================================================================
namespace {

constexpr int kShadowAbPhaseCount = 8;

struct ShadowAbPhase {
    const char* name;
    std::vector<std::pair<const char*, int>> targets;
};

const ShadowAbPhase kShadowAbPhases[kShadowAbPhaseCount] = {
    {"原始（阴影/后处理全开）", {}},
    {"Shadow SMRT 关（VSM 软阴影随机采样关）",
     {{"r.Shadow.Virtual.SMRT.RayCountDirectional", 0}, {"r.Shadow.Virtual.SMRT.RayCountLocal", 0}}},
    {"VSM 布料缓存逐帧失效关", {{"r.Shadow.Virtual.Cache.DeformableMeshesInvalidate", 0}}},
    {"VSM 关（回退 CSM）", {{"r.Shadow.Virtual.Enable", 0}}},
    {"全部阴影关", {{"r.ShadowQuality", 0}}},
    {"接触阴影关", {{"r.ContactShadows", 0}, {"r.ContactShadows.Denoise.Enable", 0}}},
    {"体积雾关", {{"r.VolumetricFog", 0}, {"r.VolumetricFog.TemporalReprojection", 0}}},
    {"后处理组合关（胶片颗粒/后处理材质/景深）",
     {{"r.FilmGrain", 0}, {"r.PostProcessing.DisableMaterials", 1}, {"r.DepthOfFieldQuality", 0}}},
};

}  // namespace

RenderTuningReport Ue4ssPort::apply_shadow_phase(const std::vector<std::pair<const char*, int>>& targets,
                                                   bool verbose) {
    RenderTuningReport report{};
    if (targets.empty()) {
        report.ok = true;
        return report;
    }

    std::string applied_text;
    std::string failed_text;
    for (const auto& target : targets) {
        bool already_snapshotted = false;
        for (const RenderCvarSnapshot& snapshot : shadow_cvar_snapshots_) {
            if (snapshot.name == target.first) {
                already_snapshotted = true;
                break;
            }
        }
        if (already_snapshotted) continue;

        int original = 0;
        if (!get_cvar_int(target.first, original)) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.first) + " 读不到";
            continue;
        }
        if (original == target.second) continue;

        if (!exec_console_command(std::string(target.first) + " " + std::to_string(target.second), world())) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.first) + " 命令执行失败";
            continue;
        }
        int current = 0;
        if (!get_cvar_int(target.first, current) || current != target.second) {
            if (!failed_text.empty()) failed_text += "；";
            failed_text += std::string(target.first) + " 回读不是目标值";
            continue;
        }

        shadow_cvar_snapshots_.push_back({target.first, original, target.second});
        ++report.applied;
        if (!applied_text.empty()) applied_text += "；";
        applied_text += std::string(target.first) + " " + std::to_string(original) + "→" + std::to_string(target.second);
    }

    report.ok = report.applied > 0 || failed_text.empty();
    if (verbose && report.applied > 0) {
        log(LogLevel::Info, "阴影/后处理对照已应用：" + applied_text + "（解冻/退出按原值还原）");
    }
    if (!failed_text.empty()) {
        log(LogLevel::Warn, "阴影/后处理对照部分项目未生效：" + failed_text);
    }
    if (verbose) {
        const std::string after_state = shadow_state_text();
        if (!after_state.empty()) log(LogLevel::Info, "阴影/后处理对照后状态：" + after_state);
    }
    return report;
}

bool Ue4ssPort::restore_shadow_options(bool verbose) {
    std::vector<RenderCvarSnapshot> remaining;
    std::string restored_text;
    int restored = 0;
    for (const RenderCvarSnapshot& snapshot : shadow_cvar_snapshots_) {
        bool ok = false;
        int current = 0;
        if (exec_console_command(snapshot.name + " " + std::to_string(snapshot.original), world()) &&
            get_cvar_int(snapshot.name, current) && current == snapshot.original) {
            ok = true;
            ++restored;
            if (!restored_text.empty()) restored_text += "；";
            restored_text += snapshot.name + " 恢复 " + std::to_string(snapshot.original);
        }
        if (!ok) remaining.push_back(snapshot);
    }
    shadow_cvar_snapshots_ = remaining;
    if (verbose && restored > 0) log(LogLevel::Info, "阴影/后处理对照已还原：" + restored_text);
    if (!remaining.empty()) log(LogLevel::Warn, "阴影/后处理对照还原失败：有 CVar 未能写回原值，退出时会重试");
    return remaining.empty();
}

RenderTuningReport Ue4ssPort::freeze_shadow_ab(bool frozen) {
    RenderTuningReport report{};
    if (!frozen) {
        report.ok = restore_shadow_options(true);
        shadow_ab_phase_ = -1;
        shadow_ab_accum_ = 0.0;
        shadow_ab_active_ = false;
        return report;
    }

    const std::string state = shadow_state_text();
    if (!state.empty()) {
        log(LogLevel::Info, "阴影/后处理状态：" + state);
    } else if (!shadow_probe_logged_) {
        shadow_probe_logged_ = true;
        log(LogLevel::Warn, "阴影/后处理状态不可读（GetConsoleVariableIntValue 不可用），阴影/后处理对照跳过");
    }
    report.ok = true;
    return report;
}

void Ue4ssPort::shadow_ab_tick(double dt, bool enabled, double phase_seconds) {
    if (!enabled) {
        if (shadow_ab_active_) {
            restore_shadow_options(true);
            shadow_ab_active_ = false;
            shadow_ab_phase_ = -1;
            shadow_ab_accum_ = 0.0;
        }
        return;
    }

    if (phase_seconds < 1.0) phase_seconds = 1.0;

    if (!shadow_ab_active_) {
        shadow_ab_active_ = true;
        shadow_ab_phase_ = 0;
        shadow_ab_accum_ = 0.0;
        char buf[320]{};
        std::snprintf(buf, sizeof(buf), "阴影/后处理A/B：阶段 0/%d %s（每 %.1f 秒自动切换）", kShadowAbPhaseCount,
                      kShadowAbPhases[0].name, phase_seconds);
        log(LogLevel::Info, buf);
        return;
    }

    shadow_ab_accum_ += dt;
    if (shadow_ab_accum_ < phase_seconds) return;
    shadow_ab_accum_ = 0.0;

    const bool restored = restore_shadow_options(false);
    shadow_ab_phase_ = (shadow_ab_phase_ + 1) % kShadowAbPhaseCount;
    const ShadowAbPhase& phase = kShadowAbPhases[shadow_ab_phase_];
    const RenderTuningReport report = apply_shadow_phase(phase.targets, false);
    char buf[360]{};
    std::snprintf(buf, sizeof(buf), "阴影/后处理A/B：阶段 %d/%d %s（CVar 改动 %d 项）", shadow_ab_phase_,
                  kShadowAbPhaseCount, phase.name, report.applied);
    log(LogLevel::Info, buf);
    if (!restored) {
        log(LogLevel::Warn, "阴影/后处理A/B：上一阶段有 CVar 未还原，本阶段数值可能叠加");
    }
}


WpoFreezeReport Ue4ssPort::freeze_wpo(bool frozen) {
    WpoFreezeReport report{};
    if (!frozen) {
        int restored = 0;
        int failed = 0;
        for (UObject* component : wpo_disabled_components_) {
            if (!is_ok(component)) {
                ++failed;
                continue;
            }
            const bool ok = invoke_void(component, "SetEvaluateWorldPositionOffset", [](ParamBlock& params) {
                return params.set_bool("bNewValue", true) || params.set_bool("NewValue", true) ||
                       params.set_bool("Value", true) || params.set_bool("bEvaluateWorldPositionOffset", true) ||
                       params.set_bool("bEvaluate", true) || params.set_bool("bValue", true) ||
                       params.set_bool("bEnable", true) || set_first_bool_param(params, true);
            });
            if (ok) {
                ++restored;
            } else {
                ++failed;
            }
        }
        wpo_disabled_components_.clear();

        int primitive_restored = 0;
        int primitive_failed = 0;
        for (const WpoPrimitiveState& state : wpo_primitive_states_) {
            if (!is_ok(state.component)) {
                ++primitive_failed;
                continue;
            }
            FProperty* property = find_property(state.component, "bEvaluateWorldPositionOffset");
            if (property == nullptr || !write_bool(property, state.component, state.evaluate)) {
                ++primitive_failed;
                continue;
            }
            ++primitive_restored;
        }
        wpo_primitive_states_.clear();

        report.acted = restored;
        report.primitive_acted = primitive_restored;
        report.ok = failed == 0 && primitive_failed == 0;
        if (restored > 0 || primitive_restored > 0) {
            char buf[220]{};
            std::snprintf(buf, sizeof(buf), "WPO 评估恢复：SkeletalMesh %d 个、其他图元 %d 个", restored,
                          primitive_restored);
            log(LogLevel::Info, buf);
        }
        if (!report.ok) log(LogLevel::Warn, "WPO 评估部分恢复失败，退出时会重试");
        return report;
    }

    wpo_disabled_components_.clear();
    wpo_primitive_states_.clear();
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("SkeletalMeshComponent"), components);
    int found = 0;
    int no_function = 0;
    UFunction* first_wpo_function = nullptr;
    for (UObject* component : components) {
        if (!is_ok(component)) continue;
        if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
        ++found;
        UFunction* wpo_function = find_function(component, "SetEvaluateWorldPositionOffset");
        if (wpo_function == nullptr) {
            ++no_function;
            continue;
        }
        if (first_wpo_function == nullptr) first_wpo_function = wpo_function;
        if (invoke_void(component, "SetEvaluateWorldPositionOffset", [](ParamBlock& params) {
                return params.set_bool("bNewValue", false) || params.set_bool("NewValue", false) ||
                       params.set_bool("Value", false) || params.set_bool("bEvaluateWorldPositionOffset", false) ||
                       params.set_bool("bEvaluate", false) || params.set_bool("bValue", false) ||
                       params.set_bool("bEnable", false) || set_first_bool_param(params, false);
            })) {
            ++report.acted;
            wpo_disabled_components_.push_back(component);
        }
    }
    report.found = found;

    // 静态网格/植被/实例化植被/地形组件也有 bEvaluateWorldPositionOffset：SkeletalMesh 之外
    // 的材质顶点动画（草、树叶、水面、旗帜）同样会在冻结期继续求值，表现为细边缘持续闪。
    {
        std::vector<UObject*> primitives;
        for (const char* class_name : {"StaticMeshComponent", "InstancedStaticMeshComponent",
                                       "HierarchicalInstancedStaticMeshComponent",
                                       "FoliageInstancedStaticMeshComponent", "SplineMeshComponent",
                                       "LandscapeComponent"}) {
            std::vector<UObject*> one_class;
            RC::Unreal::UObjectGlobals::FindAllOf(widen(class_name).c_str(), one_class);
            for (UObject* object : one_class) {
                bool duplicate = false;
                for (UObject* existing : primitives) {
                    if (existing == object) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) primitives.push_back(object);
            }
        }
        for (UObject* component : primitives) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            FProperty* property = find_property(component, "bEvaluateWorldPositionOffset");
            if (property == nullptr) continue;
            double evaluate = 0.0;
            if (!read_number(property, component, evaluate)) continue;
            ++report.primitive_found;
            if (evaluate == 0.0) continue;
            if (write_bool(property, component, false)) {
                ++report.primitive_acted;
                wpo_primitive_states_.push_back({component, true});
            }
        }
    }

    report.ok = found == 0 || report.acted > 0 || no_function == found;
    char buf[320]{};
    std::snprintf(buf, sizeof(buf),
                  "WPO 评估冻结：SkeletalMesh %d 个（本次关闭 %d；无 SetEvaluateWorldPositionOffset 的函数 %d 个）；"
                  "其他图元 %d 个（本次关闭 WPO %d 个）",
                  found, report.acted, no_function, report.primitive_found, report.primitive_acted);
    log(LogLevel::Info, buf);
    if (report.acted == 0 && first_wpo_function != nullptr) {
        log(LogLevel::Warn, "WPO setter 参数名探测：" + describe_param_names(first_wpo_function));
    }
    return report;
}

CustomClothFreezeReport Ue4ssPort::freeze_custom_cloth(bool frozen) {
    CustomClothFreezeReport report{};

    if (!frozen) {
        int restored = 0;
        int failed = 0;
        for (const SwayTickState& state : sway_tick_states_) {
            if (!is_ok(state.component)) {
                ++failed;
                continue;
            }
            bool ok = false;
            for (const char* fn : {"SetComponentTickEnabled", "SetTickEnabled"}) {
                if (invoke_void(state.component, fn, [&state](ParamBlock& params) {
                        return params.set_bool("bEnabled", state.was_enabled) ||
                               params.set_bool("bNewState", state.was_enabled);
                    })) {
                    ok = true;
                    break;
                }
            }
            if (ok) {
                ++restored;
            } else {
                ++failed;
            }
        }
        sway_tick_states_.clear();
        if (cloth_global_active_) {
            if (cloth_global_original_ >= 0 &&
                exec_console_command("r.Cloth.Enable " + std::to_string(cloth_global_original_), world())) {
                int current = 0;
                if (get_cvar_int("r.Cloth.Enable", current) && current == cloth_global_original_) {
                    cloth_global_active_ = false;
                }
            }
        }
        int groom_restored = 0;
        int groom_failed = 0;
        for (const GroomSimState& state : groom_states_) {
            if (!is_ok(state.component)) {
                ++groom_failed;
                continue;
            }
            bool ok = false;
            FProperty* property = find_property(state.component, "bSimulationEnabled");
            if (property != nullptr) {
                ok = write_bool(property, state.component, state.was_enabled);
            }
            if (!ok) {
                ok = invoke_void(state.component, "SetSimulationEnabled", [&state](ParamBlock& params) {
                    return params.set_bool("bEnabled", state.was_enabled) ||
                           params.set_bool("bNewEnabled", state.was_enabled) ||
                           set_first_bool_param(params, state.was_enabled);
                });
            }
            if (ok) {
                ++groom_restored;
            } else {
                ++groom_failed;
            }
        }
        groom_states_.clear();

        int niagara_restored = 0;
        int niagara_failed = 0;
        for (const NiagaraPauseState& state : niagara_states_) {
            if (!is_ok(state.component)) {
                ++niagara_failed;
                continue;
            }
            bool ok = false;
            if (state.tick_changed) {
                for (const char* fn : {"SetComponentTickEnabled", "SetTickEnabled"}) {
                    if (invoke_void(state.component, fn, [&state](ParamBlock& params) {
                            return params.set_bool("bEnabled", state.tick_was_enabled) ||
                                   params.set_bool("bNewState", state.tick_was_enabled);
                        })) {
                        ok = true;
                        break;
                    }
                }
            } else {
                ok = invoke_void(state.component, "SetPaused", [](ParamBlock& params) {
                    return params.set_bool("bPaused", false) || params.set_bool("bInPaused", false) ||
                           set_first_bool_param(params, false);
                });
            }
            if (ok) {
                ++niagara_restored;
            } else {
                ++niagara_failed;
            }
        }
        niagara_states_.clear();

        report.sway_acted = restored;
        report.groom_acted = groom_restored;
        report.niagara_acted = niagara_restored;
        report.ok = failed == 0 && !cloth_global_active_ && groom_failed == 0 && niagara_failed == 0;
        if (restored > 0 || report.ok) {
            char buf[260]{};
            std::snprintf(buf, sizeof(buf),
                          "自定义布料/摆动恢复：Sway/Wind 组件 %d 个、毛发 %d 个、粒子 %d 个；全局 Cloth=%s",
                          restored, groom_restored, niagara_restored, cloth_global_active_ ? "未还原" : "已还原");
            log(LogLevel::Info, buf);
        }
        if (!report.ok) log(LogLevel::Warn, "自定义布料/摆动部分还原失败，退出时会重试");
        return report;
    }

    sway_tick_states_.clear();
    int found = 0;
    std::string class_summary;

    auto contains_any = [](const std::string& text, std::initializer_list<const char*> needles) {
        for (const char* needle : needles) {
            if (text.find(needle) != std::string::npos) return true;
        }
        return false;
    };
    auto lower_copy = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };

    // 先枚举所有已加载 UClass，找出真正的类名（SwayComponent 可能只是调试字符串，
    // 实际类名可能是 PBZSwayComponent_C / BP_SwayComponent_C 等，硬编码会 0 命中）。
    std::vector<std::string> candidate_classes;
    {
        std::vector<UObject*> classes;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("Class"), classes);
        for (UObject* cls : classes) {
            if (!is_ok(cls)) continue;
            const std::string name = narrow(cls->GetName());
            const std::string lower_name = lower_copy(name);
            if (!contains_any(lower_name, {"sway", "windsystem", "windtarget", "windcomponent", "clothcomponent",
                                           "drape", "ribbon", "skirt", "cape"})) {
                continue;
            }
            if (name.size() > 120) continue;
            bool duplicate = false;
            for (const std::string& existing : candidate_classes) {
                if (existing == name) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && candidate_classes.size() < 80) candidate_classes.push_back(name);
        }
    }
    if (!candidate_classes.empty()) {
        std::string list;
        for (const std::string& name : candidate_classes) {
            if (!list.empty()) list += "，";
            list += name;
        }
        log(LogLevel::Info, "自定义布料候选类：" + list);
    }

    auto scan_class = [&](const char* class_name) {
        std::vector<UObject*> components;
        RC::Unreal::UObjectGlobals::FindAllOf(widen(class_name).c_str(), components);
        int class_found = 0;
        for (UObject* component : components) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            ++class_found;
            ++found;

            bool was_enabled = false;
            const bool state_known =
                invoke_bool(component, "IsComponentTickEnabled", [](ParamBlock&) { return true; }, was_enabled);
            if (state_known && !was_enabled) continue;

            bool called = false;
            for (const char* fn : {"SetComponentTickEnabled", "SetTickEnabled"}) {
                if (invoke_void(component, fn, [](ParamBlock& params) {
                        return params.set_bool("bEnabled", false) || params.set_bool("bNewState", false);
                    })) {
                    called = true;
                    break;
                }
            }
            bool still_enabled = false;
            const bool verify_ok =
                invoke_bool(component, "IsComponentTickEnabled", [](ParamBlock&) { return true; }, still_enabled);
            if (called && (!verify_ok || !still_enabled)) {
                ++report.sway_acted;
                sway_tick_states_.push_back({component, true});
            }
        }
        if (class_found > 0) {
            if (!class_summary.empty()) class_summary += "，";
            class_summary += std::string(class_name) + "=" + std::to_string(class_found);
        }
    };
    // 硬编码的常见类名先扫一遍（有就直接用），再按实际 UClass 名扫描候选。
    for (const char* name : {"SwayComponent", "SwayComponent_C", "BP_SwayComponent_C", "WindSystemComponent",
                             "WindSystem", "WindTargetComponent"}) {
        scan_class(name);
    }
    for (const std::string& name : candidate_classes) {
        scan_class(name.c_str());
    }

    // SwayManager 不是组件，仅记录是否找到（真正停的是它管理的 SwayComponent 与骨骼更新开关）
    std::vector<UObject*> managers;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("SwayManager"), managers);
    int manager_count = 0;
    for (UObject* manager : managers) {
        if (is_ok(manager) && narrow(manager->GetFullName()).find("Default__") == std::string::npos) ++manager_count;
    }

    // 1.0.76：不再写全局 CVar pbz.Sway.SkipSkeletalKinematicBoneUpdate=1。
    // 实机反馈「进入冻结后布料件整体挪位、褶皱形状不变」= Sway 骨骼输出被一次性换回动画姿态。
    // 改为全对象扫描找出 Sway 相关对象（实机日志里候选类 0 命中，说明 FindAllOf("Class") 这条路
    // 对 PBZ 的 Sway 不成立），是组件就停 tick（保留最后一次写出的骨骼姿态），并把真实类名写进日志。
    // 只在前两次冻结且硬编码类名没命中时全量扫，避免每次进入都遍历整个对象表。
    if (found == 0 && sway_full_scan_attempts_ < 2) {
        ++sway_full_scan_attempts_;
        std::unordered_map<UObject*, bool> class_is_sway;
        std::unordered_map<UObject*, std::string> class_names;
        std::unordered_map<std::string, int> class_counts;
        std::vector<UObject*> scan_components;
        std::string scan_samples;
        int scan_objects = 0;
        RC::Unreal::UObjectGlobals::ForEachUObject(
            [&](UObject* object, int32, int32) -> RC::LoopAction {
                if (object == nullptr) return RC::LoopAction::Continue;
                UObject* cls = object->GetClassPrivate();
                if (cls == nullptr) return RC::LoopAction::Continue;
                bool is_sway = false;
                const auto cached = class_is_sway.find(cls);
                if (cached != class_is_sway.end()) {
                    is_sway = cached->second;
                } else {
                    const std::string class_name = narrow(cls->GetName());
                    is_sway = lower_copy(class_name).find("sway") != std::string::npos;
                    class_is_sway.emplace(cls, is_sway);
                    class_names.emplace(cls, class_name);
                }
                if (!is_sway) return RC::LoopAction::Continue;
                ++scan_objects;
                const std::string& class_name = class_names.at(cls);
                ++class_counts[class_name];
                const std::string full_name = narrow(object->GetFullName());
                if (full_name.find("Default__") != std::string::npos) return RC::LoopAction::Continue;
                if (scan_samples.size() < 240) {
                    if (!scan_samples.empty()) scan_samples += "，";
                    scan_samples += full_name.substr(0, 100);
                }
                bool tick_enabled = false;
                if (invoke_bool(object, "IsComponentTickEnabled", [](ParamBlock&) { return true; }, tick_enabled) &&
                    tick_enabled) {
                    scan_components.push_back(object);
                }
                return RC::LoopAction::Continue;
            });
        int scan_stopped = 0;
        for (UObject* component : scan_components) {
            bool called = false;
            for (const char* fn : {"SetComponentTickEnabled", "SetTickEnabled"}) {
                if (invoke_void(component, fn, [](ParamBlock& params) {
                        return params.set_bool("bEnabled", false) || params.set_bool("bNewState", false);
                    })) {
                    called = true;
                    break;
                }
            }
            bool still_enabled = false;
            const bool verify_ok =
                invoke_bool(component, "IsComponentTickEnabled", [](ParamBlock&) { return true; }, still_enabled);
            if (called && (!verify_ok || !still_enabled)) {
                ++scan_stopped;
                ++report.sway_acted;
                sway_tick_states_.push_back({component, true});
            }
        }
        found += scan_stopped;
        if (scan_objects > 0) {
            std::string classes_text;
            int shown = 0;
            for (const auto& entry : class_counts) {
                if (shown++ >= 6) break;
                if (!classes_text.empty()) classes_text += "，";
                classes_text += entry.first + "=" + std::to_string(entry.second);
            }
            char buf[520]{};
            std::snprintf(buf, sizeof(buf), "Sway 全对象扫描：命中 %d 个（类名 %s）；样本：%s；本次停 tick %d 个",
                          scan_objects, classes_text.empty() ? "无" : classes_text.c_str(),
                          scan_samples.empty() ? "无" : scan_samples.c_str(), scan_stopped);
            log(LogLevel::Info, buf);
        } else {
            log(LogLevel::Info, "Sway 全对象扫描：类名含 Sway 的对象 0 个（Sway 可能不是 UObject，无法停 tick）");
        }
    }

    report.sway_found = found;
    report.ok = true;

    int cloth_original = -1;
    if (get_cvar_int("r.Cloth.Enable", cloth_original)) {
        if (cloth_original == 0) {
            report.cloth_global_ok = true;
        } else if (exec_console_command("r.Cloth.Enable 0", world())) {
            int current = 0;
            if (get_cvar_int("r.Cloth.Enable", current) && current == 0) {
                cloth_global_original_ = cloth_original;
                cloth_global_active_ = true;
                report.cloth_global_ok = true;
            }
        }
    }

    // 1.0.76：这里原本还会写全局 CVar pbz.Sway.SkipSkeletalKinematicBoneUpdate=1。
    // 实机表现为「进入冻结后布料件整体挪位、褶皱形状不变」——Sway 写出的骨骼姿态被一次性
    // 换回动画姿态。已删除；改为上面的全对象扫描停 Sway 组件 tick（保留当前姿态）。

    // 毛发（Groom）模拟：strand 模拟走独立更新，暂停骨骼动画不会停它；发丝每帧位移会在
    // 发梢形成持续闪动。冻结时把组件 bSimulationEnabled 置 false（保留当前发丝姿态）。
    {
        groom_states_.clear();
        std::vector<UObject*> groom_components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("GroomComponent"), groom_components);
        for (UObject* component : groom_components) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            ++report.groom_found;
            FProperty* property = find_property(component, "bSimulationEnabled");
            double enabled = 0.0;
            const bool readable = property != nullptr && read_number(property, component, enabled);
            if (readable && enabled == 0.0) continue;
            bool ok = property != nullptr && write_bool(property, component, false);
            if (!ok) {
                ok = invoke_void(component, "SetSimulationEnabled", [](ParamBlock& params) {
                    return params.set_bool("bEnabled", false) || params.set_bool("bNewEnabled", false) ||
                           set_first_bool_param(params, false);
                });
            }
            if (ok) {
                ++report.groom_acted;
                groom_states_.push_back({component, readable ? enabled != 0.0 : true});
            }
        }
    }

    // Niagara 粒子：CPU/GPU 粒子由组件/渲染线程推进，TimeDilation 只能缩放 DeltaTime，
    // 某些系统（雨/雪/余烬/尾迹）仍按真实时间或固定步长演化。冻结时对每个组件 SetPaused(true)，
    // 保留粒子当前姿态；没有 SetPaused 的构建退化为停组件 tick。
    {
        niagara_states_.clear();
        std::vector<UObject*> niagara_components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("NiagaraComponent"), niagara_components);
        for (UObject* component : niagara_components) {
            if (!is_ok(component)) continue;
            if (narrow(component->GetFullName()).find("Default__") != std::string::npos) continue;
            ++report.niagara_found;

            bool was_paused = false;
            bool state_known = false;
            FProperty* paused_property = find_property(component, "bPaused");
            double paused_value = 0.0;
            if (paused_property != nullptr && read_number(paused_property, component, paused_value)) {
                was_paused = paused_value != 0.0;
                state_known = true;
            } else if (invoke_bool(component, "IsPaused", [](ParamBlock&) { return true; }, was_paused)) {
                state_known = true;
            }
            if (state_known && was_paused) continue;

            bool called = invoke_void(component, "SetPaused", [](ParamBlock& params) {
                return params.set_bool("bPaused", true) || params.set_bool("bInPaused", true) ||
                       set_first_bool_param(params, true);
            });
            bool tick_changed = false;
            bool tick_was_enabled = true;
            if (!called) {
                const bool tick_known =
                    invoke_bool(component, "IsComponentTickEnabled", [](ParamBlock&) { return true; }, tick_was_enabled);
                for (const char* fn : {"SetComponentTickEnabled", "SetTickEnabled"}) {
                    if (invoke_void(component, fn, [](ParamBlock& params) {
                            return params.set_bool("bEnabled", false) || params.set_bool("bNewState", false);
                        })) {
                        called = true;
                        tick_changed = true;
                        tick_was_enabled = tick_known ? tick_was_enabled : true;
                        break;
                    }
                }
            }
            if (called) {
                ++report.niagara_acted;
                niagara_states_.push_back({component, false, tick_was_enabled, tick_changed});
            }
        }
    }

    char buf[460]{};
    std::snprintf(buf, sizeof(buf),
                  "自定义布料/摆动冻结：Sway/Wind 组件 %d 个（本次停用 %d；%s），SwayManager=%d；"
                  "毛发 %d 个（本次停模拟 %d）；Niagara %d 个（本次暂停 %d）；"
                  "r.Cloth.Enable=%s",
                  found, report.sway_acted, class_summary.empty() ? "无命中" : class_summary.c_str(), manager_count,
                  report.groom_found, report.groom_acted, report.niagara_found, report.niagara_acted,
                  report.cloth_global_ok ? "已关" : "不可用");
    log(LogLevel::Info, buf);
    log(LogLevel::Info, material_param_diag_text());
    return report;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
