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
#include "engine/umg2_layout.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FHitResult.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
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
    if (property->IsA<FEnumProperty>()) {
        FNumericProperty* underlying = static_cast<FEnumProperty*>(property)->GetUnderlyingProperty();
        if (underlying == nullptr) return false;
        underlying->SetIntPropertyValue(address, static_cast<int64_t>(value));
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
    if (property->IsA<FEnumProperty>()) {
        FNumericProperty* underlying = static_cast<FEnumProperty*>(property)->GetUnderlyingProperty();
        if (underlying == nullptr) return false;
        out = static_cast<double>(underlying->GetSignedIntPropertyValue(address));
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

namespace {

// UMG 的 UWidget::Visibility 是 TEnumAsByte<ESlateVisibility>：
// Visible=0 / Collapsed=1 / Hidden=2 / HitTestInvisible=3 / SelfHitTestInvisible=4。
// 必须走 UWidget::SetVisibility（而不是直接改属性），否则 Slate 控件不会立即刷新。
auto set_widget_visibility(UObject* widget, int visibility) -> bool {
    if (!is_ok(widget)) return false;
    if (UFunction* function = find_function(widget, "SetVisibility"); function != nullptr) {
        ParamBlock params(function);
        if (!params.valid()) return false;
        if (!params.set_number_any({"InVisibility"}, 0, static_cast<double>(visibility))) return false;
        widget->ProcessEvent(function, params.data());
        return true;
    }
    // 退路：个别版本把 SetVisibility 裁掉时，直接写属性（可能要到下一帧才刷新）
    return write_number(find_property(widget, "Visibility"), widget, static_cast<double>(visibility));
}

auto get_widget_visibility(UObject* widget, int& out) -> bool {
    if (!is_ok(widget)) return false;
    if (UFunction* function = find_function(widget, "GetVisibility"); function != nullptr) {
        ParamBlock params(function);
        if (params.valid()) {
            widget->ProcessEvent(function, params.data());
            double value = 0.0;
            if (params.get_number("ReturnValue", value)) {
                out = static_cast<int>(value);
                return true;
            }
        }
    }
    double value = 0.0;
    if (read_number(find_property(widget, "Visibility"), widget, value)) {
        out = static_cast<int>(value);
        return true;
    }
    return false;
}

// 游戏内 HUD 的 UMG 根控件。CXXHeaderDump 里只有 W_FrontEndHUDLayout_C 继承自
// UPBZHUDLayout；HP/快捷栏/武器/任务/Boss 血条都挂在它的子树下。找不到 Blueprint
// 子类时退回 PNative 基类名，最后退回整个 CommonUI 根布局 WB_OverLayerOut_C。
auto find_hud_layout_widgets(std::vector<UObject*>& out) -> void {
    RC::Unreal::UObjectGlobals::FindAllOf(STR("W_FrontEndHUDLayout_C"), out);
    if (out.empty()) {
        RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZHUDLayout"), out);
    }
    if (out.empty()) {
        // 最后的 UMG 兜底：整个 CommonUI 根布局（包含所有 Layer 栈）。
        RC::Unreal::UObjectGlobals::FindAllOf(STR("WB_OverLayerOut_C"), out);
    }
}

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
        FProperty* property = params.prop({"bHidden"}, 0);
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
    bool pv_restored = false;
    if (pv_hide_active_) {
        pv_restored = set_ui_manager_hide_for_pv(false);
        pv_hide_active_ = false;
        if (pv_restored) {
            log(LogLevel::Info, "UIManager.SetHideUIForPV(false) 已恢复游戏 UI/HUD");
        }
    }

    // 重新枚举一次做存活校验：关卡切换后旧控件可能已销毁，不能直接解引用旧指针。
    int restored_count = 0;
    for (const auto& entry : hud_widgets_) {
        if (!pointer_in(widgets, entry.first)) continue;
        if (set_widget_visibility(entry.first, entry.second)) {
            touched = true;
            ++restored_count;
        }
    }
    if (restored_count > 0) {
        log(LogLevel::Info, "UMG HUD 已恢复：控件数=" + std::to_string(restored_count));
    }
    hud_widgets_.clear();

    // AHUD 兜底恢复（即使 PV 开关没找到，也把可能被隐藏的 AHUD 恢复）
    UObject* pc = controller();
    if (is_ok(pc)) {
        UObject* hud = nullptr;
        if (read_object(find_property(pc, "MyHUD"), pc, hud) && is_ok(hud)) {
            static_cast<AActor*>(hud)->SetActorHiddenInGame(false);
            touched = true;
        }
    }
    return pv_restored || touched;
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

// ------------------------------------------------------------------ UMG 文字菜单
//
// 旧方案两条路在这作 Shipping 包里都不通：
//   1) HUD Canvas：BP_PBZHud 是纯 UMG，AHUD::DrawHUD/ReceiveDrawHUD 从不被调用（历次实机
//      日志 HUD绘制=0），Canvas 钩子永远等不到绘制时机；
//   2) PrintString：PBZ.exe 是 UE5 Shipping 单体包，UKismetSystemLibrary::PrintString 的函数体
//      被 #if !UE_BUILD_SHIPPING 整体裁掉，反射调用“成功”但什么都不画（1.0.1 实机已验证）。
// 现在用引擎 UMG 类动态拼一个菜单：UserWidget(WidgetTree→CanvasPanel→TextBlock)，
// AddToViewport 常驻，显示/隐藏走 SetVisibility，文本变化才 SetText。
// 全程反射：对象用 UObjectGlobals::NewObject 创建（内部走游戏的 StaticConstructObject，
// 原生构造函数会执行）；FText 用 KismetTextLibrary::Conv_StringToText 生成后按
// “所有权移交”复制（ParamBlock 是裸字节数组，释放时不调 FText 析构，目标属性从此持有引用）。

namespace {

auto umg_class(const char* path) -> UClass* {
    return UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widen(path).c_str());
}

// FString → FText：Conv_StringToText 的 ReturnValue（FText）按字节移交到目标 FText 属性。
auto make_ftext_into(const std::string& text, FProperty* dst, void* dst_container) -> bool {
    UObject* library = find_object("/Script/Engine.Default__KismetTextLibrary");
    if (!is_ok(library)) return false;
    UFunction* conv = find_function(library, "Conv_StringToText");
    if (conv == nullptr) return false;
    ParamBlock params(conv);
    if (!params.valid()) return false;
    if (!write_text(params.prop({"SourceString", "InString"}, 0), params.data(), text)) return false;
    library->ProcessEvent(conv, params.data());
    FProperty* ret = params.prop({"ReturnValue"}, 0);
    if (ret == nullptr || !ret->IsA<FTextProperty>() || dst == nullptr || !dst->IsA<FTextProperty>()) {
        return false;
    }
    void* src = value_ptr(ret, params.data());
    void* dst_addr = value_ptr(dst, dst_container);
    if (src == nullptr || dst_addr == nullptr) return false;
    const size_t bytes = ret->GetSize() < dst->GetSize() ? ret->GetSize() : dst->GetSize();
    memcpy(dst_addr, src, bytes);
    return true;
}

// FAnchor { FVector2D Minimum; FVector2D Maximum; }：两层嵌套的数字成员写入
auto write_anchor(FProperty* anchor_prop, void* container, double min_x, double min_y, double max_x,
                  double max_y) -> bool {
    void* base = struct_member_base(anchor_prop, container);
    if (base == nullptr) return false;
    FProperty* minimum = struct_member(anchor_prop, "Minimum");
    FProperty* maximum = struct_member(anchor_prop, "Maximum");
    if (minimum == nullptr || maximum == nullptr) return false;
    return write_struct_number(minimum, base, "X", min_x) && write_struct_number(minimum, base, "Y", min_y) &&
           write_struct_number(maximum, base, "X", max_x) && write_struct_number(maximum, base, "Y", max_y);
}

auto steady_seconds() -> double {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

// 换图/GC 之后缓存的控件指针不可信，一律按创建时记录的全名重新查找；
// 查不到说明控件已不在（不解引用悬垂指针），调用方据此重建。
RC::Unreal::UObject* Ue4ssPort::umg_live() const {
    if (umg_widget_ == nullptr || umg_widget_path_.empty()) return nullptr;
    return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widen(umg_widget_path_).c_str());
}

void Ue4ssPort::umg_drop() {
    umg_widget_ = nullptr;
    umg_tree_ = nullptr;
    umg_canvas_ = nullptr;
    umg_text_ = nullptr;
    umg_widget_path_.clear();
    umg_last_text_.clear();
    umg_visibility_state_ = -1;
    umg_shown_ = false;
}

// 动态拼装菜单控件。每一步失败都打日志（同类只打一次），真机排障靠这个定位卡在哪一步。
bool Ue4ssPort::ensure_umg_menu() {
    UClass* user_widget_class = umg_class("/Script/UMG.UserWidget");
    UClass* widget_tree_class = umg_class("/Script/UMG.WidgetTree");
    UClass* text_block_class = umg_class("/Script/UMG.TextBlock");
    UClass* canvas_panel_class = umg_class("/Script/UMG.CanvasPanel");
    if (user_widget_class == nullptr || widget_tree_class == nullptr || text_block_class == nullptr ||
        canvas_panel_class == nullptr) {
        if (!umg_failed_logged_) {
            umg_failed_logged_ = true;
            log(LogLevel::Warn, "UMG 菜单：引擎类缺失（UserWidget/WidgetTree/TextBlock/CanvasPanel），菜单不可用");
        }
        return false;
    }

    UObject* scene = world();
    if (!is_ok(scene)) return false;  // 世界没就绪：下帧再试，不算失败

    UObject* widget = UObjectGlobals::NewObject<UObject>(scene, user_widget_class);
    if (!is_ok(widget)) {
        if (!umg_failed_logged_) {
            umg_failed_logged_ = true;
            log(LogLevel::Warn, "UMG 菜单：UserWidget 创建失败");
        }
        return false;
    }

    // UUserWidget 构造器一般会建好 WidgetTree；没有就补一个（CreateWidget 流程同款）。
    UObject* tree = nullptr;
    FProperty* tree_prop = find_property(widget, "WidgetTree");
    read_object(tree_prop, widget, tree);
    if (!is_ok(tree)) {
        tree = UObjectGlobals::NewObject<UObject>(widget, widget_tree_class);
        if (!is_ok(tree) || !write_object(tree_prop, widget, tree)) {
            log(LogLevel::Warn, "UMG 菜单：WidgetTree 创建失败");
            return false;
        }
    }

    // 根画布与文本控件：按 UMG 约定 outer 到 WidgetTree 上；名字留空让引擎自动生成，
    // 重建时不会和旧对象重名。
    UObject* canvas = UObjectGlobals::NewObject<UObject>(tree, canvas_panel_class);
    UObject* text = UObjectGlobals::NewObject<UObject>(tree, text_block_class);
    if (!is_ok(canvas) || !is_ok(text)) {
        log(LogLevel::Warn, "UMG 菜单：CanvasPanel/TextBlock 创建失败");
        return false;
    }
    if (!write_object(find_property(tree, "RootWidget"), tree, canvas)) {
        log(LogLevel::Warn, "UMG 菜单：WidgetTree.RootWidget 写入失败");
        return false;
    }

    UFunction* add_child = find_function(canvas, "AddChildToCanvas");
    if (add_child == nullptr) {
        log(LogLevel::Warn, "UMG 菜单：找不到 CanvasPanel.AddChildToCanvas");
        return false;
    }
    UObject* slot = nullptr;
    {
        ParamBlock params(add_child);
        if (!params.valid() || !write_object(params.prop({"Widget"}, 0), params.data(), text)) {
            log(LogLevel::Warn, "UMG 菜单：AddChildToCanvas 参数写入失败");
            return false;
        }
        canvas->ProcessEvent(add_child, params.data());
        read_object(params.prop({"ReturnValue"}, 0), params.data(), slot);
    }
    if (!is_ok(slot)) {
        log(LogLevel::Warn, "UMG 菜单：AddChildToCanvas 没返回槽位");
        return false;
    }

    // 右下角布局：锚点(1,1) + 对齐(1,1) + 偏移(-24,-24) + 自动按文本尺寸。
    // 1.0.19 实机：四个 Set* 走 UFunction 至少一个没生效（日志“槽位布局没写全”），菜单被屏幕边裁掉。
    // 所以这里改为直接写 UCanvasPanelSlot.LayoutData（FAnchorData{Anchors,Offsets,Alignment}）+
    // bAutoSize 属性——不依赖 UFunction 的参数名；setter 保留在前（若正常也能同步 Slate 状态）。
    // 全部发生在 AddToViewport 之前：Slate 槽位在 TakeWidget 时才从 LayoutData 构建，直写一定生效。
    const bool fn_anchors = invoke(slot, "SetAnchors", [&](ParamBlock& p) {
        return write_anchor(p.prop({"InAnchors", "Anchors"}, 0), p.data(), 1.0, 1.0, 1.0, 1.0);
    });
    const bool fn_align = invoke(slot, "SetAlignment", [&](ParamBlock& p) {
        FProperty* align = p.prop({"InAlignment", "Alignment"}, 0);
        return write_struct_number(align, p.data(), "X", 1.0) && write_struct_number(align, p.data(), "Y", 1.0);
    });
    const bool fn_pos = invoke(slot, "SetPosition", [&](ParamBlock& p) {
        FProperty* pos = p.prop({"InPosition", "Position"}, 0);
        return write_struct_number(pos, p.data(), "X", -24.0) && write_struct_number(pos, p.data(), "Y", -24.0);
    });
    const bool fn_auto = invoke(slot, "SetAutoSize", [&](ParamBlock& p) { return p.set_bool("InAutoSize", true); });

    bool w_anchors = false;
    bool w_align = false;
    bool w_pos = false;
    bool w_auto = false;
    FProperty* layout_data = find_property(slot, "LayoutData");
    void* layout_base = struct_member_base(layout_data, slot);
    if (layout_base != nullptr) {
        FProperty* anchors_prop = struct_member(layout_data, "Anchors");
        FProperty* offsets_prop = struct_member(layout_data, "Offsets");
        FProperty* align_prop = struct_member(layout_data, "Alignment");
        if (anchors_prop != nullptr && offsets_prop != nullptr && align_prop != nullptr) {
            w_anchors = write_anchor(anchors_prop, layout_base, 1.0, 1.0, 1.0, 1.0);
            w_pos = write_struct_number(offsets_prop, layout_base, "Left", -24.0) &&
                    write_struct_number(offsets_prop, layout_base, "Top", -24.0);
            w_align = write_struct_number(align_prop, layout_base, "X", 1.0) &&
                      write_struct_number(align_prop, layout_base, "Y", 1.0);
        }
    }
    w_auto = write_bool(find_property(slot, "bAutoSize"), slot, true);
    if (!(fn_anchors && fn_align && fn_pos && fn_auto) || !(w_anchors && w_align && w_pos && w_auto)) {
        log(LogLevel::Warn, "UMG 菜单：槽位布局 setter(锚" + std::string(fn_anchors ? "√" : "×") + "/对齐" +
                                std::string(fn_align ? "√" : "×") + "/位置" + std::string(fn_pos ? "√" : "×") + "/自动" +
                                std::string(fn_auto ? "√" : "×") + ") 直写(锚" + std::string(w_anchors ? "√" : "×") +
                                "/对齐" + std::string(w_align ? "√" : "×") + "/位置" +
                                std::string(w_pos ? "√" : "×") + "/自动" + std::string(w_auto ? "√" : "×") + ")");
    }

    // 回读：真机核对每个字段是否真的写进去了（对不上就是字段名/结构布局有出入）
    if (layout_base != nullptr) {
        FProperty* anchors_prop = struct_member(layout_data, "Anchors");
        FProperty* offsets_prop = struct_member(layout_data, "Offsets");
        FProperty* align_prop = struct_member(layout_data, "Alignment");
        auto read_nested = [&](FProperty* parent, const char* sub, const char* member, double& out) {
            FProperty* sub_prop = struct_member(parent, sub);
            void* sub_base = struct_member_base(sub_prop, layout_base);
            return sub_base != nullptr && read_number(struct_member(sub_prop, member), sub_base, out);
        };
        double aminx = -1, aminy = -1, amaxx = -1, amaxy = -1, offl = 0, offt = 0, alx = -1, aly = -1, auto_v = -1;
        const bool r1 = read_nested(anchors_prop, "Minimum", "X", aminx) && read_nested(anchors_prop, "Minimum", "Y", aminy);
        const bool r2 = read_nested(anchors_prop, "Maximum", "X", amaxx) && read_nested(anchors_prop, "Maximum", "Y", amaxy);
        const bool r3 = read_number(struct_member(offsets_prop, "Left"), struct_member_base(offsets_prop, layout_base), offl) &&
                        read_number(struct_member(offsets_prop, "Top"), struct_member_base(offsets_prop, layout_base), offt);
        const bool r4 = read_number(struct_member(align_prop, "X"), struct_member_base(align_prop, layout_base), alx) &&
                        read_number(struct_member(align_prop, "Y"), struct_member_base(align_prop, layout_base), aly);
        const bool r5 = read_number(find_property(slot, "bAutoSize"), slot, auto_v);
        char buf[200]{};
        std::snprintf(buf, sizeof(buf),
                      "UMG 槽位回读：anchors=(%.0f,%.0f)-(%.0f,%.0f) pos=(%.0f,%.0f) align=(%.0f,%.0f) auto=%.0f（读取 %d%d%d%d%d）",
                      aminx, aminy, amaxx, amaxy, offl, offt, alx, aly, auto_v, r1 ? 1 : 0, r2 ? 1 : 0, r3 ? 1 : 0,
                      r4 ? 1 : 0, r5 ? 1 : 0);
        log(LogLevel::Info, buf);
    }
    // 槽位类的属性名 dump（一次性）：字段名对不上时真机日志直接可见
    if (!slot_props_logged_) {
        slot_props_logged_ = true;
        std::string names;
        for (TFieldIterator<FProperty> it(slot->GetClassPrivate()); it; ++it) {
            names += narrow(it->GetName());
            names.push_back(' ');
            if (names.size() > 380) {
                names += "…";
                break;
            }
        }
        log(LogLevel::Info, "槽位属性: " + names);
    }

    // 文本颜色（琥珀，沿用旧 PrintString 菜单的配色）；ColorUseRule 留 0 = 用指定色
    invoke(text, "SetColorAndOpacity", [&](ParamBlock& p) {
        FProperty* color = p.prop({"InColorAndOpacity", "ColorAndOpacity"}, 0);
        void* base = struct_member_base(color, p.data());
        if (base == nullptr) return false;
        FProperty* specified = struct_member(color, "SpecifiedColor");
        return specified != nullptr && write_struct_number(specified, base, "R", 1.0) &&
               write_struct_number(specified, base, "G", 0.85) && write_struct_number(specified, base, "B", 0.35) &&
               write_struct_number(specified, base, "A", 1.0);
    });

    if (!invoke(widget, "AddToViewport", [&](ParamBlock& p) { return p.set_number_any({"ZOrder"}, 0, 10000.0); })) {
        log(LogLevel::Warn, "UMG 菜单：AddToViewport 失败");
        return false;
    }

    umg_widget_ = widget;
    umg_tree_ = tree;
    umg_canvas_ = canvas;
    umg_text_ = text;
    umg_widget_path_ = narrow(widget->GetFullName());
    umg_last_text_.clear();
    umg_visibility_state_ = 0;  // AddToViewport 后默认可见
    umg_failed_logged_ = false;
    if (!umg_ready_logged_) {
        umg_ready_logged_ = true;
        log(LogLevel::Info, "UMG 菜单已创建：" + umg_widget_path_ + "（右下角；隐藏/显示走 SetVisibility）");
    }
    return true;
}

bool Ue4ssPort::umg_set_text(const std::string& text) {
    if (!is_ok(umg_text_)) return false;
    UFunction* set_text = find_function(umg_text_, "SetText");
    if (set_text == nullptr) return false;
    ParamBlock params(set_text);
    if (!params.valid()) return false;
    if (!make_ftext_into(text, params.prop({"InText", "Text"}, 0), params.data())) return false;
    umg_text_->ProcessEvent(set_text, params.data());
    return true;
}

bool Ue4ssPort::show_overlay(const std::string& text) {
    overlay_text_ = text;
    overlay_visible_ = true;

    // 控件被换图/GC 清掉就重建（umg_live 按全名查活，不解引用缓存指针）。
    // 重建限 0.5 秒一次：万一查活路径在引擎里解析不了，最坏也是节流失败而不是每帧狂建控件。
    if (umg_live() == nullptr) {
        const bool was_rebuild = umg_widget_ != nullptr;
        umg_drop();
        const double rebuild_now = steady_seconds();
        if (rebuild_now - umg_last_rebuild_ < 0.5) return false;
        umg_last_rebuild_ = rebuild_now;
        if (!ensure_umg_menu()) return false;
        // 只在真重建（控件曾存在又查不到了）时打日志；若这行刷屏说明查活路径有问题，实机日志能直接暴露
        if (was_rebuild) log(LogLevel::Info, "UMG 菜单：查活失败，已重建控件");
    }

    // 文本变化才 SetText：每帧重建 FText 是白费
    if (text != umg_last_text_) {
        if (!umg_set_text(text)) {
            if (!umg_failed_logged_) {
                umg_failed_logged_ = true;
                log(LogLevel::Warn, "UMG 菜单：SetText 失败");
            }
            return false;
        }
        umg_last_text_ = text;
    }

    // 每秒确认一次还在视口里：有的游戏换图会清视口内容但控件对象还活着
    const double now = steady_seconds();
    if (now - umg_last_vp_check_ >= 1.0) {
        umg_last_vp_check_ = now;
        bool in_viewport = true;
        invoke_bool(umg_widget_, "IsInViewport", [](ParamBlock&) { return true; }, in_viewport);
        if (!in_viewport) {
            if (!invoke(umg_widget_, "AddToViewport",
                        [&](ParamBlock& p) { return p.set_number_any({"ZOrder"}, 0, 10000.0); })) {
                return false;
            }
            log(LogLevel::Info, "UMG 菜单：发现不在视口，已重新 AddToViewport");
        }
    }

    if (umg_visibility_state_ != 0) {
        if (!set_widget_visibility(umg_widget_, 0)) return false;  // 0 = ESlateVisibility::Visible
        umg_visibility_state_ = 0;
    }
    umg_shown_ = true;
    return true;
}

void Ue4ssPort::hide_overlay() {
    overlay_visible_ = false;
    overlay_text_.clear();
    // 只在“可见→隐藏”的沿上碰引擎：隐藏态下这里每帧都会进来，不能每帧都调引擎
    if (!umg_shown_) return;
    umg_shown_ = false;
    umg_last_text_.clear();
    UObject* live = umg_live();
    if (live != nullptr && umg_visibility_state_ != 1) {
        if (set_widget_visibility(live, 1)) umg_visibility_state_ = 1;  // 1 = ESlateVisibility::Collapsed
    }
}

std::string Ue4ssPort::umg_menu_state() const {
    if (umg_widget_ == nullptr) return "未创建";
    return umg_shown_ ? "显示" : "隐藏";
}

// ------------------------------------------------------------------ UMG 交互菜单（v1.2.1）
//
// 帧生成插件（FSR3-FG 等）包装交换链并管住 back buffer 的资源状态，任何 D3D 注入式渲染
// 都会跟它们抢状态机（1.1.2 实机 dump）。这版改用**游戏自己的 UMG 控件**：帧生成把它当
// 普通画面合成，零冲突。
//
// **控件词汇表刻意收缩到 1.0.19/1.0.20 整场实机验证过的安全集**：CanvasPanel + TextBlock，
// 全部操作 = NewObject/AddChildToCanvas/LayoutData 直写/SetText/SetVisibility/AddToViewport/
// IsInViewport。1.2.0 里引入的 VerticalBox/Slider/Button 及其 ProcessEvent 在本作触发了游戏
// 代码内的访问违例（08:17 dump：ProcessEvent 派发进 PBZ.exe 后读 0xFFFFFFFFFFFFFFFF），全部移除。
//
// 交互不依赖 Slate 路由：「按钮」是文本，命中判定由 mod_app 用自己的鼠标状态 + 命中区
// （engine/umg2_layout.h）完成，点击动作经 Umg2Clicks 传进来。

RC::Unreal::UObject* Ue4ssPort::umg2_live() const {
    if (umg2_widget_ == nullptr || umg2_path_.empty()) return nullptr;
    return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widen(umg2_path_).c_str());
}

void Ue4ssPort::umg2_drop() {
    umg2_widget_ = nullptr;
    umg2_tree_ = nullptr;
    umg2_status_ = nullptr;
    umg2_fov_value_ = nullptr;
    umg2_speed_value_ = nullptr;
    umg2_sens_value_ = nullptr;
    umg2_path_.clear();
    umg2_visibility_state_ = -1;
    umg2_shown_ = false;
}

bool Ue4ssPort::umg2_set_text(UObject* widget, const std::string& text) {
    if (!is_ok(widget)) return false;
    UFunction* set_text = find_function(widget, "SetText");
    if (set_text == nullptr) return false;
    ParamBlock params(set_text);
    if (!params.valid()) return false;
    if (!make_ftext_into(text, params.prop({"InText", "Text"}, 0), params.data())) return false;
    widget->ProcessEvent(set_text, params.data());
    return true;
}

bool Ue4ssPort::ensure_umg2_menu() {
    UClass* user_widget_class = umg_class("/Script/UMG.UserWidget");
    UClass* widget_tree_class = umg_class("/Script/UMG.WidgetTree");
    UClass* canvas_panel_class = umg_class("/Script/UMG.CanvasPanel");
    UClass* text_block_class = umg_class("/Script/UMG.TextBlock");
    if (user_widget_class == nullptr || widget_tree_class == nullptr || canvas_panel_class == nullptr ||
        text_block_class == nullptr) {
        if (!umg2_failed_logged_) {
            umg2_failed_logged_ = true;
            log(LogLevel::Warn, "交互菜单：引擎 UMG 类缺失，回退文字菜单");
        }
        umg2_unavailable_ = true;
        return false;
    }

    UObject* scene = world();
    if (!is_ok(scene)) return false;  // 世界没就绪：下帧再试

    UObject* widget = UObjectGlobals::NewObject<UObject>(scene, user_widget_class);
    if (!is_ok(widget)) {
        if (!umg2_failed_logged_) {
            umg2_failed_logged_ = true;
            log(LogLevel::Warn, "交互菜单：UserWidget 创建失败，回退文字菜单");
        }
        umg2_unavailable_ = true;
        return false;
    }

    UObject* tree = nullptr;
    FProperty* tree_prop = find_property(widget, "WidgetTree");
    read_object(tree_prop, widget, tree);
    if (!is_ok(tree)) {
        tree = UObjectGlobals::NewObject<UObject>(widget, widget_tree_class);
        if (!is_ok(tree) || !write_object(tree_prop, widget, tree)) {
            log(LogLevel::Warn, "交互菜单：WidgetTree 创建失败");
            umg2_unavailable_ = true;
            return false;
        }
    }

    UObject* canvas = UObjectGlobals::NewObject<UObject>(tree, canvas_panel_class);
    if (!is_ok(canvas)) {
        log(LogLevel::Warn, "交互菜单：CanvasPanel 创建失败");
        umg2_unavailable_ = true;
        return false;
    }
    if (!write_object(find_property(tree, "RootWidget"), tree, canvas)) {
        log(LogLevel::Warn, "交互菜单：RootWidget 写入失败");
        umg2_unavailable_ = true;
        return false;
    }

    // 按 umg2layout::kTexts 摆放全部文本（AddChildToCanvas + LayoutData 直写，1.0.20 验证过）；
    // @开头的动态行缓存指针供 umg2_tick 更新
    UObject* title = nullptr;
    for (const auto& item : umg2layout::kTexts) {
        UObject* w = UObjectGlobals::NewObject<UObject>(tree, text_block_class);
        if (!is_ok(w)) {
            log(LogLevel::Warn, "交互菜单：TextBlock 创建失败");
            umg2_drop();
            umg2_unavailable_ = true;
            return false;
        }
        UFunction* add_child = find_function(canvas, "AddChildToCanvas");
        UObject* slot = nullptr;
        if (add_child != nullptr) {
            ParamBlock params(add_child);
            if (params.valid() && write_object(params.prop({"Widget"}, 0), params.data(), w)) {
                canvas->ProcessEvent(add_child, params.data());
                read_object(params.prop({"ReturnValue"}, 0), params.data(), slot);
            }
        }
        if (!is_ok(slot)) {
            log(LogLevel::Warn, "交互菜单：画布槽位创建失败");
            umg2_drop();
            umg2_unavailable_ = true;
            return false;
        }
        FProperty* layout_data = find_property(slot, "LayoutData");
        void* base = struct_member_base(layout_data, slot);
        if (base != nullptr) {
            FProperty* anchors = struct_member(layout_data, "Anchors");
            FProperty* offsets = struct_member(layout_data, "Offsets");
            FProperty* alignment = struct_member(layout_data, "Alignment");
            if (anchors != nullptr && offsets != nullptr && alignment != nullptr) {
                // 右上角锚定（与 mod_app 的命中判定同一套坐标）：锚点(1,0) + 对齐(0,0)，
                // Left = 面板内x - kPanelFromRight、Top = 面板内y + kPanelTop。
                // 1.2.1 漏了这层锚定（全 0 锚点把面板钉在屏幕左上角），命中测试却按右上角算，点击全部落空。
                write_anchor(anchors, base, 1.0, 0.0, 1.0, 0.0);
                write_struct_number(offsets, base, "Left", item.x - umg2layout::kPanelFromRight);
                write_struct_number(offsets, base, "Top", item.y + umg2layout::kPanelTop);
                write_struct_number(alignment, base, "X", 0.0);
                write_struct_number(alignment, base, "Y", 0.0);
            }
        }
        write_bool(find_property(slot, "bAutoSize"), slot, true);

        const std::string text = item.text;
        if (text == "@status") {
            umg2_status_ = w;
            umg2_set_text(w, "STATE : ...");
        } else if (text == "@fov") {
            umg2_fov_value_ = w;
            umg2_set_text(w, "84.0");
        } else if (text == "@speed") {
            umg2_speed_value_ = w;
            umg2_set_text(w, "400 /s");
        } else if (text == "@sens") {
            umg2_sens_value_ = w;
            umg2_set_text(w, "0.10");
        } else {
            umg2_set_text(w, text);
            if (title == nullptr) {
                title = w;
                invoke(w, "SetColorAndOpacity", [&](ParamBlock& p) {
                    FProperty* color = p.prop({"InColorAndOpacity", "ColorAndOpacity"}, 0);
                    void* color_base = struct_member_base(color, p.data());
                    if (color_base == nullptr) return false;
                    FProperty* specified = struct_member(color, "SpecifiedColor");
                    return specified != nullptr && write_struct_number(specified, color_base, "R", 1.0) &&
                           write_struct_number(specified, color_base, "G", 0.85) &&
                           write_struct_number(specified, color_base, "B", 0.35) &&
                           write_struct_number(specified, color_base, "A", 1.0);
                });
            }
        }
    }

    if (!is_ok(umg2_status_) || !is_ok(umg2_fov_value_) || !is_ok(umg2_speed_value_) || !is_ok(umg2_sens_value_)) {
        log(LogLevel::Warn, "交互菜单：动态行缺失，回退文字菜单");
        umg2_drop();
        umg2_unavailable_ = true;
        return false;
    }

    if (!invoke(widget, "AddToViewport", [&](ParamBlock& p) { return p.set_number_any({"ZOrder"}, 0, 10000.0); })) {
        log(LogLevel::Warn, "交互菜单：AddToViewport 失败");
        umg2_drop();
        umg2_unavailable_ = true;
        return false;
    }

    umg2_widget_ = widget;
    umg2_tree_ = tree;
    umg2_path_ = narrow(widget->GetFullName());
    umg2_visibility_state_ = 0;
    umg2_shown_ = true;
    umg2_last_status_.clear();
    umg2_last_fov_text_.clear();
    umg2_last_speed_text_.clear();
    umg2_last_sens_text_.clear();
    umg2_failed_logged_ = false;
    if (!umg2_ready_logged_) {
        umg2_ready_logged_ = true;
        log(LogLevel::Info, "交互菜单已创建：" + umg2_path_ + "（右上角；文本按钮 + 自判定点击）");
    }
    return true;
}

bool Ue4ssPort::umg2_viewport_size(float& w, float& h) {
    w = 0.0f;
    h = 0.0f;
    UObject* ksl =
        UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/Engine.Default__KismetSystemLibrary");
    if (!is_ok(ksl)) return false;
    UFunction* fn = find_function(ksl, "GetViewportSize");
    if (fn == nullptr) return false;
    UObject* scene = world();
    if (!is_ok(scene)) return false;
    ParamBlock params(fn);
    if (!params.valid()) return false;
    if (!write_object(params.prop({"WorldContextObject"}, 0), params.data(), scene)) return false;
    ksl->ProcessEvent(fn, params.data());
    double sx = 0.0;
    double sy = 0.0;
    if (!read_number(params.prop({"SizeX"}, 0), params.data(), sx) ||
        !read_number(params.prop({"SizeY"}, 0), params.data(), sy)) {
        return false;
    }
    if (sx <= 1.0 || sy <= 1.0) return false;
    w = static_cast<float>(sx);
    h = static_cast<float>(sy);
    return true;
}

bool Ue4ssPort::umg2_available() const { return !umg2_unavailable_; }

std::string Ue4ssPort::umg2_state() const {
    if (umg2_unavailable_) return "不可用";
    if (umg2_widget_ == nullptr) return "未创建";
    return umg2_shown_ ? "交互" : "隐藏";
}

void Ue4ssPort::umg2_hide() {
    if (!umg2_shown_) return;
    umg2_shown_ = false;
    if (umg2_cursor_shown_) {
        if (UObject* pc = controller(); is_ok(pc)) {
            write_bool(find_property(pc, "bShowMouseCursor"), pc, false);
        }
        umg2_cursor_shown_ = false;
    }
    UObject* live = umg2_live();
    if (live != nullptr && umg2_visibility_state_ != 1) {
        if (set_widget_visibility(live, 1)) umg2_visibility_state_ = 1;  // Collapsed
    }
}

bool Ue4ssPort::umg2_tick(const std::string& status, double fov, double speed, double sens,
                          const Umg2Clicks& clicks, Umg2Commands& out) {
    if (umg2_unavailable_) return false;

    // 查活 + 节流重建（与文字菜单同款防 GC 策略）
    if (umg2_live() == nullptr) {
        const bool was_rebuild = umg2_widget_ != nullptr;
        umg2_drop();
        const double now = steady_seconds();
        if (now - umg2_last_rebuild_ < 0.5) return false;
        umg2_last_rebuild_ = now;
        if (!ensure_umg2_menu()) return false;
        if (was_rebuild) log(LogLevel::Info, "交互菜单：查活失败，已重建控件");
    }

    // ---- 点击 → 动作（命中判定在 mod_app，传进来的是面板内按钮的点击沿）
    if (clicks.fov_dec) out.fov_step -= 1;
    if (clicks.fov_inc) out.fov_step += 1;
    if (clicks.speed_dec) out.speed_delta -= 20.0;
    if (clicks.speed_inc) out.speed_delta += 20.0;
    if (clicks.sens_dec) out.sens_delta -= 0.01;
    if (clicks.sens_inc) out.sens_delta += 0.01;
    if (clicks.freeze) out.toggle_freeze = true;
    if (clicks.reset) out.reset_view = true;
    if (clicks.exit) out.exit_mode = true;

    // ---- 数值标签（变化才 SetText）
    char buf[48]{};
    if (is_ok(umg2_fov_value_)) {
        std::snprintf(buf, sizeof(buf), "%.1f", fov);
        if (umg2_last_fov_text_ != buf) {
            umg2_set_text(umg2_fov_value_, buf);
            umg2_last_fov_text_ = buf;
        }
    }
    if (is_ok(umg2_speed_value_)) {
        std::snprintf(buf, sizeof(buf), "%.0f /s", speed);
        if (umg2_last_speed_text_ != buf) {
            umg2_set_text(umg2_speed_value_, buf);
            umg2_last_speed_text_ = buf;
        }
    }
    if (is_ok(umg2_sens_value_)) {
        std::snprintf(buf, sizeof(buf), "%.2f", sens);
        if (umg2_last_sens_text_ != buf) {
            umg2_set_text(umg2_sens_value_, buf);
            umg2_last_sens_text_ = buf;
        }
    }
    if (is_ok(umg2_status_) && umg2_last_status_ != status) {
        umg2_set_text(umg2_status_, "STATE : " + status);
        umg2_last_status_ = status;
    }

    // ---- 鼠标指针：拍照模式游戏会藏指针，菜单打开期间持续保持可见（纯属性写，无游戏代码执行）
    if (UObject* pc = controller(); is_ok(pc)) {
        write_bool(find_property(pc, "bShowMouseCursor"), pc, true);
        umg2_cursor_shown_ = true;
    }

    // ---- 每秒确认还在视口（换图清视口但控件还活着的情形）
    const double now = steady_seconds();
    if (now - umg2_vp_check_ >= 1.0) {
        umg2_vp_check_ = now;
        bool in_viewport = true;
        invoke_bool(umg2_widget_, "IsInViewport", [](ParamBlock&) { return true; }, in_viewport);
        if (!in_viewport && invoke(umg2_widget_, "AddToViewport",
                                   [&](ParamBlock& p) { return p.set_number_any({"ZOrder"}, 0, 10000.0); })) {
            umg2_visibility_state_ = 0;
            log(LogLevel::Info, "交互菜单：发现不在视口，已重新 AddToViewport");
        }
    }

    if (umg2_visibility_state_ != 0) {
        UObject* live = umg2_live();
        if (live != nullptr && set_widget_visibility(live, 0)) umg2_visibility_state_ = 0;
    }
    umg2_shown_ = true;
    return true;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
