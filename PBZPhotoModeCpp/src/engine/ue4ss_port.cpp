// =============================================================================
//  UE4SS 反射层实现（EnginePort 的 C++ 版）
//
//  这一层只做一件事：把 EnginePort 的语义翻译成 UE4SS/UEPseudo 的真实调用。
//  这里用到的每个 API 都在 UE4SS v3.0.1-1183（tag: v3.0.1-1183-gcdbc0b66）的
//  头文件里核对过，不再依赖“猜出来的名字”。
//
//  设计要点：
//   1) 参数块不硬编码布局：用反射按“参数名 + 参数类型”写值，引擎换版本也不会错位。
//   2) 能走强类型 helper 的（AActor / UWorld::SpawnActor）就走 helper，
//      只有 PlayerController 这类没有 helper 头文件的才用 ProcessEvent。
//   3) 一切失败都返回 false，由 core/photo_mode.cpp 决定回滚，不在这里抛异常。
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FHitResult.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/World.hpp>

// Windows.h 必须在 UE 头文件之后、并且关掉 min/max 宏：
// 否则 UnrealString.hpp / Hooks/Constants.hpp 里的 std::max(...) 会被当成宏调用
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace pbzpm {
namespace {

using namespace RC::Unreal;

// ------------------------------------------------------------------ 基础工具

auto widen(const std::string& text) -> RC::StringType {
    if (text.empty()) return {};
    const int needed =
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};
    RC::StringType out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

auto narrow(const RC::StringType& text) -> std::string {
    if (text.empty()) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                             nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), 0);
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed, nullptr,
                          nullptr);
    return out;
}

auto emit(pbzpm::LogLevel level, const RC::StringType& text) -> void {
    switch (level) {
    case pbzpm::LogLevel::Error:
        RC::Output::send<RC::LogLevel::Error>(text);
        break;
    case pbzpm::LogLevel::Warn:
        RC::Output::send<RC::LogLevel::Warning>(text);
        break;
    case pbzpm::LogLevel::Debug:
        RC::Output::send<RC::LogLevel::Verbose>(text);
        break;
    case pbzpm::LogLevel::Info:
    default:
        RC::Output::send<RC::LogLevel::Normal>(text);
        break;
    }
}

auto is_ok(const UObject* object) -> bool {
    return object != nullptr && object->GetClassPrivate() != nullptr;
}

// ------------------------------------------------------------------ 反射小工具

// 在 UStruct/UClass/UFunction 的字段链里按名字找属性
auto find_property(const UStruct* owner, const RC::StringType& name) -> FProperty* {
    if (owner == nullptr) return nullptr;
    for (TFieldIterator<FProperty> it(const_cast<UStruct*>(owner)); it; ++it) {
        FProperty* candidate = *it;
        if (candidate != nullptr && candidate->GetName() == name) return candidate;
    }
    return nullptr;
}

auto find_property(UObject* owner, const char* name) -> FProperty* {
    if (owner == nullptr) return nullptr;
    return find_property(owner->GetClassPrivate(), widen(name));
}

auto find_function(UObject* owner, const char* name) -> UFunction* {
    if (owner == nullptr) return nullptr;
    return owner->GetFunctionByNameInChain(widen(name).c_str());
}

auto value_ptr(FProperty* property, void* container) -> void* {
    if (property == nullptr || container == nullptr) return nullptr;
    return property->ContainerPtrToValuePtr<void>(container);
}

// 数值型：float / double / byte / int / bool 全自适应
auto write_number(FProperty* property, void* container, double value) -> bool {
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    if (property->IsA<FFloatProperty>()) {
        *static_cast<float*>(address) = static_cast<float>(value);
        return true;
    }
    if (property->IsA<FDoubleProperty>()) {
        *static_cast<double*>(address) = value;
        return true;
    }
    if (property->IsA<FByteProperty>()) {
        *static_cast<uint8_t*>(address) = static_cast<uint8_t>(value);
        return true;
    }
    if (property->IsA<FIntProperty>()) {
        *static_cast<int32_t*>(address) = static_cast<int32_t>(value);
        return true;
    }
    if (property->IsA<FUInt32Property>()) {
        *static_cast<uint32_t*>(address) = static_cast<uint32_t>(value);
        return true;
    }
    if (property->IsA<FBoolProperty>()) {
        static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(container, value != 0.0);
        return true;
    }
    return false;
}

auto read_number(FProperty* property, void* container, double& out) -> bool {
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    if (property->IsA<FFloatProperty>()) {
        out = *static_cast<float*>(address);
        return true;
    }
    if (property->IsA<FDoubleProperty>()) {
        out = *static_cast<double*>(address);
        return true;
    }
    if (property->IsA<FByteProperty>()) {
        out = *static_cast<uint8_t*>(address);
        return true;
    }
    if (property->IsA<FIntProperty>()) {
        out = *static_cast<int32_t*>(address);
        return true;
    }
    if (property->IsA<FBoolProperty>()) {
        out = static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(container) ? 1.0 : 0.0;
        return true;
    }
    return false;
}

auto write_object(FProperty* property, void* container, UObject* value) -> bool {
    if (property == nullptr || !property->IsA<FObjectProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    *static_cast<UObject**>(address) = value;
    return true;
}

auto read_object(FProperty* property, void* container, UObject*& out) -> bool {
    if (property == nullptr || !property->IsA<FObjectProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    out = *static_cast<UObject**>(address);
    return out != nullptr;
}

auto write_bool(FProperty* property, void* container, bool value) -> bool {
    if (property == nullptr) return false;
    if (property->IsA<FBoolProperty>()) {
        static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(container, value);
        return true;
    }
    return write_number(property, container, value ? 1.0 : 0.0);
}

auto write_text(FProperty* property, void* container, const std::string& text) -> bool {
    if (property == nullptr || !property->IsA<FStrProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    const RC::StringType wide = widen(text);
    *static_cast<FString*>(address) = FString(wide.c_str());
    return true;
}

auto write_fname(FProperty* property, void* container, const char* text) -> bool {
    if (property == nullptr || !property->IsA<FNameProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    const RC::StringType wide = widen(text);
    *static_cast<FName*>(address) = FName(wide.c_str(), FNAME_Add);
    return true;
}

// 结构体成员（一层）：FVector 的 X/Y/Z、FRotator 的 Pitch/Yaw/Roll、FLinearColor 的 R/G/B/A
auto struct_member_base(FProperty* struct_property, void* container) -> void* {
    if (struct_property == nullptr || !struct_property->IsA<FStructProperty>()) return nullptr;
    return value_ptr(struct_property, container);
}

auto struct_member(FProperty* struct_property, const char* member) -> FProperty* {
    if (struct_property == nullptr || !struct_property->IsA<FStructProperty>()) return nullptr;
    auto* as_struct = static_cast<FStructProperty*>(struct_property);
    UScriptStruct* script_struct = as_struct->GetStruct();
    return find_property(static_cast<UStruct*>(script_struct), widen(member));
}

auto write_struct_number(FProperty* struct_property, void* container, const char* member, double value)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return write_number(struct_member(struct_property, member), base, value);
}

auto read_struct_number(FProperty* struct_property, void* container, const char* member, double& out)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_number(struct_member(struct_property, member), base, out);
}

auto read_name(FProperty* property, void* container, std::string& out) -> bool {
    if (property == nullptr || !property->IsA<FNameProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    out = narrow(static_cast<FName*>(address)->ToString());
    return true;
}

auto read_struct_name(FProperty* struct_property, void* container, const char* member, std::string& out)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_name(struct_member(struct_property, member), base, out);
}

auto read_struct_object(FProperty* struct_property, void* container, const char* member, UObject*& out) -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_object(struct_member(struct_property, member), base, out);
}

// ------------------------------------------------------------------ 参数块

// UFUNCTION 的实参块：只按属性名写值，不假设内存布局
class ParamBlock {
public:
    explicit ParamBlock(UFunction* function) : function_(function) {
        const int32 size = function_ != nullptr ? function_->GetStructureSize() : 0;
        if (size > 0) buffer_.assign(static_cast<size_t>(size), 0);
    }

    [[nodiscard]] auto valid() const -> bool { return function_ != nullptr && !buffer_.empty(); }
    auto data() -> void* { return buffer_.data(); }
    auto function() -> UFunction* { return function_; }

    auto property(const char* name) -> FProperty* { return find_property(function_, widen(name)); }

    // 参数名在不同版本里可能被改（InString→Text 之类）。先按候选名找，找不到就按声明顺序取第 index 个。
    // 这是“不硬编码参数块布局”的进一步退化策略：宁可位置错，也不要整条功能不可用。
    auto prop(std::initializer_list<const char*> names, int index) -> FProperty* {
        for (const char* name : names) {
            if (FProperty* by_name = property(name); by_name != nullptr) return by_name;
        }
        if (function_ == nullptr || index < 0) return nullptr;
        int current = 0;
        for (TFieldIterator<FProperty> it(function_); it; ++it, ++current) {
            if (current == index) return *it;
        }
        return nullptr;
    }

    auto set_object(const char* name, UObject* value) -> bool {
        return write_object(property(name), data(), value);
    }
    auto set_number(const char* name, double value) -> bool {
        return write_number(property(name), data(), value);
    }
    // 属性名大小写敏感（FProperty::GetName() 保留原始大小写），
    // 引擎里 PrintString 的参数就叫小写 duration，这里允许候选名。
    auto set_number_any(std::initializer_list<const char*> names, int index, double value) -> bool {
        return write_number(prop(names, index), data(), value);
    }
    auto set_bool(const char* name, bool value) -> bool { return write_bool(property(name), data(), value); }
    auto set_text(const char* name, const std::string& value) -> bool {
        return write_text(property(name), data(), value);
    }
    auto set_name(const char* name, const char* value) -> bool {
        return write_fname(property(name), data(), value);
    }
    auto set_struct(const char* name, const char* member, double value) -> bool {
        return write_struct_number(property(name), data(), member, value);
    }

    auto get_number(const char* name, double& out) -> bool { return read_number(property(name), data(), out); }

private:
    UFunction* function_{};
    std::vector<uint8_t> buffer_{};
};

// 建参数块 + 执行
template <typename Fn>
auto invoke(UObject* object, const char* function_name, Fn&& fill) -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!fill(params)) return false;
    object->ProcessEvent(function, params.data());
    return true;
}

// 调用返回 bool 的 UFUNCTION：ProcessEvent 成功不代表游戏逻辑接受，
// 例如 GameMode 的 bPauseable=false 时 PlayerController::SetPause 会返回 false。
template <typename Fn>
auto invoke_bool(UObject* object, const char* function_name, Fn&& fill, bool& result) -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!fill(params)) return false;
    object->ProcessEvent(function, params.data());
    double value = 1.0;
    result = !params.get_number("ReturnValue", value) || value != 0.0;
    return true;
}

// AActor::K2_SetActorRotation 是 UEPseudo 手写的包装，内部把 FRotator 的
// Pitch/Yaw/Roll 当成 float 写（UE5 里 FRotator 是 3 个 double，24 字节）。
// 结果只能写进 double 的低 4 字节，实际角度变成约 1e-315，等价于 0：
// 相机位置能动、朝向却永远 (0,0)。这里改成按反射属性写，自动识别 FDoubleProperty。
auto set_actor_rotation(UObject* actor, const FRotator& rotation, bool teleport) -> bool {
    if (!is_ok(actor)) return false;
    UFunction* function = find_function(actor, "K2_SetActorRotation");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* new_rotation = params.property("NewRotation");
    if (!write_struct_number(new_rotation, params.data(), "Pitch", rotation.GetPitch()) ||
        !write_struct_number(new_rotation, params.data(), "Yaw", rotation.GetYaw()) ||
        !write_struct_number(new_rotation, params.data(), "Roll", rotation.GetRoll())) {
        return false;
    }
    if (!params.set_bool("bTeleportPhysics", teleport)) return false;
    actor->ProcessEvent(function, params.data());

    // K2_SetActorRotation 返回 bool；读不到返回值时按“调用成功”处理。
    double result = 1.0;
    if (params.get_number("ReturnValue", result)) return result != 0.0;
    return true;
}

auto find_object(const char* path) -> UObject* {
    if (path == nullptr) return nullptr;
    return RC::Unreal::UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widen(path).c_str());
}

auto gameplay_statics() -> UObject* {
    return find_object("/Script/Engine.Default__GameplayStatics");
}

auto kismet_system_library() -> UObject* {
    return find_object("/Script/Engine.Default__KismetSystemLibrary");
}

}  // namespace

// ------------------------------------------------------------------ 构造/析构

Ue4ssPort::Ue4ssPort(std::filesystem::path mod_dir) : mod_dir_(std::move(mod_dir)) {}

Ue4ssPort::~Ue4ssPort() {
    if (camera_actor_ != nullptr) {
        destroy_camera();
    }
}

// ------------------------------------------------------------------ 上下文查找

RC::Unreal::UObject* Ue4ssPort::controller() const {
    if (is_ok(controller_cache_)) return controller_cache_;

    // FindFirstOf 会返回对象数组里第一个 PlayerController，可能是过场/spectator 的，
    // 甚至是已经失效的旧实例。真正在玩的那个一定同时有 Player(ULocalPlayer) 和 Pawn。
    std::vector<UObject*> candidates;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("PlayerController"), candidates);

    UObject* fallback = nullptr;
    for (UObject* candidate : candidates) {
        if (!is_ok(candidate)) continue;
        if (fallback == nullptr) fallback = candidate;

        UObject* player = nullptr;
        UObject* pawn = nullptr;
        const bool has_player = read_object(find_property(candidate, "Player"), candidate, player);
        const bool has_pawn = read_object(find_property(candidate, "Pawn"), candidate, pawn);
        if (has_player && has_pawn) {
            controller_cache_ = candidate;
            return candidate;
        }
    }
    controller_cache_ = fallback;
    return fallback;
}

RC::Unreal::UObject* Ue4ssPort::world() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    return pc->GetWorld();
}

// AWorldSettings 是 ULevel.WorldSettings（UWorld 本身没有这个属性）。
// 先走 PersistentLevel.WorldSettings，失败再退到 UWorld::K2_GetWorldSettings()。
RC::Unreal::UObject* Ue4ssPort::world_settings() const {
    UObject* scene = world();
    if (!is_ok(scene)) return nullptr;

    UObject* level = nullptr;
    if (read_object(find_property(scene, "PersistentLevel"), scene, level) && is_ok(level)) {
        UObject* settings = nullptr;
        if (read_object(find_property(level, "WorldSettings"), level, settings) && is_ok(settings)) {
            return settings;
        }
    }

    UFunction* function = find_function(scene, "K2_GetWorldSettings");
    if (function == nullptr) return nullptr;
    ParamBlock params(function);
    if (!params.valid()) return nullptr;
    scene->ProcessEvent(function, params.data());
    UObject* settings = nullptr;
    if (read_object(params.prop({"ReturnValue"}, 0), params.data(), settings)) return settings;
    return nullptr;
}

RC::Unreal::UObject* Ue4ssPort::camera_manager() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    UObject* manager = nullptr;
    if (!read_object(find_property(pc, "PlayerCameraManager"), pc, manager)) return nullptr;
    return manager;
}

RC::Unreal::UObject* Ue4ssPort::player_pawn() const {
    UObject* pc = controller();
    if (!is_ok(pc)) return nullptr;
    UObject* pawn = nullptr;
    if (!read_object(find_property(pc, "Pawn"), pc, pawn)) return nullptr;
    return pawn;
}

RC::Unreal::UObject* Ue4ssPort::camera_component() {
    if (camera_component_cache_ != nullptr) return camera_component_cache_;
    if (!is_ok(camera_actor_)) return nullptr;
    UObject* component = nullptr;
    if (!read_object(find_property(camera_actor_, "CameraComponent"), camera_actor_, component)) {
        return nullptr;
    }
    camera_component_cache_ = component;
    return camera_component_cache_;
}

// ------------------------------------------------------------------ EnginePort

bool Ue4ssPort::world_valid() { return is_ok(world()); }

// 调一个返回结构体的无参 UFUNCTION（例如 GetCameraLocation / GetCameraRotation），
// 按字段名读 3 个 double。FVector 用 X/Y/Z，FRotator 用 Pitch/Yaw/Roll。
static auto read_return_vector(UObject* object, const char* function_name, const char* a_name,
                               const char* b_name, const char* c_name, double& a, double& b, double& c)
    -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    object->ProcessEvent(function, params.data());
    FProperty* result = params.prop({"ReturnValue"}, 0);
    if (result == nullptr) return false;
    return read_struct_number(result, params.data(), a_name, a) &&
           read_struct_number(result, params.data(), b_name, b) &&
           read_struct_number(result, params.data(), c_name, c);
}

bool Ue4ssPort::read_pov(CameraPose& out) {
    bool any = false;

    // 最准的来源是相机管理器这一帧真正在用的视角。进模式时用它做初始位姿，
    // 相机就会从玩家当前镜头位置开始，而不是卡在角色身体里（上一版「人物消失」就是这个）。
    if (UObject* manager = camera_manager(); is_ok(manager)) {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double pitch = 0.0;
        double yaw = 0.0;
        double roll = 0.0;
        const bool have_location = read_return_vector(manager, "GetCameraLocation", "X", "Y", "Z", x, y, z);
        const bool have_rotation = read_return_vector(manager, "GetCameraRotation", "Pitch", "Yaw", "Roll", pitch, yaw, roll);
        if (have_location) {
            out.loc.x = x;
            out.loc.y = y;
            out.loc.z = z;
            any = true;
        }
        if (have_rotation) {
            out.rot.pitch = pitch;
            out.rot.yaw = yaw;
            out.rot.roll = roll;
            any = true;
        }
        if (have_location && have_rotation) {
            log(LogLevel::Info, "初始位姿取自相机管理器视角");
            return any;  // FOV 还是走下面的分支统一处理
        }
    }

    if (UObject* pawn = player_pawn(); is_ok(pawn)) {
        auto* actor = static_cast<AActor*>(pawn);
        FVector location = actor->K2_GetActorLocation();
        out.loc.x = location.X();
        out.loc.y = location.Y();
        out.loc.z = location.Z();
        any = true;
    }

    if (UObject* pc = controller(); is_ok(pc)) {
        if (FProperty* control_rotation = find_property(pc, "ControlRotation")) {
            double pitch = 0.0;
            double yaw = 0.0;
            double roll = 0.0;
            if (read_struct_number(control_rotation, pc, "Pitch", pitch) &&
                read_struct_number(control_rotation, pc, "Yaw", yaw) &&
                read_struct_number(control_rotation, pc, "Roll", roll)) {
                out.rot.pitch = pitch;
                out.rot.yaw = yaw;
                out.rot.roll = roll;
                any = true;
            }
        }
    }

    // FOV：优先用摄影相机的当前值，其次用相机管理器的默认值
    if (UObject* component = camera_component(); is_ok(component)) {
        double fov = 0.0;
        if (read_number(find_property(component, "FieldOfView"), component, fov) && fov > 1.0) {
            out.fov = fov;
            any = true;
        }
    } else if (UObject* manager = camera_manager(); is_ok(manager)) {
        double fov = 0.0;
        if (read_number(find_property(manager, "DefaultFOV"), manager, fov) && fov > 1.0) {
            out.fov = fov;
            any = true;
        }
    }

    return any;
}

bool Ue4ssPort::spawn_camera(const CameraPose& pose) {
    if (is_ok(camera_actor_)) return true;

    UObject* scene = world();
    if (!is_ok(scene)) {
        log(LogLevel::Warn, "拿不到 UWorld，无法生成摄影相机");
        return false;
    }

    UClass* camera_class =
        RC::Unreal::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.CameraActor"));
    if (camera_class == nullptr) {
        log(LogLevel::Warn, "找不到 /Script/Engine.CameraActor 类");
        return false;
    }

    FVector location(pose.loc.x, pose.loc.y, pose.loc.z);
    FRotator rotation(pose.rot.pitch, pose.rot.yaw, pose.rot.roll);

    auto* as_world = static_cast<UWorld*>(scene);
    AActor* actor = as_world->SpawnActor(camera_class, &location, &rotation);
    if (actor == nullptr) {
        log(LogLevel::Warn, "UWorld::SpawnActor 返回空，摄影相机没生成");
        return false;
    }

    camera_actor_ = actor;
    camera_component_cache_ = nullptr;

    // 生成的 CameraActor 如果根组件是 Static，SetActorLocation/Rotation 会被引擎静默拒绝，
    // 现象就是「视角完全不跟输入动」。这里强制把根组件和相机组件的 Mobility 写成 Movable(2)。
    if (UObject* root = nullptr; read_object(find_property(actor, "RootComponent"), actor, root) && is_ok(root)) {
        if (!write_number(find_property(root, "Mobility"), root, 2.0)) {
            log(LogLevel::Warn, "根组件 Mobility 写不进去（可能不是 byte 属性）");
        }
    } else {
        log(LogLevel::Warn, "相机 Actor 上没有 RootComponent");
    }
    if (UObject* component = camera_component(); is_ok(component)) {
        write_number(find_property(component, "Mobility"), component, 2.0);
    }
    return true;
}

bool Ue4ssPort::hook_input_axis(AxisSink sink) {
    if (axis_hook_installed_) return true;

    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    UObject* pc_class = pc->GetClassPrivate();
    if (!is_ok(pc_class)) return false;

    // 引擎侧的鼠标输入最终都会走 PlayerController::InputAxis(Key, Delta, ...)，
    // 这里拿到的是引擎算好的真实增量，比我们自己抓 Raw Input 稳。
    UFunction* function = find_function(pc_class, "InputAxis");
    if (function == nullptr) {
        function = find_function(pc_class, "InputKey");  // 老版本引擎可能只有 InputKey
    }
    if (function == nullptr) {
        // 这个函数在重试循环里会被反复调用，只打一次日志，别刷屏
        if (!axis_probe_logged_) {
            axis_probe_logged_ = true;
            log(LogLevel::Warn, "PlayerController 上没有 InputAxis/InputKey，只能用系统层输入");
        }
        return false;
    }

    axis_function_ = function;
    axis_sink_ = std::move(sink);
    const auto ids = RC::Unreal::UObjectGlobals::RegisterHook(
        function,
        [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
            auto* self = static_cast<Ue4ssPort*>(custom);
            if (self != nullptr) self->on_axis_event(context);
        },
        nullptr, this);
    if (ids.first < 0 && ids.second < 0) {
        log(LogLevel::Warn, "InputAxis 钩子注册失败");
        axis_function_ = nullptr;
        return false;
    }
    axis_hook_installed_ = true;
    log(LogLevel::Info, std::string("已挂上引擎输入钩子：") + narrow(function->GetFullName()));
    return true;
}

void Ue4ssPort::on_axis_event(RC::Unreal::UnrealScriptFunctionCallableContext& context) {
    if (axis_function_ == nullptr || axis_sink_ == nullptr) return;
    void* locals = context.TheStack.Locals();
    if (locals == nullptr) return;

    std::string key_name;
    double amount = 0.0;
    bool have_amount = false;
    bool have_key = false;

    for (TFieldIterator<FProperty> it(axis_function_); it; ++it) {
        FProperty* property = *it;
        if (property == nullptr) continue;
        const std::string name = narrow(property->GetName());
        if (name == "Key") {
            // FKey 结构体里的 KeyName 才是 "MouseX"/"MouseY"/"MouseWheelAxis"
            have_key = read_struct_name(property, locals, "KeyName", key_name);
        } else if (name == "Delta" || name == "Value" || name == "AmountDepressed") {
            if (read_number(property, locals, amount)) have_amount = true;
        }
    }

    if (have_key && have_amount) axis_sink_(key_name, amount);
}

// 引擎这一帧的鼠标增量：PlayerController::GetInputMouseDelta(DeltaX&, DeltaY&)
// 这是游戏自己用的同一条输入通道，不依赖 OS 钩子/前台窗口，是我们最可靠的鼠标来源。
bool Ue4ssPort::read_mouse_delta(double& dx, double& dy) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    UFunction* function = find_function(pc, "GetInputMouseDelta");
    if (function == nullptr) return false;

    ParamBlock params(function);
    if (!params.valid()) return false;
    pc->ProcessEvent(function, params.data());

    double out_x = 0.0;
    double out_y = 0.0;
    if (!read_number(params.prop({"DeltaX", "X"}, 0), params.data(), out_x)) return false;
    if (!read_number(params.prop({"DeltaY", "Y"}, 1), params.data(), out_y)) return false;
    dx = out_x;
    dy = out_y;
    return true;
}

bool Ue4ssPort::attach_view() {
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return false;
    const bool ok = invoke(pc, "SetViewTargetWithBlend", [&](ParamBlock& params) {
        return params.set_object("NewViewTarget", camera_actor_) && params.set_number("BlendTime", 0.0);
    });
    if (!ok) {
        log(LogLevel::Warn, "调用 SetViewTargetWithBlend 失败（参数块没填上或函数不存在）");
        return false;
    }

    // 回读确认：读相机管理器的 ViewTarget.Target（PlayerController.ViewTarget 在这作里读不出来）
    UObject* current = nullptr;
    if (UObject* manager = camera_manager(); is_ok(manager) &&
        read_struct_object(find_property(manager, "ViewTarget"), manager, "Target", current)) {
        if (current != camera_actor_) {
            log(LogLevel::Warn, std::string("ViewTarget 校验失败：管理器当前目标是 ") +
                                    (is_ok(current) ? narrow(current->GetFullName()) : "null") +
                                    "，不是摄影相机");
            view_verified_ = false;
        } else if (!view_verified_) {
            view_verified_ = true;
            log(LogLevel::Info, "相机接管确认：ViewTarget 已是摄影相机");
        }
    }
    return true;
}

void Ue4ssPort::reassert_view_target() {
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return;
    invoke(pc, "SetViewTargetWithBlend", [&](ParamBlock& params) {
        return params.set_object("NewViewTarget", camera_actor_) && params.set_number("BlendTime", 0.0);
    });
}

bool Ue4ssPort::restore_view() {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    UObject* pawn = player_pawn();
    return invoke(pc, "SetViewTargetWithBlend", [&](ParamBlock& params) {
        if (!params.set_object("NewViewTarget", pawn)) return false;
        params.set_number("BlendTime", 0.0);
        return true;
    });
}

bool Ue4ssPort::apply_pose(const CameraPose& pose) {
    if (!is_ok(camera_actor_)) return false;
    auto* actor = static_cast<AActor*>(camera_actor_);
    FVector location(pose.loc.x, pose.loc.y, pose.loc.z);
    FRotator rotation(pose.rot.pitch, pose.rot.yaw, pose.rot.roll);

    // 位置和旋转分开设：K2_SetActorLocationAndRotation 在 5.7 里可能整块失败，
    // 分开调更容易定位是哪一个没生效，也避免一个失败带着另一个一起失败。
    FHitResult hit{};
    const bool moved = actor->K2_SetActorLocation(location, false, hit, false);
    // 不能用 AActor::K2_SetActorRotation：UEPseudo 那个包装按 float 写 FRotator，
    // UE5 的 FRotator 是 double，写进去等于 0（详见 set_actor_rotation 的注释）。
    const bool turned = set_actor_rotation(camera_actor_, rotation, true);

    if (!moved || !turned) {
        if (!pose_apply_failure_logged_) {
            pose_apply_failure_logged_ = true;
            char buf[220]{};
            std::snprintf(buf, sizeof(buf),
                          "相机位姿应用失败：移动=%s 旋转=%s 目标 loc=(%.0f,%.0f,%.0f) rot=(%.1f,%.1f) "
                          "（旋转失败通常是写属性失败，不是 Mobility）",
                          moved ? "成功" : "失败", turned ? "成功" : "失败", pose.loc.x, pose.loc.y, pose.loc.z,
                          pose.rot.pitch, pose.rot.yaw);
            log(LogLevel::Warn, buf);
        }
    } else {
        pose_apply_failure_logged_ = false;
    }
    return moved && turned;
}

bool Ue4ssPort::apply_fov(double fov) {
    UObject* component = camera_component();
    if (!is_ok(component)) return false;
    return write_number(find_property(component, "FieldOfView"), component, fov);
}

bool Ue4ssPort::destroy_camera() {
    const bool had_view = is_ok(camera_actor_);
    if (had_view) {
        restore_view();
        if (auto* actor = static_cast<AActor*>(camera_actor_); actor != nullptr) {
            actor->K2_DestroyActor();
        }
    }
    camera_actor_ = nullptr;
    camera_component_cache_ = nullptr;
    controller_cache_ = nullptr;
    view_verified_ = false;
    return had_view;
}

bool Ue4ssPort::set_ignore_move_input(bool ignore) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    return invoke(pc, "SetIgnoreMoveInput", [&](ParamBlock& params) {
        return params.set_bool("bNewMoveInput", ignore) || params.set_bool("bIgnore", ignore) ||
               params.set_bool("NewMoveInput", ignore);
    });
}

bool Ue4ssPort::set_game_input_blocked(bool blocked) {
    bool touched = false;

    // 1) 输入栈中断：PC / Pawn 上的 InputComponent 打 bBlockInput。
    //    动作类绑定（攻击、交互、技能）挂在 Pawn 的 InputComponent 上，
    //    SetIgnoreMoveInput 只管移动轴，挡不住它们。
    UObject* owners[2] = {controller(), player_pawn()};
    for (UObject* owner : owners) {
        if (!is_ok(owner)) continue;
        if (write_bool(find_property(owner, "bBlockInput"), owner, blocked)) touched = true;

        UObject* component = nullptr;
        if (read_object(find_property(owner, "InputComponent"), owner, component) && is_ok(component)) {
            if (write_bool(find_property(component, "bBlockInput"), component, blocked)) touched = true;
        }
    }

    // 2) 视角轴一起关掉：否则游戏仍会消耗鼠标位移、把玩家的控制旋转算进去
    UObject* pc = controller();
    if (is_ok(pc)) {
        invoke(pc, "SetIgnoreLookInput", [&](ParamBlock& params) {
            return params.set_bool("bNewLookInput", blocked) || params.set_bool("bIgnore", blocked) ||
                   params.set_bool("NewLookInput", blocked);
        });
        touched = true;

        // 3) 最后一道闸：把 PC 与 Pawn 的 InputComponent 直接停用。
        //    bBlockInput 只管“输入栈是否继续往下走”，蓝图里某些动作可能绕过它；
        //    DisableInput 是官方语义上的“这个 Actor 不再接收输入”。
        const char* function = blocked ? "DisableInput" : "EnableInput";
        invoke(pc, function, [&](ParamBlock& params) { return params.set_object("PlayerController", pc); });
        UObject* pawn = player_pawn();
        if (is_ok(pawn)) {
            invoke(pawn, function, [&](ParamBlock& params) { return params.set_object("PlayerController", pc); });
        }
    }
    return touched;
}

bool Ue4ssPort::set_player_hidden(bool hidden) {
    UObject* pawn = player_pawn();
    if (!is_ok(pawn)) return false;
    static_cast<AActor*>(pawn)->SetActorHiddenInGame(hidden);
    return true;
}

bool Ue4ssPort::set_hud_visible(bool visible) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    UObject* hud = nullptr;
    if (!read_object(find_property(pc, "MyHUD"), pc, hud)) return false;
    static_cast<AActor*>(hud)->SetActorHiddenInGame(!visible);
    return true;
}

double Ue4ssPort::get_time_dilation() {
    UObject* statics = gameplay_statics();
    UObject* scene = world();
    if (is_ok(statics) && is_ok(scene)) {
        UFunction* function = find_function(statics, "GetGlobalTimeDilation");
        if (function != nullptr) {
            ParamBlock params(function);
            if (params.valid()) {
                params.set_object("WorldContextObject", scene);
                statics->ProcessEvent(function, params.data());
                double value = 1.0;
                if (params.get_number("ReturnValue", value) && value > 0.0) return value;
            }
        }
    }

    // 兜底：直接读 WorldSettings.当 bAllowTimeDilation=false 时它仍可能是旧值，
    // 但至少比恒返回 1.0 更能反映真实状态。
    if (UObject* settings = world_settings(); is_ok(settings)) {
        double value = 1.0;
        if (read_number(find_property(settings, "TimeDilation"), settings, value) && value > 0.0) return value;
    }
    return 1.0;
}

std::string Ue4ssPort::time_settings_summary() {
    UObject* settings = world_settings();
    if (!is_ok(settings)) return "WorldSettings=读不到";
    double allow = 1.0;
    double dilation = 1.0;
    double min_dilation = 0.0;
    double max_dilation = 0.0;
    const bool have_allow = read_number(find_property(settings, "bAllowTimeDilation"), settings, allow);
    const bool have_dilation = read_number(find_property(settings, "TimeDilation"), settings, dilation);
    const bool have_min = read_number(find_property(settings, "MinGlobalTimeDilation"), settings, min_dilation);
    const bool have_max = read_number(find_property(settings, "MaxGlobalTimeDilation"), settings, max_dilation);
    char buf[220]{};
    std::snprintf(buf, sizeof(buf),
                  "WorldSettings bAllowTimeDilation=%s TimeDilation=%.4f Min=%.4f Max=%.4f（字段读取 %d/%d/%d/%d）",
                  have_allow ? (allow != 0.0 ? "true" : "false") : "?", have_dilation ? dilation : -1.0,
                  have_min ? min_dilation : -1.0, have_max ? max_dilation : -1.0, have_allow ? 1 : 0,
                  have_dilation ? 1 : 0, have_min ? 1 : 0, have_max ? 1 : 0);
    return buf;
}

// 时间冻结的真正入口：
//   UGameplayStatics::SetGlobalTimeDilation 最终只是 AWorldSettings::SetTimeDilation，
//   而 SetTimeDilation 会被 Min/MaxGlobalTimeDilation 夹紧；GetEffectiveTimeDilation
//   还会在 bAllowTimeDilation=false 时直接返回 1.0。所以这里直接写这三个字段，
//   再调用一次引擎函数保持引擎侧状态同步，最后回读确认是否真的生效。
bool Ue4ssPort::set_time_dilation(double dilation) {
    UObject* settings = world_settings();
    if (!is_ok(settings)) {
        // 找不到 WorldSettings 时退回原路径（至少不会比旧版更差）
        UObject* statics = gameplay_statics();
        UObject* scene = world();
        if (!is_ok(statics) || !is_ok(scene)) return false;
        return invoke(statics, "SetGlobalTimeDilation", [&](ParamBlock& params) {
            return params.set_object("WorldContextObject", scene) && params.set_number("TimeDilation", dilation);
        });
    }

    if (!time_settings_saved_) {
        double allow = 1.0;
        double current = 1.0;
        double min_dilation = 0.0001;
        double max_dilation = 20.0;
        if (!read_number(find_property(settings, "bAllowTimeDilation"), settings, allow)) allow = 1.0;
        if (!read_number(find_property(settings, "TimeDilation"), settings, current)) current = 1.0;
        if (!read_number(find_property(settings, "MinGlobalTimeDilation"), settings, min_dilation)) min_dilation = 0.0001;
        if (!read_number(find_property(settings, "MaxGlobalTimeDilation"), settings, max_dilation)) max_dilation = 20.0;
        time_original_allow_ = allow != 0.0;
        time_original_dilation_ = current;
        time_original_min_ = min_dilation;
        time_original_max_ = max_dilation;
        time_original_effective_ = get_time_dilation();
        time_settings_saved_ = true;
        log(LogLevel::Info, "时间设置原始值：" + time_settings_summary());
    }

    // 恢复路径：core 传来的就是进入摄影模式前的“生效时间倍率”。
    // 注意这里恢复的不只是 TimeDilation，还包括 bAllowTimeDilation 与上下限。
    const bool restoring = time_override_active_ &&
                           std::abs(dilation - time_original_effective_) <=
                               0.001 * (1.0 + std::abs(time_original_effective_));
    if (restoring) {
        const bool restored_allow = write_bool(find_property(settings, "bAllowTimeDilation"), settings, time_original_allow_);
        const bool restored_dilation = write_number(find_property(settings, "TimeDilation"), settings, time_original_dilation_);
        const bool restored_min = write_number(find_property(settings, "MinGlobalTimeDilation"), settings, time_original_min_);
        const bool restored_max = write_number(find_property(settings, "MaxGlobalTimeDilation"), settings, time_original_max_);
        time_override_active_ = false;
        const bool ok = restored_allow && restored_dilation && restored_min && restored_max;
        if (ok) time_settings_saved_ = false;  // 下次冻结重新采样原始值
        log(ok ? LogLevel::Info : LogLevel::Warn,
            std::string(ok ? "时间设置已恢复：" : "时间设置恢复不完整：") + time_settings_summary());
        return ok;
    }

    // 冻结路径：先解锁 bAllowTimeDilation，再把 Min/Max 放宽到目标值以内，
    // 最后写 TimeDilation 并调用引擎函数（引擎内部还会按 Min/Max 夹一次）。
    const bool allow_ok = write_bool(find_property(settings, "bAllowTimeDilation"), settings, true);
    double min_dilation = time_original_min_;
    double max_dilation = time_original_max_;
    bool range_ok = true;
    if (dilation < min_dilation) {
        range_ok = write_number(find_property(settings, "MinGlobalTimeDilation"), settings, dilation) && range_ok;
        min_dilation = dilation;
    }
    if (dilation > max_dilation) {
        range_ok = write_number(find_property(settings, "MaxGlobalTimeDilation"), settings, dilation) && range_ok;
        max_dilation = dilation;
    }
    const bool write_ok = write_number(find_property(settings, "TimeDilation"), settings, dilation);
    time_override_active_ = true;

    UObject* statics = gameplay_statics();
    UObject* scene = world();
    bool engine_call_ok = false;
    if (is_ok(statics) && is_ok(scene)) {
        engine_call_ok = invoke(statics, "SetGlobalTimeDilation", [&](ParamBlock& params) {
            return params.set_object("WorldContextObject", scene) && params.set_number("TimeDilation", dilation);
        });
    }

    const double effective = get_time_dilation();
    const bool effective_ok = std::abs(effective - dilation) <= 1e-3 * (1.0 + std::abs(dilation));
    char buf[300]{};
    std::snprintf(buf, sizeof(buf), "时间设置目标=%.4f 生效=%.4f 字段写入=%s/%s 引擎函数=%s | %s",
                  dilation, effective, allow_ok ? "ok" : "fail", (write_ok && range_ok) ? "ok" : "fail",
                  engine_call_ok ? "ok" : "fail", time_settings_summary().c_str());
    log(effective_ok ? LogLevel::Info : LogLevel::Warn, buf);
    return write_ok && allow_ok && range_ok;
}

bool Ue4ssPort::set_paused(bool paused) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;

    if (paused) {
        // 记录 PlayerController 的暂停完整 Tick 标记。默认暂停时 PC 只做最小 Tick，
        // 相机管理器不会更新，摄影相机看起来会“卡死”；打开后相机可以继续动。
        double full_tick = 0.0;
        if (read_number(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, full_tick)) {
            pause_flags_saved_ = true;
            pause_original_full_tick_ = full_tick != 0.0;
        }
        write_bool(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, true);

        // 有些 GameMode 把 bPauseable 关了，SetPause 会直接返回 false。
        if (UObject* scene = world(); is_ok(scene)) {
            UObject* game_mode = nullptr;
            if (read_object(find_property(scene, "AuthorityGameMode"), scene, game_mode) && is_ok(game_mode)) {
                double pauseable = 1.0;
                if (read_number(find_property(game_mode, "bPauseable"), game_mode, pauseable)) {
                    pause_original_game_mode_pauseable_ = pauseable != 0.0;
                }
                write_bool(find_property(game_mode, "bPauseable"), game_mode, true);
            }
        }
    }

    bool accepted = false;
    bool invoked = invoke_bool(pc, "SetPause", [&](ParamBlock& params) { return params.set_bool("bPause", paused); }, accepted);
    if (!invoked || (paused && !accepted)) {
        // 有些游戏在 PlayerController 上禁用了 SetPause，改用 GameplayStatics 的全局暂停。
        if (UObject* statics = gameplay_statics(); is_ok(statics)) {
            if (UObject* scene = world(); is_ok(scene)) {
                invoked = invoke(statics, "SetGamePaused", [&](ParamBlock& params) {
                    return params.set_object("WorldContextObject", scene) && params.set_bool("bPaused", paused);
                });
                if (invoked) accepted = true;
            }
        }
    }

    // 回读 IsGamePaused，作为最终是否真的暂停的依据
    bool actually_paused = accepted;
    if (UObject* statics = gameplay_statics(); is_ok(statics)) {
        if (UObject* scene = world(); is_ok(scene)) {
            UFunction* function = find_function(statics, "IsGamePaused");
            if (function != nullptr) {
                ParamBlock params(function);
                if (params.valid()) {
                    params.set_object("WorldContextObject", scene);
                    statics->ProcessEvent(function, params.data());
                    double value = 0.0;
                    if (params.get_number("ReturnValue", value)) actually_paused = value != 0.0;
                }
            }
        }
    }

    const bool ok = paused ? (invoked && accepted && actually_paused) : (invoked || !actually_paused);

    // 暂停失败时把临时改的 PC/GameMode 状态还原，避免停留在“暂停时完整 Tick”的状态。
    if (paused && !ok) {
        if (pause_flags_saved_) {
            write_bool(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, pause_original_full_tick_);
            pause_flags_saved_ = false;
        }
        if (UObject* scene = world(); is_ok(scene)) {
            UObject* game_mode = nullptr;
            if (read_object(find_property(scene, "AuthorityGameMode"), scene, game_mode) && is_ok(game_mode)) {
                write_bool(find_property(game_mode, "bPauseable"), game_mode, pause_original_game_mode_pauseable_);
            }
        }
    }

    if (!paused) {
        // 恢复暂停前的 PC/GameMode 状态。
        if (pause_flags_saved_) {
            write_bool(find_property(pc, "bShouldPerformFullTickWhenPaused"), pc, pause_original_full_tick_);
            pause_flags_saved_ = false;
        }
        if (UObject* scene = world(); is_ok(scene)) {
            UObject* game_mode = nullptr;
            if (read_object(find_property(scene, "AuthorityGameMode"), scene, game_mode) && is_ok(game_mode)) {
                write_bool(find_property(game_mode, "bPauseable"), game_mode, pause_original_game_mode_pauseable_);
            }
        }
    }

    log(ok ? LogLevel::Info : LogLevel::Warn,
        std::string(ok ? (paused ? "游戏暂停成功" : "游戏暂停已恢复") : (paused ? "游戏暂停失败" : "恢复游戏运行失败")) +
            " | IsGamePaused=" + (actually_paused ? "true" : "false") +
            (paused ? "（bShouldPerformFullTickWhenPaused=true，相机仍可移动）" : ""));
    return ok;
}

bool Ue4ssPort::save_postfx() {
    // 后期处理设置在这版里不改，因此无需保存/还原
    postfx_saved_ = false;
    return false;
}

void Ue4ssPort::restore_postfx() { postfx_saved_ = false; }

bool Ue4ssPort::run_command(const std::string& command) {
    // PBZ 关闭了控制台，且 PlayerController::ConsoleCommand 需要 FString 参数；
    // 这里只在日志里留痕，真正截图请用引擎自带的 HighResShot 快捷键。
    emit(LogLevel::Info, widen("[PBZPhotoMode] 忽略控制台命令：" + command));
    return false;
}

void Ue4ssPort::log(LogLevel level, const std::string& message) {
    emit(level, widen("[PBZPhotoMode] " + message + "\n"));
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
                FProperty* result = params.prop({"ReturnValue"}, 0);
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
                FProperty* result = params.prop({"ReturnValue"}, 0);
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

namespace {
// ProcessEvent 钩子是全局的（没有 custom data），这里记下当前端口实例
Ue4ssPort* g_overlay_port = nullptr;

// 全局 ProcessEvent 钩子：UE4SS 里唯一能稳定拿到"HUD 正在画"的时机。
// 为什么不用 UObjectGlobals::RegisterHook(HUD.ReceiveDrawHUD)：
//   ReceiveDrawHUD 是 UFUNCTION(BlueprintImplementableEvent)，本作 BP_PBZHud_C 没实现它
//   → UFunction::Script.Num()==0 → ProcessInternal 直接 return → ProcessLocalScriptFunction
//     不会被调用 → GlobalScriptHook（按 full name 匹配）永远不会触发。
//   ProcessEvent 则一定被 AHUD::DrawHUD 调用（原生 stub 内部 ProcessEvent），所以在
//   ProcessEvent 层按函数名过滤是可靠的。
void process_event_pre(RC::Unreal::UObject* context, RC::Unreal::UFunction* function, void* /*parms*/) {
    if (g_overlay_port == nullptr) return;
    g_overlay_port->on_process_event(context, function);
}
}  // namespace

void Ue4ssPort::on_process_event(UObject* context, UFunction* function) {
    if (function == nullptr || !is_ok(context)) return;
    if (function->GetFName().ToUnstableInt() != overlay_target_name_) return;
    ++overlay_hook_hits_;
    draw_overlay(context);
}

bool Ue4ssPort::ensure_overlay_hook() {
    if (overlay_hook_installed_) return true;
    if (overlay_hook_failed_) return false;

    UObject* pc = controller();
    UObject* hud = nullptr;
    if (!is_ok(pc) || !read_object(find_property(pc, "MyHUD"), pc, hud) || !is_ok(hud)) {
        return false;  // HUD 还没建好（主菜单/读盘中），下一帧再试
    }

    // 用实例的类去查 ReceiveDrawHUD：拿到真实的 UFunction（父类 AHUD 上的那个）
    UFunction* draw_hud = hud->GetFunctionByNameInChain(widen("ReceiveDrawHUD").c_str());
    if (draw_hud == nullptr) {
        draw_hud = hud->GetFunctionByNameInChain(widen("DrawHUD").c_str());
    }
    if (draw_hud == nullptr) {
        overlay_hook_failed_ = true;
        log(LogLevel::Warn, "HUD 上找不到 ReceiveDrawHUD，游戏内菜单不可用（ImGui 面板仍可用）");
        return false;
    }

    // 强制 bShowHUD：AHUD::DrawHUD 开头就是 if(!bShowHUD) return;，
    // 本作 HUD 完全靠 UMG，原生 canvas 很可能被关掉了。
    if (!overlay_show_hud_forced_) {
        const bool ok = write_bool(find_property(hud, "bShowHUD"), hud, true);
        overlay_show_hud_forced_ = true;
        log(LogLevel::Info, std::string("已强制 HUD.bShowHUD=true：") + (ok ? "成功" : "属性没找到"));
    }

    overlay_hud_ = hud;
    overlay_target_name_ = draw_hud->GetFName().ToUnstableInt();
    g_overlay_port = this;

    if (!overlay_process_hook_installed_) {
        RC::Unreal::Hook::RegisterProcessEventPreCallback(process_event_pre);
        overlay_process_hook_installed_ = true;
    }

    overlay_hook_installed_ = true;
    log(LogLevel::Info, "HUD Canvas 菜单钩子已挂上（ProcessEvent 过滤 ReceiveDrawHUD，HUD=" +
                            narrow(hud->GetFullName()) + "）；若 HUD绘制 一直为 0 会自动改用 PrintString");
    return true;
}

void Ue4ssPort::ensure_screen_messages_enabled() {
    if (screen_messages_checked_) return;
    screen_messages_checked_ = true;

    std::vector<UObject*> engines;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("GameEngine"), engines);
    for (UObject* engine : engines) {
        if (!is_ok(engine)) continue;
        write_bool(find_property(engine, "bEnableOnScreenDebugMessages"), engine, true);
        write_bool(find_property(engine, "bEnableOnScreenDebugMessagesDisplay"), engine, true);
    }
}

// PrintString 是左上角起画，按 1080p 的引擎调试字体尺寸估算，把 ASCII 菜单推到右下角。
std::string Ue4ssPort::build_print_menu_text(const std::string& text) {
    double screen_w = 1920.0;
    double screen_h = 1080.0;
    if (UObject* pc = controller(); is_ok(pc)) {
        if (UFunction* get_size = find_function(pc, "GetViewportSize"); get_size != nullptr) {
            ParamBlock params(get_size);
            if (params.valid()) {
                pc->ProcessEvent(get_size, params.data());
                double sx = 0.0;
                double sy = 0.0;
                if (params.get_number("SizeX", sx) && params.get_number("SizeY", sy) && sx > 64.0 && sy > 64.0) {
                    screen_w = sx;
                    screen_h = sy;
                }
            }
        }
    }

    std::vector<std::string> lines;
    std::string current;
    for (const char ch : text) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (ch != '\r') {
            current.push_back(ch);
        }
    }
    if (!current.empty()) lines.push_back(current);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.empty()) return text;

    std::size_t longest = 0;
    for (const std::string& line : lines) longest = std::max(longest, line.size());

    const double line_h = std::max(6.0, screen_h / 78.0);
    const double char_w = std::max(3.0, screen_w / 290.0);
    const int rows = std::max(1, static_cast<int>((screen_h - 24.0) / line_h));
    const int cols = std::max(1, static_cast<int>((screen_w - 24.0) / char_w));
    const int pad_top = std::max(0, rows - static_cast<int>(lines.size()) - 2);
    const int pad_left = std::max(0, cols - static_cast<int>(longest) - 4);

    std::string out;
    out.append(static_cast<std::size_t>(pad_top), '\n');
    const std::string prefix(static_cast<std::size_t>(pad_left), ' ');
    for (const std::string& line : lines) {
        out += prefix;
        out += line;
        out.push_back('\n');
    }
    return out;
}

// 兜底路径：引擎的 on-screen debug message。Lua 版已证明本作可用，
// 关键是 duration 参数必须写对（见 overlay_via_print_string）。
bool Ue4ssPort::overlay_via_print_string(const std::string& text, double duration) {
    ensure_screen_messages_enabled();
    UObject* library = kismet_system_library();
    UObject* scene = world();
    if (!is_ok(library) || !is_ok(scene)) return false;

    return invoke(library, "PrintString", [&](ParamBlock& params) {
        if (!params.set_object("WorldContextObject", scene)) return false;
        if (!params.set_text("InString", text)) return false;
        if (!params.set_bool("bPrintToScreen", true)) return false;
        // 不写日志：菜单每 0.35 秒重推一次，打开 bPrintToLog 会刷爆 UE4SS.log
        params.set_bool("bPrintToLog", false);
        if (FProperty* color = params.property("TextColor"); color != nullptr) {
            write_struct_number(color, params.data(), "R", 1.0);
            write_struct_number(color, params.data(), "G", 0.85);
            write_struct_number(color, params.data(), "B", 0.35);
            write_struct_number(color, params.data(), "A", 1.0);
        }
        // 关键：本作 UEngine 里这个参数名是小写的 duration。以前写成 "Duration"
        // 按名字查不到，ParamBlock 静默失败后 duration=0，消息立刻消失——看起来就像
        // “PrintString 调用成功但屏幕上什么都没有”。
        if (!params.set_number_any({"duration", "Duration"}, -1, duration)) return false;
        if (!params.set_name("Key", "PBZPhotoModeMenu")) return false;
        return true;
    });
}

void Ue4ssPort::draw_overlay(UObject* hud) {
    if (!overlay_visible_ || overlay_text_.empty() || !is_ok(hud)) return;

    // 1) 画布尺寸（右下角定位用）
    double size_x = 1920.0;
    double size_y = 1080.0;
    UObject* canvas = nullptr;
    const bool canvas_ok = read_object(find_property(hud, "Canvas"), hud, canvas) && is_ok(canvas);
    if (canvas_ok) {
        double value = 0.0;
        if (read_number(find_property(canvas, "SizeX"), canvas, value) && value > 1.0) size_x = value;
        if (read_number(find_property(canvas, "SizeY"), canvas, value) && value > 1.0) size_y = value;
    }

    // 第一次真正进来时打一遍诊断：画布在不在、多大、量到多宽——真机排障就靠这三行
    if (!overlay_diag_logged_) {
        overlay_diag_logged_ = true;
        char buf[200]{};
        std::snprintf(buf, sizeof(buf), "HUD 绘制诊断：命中=%u Canvas=%s 尺寸=%.0fx%.0f", overlay_hook_hits_,
                      canvas_ok ? "有效" : "空", size_x, size_y);
        log(LogLevel::Info, buf);
    }

    // 2) 量文字尺寸：AHUD::GetTextSize(Text, OutWidth, OutHeight, Font, Scale)
    double width = 0.0;
    double height = 0.0;
    bool measured = false;
    if (UFunction* measure = find_function(hud, "GetTextSize"); measure != nullptr) {
        ParamBlock params(measure);
        if (params.valid()) {
            write_text(params.prop({"Text", "InString"}, 0), params.data(), overlay_text_);
            write_number(params.prop({"Scale"}, 4), params.data(), 1.0);
            hud->ProcessEvent(measure, params.data());
            measured = read_number(params.prop({"OutWidth", "Width"}, 1), params.data(), width) &&
                       read_number(params.prop({"OutHeight", "Height"}, 2), params.data(), height);
        }
    }
    if (width <= 1.0 || height <= 1.0) {
        // 估算兜底：引擎小字体约 8px 宽 / 16px 高
        std::size_t longest = 0;
        std::size_t current = 0;
        int lines = 1;
        for (const char ch : overlay_text_) {
            if (ch == 0x0A) {
                longest = std::max(longest, current);
                current = 0;
                ++lines;
            } else {
                ++current;
            }
        }
        longest = std::max(longest, current);
        width = static_cast<double>(longest) * 8.0;
        height = static_cast<double>(lines) * 16.0;
    }

    const float x = static_cast<float>(std::max(8.0, size_x - width - 24.0));
    const float y = static_cast<float>(std::max(8.0, size_y - height - 24.0));

    // 3) AHUD::DrawText(Text, TextColor, ScreenX, ScreenY, Font, Scale, bScalePosition)
    //    走 HUD 的 Canvas，和游戏自己的 UI 同一条绘制管线；Font 传空时 AHUD 会自动用小字体。
    UFunction* draw_text = find_function(hud, "DrawText");
    if (draw_text == nullptr) return;
    ParamBlock params(draw_text);
    if (!params.valid()) return;
    void* data = params.data();
    if (!write_text(params.prop({"Text", "InString"}, 0), data, overlay_text_)) return;
    FProperty* color = params.prop({"TextColor", "Color"}, 1);
    write_struct_number(color, data, "R", 1.0);
    write_struct_number(color, data, "G", 1.0);
    write_struct_number(color, data, "B", 1.0);
    write_struct_number(color, data, "A", 1.0);
    write_number(params.prop({"ScreenX", "X"}, 2), data, x);
    write_number(params.prop({"ScreenY", "Y"}, 3), data, y);
    write_number(params.prop({"Scale"}, 5), data, 1.0);
    write_bool(params.prop({"bScalePosition"}, 6), data, false);
    hud->ProcessEvent(draw_text, params.data());
}

namespace {
auto steady_seconds() -> double {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

bool Ue4ssPort::show_overlay(const std::string& text) {
    overlay_text_ = text;
    overlay_visible_ = true;

    // 主路径：HUD Canvas。只有真的收到 ReceiveDrawHUD 回调（overlay_hook_hits_ > 0）
    // 才说明这条管线可用；本作 BP_PBZHud 根本不走 AHUD::DrawHUD，所以这里通常为 0。
    if (!overlay_hook_installed_ && !overlay_hook_failed_) {
        ensure_overlay_hook();
    }
    if (overlay_hook_installed_ && overlay_hook_hits_ > 0) {
        print_path_working_ = false;
        return true;
    }

    // 兜底：KismetSystemLibrary::PrintString。Lua 版 1.0.4 已经证明这条路在本作可用；
    // 关键是 duration 不能是 0，并且每 0.35 秒用同一个 FName Key 重推一次。
    const double now = steady_seconds();
    if (now - last_print_push_ < 0.35) return print_path_working_;
    last_print_push_ = now;
    const bool ok = overlay_via_print_string(build_print_menu_text(text), 1.0);
    if (ok != print_path_working_) {
        print_path_working_ = ok;
        if (ok) {
            log(LogLevel::Info, "游戏内菜单改走 PrintString（HUD 绘制回调未触发）");
        } else {
            log(LogLevel::Warn, "游戏内菜单 PrintString 调用失败（打印属性不匹配）");
        }
    }
    return ok;
}

void Ue4ssPort::hide_overlay() {
    if (overlay_visible_ && print_path_working_) {
        // 用同一个 Key 推一条空消息，立刻把残留的菜单擦掉
        overlay_via_print_string("", 0.05);
    }
    overlay_visible_ = false;
    overlay_text_.clear();
    print_path_working_ = false;
    last_print_push_ = 0.0;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
