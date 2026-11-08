// =============================================================================
//  Ue4ssPort 成员函数实现：景深/对焦/胶片效果（DofPort）
//
//  通道（背景见 core/tuning.h 顶部与 core/dof_math.h）：
//    门控 = NLP 过场渲染档（PBZLevelSequenceManager.SetNLPProfileCinematicEnable，
//           瞬态 LevelSequence 作所有权标记、不播放）+ tagged cvar 覆盖
//           （PBZBlueprintFunctionLibrary.SetConsoleVariableTagged，游戏自带接口，
//             不受「console 命令不执行」限制；每次写入都回读校验）。
//           NLP 档严格跟随景深主开关：只在 need_dof（主开关开）时请求，主开关关掉由
//           core 每帧同步释放（release_nlp_profile）——档位是全局渲染档，开着它游戏原生
//           过场景深（游戏自己的对焦逻辑）就在画面里，本 mod 参数管不到
//           （2026-11-08 实机取证：主开关关 + 仅色差建控 → 随镜头移动的不可调景深）。
//    画面 = spawn 出的相机 Actor 的相机组件 PostProcessSettings（权重 1）。
//           首次写入前把管理器 POV 的 PostProcessSettings 整份拷贝过去，保留游戏原调色。
//    对焦 = 纯手动步进（core 每帧下发 ManualFocusDistance；中心/点击取焦线已于同日下线）。
//
//  实证参照：Soul's Cinematic Studio 1.2.0（Steam 构建 26367565）用同一组接口在拍照模式中
//  实现了景深/散景/光圈控制；本文件是同一机制在 UE4SS 反射层的 C++ 复刻。
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/tuning.h"

namespace pbzpm {
using namespace detail;

namespace {

// ---- 文件内共用的小工具（不进头文件：只有本文件用）----
// 注意：detail:: 已有 kismet_system_library() / gameplay_statics()（CDO），这里不重复定义，
// 直接用 using namespace detail 引入的版本；只补游戏专属的 PBZ 蓝图库。

auto pbz_blueprint_library() -> UObject* {
    return find_object("/Script/ProjectPBZ.Default__PBZBlueprintFunctionLibrary");
}

// NLP token 的 Outer：与 564 完全一致用 GameInstance（native.js 的 context() 就是 GameInstance）。
// 首次实机用 UWorld 当 Outer 时，NLP 开启调用返回后 ~1ms 崩在 PBZ.exe 的虚表调用上；
// 对齐 564 的构造方式是差异最小的高概率修复。取不到 GameInstance 时回退 UWorld。
auto nlp_token_outer(UObject* scene) -> UObject* {
    if (is_ok(scene)) {
        FProperty* gi = find_property(scene, "OwningGameInstance");
        UObject* game_instance = nullptr;
        if (gi != nullptr && read_object(gi, scene, game_instance) && is_ok(game_instance)) {
            return game_instance;
        }
    }
    return scene;
}

// 字符串参数的自适应写入：参数可能是 FString / FText / FName，按候选名命中后先试 text 再试 name。
auto fill_string_param(ParamBlock& params, UFunction* fn, std::initializer_list<const char*> names,
                       const std::string& value) -> bool {
    const int hit = resolve_param_name(names, [&](const char* n) { return params.property(n) != nullptr; });
    if (hit < 0) {
        warn_param_name_miss(fn, names);
        return false;
    }
    const char* name = names.begin()[hit];
    if (params.set_text(name, value)) return true;
    return params.set_name(name, value.c_str());
}

// 对象参数按候选名写入
auto fill_object_param(ParamBlock& params, UFunction* fn, std::initializer_list<const char*> names,
                       UObject* value) -> bool {
    const int hit = resolve_param_name(names, [&](const char* n) { return params.property(n) != nullptr; });
    if (hit < 0) {
        warn_param_name_miss(fn, names);
        return false;
    }
    return params.set_object(names.begin()[hit], value);
}

// 布尔参数按候选名写入（ParamBlock 只有单名 set_bool，这里补候选名解析）
auto fill_bool_param(ParamBlock& params, UFunction* fn, std::initializer_list<const char*> names,
                     bool value) -> bool {
    const int hit = resolve_param_name(names, [&](const char* n) { return params.property(n) != nullptr; });
    if (hit < 0) {
        warn_param_name_miss(fn, names);
        return false;
    }
    return params.set_bool(names.begin()[hit], value);
}

// ---- DOF 组件通道写入（SEH 兜底；函数体内只允许 POD，见 hotfix31 的 C2712 说明）----

struct DofWriteArgs {
    void* comp_prop{nullptr};      // 组件的 PostProcessSettings 属性
    void* comp_base{nullptr};      // 其在组件内的基址
    void* pov_prop{nullptr};       // 管理器 POV 的 PostProcessSettings 属性（拷贝源，可空）
    void* pov_base{nullptr};
    size_t copy_size{0};
    void* wb_prop{nullptr};        // 组件 WeightedBlendables 属性（反别名置空用，可空）
    void* wbo_prop{nullptr};       // 组件 bOverride_WeightedBlendables 属性（可空）
    bool seed{false};              // 本帧是否需要做整份拷贝
    bool dof_enabled{false};
    double focal_cm{0.0};
    double fstop{2.8};
    double sensor_mm{36.0};
    double ca{0.0};
};

auto pp_set_scalar(RC::Unreal::FProperty* settings, void* base, const char* override_name, const char* value_name,
                   bool override_on, double value) -> bool {
    RC::Unreal::FProperty* o = struct_member(settings, override_name);
    RC::Unreal::FProperty* p = struct_member(settings, value_name);
    if (o == nullptr || p == nullptr) return false;
    return write_bool(o, base, override_on) && (!override_on || write_number(p, base, value));
}

auto dof_write_impl(const DofWriteArgs& a) -> bool {
    auto* settings = static_cast<RC::Unreal::FProperty*>(a.comp_prop);
    if (a.seed && a.pov_prop != nullptr && a.pov_base != nullptr) {
        auto* pov_settings = static_cast<RC::Unreal::FProperty*>(a.pov_prop);
        const size_t size = static_cast<size_t>(std::min(settings->GetSize(), pov_settings->GetSize()));
        std::memcpy(a.comp_base, a.pov_base, size);  // 整份拷贝：保留游戏自己的调色/曝光覆盖
        // 反别名（2026-11-07 崩溃家族根因修复）：FPostProcessSettings 唯一的堆成员是
        // WeightedBlendables（TArray<FWeightedBlendable>+TObjectPtr<const UObject>）。上面的 memcpy
        // 把管理器 POV 的堆缓冲「指针」原样拷进组件——两者别名同一段内存；而 ViewTarget 指向我们的
        // 相机后，UpdateCameraManager 每帧「POV ← 组件」结构化拷贝时对自己的别名数组 realloc/free，
        // 组件指针悬空 → 堆逐帧腐化（第 1-8 次崩溃全家族：ForceUpdateCameraView 必崩、call rdx→零页、
        // RAX=0x14AF20D01 均出于此）。564 用属性赋值（引擎深拷贝）天然免疫。这里把组件侧数组置空
        // （Data/Num/Max 三件套 16 字节）并清除覆盖标志，代价：游戏经 WeightedBlendables 挂在 POV 上的
        // 动态材质效果不在摄影相机重现——崩溃面前可接受。
        if (a.wb_prop != nullptr) {
            auto* wb = static_cast<RC::Unreal::FProperty*>(a.wb_prop);
            std::memset(wb->ContainerPtrToValuePtr<void>(a.comp_base), 0, 16);
        }
        if (a.wbo_prop != nullptr) {
            (void)write_bool(static_cast<RC::Unreal::FProperty*>(a.wbo_prop), a.comp_base, false);
        }
    }
    if (a.dof_enabled) {
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldFocalDistance",
                           "DepthOfFieldFocalDistance", true, a.focal_cm)) return false;
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldFstop", "DepthOfFieldFstop", true,
                           a.fstop)) return false;
        // MinFstop = F 值：核心套件没有「圆润度」概念，开到与 F 值一致（564 在 roundness=100% 时同值）
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldMinFstop", "DepthOfFieldMinFstop", true,
                           a.fstop)) return false;
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldSensorWidth", "DepthOfFieldSensorWidth",
                           true, a.sensor_mm)) return false;
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldScale", "DepthOfFieldScale", true,
                           1.0)) return false;
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldBladeCount", "DepthOfFieldBladeCount", true,
                           7.0)) return false;
    } else {
        // 关闭：主开关归零（564 同款：Scale=0 + 焦点距离 0），其余覆盖保留值但不再参与
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldScale", "DepthOfFieldScale", true,
                           0.0)) return false;
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_DepthOfFieldFocalDistance",
                           "DepthOfFieldFocalDistance", true, 0.0)) return false;
    }
    if (a.ca > 1e-6) {
        if (!pp_set_scalar(settings, a.comp_base, "bOverride_SceneFringeIntensity", "SceneFringeIntensity", true,
                           a.ca)) return false;
    } else {
        pp_set_scalar(settings, a.comp_base, "bOverride_SceneFringeIntensity", "SceneFringeIntensity", false, 0.0);
    }
    return true;
}

auto dof_write_guarded(const DofWriteArgs& args) -> bool {
    __try {
        return dof_write_impl(args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 单 bool 参数调用（SEH 兜底）：SetActorTickEnabled(bEnabled) / SetComponentTickEnabled(bEnabled) /
// SetActorEnableCollision(bNewActorEnableCollision) / CloseWorldPartitionStreamingSource(bClose) 一族。
// 返回值允许存在（void/bool 皆可），非 bool 参数即拒绝——与 detail::invoke_set_bool 同纪律，这里补 SEH。
auto invoke_bool_seh(UObject* target, const char* fn, bool value) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    int bool_params = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        if (!prop->IsA<FBoolProperty>()) return false;
        ++bool_params;
    }
    if (bool_params != 1) return false;
    if (!fill_bool_param(params, function,
                         {"bEnabled", "bEnable", "bNewActorEnableCollision", "bClose", "Enable", "Value"},
                         value)) {
        return false;
    }
    return seh_process_event(target, function, params.data());
}

// ---- 564 全保真度：CineCamera 原生 setter 通道（2026-11-07 第 15 轮）----
// 564（已实测同机不崩）对 CineCamera 组件全部走原生函数：SetLensSettings/SetFilmback/
// SetFocusSettings/SetCurrentAperture；我们此前直接写属性——绕过原生 setter 后组件的内部
// 派生状态（焦点缓存等）不更新，渲染端拿到自相矛盾的镜头状态。
// 通用形态：单结构体参数 + 逐成员写值（成员名→数值），SEH 兜底，返回值忽略。

auto set_struct_members_seh(UObject* target, const char* fn,
                            std::initializer_list<std::pair<const char*, double>> numbers,
                            std::initializer_list<std::pair<const char*, bool>> bools) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    FProperty* struct_param = nullptr;
    int value_params = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        if (!prop->IsA<FStructProperty>()) return false;
        ++value_params;
        struct_param = prop;
    }
    if (value_params != 1 || struct_param == nullptr) return false;
    for (const auto& [name, value] : numbers) {
        FProperty* member = struct_member(struct_param, name);
        if (member == nullptr || !write_number(member, params.data(), value)) return false;
    }
    for (const auto& [name, value] : bools) {
        FProperty* member = struct_member(struct_param, name);
        if (member == nullptr || !write_bool(member, params.data(), value)) return false;
    }
    return seh_process_event(target, function, params.data());
}

auto set_single_float_seh(UObject* target, const char* fn, double value) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    FProperty* float_param = nullptr;
    int value_params = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        ++value_params;
        float_param = prop;
    }
    if (value_params != 1 || float_param == nullptr) return false;
    if (!write_number(float_param, params.data(), value)) return false;
    return seh_process_event(target, function, params.data());
}

// 读 FBoolProperty（bitfield/native bool 两态）：bAutoManageActiveCameraTarget 原值快照用。
// 布局按 MemberVariableLayout 实证：FieldSize=0x70 ByteOffset=0x71 ByteMask=0x72。
auto read_bool_prop(RC::Unreal::FProperty* prop, void* base, bool& out) -> bool {
    if (prop == nullptr || base == nullptr) return false;
    auto* bp = reinterpret_cast<uint8_t*>(prop);
    const uint8_t field_size = bp[0x70];
    const uint8_t byte_offset = bp[0x71];
    const uint8_t byte_mask = bp[0x72];
    auto* p = static_cast<uint8_t*>(base) + byte_offset;
    out = field_size == 1 ? ((*p & byte_mask) != 0) : ((*reinterpret_cast<uint32_t*>(p) & 0xFFu) != 0);
    return true;
}

// 无参 bool 返回函数调用：IsWorldPartitionStreamingSourceClosed()。
auto call_bool_fn_seh(UObject* target, const char* fn, bool& out) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!seh_process_event(target, function, params.data())) return false;
    double v = 0.0;
    if (!params.get_number("ReturnValue", v)) return false;
    out = v != 0.0;
    return true;
}

// ---- tagged cvar 闸门（写入 + 回读校验）----

// 读 cvar 数值：KismetSystemLibrary::GetConsoleVariableFloatValue（数值读不受「console 不执行」
// 影响——这是读取 cvar，不是执行命令；本作实机验证过 cvar 反射读可用）。
auto read_cvar_number_strict(const char* cvar, double& out) -> bool {
    UObject* kismet = kismet_system_library();
    if (!is_ok(kismet)) return false;
    UFunction* fn = find_function(kismet, "GetConsoleVariableFloatValue");
    if (fn == nullptr) return false;
    ParamBlock params(fn);
    if (!params.valid()) return false;
    if (!fill_string_param(params, fn, {"VariableName", "CVarName", "Name"}, cvar)) return false;
    if (!seh_process_event(kismet, fn, params.data())) return false;  // 本作原生代码出过读 -1 的访问违例
    return params.get_number("ReturnValue", out);
}

// 写入 + 回读校验。返回 false = 写入失败或回读不一致（调用方只告警不中断）。
auto gate_write(Ue4ssPort& self, const char* cvar, double target, const std::string& tag,
                bool only_if_below = false, bool only_if_zero = false) -> bool {
    double current = 0.0;
    if (!read_cvar_number_strict(cvar, current)) {
        return false;  // 读不到就别乱写（564 同策略：读不到的项跳过）
    }
    if (!(current >= 0.0)) return false;  // 异常值（-1 之类）不碰
    if (only_if_zero && current > 0.0) return true;
    if (only_if_below && current >= target) return true;
    const std::string value_text = [target] {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%g", target);
        return std::string(buf);
    }();
    UObject* lib = pbz_blueprint_library();
    if (!is_ok(lib)) return false;
    UFunction* fn = find_function(lib, "SetConsoleVariableTagged");
    if (fn == nullptr) return false;
    ParamBlock params(fn);
    if (!params.valid()) return false;
    const bool filled = fill_string_param(params, fn, {"CVarName", "Name", "VariableName"}, cvar) &&
                        fill_string_param(params, fn, {"Value", "NewValue", "InValue"}, value_text) &&
                        fill_string_param(params, fn, {"Tag", "OwnerTag", "InTag"}, tag);
    if (!filled) return false;
    const bool set_ok = seh_process_event(lib, fn, params.data());  // 本作原生代码出过读 -1 的访问违例
    double applied = 0.0;
    const bool verify = set_ok && read_cvar_number_strict(cvar, applied) &&
                        std::fabs(applied - target) <= 1e-4 * std::max(1.0, std::fabs(target));
    if (!verify) {
        char buf[192]{};
        std::snprintf(buf, sizeof(buf), "闸门 %s 写入未生效（当前=%.4g 目标=%g）——渲染设置可能阻止了该项",
                      cvar, current, target);
        self.log(LogLevel::Warn, buf);
        return false;
    }
    return true;
}

auto tag_unset(Ue4ssPort& self, const std::string& tag) -> bool {
    UObject* lib = pbz_blueprint_library();
    if (!is_ok(lib)) return false;
    UFunction* unset_fn = find_function(lib, "UnsetConsoleVariablesWithTag");
    if (unset_fn == nullptr) return false;
    ParamBlock unset_params(unset_fn);
    if (!unset_params.valid()) return false;
    const bool unset_filled = fill_string_param(unset_params, unset_fn, {"Tag", "OwnerTag", "InTag"}, tag);
    const bool unset_ok = unset_filled && seh_process_event(lib, unset_fn, unset_params.data());
    // HasConsoleVariablesWithTag(tag) 应为 false：残留 = 有别的 owner 占着同名标签或解绑失败
    bool still_has = false;
    UFunction* has_fn = find_function(lib, "HasConsoleVariablesWithTag");
    bool has_ok = false;
    if (has_fn != nullptr) {
        ParamBlock has_params(has_fn);
        if (has_params.valid() &&
            fill_string_param(has_params, has_fn, {"Tag", "OwnerTag", "InTag"}, tag)) {
            has_ok = seh_process_event(lib, has_fn, has_params.data());
            if (has_ok) {
                double hv = 0.0;
                has_ok = has_params.get_number("ReturnValue", hv);
                still_has = hv != 0.0;
            }
        }
    }
    if (!unset_ok || (has_ok && still_has)) {
        self.log(LogLevel::Warn, "tagged cvar 覆盖解绑未确认干净：tag=" + tag);
        return false;
    }
    return true;
}
}  // namespace

// ------------------------------------------------------------------ 门控

bool Ue4ssPort::ensure_dof_gate(bool need_dof, bool need_film) {
    if (!is_ok(world())) return false;
    if (!is_ok(controller())) return false;
    // 组件通道前提：spawn 出的相机仍在，且组件上有 PostProcessSettings（CameraActor/CineCamera 都有）
    UObject* component = camera_component();
    if (!is_ok(camera_actor_) || !is_ok(component) || find_property(component, "PostProcessSettings") == nullptr) {
        if (!dof_gate_probe_logged_) {
            dof_gate_probe_logged_ = true;
            log(LogLevel::Warn, "景深/胶片：摄影相机组件不可用（需要独立相机后端），无法启用");
        }
        return false;
    }
    if (!is_ok(pbz_blueprint_library())) {
        if (!dof_gate_probe_logged_) {
            dof_gate_probe_logged_ = true;
            log(LogLevel::Warn, "景深/胶片：游戏未提供 PBZBlueprintFunctionLibrary（tagged cvar 接口缺失），无法启用");
        }
        return false;
    }

    // 顺序（2026-11-07 崩溃取证后的刻意安排）：先做全部 tagged cvar 闸门（纯 cvar 读写，
    // 最安全的 ProcessEvent），NLP 过场渲染档最后开——它的所有权处理在游戏原生更新里进行，
    // 首次实机在开启调用返回后 ~1ms 崩在 PBZ.exe 的虚表调用上（读 -1）。（历史：当年把 NLP
    // 放最后 + 独立取证开关，本为在「只有闸门」配置下实测 DOF——2026-11-08 实机证伪该假设，
    // gates-only 无任何景深画面，独立键已删并入总开关。）
    const bool first_run = !dof_gate_steps_logged_;
    if (first_run) {
        dof_gate_steps_logged_ = true;
        log(LogLevel::Info, "景深门控：开始逐步建立（首启逐步打点，此后不再重复）");
    }

    if (need_dof && !dof_cvar_tag_active_) {
        if (first_run) log(LogLevel::Info, "景深门控：写 DOF cvar 闸门…");
        // 复用上一轮未解绑的标签（部分建成失败重试）：全部闸门在同一标签下重写，release 才能
        // 一次解绑干净；换新标签会让旧标签的已生效覆盖永远无人解绑（泄漏到游戏重启）
        if (dof_cvar_tag_.empty()) {
            dof_cvar_tag_ = "PBZPM.DOF." + std::to_string(++dof_tag_serial_);
        }
        bool all_ok = true;
        // ShowFlag.DepthOfField 是当年反射拿不到的总开关；值 ≠1 就写 1
        {
            double showflag = 0.0;
            if (read_cvar_number_strict("ShowFlag.DepthOfField", showflag)) {
                if (static_cast<int>(showflag) != tuning::kDofGateShowFlag) {
                    all_ok = gate_write(*this, "ShowFlag.DepthOfField",
                                        static_cast<double>(tuning::kDofGateShowFlag), dof_cvar_tag_) && all_ok;
                }
            } else {
                log(LogLevel::Warn, "读不到 ShowFlag.DepthOfField，跳过总开关闸门（NLP 档位可能已带 DOF pass）");
            }
        }
        all_ok = gate_write(*this, "r.DepthOfFieldQuality", tuning::kDofGateQuality, dof_cvar_tag_, false, true) &&
                 all_ok;
        all_ok = gate_write(*this, "r.DOF.Kernel.MaxForegroundRadius", tuning::kDofGateKernelRadius, dof_cvar_tag_,
                            false, true) &&
                 all_ok;
        all_ok = gate_write(*this, "r.DOF.Kernel.MaxBackgroundRadius", tuning::kDofGateKernelRadius, dof_cvar_tag_,
                            false, true) &&
                 all_ok;
        // bokeh 组 = 2026-11-07 崩溃二分首位嫌疑（第 5/6 次崩溃 call rdx→全零页，逐字节确定复现；
        // bokeh 路径可能在本作 shipping 无实现，cvar 存在但实现是空壳）。dof_bokeh_gate=false 时整组跳过，
        // 仅保留 ShowFlag/画质/kernel 半径闸门——DOF pass 仍应能开（散景形状退化为默认圆盘）。
        if (bokeh_gate_config_) {
            all_ok = gate_write(*this, "r.DOF.Gather.EnableBokehSettings", tuning::kDofGateBokehSettings,
                                dof_cvar_tag_, true, false) &&
                     all_ok;
            all_ok = gate_write(*this, "r.DOF.Recombine.EnableBokehSettings", tuning::kDofGateBokehSettings,
                                dof_cvar_tag_, true, false) &&
                     all_ok;
            all_ok = gate_write(*this, "r.DOF.Recombine.Quality", tuning::kDofGateRecombineQuality, dof_cvar_tag_,
                                true, false) &&
                     all_ok;
        } else if (first_run) {
            log(LogLevel::Info, "景深门控：dof_bokeh_gate=false，跳过 bokeh 组 cvar（崩溃二分）");
        }
        if (all_ok) dof_cvar_tag_active_ = true;
    }
    if (need_film && !film_cvar_tag_active_) {
        if (first_run) log(LogLevel::Info, "景深门控：写胶片 cvar 闸门（色差）…");
        // 同 DOF 标签：重试复用同一未解绑标签，防止部分建成的旧标签泄漏
        if (film_cvar_tag_.empty()) {
            film_cvar_tag_ = "PBZPM.Film." + std::to_string(++dof_tag_serial_);
        }
        bool all_ok = true;
        all_ok = gate_write(*this, "r.Tonemapper.Quality", tuning::kFilmGateTonemapperQuality, film_cvar_tag_, true,
                            false) &&
                 all_ok;
        all_ok = gate_write(*this, "r.SceneColorFringeQuality", 1.0, film_cvar_tag_, false, true) && all_ok;
        all_ok = gate_write(*this, "r.LUT.Size", tuning::kFilmGateLutSize, film_cvar_tag_, true, false) && all_ok;
        // 回环量化抖动只在 SDR 下抬（HDR 输出有自己的量化策略，564 同款判定）
        double hdr_output = 0.0, hdr_device = 0.0;
        const bool sdr = read_cvar_number_strict("r.HDR.EnableHDROutput", hdr_output) &&
                         read_cvar_number_strict("r.HDR.Display.OutputDevice", hdr_device) &&
                         hdr_output == 0.0 && hdr_device < 3.0;
        if (sdr) {
            all_ok = gate_write(*this, "r.BackbufferQuantizationDitheringOverride",
                                tuning::kFilmGateDitherOverride, film_cvar_tag_, true, false) &&
                     all_ok;
        }
        if (all_ok) film_cvar_tag_active_ = true;
    }
    // NLP 过场渲染档最后开（见本函数开头的顺序说明）。need_dof 是硬前提（2026-11-08 实机取证）：
    // NLP 档是**全局渲染档**，请求成功游戏原生过场 DOF 立即出现在画面里（游戏自己的对焦逻辑、
    // 随镜头移动、本 mod 参数管不到）——胶片（色差）建控时绝不能顺手开档，否则「景深总开关
    // 关着却有景深、参数调不动」。主开关关掉后的档位释放由 core 每帧同步
    //（apply_dof_if_needed → release_nlp_profile），退出路径兜底。
    // NLP 已并入景深总开关（2026-11-08 用户决策，独立键登记 Deprecated）：总开关开 = 闸门 +
    // NLP 档 + 组件通道一体（564 同款形态）——实机已证伪「仅靠 ShowFlag/画质闸门 DOF 就能渲染」
    //（gates-only 无任何景深画面），档位没有独立存在的理由。
    if (need_dof && !dof_nlp_active_ && !nlp_enable_exhausted_) {
        if (first_run) log(LogLevel::Info, "景深门控：请求 NLP 过场渲染档…");
        if (!ensure_nlp_profile(true)) {
            // 开启方向的失败全是会话内永久性的（接口缺失/类缺失/参数不匹配/AV 闩）：
            // 置闩停止后续重试——core 的 nlp_owed 早退放行靠它收口，避免每帧解冻重请求
            nlp_enable_exhausted_ = true;
            if (first_run) {
                log(LogLevel::Warn, "景深门控：NLP 档未开启（cvar 闸门已建，但本作 gates-only 无 DOF 画面）；本会话不再重试");
            }
        }
    }
    const bool usable = dof_cvar_tag_active_ || film_cvar_tag_active_ || dof_nlp_active_;
    if (!usable) {
        // 三组全都没建成：就地解绑部分建成的 tag 残留——core 会对 false 置 unavailable 闩，
        // 本会话不会再进来，退出路径也就没有解绑机会了（best-effort，失败仅告警）
        if (!film_cvar_tag_.empty() && tag_unset(*this, film_cvar_tag_)) film_cvar_tag_.clear();
        if (!dof_cvar_tag_.empty() && tag_unset(*this, dof_cvar_tag_)) dof_cvar_tag_.clear();
        return false;
    }
    if (dof_probe_pending_frames_ == 0) dof_probe_pending_frames_ = 8;
    return usable;
}

bool Ue4ssPort::ensure_nlp_profile(bool enable) {
    // 本会话开启方向已触发过 AV：兑现「不再尝试」的日志承诺（关闭方向不受闸，退出流程必须能重试解绑）
    if (enable && nlp_av_disabled_) return false;
    UObject* manager = find_object("/Script/ProjectPBZ.Default__PBZLevelSequenceManager");
    if (!is_ok(manager)) {
        if (!dof_gate_probe_logged_) {
            dof_gate_probe_logged_ = true;
            log(LogLevel::Warn, "景深/胶片：PBZLevelSequenceManager 不可用（NLP 过场渲染档接口缺失），无法启用");
        }
        return false;
    }
    UFunction* fn = find_function(manager, "SetNLPProfileCinematicEnable");
    if (fn == nullptr) {
        if (!dof_gate_probe_logged_) {
            dof_gate_probe_logged_ = true;
            log(LogLevel::Warn, "景深/胶片：SetNLPProfileCinematicEnable 不存在（游戏更新可能改名），无法启用");
        }
        return false;
    }
    if (enable && !is_ok(dof_nlp_token_)) {
        // 瞬态 LevelSequence 作所有权标记：不播放任何序列；RF_Standalone 防 GC，进程内复用。
        // 类路径候选：ULevelSequence 属于 LevelSequence 模块（/Script/LevelSequence.LevelSequence），
        // 不在 MovieScene 包下（2026-11-07 实机日志：MovieScene 路径找不到）。
        auto* seq_class = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UClass*>(
            nullptr, nullptr, STR("/Script/LevelSequence.LevelSequence"));
        if (seq_class == nullptr) {
            seq_class = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UClass*>(
                nullptr, nullptr, STR("/Script/MovieScene.LevelSequence"));
        }
        if (seq_class == nullptr) {
            if (!dof_gate_probe_logged_) {
                dof_gate_probe_logged_ = true;
                log(LogLevel::Warn,
                    "景深/胶片：找不到 LevelSequence 类（/Script/LevelSequence 与 /Script/MovieScene 均未命中），"
                    "无法创建 NLP 所有权标记");
            }
            return false;
        }
        dof_nlp_token_ = RC::Unreal::UObjectGlobals::NewObject<RC::Unreal::UObject>(
            nlp_token_outer(world()), seq_class, RC::Unreal::FName{}, RC::Unreal::RF_Standalone);
        if (!is_ok(dof_nlp_token_)) {
            log(LogLevel::Warn, "景深/胶片：NLP 所有权标记创建失败");
            return false;
        }
    }
    ParamBlock params(fn);
    if (!params.valid()) return false;
    const bool filled = fill_object_param(params, fn, {"Sequence", "Owner", "Token", "LevelSequence"},
                                          dof_nlp_token_) &&
                        fill_bool_param(params, fn, {"bEnable", "Enable", "bEnabled"}, enable);
    if (!filled) return false;
    const bool set_ok = seh_process_event(manager, fn, params.data());
    if (!set_ok) {
        // 本作原生代码出过读 -1 的访问违例（三份 crash dump 同址：PBZ.exe+0x43AE0D4 附近）；
        // 拦下后游戏状态可能半改。开启方向本会话拉闸（nlp_av_disabled_）；关闭方向保留给
        // 退出流程退避重试（拉闸会让门控永远解不干净）
        log(LogLevel::Warn, std::string("NLP 过场渲染档") + (enable ? "开启" : "关闭") +
                                "调用触发访问违例（已拦下，游戏原生 NLP 处理不兼容；" +
                                (enable ? "本会话不再尝试开启" : "由退出流程继续重试关闭") + "）");
        if (enable) nlp_av_disabled_ = true;
        return false;
    }
    dof_nlp_active_ = enable;
    log(LogLevel::Info, enable ? "NLP 过场渲染档已请求开启（瞬态序列作所有权标记，不播放）"
                               : "NLP 过场渲染档已关闭（所有权标记释放）");
    return true;
}

// 主开关关掉时由 core 每帧同步调用（apply_dof_if_needed）：NLP 档必须严格跟随景深主开关，
// 否则游戏原生过场景深在总开关关着时继续渲染（2026-11-08 实机取证，见 ensure_dof_gate 注释）。
// 解绑纪律与 release_dof_gate 相同：NLP 调用不在暂停态进行（游戏原生更新处理所有权，暂停态会崩）。
bool Ue4ssPort::release_nlp_profile() {
    if (!dof_nlp_active_) return true;
    bool was_paused = false;
    if (world_paused()) {
        (void)set_paused(false);
        was_paused = true;
    }
    const bool ok = ensure_nlp_profile(false);
    if (was_paused) (void)set_paused(true);
    return ok;
}

bool Ue4ssPort::release_dof_gate() {
    bool ok = true;
    // 标签非空即尝试解绑：active=完整建成；非空未 active=部分建成的残留（中途失败），同样要还。
    // 解绑成功清空字符串（下次建控用新序号）；失败保留（core 退避重试时复用同一标签）。
    if (film_cvar_tag_active_ || !film_cvar_tag_.empty()) {
        if (tag_unset(*this, film_cvar_tag_)) {
            film_cvar_tag_active_ = false;
            film_cvar_tag_.clear();
        } else {
            ok = false;
        }
    }
    if (dof_cvar_tag_active_ || !dof_cvar_tag_.empty()) {
        if (tag_unset(*this, dof_cvar_tag_)) {
            dof_cvar_tag_active_ = false;
            dof_cvar_tag_.clear();
        } else {
            ok = false;
        }
    }
    if (dof_nlp_active_) {
        // 与启用同理：NLP 调用不在冻结状态下进行（游戏原生更新处理所有权，暂停态会崩）。
        // 解冻窗口内关档；随后 PhotoMode 的冻结还原会再解一次，最终状态不受影响。
        bool was_paused = false;
        if (world_paused()) {
            (void)set_paused(false);
            was_paused = true;
        }
        if (!ensure_nlp_profile(false)) ok = false;  // 失败保留 active，由 core 退避重试
        if (was_paused) (void)set_paused(true);
    }
    dof_current_valid_ = false;
    dof_component_seeded_ = false;
    dof_seeded_actor_ = nullptr;
    dof_probe_pending_frames_ = 0;
    // NLP 开启失败的会话闩随退出清零：重进摄影模式重新探测（与 core 的 dof_unavailable_ 同语义）
    nlp_enable_exhausted_ = false;
    // 564 保真度还原：bAutoManageActiveCameraTarget 回原值
    if (auto_camera_managed_original_) {
        UObject* pc = controller();
        FProperty* auto_prop = is_ok(pc) ? find_property(pc, "bAutoManageActiveCameraTarget") : nullptr;
        if (auto_prop != nullptr) (void)write_bool(auto_prop, pc, true);
        auto_camera_managed_original_ = false;
    }
    dof_lens_seeded_ = false;
    dof_lens_actor_ = nullptr;
    // 564 同款防御还原：流送源回到进模式前状态（相机 actor 随退出销毁，tick/碰撞无需还原）
    if (dof_streaming_pinned_) {
        UObject* pc = controller();
        if (is_ok(pc)) {
            (void)invoke_bool_seh(pc, "CloseWorldPartitionStreamingSource", dof_streaming_closed_before_);
        }
        dof_streaming_pinned_ = false;
        log(LogLevel::Info, dof_streaming_closed_before_ ? "景深防御还原：流送源保持原关闭态"
                                                        : "景深防御还原：流送源已重新开放");
    }
    dof_defenses_actor_ = nullptr;
    dof_defenses_warned_ = nullptr;
    return ok;
}

// ------------------------------------------------------------------ 组件通道

bool Ue4ssPort::film_gate_active() const { return film_cvar_tag_active_; }

bool Ue4ssPort::nlp_profile_active() const { return dof_nlp_active_; }

bool Ue4ssPort::nlp_enable_exhausted() const { return nlp_enable_exhausted_; }

bool Ue4ssPort::apply_dof(const DofParams& params) {
    if (!is_ok(camera_actor_)) return false;
    UObject* component = camera_component();
    if (!is_ok(component)) return false;
    FProperty* settings_prop = find_property(component, "PostProcessSettings");
    if (settings_prop == nullptr) return false;
    void* comp_base = struct_member_base(settings_prop, component);

    // 564 同款防御（2026-11-07 崩溃取证）：第 5/6/7 次崩溃 `call rdx`→全零页，自定义小栈 worker，
    // 相机移动（W/鼠标）触发、与门控配置无关。差异取证结论：564 在 DOF 活跃期 ①CloseWorldPartition
    // StreamingSource 钉住流送源（注释原话 "do not stream a new world from the free camera"——暂停期
    // full-tick 里自由相机移动会让流送源跟着 ViewTarget 走，触发瓦片加载/卸载）；②禁相机/组件 tick
    // 与碰撞。我们此前全都没有。按相机 Actor 记账只做一次，失败告警不阻断。
    if (dof_defenses_actor_ != camera_actor_) {
        UObject* pc = controller();
        bool closed_before = false;
        const bool closed_readable = is_ok(pc) && call_bool_fn_seh(pc, "IsWorldPartitionStreamingSourceClosed", closed_before);
        const bool pin_ok = closed_readable && !closed_before &&
                            invoke_bool_seh(pc, "CloseWorldPartitionStreamingSource", true);
        if (closed_readable && closed_before) log(LogLevel::Info, "景深防御：流送源原本已关闭，无需钉住");
        const bool actor_tick_ok = is_ok(camera_actor_) && invoke_bool_seh(camera_actor_, "SetActorTickEnabled", false);
        const bool comp_tick_ok = invoke_bool_seh(component, "SetComponentTickEnabled", false);
        const bool collision_ok = is_ok(camera_actor_) && invoke_bool_seh(camera_actor_, "SetActorEnableCollision", false);
        const bool ticks_ok = actor_tick_ok || comp_tick_ok || collision_ok;
        if (pin_ok || ticks_ok) {
            dof_defenses_actor_ = camera_actor_;
            dof_streaming_pinned_ = pin_ok;
            dof_streaming_closed_before_ = closed_before;
            log(LogLevel::Info, std::string("景深防御（564 同款）：流送源") +
                                    (pin_ok ? "已钉住" : (closed_readable ? "原已关闭" : "钉住失败")) +
                                    "，相机/组件 tick 与碰撞禁用 " +
                                    (ticks_ok ? "完成" : "失败") + "（退出还原）");
        } else if (dof_defenses_warned_ != camera_actor_) {
            dof_defenses_warned_ = camera_actor_;
            log(LogLevel::Warn, "景深防御全部失败（流送钉住/tick 禁用均不可用），DOF 活跃期移动相机有崩溃风险");
        }
    }

    DofWriteArgs args{};
    args.comp_prop = settings_prop;
    args.comp_base = comp_base;
    args.wb_prop = struct_member(settings_prop, "WeightedBlendables");
    args.wbo_prop = struct_member(settings_prop, "bOverride_WeightedBlendables");
    args.dof_enabled = params.dof_enabled;
    args.focal_cm = params.focus_distance_cm;
    args.fstop = params.fstop;
    args.sensor_mm = params.sensor_width_mm;
    args.ca = params.chromatic_aberration;
    args.seed = !dof_component_seeded_ || dof_seeded_actor_ != camera_actor_;
    if (args.seed) {
        // 拷贝源 = 管理器 POV 的 PostProcessSettings（保留游戏原调色/曝光；拷贝失败不阻断）
        PovView pov{};
        if (resolve_pov_view(pov)) {
            FProperty* pov_settings = struct_member(pov.pov, "PostProcessSettings");
            if (pov_settings != nullptr) {
                args.pov_prop = pov_settings;
                args.pov_base = struct_member_base(pov_settings, pov.base);
                args.copy_size = static_cast<size_t>(std::min(settings_prop->GetSize(), pov_settings->GetSize()));
            }
        }
    }
    if (!dof_write_guarded(args)) {
        log(LogLevel::Warn, "景深/胶片：组件 PostProcessSettings 写入触发异常（已拦下，本帧放弃）");
        return false;
    }
    dof_component_seeded_ = true;
    dof_seeded_actor_ = camera_actor_;
    if (!dof_apply_step_logged_) {
        dof_apply_step_logged_ = true;
        log(LogLevel::Info, std::string("景深通道：组件 PostProcessSettings 写入完成（含 POV 拷贝），"
                                        "反别名补丁 WeightedBlendables=") +
                               (args.wb_prop != nullptr ? "已置空" : "属性未找到(补丁未生效!)") +
                               "，不再调 ForceUpdateCameraView（AV 毒源隔离）");
    }

    // 组件通道权重拉满（ POV 每帧由相机组件重建，权重 0 的组件设置不进画面）
    (void)write_number(find_property(component, "PostProcessBlendWeight"), component, 1.0);

    // 564 全保真度（第 15 轮）：镜头元数据全部走原生 setter（SetFocusSettings/SetCurrentAperture/
    // SetLensSettings/SetFilmback）——直接写属性绕过原生函数后组件内部派生状态不更新。
    // 564 实测参数：LensSettings MinF=1/MaxF=32/MinFocusDist=5cm/BladeCount=7/Squeeze=1；
    // Filmback 传感器宽 36mm、高 36/宽高比。失败逐项告警不阻断（普通 CameraActor 缺函数自然跳过）。
    bool lens_ok = false;
    if (!dof_lens_seeded_ || dof_lens_actor_ != camera_actor_) {
        // SetLensSettings(FCameraLensSettings)：一次性镜头骨架（对焦/光圈范围按 564）
        lens_ok = set_struct_members_seh(
            component, "SetLensSettings",
            {{"MinFocalLength", 12.0},
             {"MaxFocalLength", 200.0},
             {"MinFStop", 1.0},
             {"MaxFStop", 32.0},
             {"MinimumFocusDistance", 5.0},
             {"SqueezeFactor", 1.0},
             {"DiaphragmBladeCount", 7.0}},
            {});
        // 注意：不写 SetFilmback——我们的相机是游戏关卡里现成的 CineCameraActor（非新生成），
        // FOV 同步按组件当前 SensorWidth 反算焦距（camera_view.cpp），踩 Filmback 会改变 FOV 映射
        //（第 15 轮实测：进模式画面放大）。564 用 36mm 是因为它 spawn 全新相机，场景不同。
        // bAutoManageActiveCameraTarget=false（564 同款，防止游戏自动相机管理抢回 ViewTarget；
        // 原值在 release_dof_gate 还原）
        UObject* pc = controller();
        bool auto_managed = false;
        FProperty* auto_prop = is_ok(pc) ? find_property(pc, "bAutoManageActiveCameraTarget") : nullptr;
        if (auto_prop != nullptr && read_bool_prop(auto_prop, pc, auto_managed)) {
            if (auto_managed && write_bool(auto_prop, pc, false)) {
                auto_camera_managed_original_ = true;
            }
        }
        dof_lens_seeded_ = true;
        dof_lens_actor_ = camera_actor_;
        if (!lens_ok && !dof_lens_warn_logged_) {
            dof_lens_warn_logged_ = true;
            log(LogLevel::Warn, "景深通道：SetLensSettings 不可用（回退属性写入语义）");
        }
    }
    // SetFocusSettings(FCameraFocusSettings)：对焦方式/距离/平滑（每次应用都写，564 同款）
    const bool focus_ok = set_struct_members_seh(
        component, "SetFocusSettings",
        {{"FocusMethod", params.dof_enabled ? 1.0 : 0.0},
         {"ManualFocusDistance", params.focus_distance_cm},
         {"FocusOffset", 0.0}},
        {{"bSmoothFocusChanges", false}});
    // SetCurrentAperture(float)：光圈
    const bool aperture_ok = set_single_float_seh(component, "SetCurrentAperture", params.fstop);
    if (!dof_apply_step_logged_) {
        dof_apply_step_logged_ = true;
        log(LogLevel::Info, std::string("景深通道：原生 setter Focus=") + (focus_ok ? "OK" : "失败") +
                                " Aperture=" + (aperture_ok ? "OK" : "失败") +
                                " Lens=" + (lens_ok ? "OK" : (dof_lens_seeded_ ? "已 seed" : "失败")));
    }

    dof_current_ = params;
    dof_current_valid_ = true;
    call_force_update_view();
    return true;
}

// ------------------------------------------------------------------ 每帧

bool Ue4ssPort::call_force_update_view() {
    // 2026-11-07 第 9 轮取证：本调用在 UE4SS 钩子语境下每次会话首调必 AV（PBZ.exe+0x43AE0D4 家族），
    // SEH 吞掉后相机管理器处于半更新态，后续每帧 UpdateCameraManager 消费损坏状态 → 崩溃家族。
    // 564 在 PuerTS 定时器语境调同一函数无恙，语境不可复制，故本环境彻底不调；
    // 暂停期常规相机更新每帧照跑（bShouldPerformFullTickWhenPaused=true），视图刷新天然兜底。
    if (force_view_never_) return false;
    if (force_update_view_fn_missing_) return false;
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    if (force_update_view_fn_ == nullptr) {
        force_update_view_fn_ = find_function(pc, "ForceUpdateCameraView");
        if (force_update_view_fn_ == nullptr) {
            force_update_view_fn_missing_ = true;
            log(LogLevel::Warn, "PlayerController::ForceUpdateCameraView 不存在（冻结期镜头可能不刷新）");
            return false;
        }
    }
    ParamBlock params(force_update_view_fn_);
    if (!params.valid()) return false;
    if (!seh_process_event(pc, force_update_view_fn_, params.data())) {
        // 三次实机崩溃均发生在 NLP/门控之后的相机刷新链路上；拦下并永久停用本调用，
        // 相机刷新依赖暂停期 UpdateCameraManager 的常规每帧更新兜底
        force_update_view_fn_missing_ = true;
        log(LogLevel::Warn, "ForceUpdateCameraView 触发访问违例（已拦下并停用本调用；"
                            "相机刷新改由暂停期常规相机更新兜底）");
        return false;
    }
    return true;
}

void Ue4ssPort::dof_frame_tick() {
    // 世界时钟停住时原生镜头/视图不会自动刷新：DOF/胶片活动期间每帧强制刷新（564 同款）
    if (dof_current_valid_ && dof_current_.any_active()) call_force_update_view();

    // 延迟回读探针：启用后第 8 帧（NLP 档在原生更新里生效需要几帧）一次性记录渲染状态
    if (dof_probe_pending_frames_ > 0 && --dof_probe_pending_frames_ == 0) {
        double recombine = -1.0, scene_format = -1.0, sss_quality = -1.0, showflag = -1.0, quality = -1.0;
        const bool has_recombine = read_cvar_number_strict("r.DOF.Recombine.Quality", recombine);
        const bool has_format = read_cvar_number_strict("r.SceneColorFormat", scene_format);
        (void)read_cvar_number_strict("r.SSS.Quality", sss_quality);
        (void)read_cvar_number_strict("ShowFlag.DepthOfField", showflag);
        (void)read_cvar_number_strict("r.DepthOfFieldQuality", quality);
        char buf[320]{};
        std::snprintf(buf, sizeof(buf),
                      "景深诊断：NLP档已请求=%d showflag=%.0f dofQuality=%.0f recombine=%.0f(读%s) "
                      "sceneColorFormat=%.0f(读%s) sssQuality=%.0f 组件FOC=%.0fcm F=%.2f scale=%.1f",
                      dof_nlp_active_ ? 1 : 0, showflag, quality, recombine, has_recombine ? "到" : "不到",
                      scene_format, has_format ? "到" : "不到", sss_quality, dof_current_.focus_distance_cm,
                      dof_current_.fstop, dof_current_.dof_enabled ? 1.0 : 0.0);
        log(LogLevel::Info, buf);
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
