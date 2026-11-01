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
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FHitResult.hpp>
#include <Unreal/FSoftObjectPath.hpp>
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
        // 无参函数（如 Sequencer 的 Pause/Play）size 为 0：缓冲区为空会让 valid() 误判失败，
        // 而 ProcessEvent 仍需要一个有效指针，所以至少留 1 字节
        //（1.0.24 真机日志"命中 1，执行 0"的根因：Pause 调不出去）。
        buffer_.assign(size > 0 ? static_cast<size_t>(size) : 1, 0);
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

auto lower_ascii(std::string text) -> std::string {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// Wwise 集成类都以 Ak 开头；游戏自己的音频/语音/对话类从名字里认
auto is_audio_related_name(const std::string& name) -> bool {
    if (name.rfind("Ak", 0) == 0) return true;
    const std::string lower = lower_ascii(name);
    return lower.find("audio") != std::string::npos || lower.find("sound") != std::string::npos ||
           lower.find("voice") != std::string::npos || lower.find("dialog") != std::string::npos ||
           lower.find("wwise") != std::string::npos;
}

// 把一个类里可疑的音频控制函数列出来（Stop/Pause/Mute/Volume/Voice/Dialog…）
auto list_audio_functions(UObject* sample) -> std::string {
    std::string out;
    int listed = 0;
    for (TFieldIterator<UFunction> it(sample->GetClassPrivate()); it; ++it) {
        const std::string name = narrow(it->GetName());
        const std::string lower = lower_ascii(name);
        const bool interesting =
            lower.find("stop") != std::string::npos || lower.find("pause") != std::string::npos ||
            lower.find("mute") != std::string::npos || lower.find("silence") != std::string::npos ||
            lower.find("volume") != std::string::npos || lower.find("suspend") != std::string::npos ||
            lower.find("resume") != std::string::npos || lower.find("voice") != std::string::npos ||
            lower.find("dialog") != std::string::npos || lower.find("talk") != std::string::npos ||
            lower.find("audio") != std::string::npos || lower.find("sound") != std::string::npos;
        if (!interesting) continue;
        if (!out.empty()) out += ",";
        out += name;
        if (++listed >= 20) {
            out += ",…";
            break;
        }
    }
    return out.empty() ? "无可疑控制函数" : out;
}

// 全量函数名（不筛关键词）：用于 AkGameplayStatics 这类“BP 入口”类
auto list_all_functions(UObject* sample, int cap) -> std::string {
    std::string out;
    int listed = 0;
    for (TFieldIterator<UFunction> it(sample->GetClassPrivate()); it; ++it) {
        if (!out.empty()) out += ",";
        out += narrow(it->GetName());
        if (++listed >= cap) {
            out += ",…";
            break;
        }
    }
    return out.empty() ? "无函数" : out;
}

// 带签名校验的单数值参数调用（如 AkComponent.SetOutputBusVolume(volume)、
// PBZSoundFXSubsystem.PBZPausePlayingID(PlayingID)）：唯一参数必须是数值型
//（float/double/int），出现其他参数直接放弃，宁可不调也不瞎传。
auto invoke_set_number(UObject* target, const char* fn, double value) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    int numeric_params = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        const bool numeric = prop->IsA<FFloatProperty>() || prop->IsA<FDoubleProperty>() ||
                             prop->IsA<FIntProperty>() || prop->IsA<FUInt32Property>();
        if (!numeric) return false;
        ++numeric_params;
    }
    if (numeric_params != 1) return false;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        write_number(prop, params.data(), value);
    }
    target->ProcessEvent(function, params.data());
    return true;
}

// 参数签名描述（类型:名），调用被签名校验拦下时打进日志，下一轮照抄真实签名
auto describe_params(UFunction* function) -> std::string {
    std::string out;
    FProperty* ret = function != nullptr ? function->GetReturnProperty() : nullptr;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        const bool is_return = prop == ret;
        std::string type = "other";
        if (prop->IsA<FFloatProperty>()) type = "float";
        else if (prop->IsA<FDoubleProperty>()) type = "double";
        else if (prop->IsA<FIntProperty>()) type = "int32";
        else if (prop->IsA<FUInt32Property>()) type = "uint32";
        else if (prop->IsA<FByteProperty>()) type = "byte";
        else if (prop->IsA<FBoolProperty>()) type = "bool";
        else if (prop->IsA<FObjectProperty>()) type = "object";
        else if (prop->IsA<FStrProperty>()) type = "string";
        if (!out.empty()) out += ",";
        out += type + ":" + narrow(prop->GetName());
        if (is_return) out += "(ret)";
    }
    return out;
}

// 带签名校验的“按 ID 暂停/恢复”调用：真机签名已确认（1.0.34 日志）——
// PBZPausePlayingID / PBZResumePlayingID = (int32:PlayingID, float:FadeTime)。
// 名字含 playingid 填 ID、含 fade 填 FadeTime（0=立即）、含 worldcontext 填 world；
// 其余参数直接放弃并返回 false。
auto invoke_pause_id(UObject* target, const char* fn, int32_t id, double fade, UObject* world) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    bool filled_id = false;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        const bool numeric = prop->IsA<FFloatProperty>() || prop->IsA<FDoubleProperty>() ||
                             prop->IsA<FIntProperty>() || prop->IsA<FUInt32Property>();
        const std::string name = lower_ascii(narrow(prop->GetName()));
        if (numeric) {
            if (name.find("playingid") != std::string::npos) {
                write_number(prop, params.data(), static_cast<double>(id));
                filled_id = true;
            } else if (name.find("fade") != std::string::npos) {
                write_number(prop, params.data(), fade);
            } else {
                return false;
            }
        } else if (prop->IsA<FObjectProperty>() && name.find("worldcontext") != std::string::npos) {
            write_object(prop, params.data(), world);
        } else {
            return false;
        }
    }
    if (!filled_id) return false;
    target->ProcessEvent(function, params.data());
    return true;
}

// ------------------------------------------------------------------ 冻结期残余微动治理

// 带签名校验的单 bool 参数调用（StopAllCameraShakes(bool) / DisableModifier(bool)）：
// 唯一参数必须是 bool 型。参数名对不上时绝不照调——DisableModifier 写不进参数
// 就等于发 bDisable=0（启用），方向正好相反。
auto invoke_set_bool(UObject* target, const char* fn, bool value) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, fn);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    FProperty* the_param = nullptr;
    int bool_params = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == ret) continue;
        if (!prop->IsA<FBoolProperty>()) return false;
        ++bool_params;
        the_param = prop;
    }
    if (bool_params != 1 || the_param == nullptr) return false;
    write_bool(the_param, params.data(), value);
    target->ProcessEvent(function, params.data());
    return true;
}

auto kismet_material_library() -> UObject* {
    return find_object("/Script/Engine.Default__KismetMaterialLibrary");
}

// 数组属性判定：IsA<FArrayProperty> 走 UE4SS 的属性类注册表（FFieldClassRegistrar），
// 注册失败时 IsA 对该类型永远 false 且无任何报错——1.0.46 实机 CameraModifiers 与
// MPC 的 ScalarParameters/VectorParameters 全部因此被误判为“不是数组”。
// 字段类名读的是引擎数据（FFieldClassVariant::GetFName），作权威判定；
// 类名 "ArrayProperty" 即 FArrayProperty，此时 static_cast 是安全的。
auto as_array_property(FProperty* prop) -> FArrayProperty* {
    if (prop == nullptr) return nullptr;
    if (prop->IsA<FArrayProperty>()) return static_cast<FArrayProperty*>(prop);
    const std::string field_class = narrow(prop->GetClass().GetFName().ToString());
    if (field_class == "ArrayProperty") return static_cast<FArrayProperty*>(prop);
    return nullptr;
}

// 读 UMaterialParameterCollection 的 ScalarParameters / VectorParameters 参数名
//（TArray<FCollectionScalarParameter/FCollectionVectorParameter>，元素结构体取 ParameterName）
auto mpc_read_param_names(UObject* collection, const char* array_name, std::vector<std::string>& out)
    -> void {
    FProperty* prop = find_property(collection, array_name);
    auto* array_prop = as_array_property(prop);
    if (array_prop == nullptr) return;
    FScriptArrayHelper_InContainer helper(array_prop, static_cast<const void*>(collection));
    const int32 count = helper.Num();
    for (int32 i = 0; i < count && i < 64; ++i) {  // 上限防异常数据
        void* element = helper.GetRawPtr(i);
        std::string name;
        if (read_struct_name(array_prop->GetInner(), element, "ParameterName", name) && !name.empty()) {
            out.push_back(std::move(name));
        }
    }
}

// KismetMaterialLibrary 的 MPC 读写：走世界的 CollectionInstance，材质 uniforms 同步更新，
// 这是运行时改 MPC 的正路（直接改资产默认值不影响运行中的实例）。
auto mpc_get_scalar(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    double& out) -> bool {
    UFunction* function = find_function(statics, "GetScalarParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    statics->ProcessEvent(function, params.data());
    // 实机签名（1.0.48 诊断）：object,object,other:ParameterName,float:ReturnValue(ret)——
    // 该版本把唯一的出参直接作为返回值（属性名 ReturnValue），旧签名才是 bool + OutValue 出参
    return params.get_number("OutValue", out) || params.get_number("ReturnValue", out);
}

auto mpc_set_scalar(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    double value) -> bool {
    UFunction* function = find_function(statics, "SetScalarParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    if (!params.set_number("Value", value)) {
        // 值参数名对不上时按位置兜底（第 4 个参数；类型校验仍由 write_number 把关）
        if (!write_number(params.prop({"Value"}, 3), params.data(), value)) return false;
    }
    statics->ProcessEvent(function, params.data());
    return true;
}

auto mpc_get_vector(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    float out[4]) -> bool {
    UFunction* function = find_function(statics, "GetVectorParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    statics->ProcessEvent(function, params.data());
    // 与 get_scalar 同款：出参可能叫 OutValue（旧）或直接是返回值 ReturnValue（实机）
    FProperty* out_value = params.property("OutValue");
    if (out_value == nullptr) out_value = params.property("ReturnValue");
    static const char* kMembers[4] = {"R", "G", "B", "A"};
    for (int i = 0; i < 4; ++i) {
        double channel = 0.0;
        if (!read_struct_number(out_value, params.data(), kMembers[i], channel)) return false;
        out[i] = static_cast<float>(channel);
    }
    return true;
}

auto mpc_set_vector(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    const float in[4]) -> bool {
    UFunction* function = find_function(statics, "SetVectorParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    FProperty* value_prop = params.property("Value");
    if (value_prop == nullptr) value_prop = params.prop({"Value"}, 3);
    static const char* kMembers[4] = {"R", "G", "B", "A"};
    for (int i = 0; i < 4; ++i) {
        if (!write_struct_number(value_prop, params.data(), kMembers[i], in[i])) return false;
    }
    statics->ProcessEvent(function, params.data());
    return true;
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

// 调一个返回数值的无参 UFUNCTION（例如 PlayerCameraManager::GetFOVAngle），读 ReturnValue。
static auto read_return_number(UObject* object, const char* function_name, double& out) -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    object->ProcessEvent(function, params.data());
    return params.get_number("ReturnValue", out);
}

// APlayerCameraManager::CameraCache（FMinimalViewInfo）存着这一帧最终视角。
// 5.4 及以前 FOV 是直接成员，5.5 起挪进 Lens（FieldOfView）；两处都试。
static auto read_camera_cache_fov(UObject* manager, double& out) -> bool {
    if (!is_ok(manager)) return false;
    FProperty* cache = find_property(manager, "CameraCache");
    if (cache == nullptr || !cache->IsA<FStructProperty>()) return false;
    if (read_struct_number(cache, manager, "FOV", out) && out > 1.0) return true;
    void* base = struct_member_base(cache, manager);
    FProperty* field_of_view = struct_member(struct_member(cache, "Lens"), "FieldOfView");
    return base != nullptr && field_of_view != nullptr && read_number(field_of_view, base, out) && out > 1.0;
}

bool Ue4ssPort::read_pov(CameraPose& out) {
    bool any = false;

    // 兜底链：摄影相机旧值（再次进入）/ 管理器默认值。两者都不是「玩家眼前这一帧」
    // 的 FOV，只在当前视角读不到时使用
    auto fov_fallback = [&]() {
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
    };

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
            // FOV 必须在这里一并取：GetFOVAngle 返回当前 CameraCache 的 FOV，游戏相机
            // 蓝图/瞄准改过的值也在里面；DefaultFOV 只是默认值（UE 默认 90），跟玩家
            // 眼前镜头无关。此前这里直接 return，FOV 恒落 CameraPose 默认 90，
            // 一进摄影模式画面就突然变广。
            double fov = 0.0;
            bool have_fov = read_return_number(manager, "GetFOVAngle", fov) && fov > 1.0;
            if (!have_fov) have_fov = read_camera_cache_fov(manager, fov) && fov > 1.0;
            if (have_fov) {
                out.fov = fov;
            } else {
                fov_fallback();
            }
            log(LogLevel::Info, "初始位姿取自相机管理器视角");
            return any;
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

    fov_fallback();
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
    // 每帧调用：函数查找走缓存（UFunction 挂在 PC 类上，进程内稳定）
    if (mouse_delta_fn_missing_) return false;
    if (mouse_delta_fn_ == nullptr) {
        mouse_delta_fn_ = find_function(pc, "GetInputMouseDelta");
        if (mouse_delta_fn_ == nullptr) {
            mouse_delta_fn_missing_ = true;
            return false;
        }
    }
    UFunction* function = mouse_delta_fn_;

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

// SetViewTargetWithBlend 的调用走缓存：reassert_view_target 每帧一次，
// find_function 每次都要走一遍函数链，缓存掉这步反射查找
bool Ue4ssPort::set_view_target_cached(UObject* pc, UObject* target) {
    if (!is_ok(pc)) return false;
    if (set_view_target_fn_missing_) return false;
    if (set_view_target_fn_ == nullptr) {
        set_view_target_fn_ = find_function(pc, "SetViewTargetWithBlend");
        if (set_view_target_fn_ == nullptr) {
            set_view_target_fn_missing_ = true;
            return false;
        }
    }
    ParamBlock params(set_view_target_fn_);
    if (!params.valid()) return false;
    if (!params.set_object("NewViewTarget", target) || !params.set_number("BlendTime", 0.0)) return false;
    pc->ProcessEvent(set_view_target_fn_, params.data());
    return true;
}

bool Ue4ssPort::attach_view() {
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return false;
    const bool ok = set_view_target_cached(pc, camera_actor_);
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
    set_view_target_cached(pc, camera_actor_);
}

bool Ue4ssPort::restore_view() {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    UObject* pawn = player_pawn();
    return set_view_target_cached(pc, pawn);
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

// 设置开关 setter 的参数名离线确认不了，按候选名试；全不中由调用方直写属性。
auto fill_prompt_setting_param(ParamBlock& params, bool value) -> bool {
    return params.set_bool("bVisible", value) || params.set_bool("bNewValue", value) ||
           params.set_bool("bNewVisible", value) || params.set_bool("bNewVisibility", value) ||
           params.set_bool("Value", value) || params.set_bool("InVisible", value);
}

}  // namespace

bool Ue4ssPort::set_interact_prompts_visible(bool visible) {
    bool touched = false;

    // ---- 路线 ①：游戏设置开关（管住之后新出现的提示）----
    if (!visible) {
        // 已保存过就直接用缓存的 owner：find_scene_prompt_setting_owner 要按 9 个候选类名
        // 逐个 FindAllOf（每个都是一遍全对象数组扫描），失败重试期间是纯浪费
        UObject* owner = prompt_setting_saved_ ? prompt_setting_owner_ : find_scene_prompt_setting_owner();
        if (is_ok(owner)) {
            FProperty* property = find_property(owner, "bSceneInteractPromptVisible");
            double original = 1.0;
            if (read_number(property, owner, original)) {
                prompt_setting_saved_ = true;
                prompt_setting_original_ = original != 0.0;
                prompt_setting_owner_ = owner;
            }
            bool set_ok = invoke(owner, "SetSceneInteractPromptVisible",
                                 [&](ParamBlock& params) { return fill_prompt_setting_param(params, visible); });
            // setter 参数名对不上时直接写属性（值本身一定在）；广播缺失只影响“立刻隐藏已画出的”，
            // 那部分由路线 ② 的组件隐藏兜住。
            if (!set_ok) set_ok = write_bool(property, owner, visible);
            if (set_ok) {
                touched = true;
                log(LogLevel::Info, "场景交互提示开关已" + std::string(visible ? "打开" : "关闭") +
                                        "（owner=" + narrow(owner->GetFullName()).substr(0, 140) + "）");
            }
        } else if (!prompt_setting_missing_logged_) {
            prompt_setting_missing_logged_ = true;
            log(LogLevel::Warn, "候选对象里找不到场景交互提示开关（bSceneInteractPromptVisible）");
        }
    } else if (prompt_setting_saved_) {
        bool restored = false;
        if (is_ok(prompt_setting_owner_)) {
            restored = invoke(prompt_setting_owner_, "SetSceneInteractPromptVisible",
                              [&](ParamBlock& params) { return fill_prompt_setting_param(params, prompt_setting_original_); });
            if (!restored) {
                restored = write_bool(find_property(prompt_setting_owner_, "bSceneInteractPromptVisible"),
                                      prompt_setting_owner_, prompt_setting_original_);
            }
        }
        prompt_setting_saved_ = false;
        prompt_setting_owner_ = nullptr;
        if (restored) {
            touched = true;
            log(LogLevel::Info, "场景交互提示开关已还原");
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
        for (const auto& entry : prompt_components_) {
            if (!pointer_in(components, entry.first)) continue;
            if (set_component_visibility(entry.first, entry.second)) ++restored_count;
        }
        if (restored_count > 0) {
            touched = true;
            log(LogLevel::Info, "已恢复交互提示控件：数量=" + std::to_string(restored_count));
        }
        prompt_components_.clear();
    }

    return touched;
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

// setter 参数名离线确认不了，按候选名试；全不中按声明顺序取第 0 个参数
auto fill_bar_mode_param(ParamBlock& params, double value) -> bool {
    return params.set_number_any({"Mode", "InMode", "NewMode", "InMonsterHealthBarMode",
                                  "NewMonsterHealthBarMode", "Value"},
                                 0, value);
}

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
        bars_setting_saved_ = false;
        bars_setting_owner_ = nullptr;
        if (restored) {
            touched = true;
            log(LogLevel::Info, "敌人血条显示模式已还原");
        }
    }
    return touched;
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



// 字幕开关的真属性名：1.0.33 实机诊断确认设置类上是 **bEnableSubtitles**（此前一直找
// bSubtitlesEnabled——那是引擎 GlobalUserParam 的名字，不在设置类属性链里，导致原值读取
// 失败、还原只能按默认兜底）。两个名字都试，覆盖引擎实例路径。
auto find_subtitle_flag_property(UObject* owner) -> FProperty* {
    if (owner == nullptr) return nullptr;
    if (FProperty* p = find_property(owner, "bEnableSubtitles"); p != nullptr) return p;
    return find_property(owner, "bSubtitlesEnabled");
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

// 读当前字幕开关状态：设置类上没有 property 时走 AreSubtitlesEnabled（exe 扫描确认存在，
// 与 SetSubtitlesEnabled 成对）；都没有才失败（调用方按 true 兜底）。
auto read_subtitles_enabled_state(UObject* owner, FProperty* property, bool& out) -> bool {
    double raw = 0.0;
    if (read_number(property, owner, raw)) {
        out = raw != 0.0;
        return true;
    }
    bool getter_result = true;
    if (invoke_bool(owner, "AreSubtitlesEnabled", [](ParamBlock&) { return true; }, getter_result)) {
        out = getter_result;
        return true;
    }
    return false;
}

// 设置开关 setter 的参数名离线确认不了，按候选名试；全不中由调用方直写属性。
auto fill_subtitle_setting_param(ParamBlock& params, bool value) -> bool {
    return params.set_bool("bEnabled", value) || params.set_bool("bInEnabled", value) ||
           params.set_bool("bNewValue", value) || params.set_bool("bInSubtitlesEnabled", value) ||
           params.set_bool("bNewEnabled", value) || params.set_bool("Value", value);
}

}  // namespace

// 字幕控件全量扫描的类级判定：继承链 + 类名匹配只依赖类，按类缓存一次算完。
// 对象几十万、类 ~1 万，此前逐对象 narrow+lower 是进入/退出卡顿的另一大来源
struct SubtitleClassVerdict {
    bool matched{false};
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
            verdict.matched =
                is_subtitle_display ||
                (is_user_widget && (lower_name.find("subtitle") != std::string::npos ||
                                    lower_name.find("flowdialog") != std::string::npos));
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

    // ---- 路线 ①：游戏设置开关（管住之后新出的字幕）----
    // 1.0.33 首版的结构 bug（实机日志钉死）：!visible && !touched_ / else if touched_ 的写法，
    // 让每秒重申（visible=false 且 touched_=true）掉进还原分支 → 设置开关每秒 关/开 打摆。
    // 必须把"隐藏"与"还原"按 visible 彻底分开；已在关状态时重申只跳过，不碰开关。
    if (!visible) {
        if (!subtitle_setting_touched_) {
            UObject* owner = find_subtitle_setting_owner();
            if (is_ok(owner)) {
                dump_subtitle_setting_diagnostics(owner);
                FProperty* property = find_subtitle_flag_property(owner);
                bool captured = read_subtitles_enabled_state(owner, property, subtitle_setting_original_);
                subtitle_setting_saved_ = captured;
                subtitle_setting_touched_ = true;  // 只要动过开关，退出就必须还原（原值读不到按 true 还原）
                subtitle_setting_owner_ = owner;
                bool set_ok = invoke(owner, "SetSubtitlesEnabled",
                                     [&](ParamBlock& params) { return fill_subtitle_setting_param(params, false); });
                if (!set_ok) set_ok = write_bool(property, owner, false);
                // 回读验证：bEnableSubtitles 在设置类上，读得到就核对真值；不匹配用属性强写一次
                double actual = -1.0;
                if (read_number(property, owner, actual)) {
                    const bool actual_on = actual != 0.0;
                    if (actual_on) {
                        write_bool(property, owner, false);
                        read_number(property, owner, actual);
                        actual = actual != 0.0 ? 1.0 : 0.0;
                    }
                    log(LogLevel::Info, "字幕设置开关已关闭（owner=" +
                                            narrow(owner->GetFullName()).substr(0, 140) + "，原值=" +
                                            (subtitle_setting_original_ ? "开" : "关") + "，回读=" +
                                            (actual != 0.0 ? "开-未生效!" : "关") + "）");
                    touched = true;
                } else {
                    if (set_ok) {
                        touched = true;
                        log(LogLevel::Info, "字幕设置开关已关闭（owner=" +
                                                narrow(owner->GetFullName()).substr(0, 140) + "，原值=" +
                                                (subtitle_setting_original_ ? "开" : "关") +
                                                "，属性读不到无法回读）");
                    }
                }
            } else if (!subtitle_setting_missing_logged_) {
                subtitle_setting_missing_logged_ = true;
                log(LogLevel::Warn, "候选对象里找不到字幕设置开关（SetSubtitlesEnabled/bEnableSubtitles），退回只藏字幕控件");
            }
        }
    } else if (subtitle_setting_touched_) {
        bool restored = false;
        if (is_ok(subtitle_setting_owner_)) {
            FProperty* property = find_subtitle_flag_property(subtitle_setting_owner_);
            restored = invoke(subtitle_setting_owner_, "SetSubtitlesEnabled",
                              [&](ParamBlock& params) { return fill_subtitle_setting_param(params, subtitle_setting_original_); });
            if (!restored) {
                restored = write_bool(property, subtitle_setting_owner_, subtitle_setting_original_);
            }
        }
        subtitle_setting_touched_ = false;
        subtitle_setting_saved_ = false;
        subtitle_setting_owner_ = nullptr;
        if (restored) {
            touched = true;
            log(LogLevel::Info, "字幕设置开关已还原");
        } else if (!subtitle_restore_failed_logged_) {
            subtitle_restore_failed_logged_ = true;
            log(LogLevel::Warn, "字幕设置开关还原失败（setter 与属性都写不进去），字幕可能保持隐藏——把这条日志发回来");
        }
    }

    // ---- 路线 ②：字幕显示控件（管住此刻已经画出来的）----
    // 进入后重申：对话 UI 创建时才读设置，运行中的对话由游戏逐条把控件重新显示，
    // 一次性的隐藏会被盖回去（1.0.32 实机：进模式后字幕不消失）。重申是幂等的：
    // 已管的控件若被游戏改回可见就再压回去，新实例收编，设置开关不重复写。
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

    if (!visible) {
        // 暂停期跳过了收集：控件 Tick 冻住、游戏不会重新显示控件，本轮无事可做。
        // （1.0.39 实机：空 widgets 落到下面“没找到控件”告警，白跑一次候选全扫描）
        if (!discovery) return touched;
        int hidden_count = 0;
        int reasserted_count = 0;
        std::string hidden_names;
        for (UObject* widget : widgets) {
            if (!is_ok(widget)) continue;
            if (tracked_hud_widget(subtitle_widgets_, widget)) {
                // 已在管：游戏每条字幕可能把控件重新 SetVisibility(Visible)，重申 Collapsed。
                // 不重复记录原值——原值在首次收编时已存，退出时按它恢复。
                int current = 1;
                if (get_widget_visibility(widget, current) && current != 1) {
                    if (set_widget_visibility(widget, 1)) ++reasserted_count;
                }
                continue;
            }
            int original = 0;
            if (!get_widget_visibility(widget, original)) original = 0;  // 默认 Visible
            if (set_widget_visibility(widget, 1)) {  // Collapsed
                subtitle_widgets_.emplace_back(widget, original);
                ++hidden_count;
                if (is_ok(widget)) {
                    hidden_names += (hidden_names.empty() ? "" : ", ") + narrow(widget->GetClassPrivate()->GetName());
                }
            }
        }
        if (hidden_count > 0) {
            touched = true;
            log(LogLevel::Info, "已隐藏字幕显示控件：数量=" + std::to_string(hidden_count) + "（" + hidden_names +
                                    "），退出摄影模式时恢复原可见性");
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
        for (const auto& entry : subtitle_widgets_) {
            if (!pointer_in(widgets, entry.first)) continue;
            if (set_widget_visibility(entry.first, entry.second)) ++restored_count;
        }
        if (restored_count > 0) {
            touched = true;
            log(LogLevel::Info, "已恢复字幕显示控件：数量=" + std::to_string(restored_count));
        }
        subtitle_widgets_.clear();
        subtitle_scanned_ = false;  // 下次进入重新做一次全量扫描（新对话控件类可能不同）
    }

    return touched;
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
        const LockonClassVerdict& verdict = lockon_class_verdict(cls);
        if (!verdict.in_superset) return RC::LoopAction::Continue;
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
            if (verdict.name_matched) {
                candidates.push_back({object, verdict.is_widget, verdict.class_name});
                return RC::LoopAction::Continue;
            }
            if (verdict.is_widget || verdict.asset_props->empty()) return RC::LoopAction::Continue;
            for (FProperty* property : *verdict.asset_props) {
                UObject* asset = nullptr;
                if (read_object(property, object, asset) && is_ok(asset) &&
                    name_matches_lockon(lower_ascii(narrow(asset->GetName())))) {
                    candidates.push_back({object, verdict.is_widget, verdict.class_name});
                    break;
                }
            }
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

        if (hidden_count > 0) {
            touched = true;
            log(LogLevel::Info, "已隐藏锁定标记：数量=" + std::to_string(hidden_count) + "（" + hidden_names +
                                    "），退出摄影模式时还原原可见性");
            return true;
        }
        if (hide_failed > 0) return false;  // 有目标但藏不住：按秒重试

        // 全量扫描零可隐藏对象：白点要么此刻不在（没锁定），要么类名不在关键词网里。
        // 打一次性诊断清单（可见控件/可视组件/玩家组件），真实类名靠这份日志收敛。
        lockon_absent_ = true;
        dump_lockon_diagnostics();
        if (!lockon_absent_logged_) {
            lockon_absent_logged_ = true;
            log(LogLevel::Warn,
                "场上未找到可见的锁定标记对象（锁定/目标/标记关键词零命中，已按隐藏=" + std::to_string(already_hidden) +
                    " 个同名家族对象）——若白点仍在屏上，把上面的诊断清单发回来");
        }
        return true;
    }

    // 恢复：重新枚举白点可能所在的超集做存活校验（换图/GC 后旧对象可能已销毁，
    // 不能直接解引用旧指针）
    std::vector<UObject*> alive;
    collect_lockon_superset(alive);
    int restored_count = 0;
    for (const auto& entry : lockon_widgets_) {
        if (!pointer_in(alive, entry.first)) continue;
        if (set_widget_visibility(entry.first, entry.second)) ++restored_count;
    }
    for (const auto& entry : lockon_components_) {
        if (!pointer_in(alive, entry.first)) continue;
        if (set_component_visibility(entry.first, entry.second)) ++restored_count;
    }
    if (restored_count > 0) {
        touched = true;
        log(LogLevel::Info, "已恢复锁定标记：数量=" + std::to_string(restored_count));
    }
    lockon_widgets_.clear();
    lockon_components_.clear();
    lockon_absent_ = false;  // 下次进入重新扫描（不同场景的锁定标记可能不同）
    return touched;
}

// 一次性诊断（每会话一次，关键词没接住白点时给下一轮指认真身）：
//   ① 可见 UserWidget 类——白点若是屏幕/世界 UMG，必在其中；
//   ② 场上可视组件类——白点若是粒子/贴花/小面片，类名在这里；
//   ③ 玩家 pawn 名下类名带锁定/目标语义的组件——锁定目标状态所在，为下一版直接清锁定指路。
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
                            (visible_widgets.empty() ? std::string("（无）") : join_counts(visible_widgets, 40)));
    log(LogLevel::Info, "锁定标记诊断② 可视组件：" +
                            (visual_components.empty() ? std::string("（无）") : join_counts(visual_components, 40)));

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
}

// ------------------------------------------------------------------ 超宽屏画幅解锁

// 游戏把画幅锁在 16:9，超宽屏（21:9/32:9）两侧补黑边。UE 里这把锁最常挂在两处：
// ① APlayerCameraManager.bDefaultConstrainAspectRatio / DefaultAspectRatio —— 对任何 ViewTarget 生效，
//   就算 ViewTarget 换成摄影相机，黑边照样在（用户反馈的超宽屏黑边就是它）；
// ② 相机组件自己的 bConstrainAspectRatio —— 我们生成的 CameraActor 默认不锁，显式再关一次保险。
bool Ue4ssPort::unlock_aspect_ratio() {
    bool ok = false;

    UObject* manager = camera_manager();
    if (is_ok(manager)) {
        FProperty* constrain_prop = find_property(manager, "bDefaultConstrainAspectRatio");
        FProperty* value_prop = find_property(manager, "DefaultAspectRatio");
        double constrain = 0.0;
        if (constrain_prop == nullptr || value_prop == nullptr ||
            !read_number(constrain_prop, manager, constrain)) {
            log(LogLevel::Warn,
                "画幅解锁：相机管理器读不到 bDefaultConstrainAspectRatio/DefaultAspectRatio"
                "（黑边若来自别的机制，把这条日志发回来）");
        } else if (constrain == 0.0) {
            log(LogLevel::Info, "画幅：相机管理器未锁定画幅，无需解锁");
            ok = true;  // 没锁也算成功：退出时无需写回
        } else {
            // 只在确实锁着时才记原值；还原前校验管理器还是同一个，防止换持有时把旧值写进新管理器
            aspect_saved_ = true;
            aspect_original_constrain_ = true;
            aspect_manager_ = manager;
            if (!read_number(value_prop, manager, aspect_original_value_)) {
                aspect_original_value_ = 16.0 / 9.0;
            }
            if (write_bool(constrain_prop, manager, false)) {
                char buf[96]{};
                std::snprintf(buf, sizeof(buf), "画幅解锁：已解除 16:9 锁定（原比例=%.3f），超宽屏黑边消除",
                              aspect_original_value_);
                log(LogLevel::Info, buf);
                ok = true;
            } else {
                aspect_saved_ = false;
                aspect_manager_ = nullptr;
                log(LogLevel::Warn, "画幅解锁失败：bDefaultConstrainAspectRatio 写不进去");
            }
        }
    } else {
        log(LogLevel::Warn, "画幅解锁：拿不到相机管理器，跳过管理器这一路");
    }

    // 摄影相机自己的约束一并关掉（CameraActor 默认不锁，这里防类默认值被改过）
    if (UObject* component = camera_component(); is_ok(component)) {
        write_bool(find_property(component, "bConstrainAspectRatio"), component, false);
    }
    return ok;
}

void Ue4ssPort::restore_aspect_ratio() {
    if (!aspect_saved_) return;
    aspect_saved_ = false;

    UObject* manager = camera_manager();
    if (!is_ok(manager) || manager != aspect_manager_) {
        // 管理器换了（重新持有等）：旧值属于旧对象，不能写进新管理器
        log(LogLevel::Info, "画幅还原：相机管理器已不是进入时的对象，跳过写回");
        aspect_manager_ = nullptr;
        return;
    }
    aspect_manager_ = nullptr;
    FProperty* constrain_prop = find_property(manager, "bDefaultConstrainAspectRatio");
    if (write_bool(constrain_prop, manager, aspect_original_constrain_)) {
        if (aspect_original_constrain_) {
            if (FProperty* value_prop = find_property(manager, "DefaultAspectRatio"); value_prop != nullptr) {
                write_number(value_prop, manager, aspect_original_value_);
            }
            log(LogLevel::Info, "画幅还原：相机管理器已恢复 16:9 锁定");
        }
    } else {
        log(LogLevel::Warn, "画幅还原失败：bDefaultConstrainAspectRatio 写不回去");
    }
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

bool Ue4ssPort::world_paused() {
    UObject* statics = gameplay_statics();
    UObject* scene = world();
    if (!is_ok(statics) || !is_ok(scene)) return false;
    UFunction* function = find_function(statics, "IsGamePaused");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    params.set_object("WorldContextObject", scene);
    statics->ProcessEvent(function, params.data());
    double value = 0.0;
    return params.get_number("ReturnValue", value) && value != 0.0;
}

// 剧情过场（Sequencer）播放器：世界暂停与时间缩放拦不住剧情系统的自主更新
// （画面像冻住了、动作和声音还在走），冻结时必须对 SequencePlayer 的 Pause/Play 单独下手。
// FindAllOf 沿继承链匹配，LevelSequencePlayer 与游戏自定义派生播放器都能扫到。
SequenceFreezeReport Ue4ssPort::pause_sequences(bool paused) {
    SequenceFreezeReport report{};
    report.ok = true;

    std::vector<UObject*> players;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("MovieSceneSequencePlayer"), players);
    const int direct_count = static_cast<int>(players.size());

    // 兜底：过场 Actor（含游戏自定义派生类）身上的 SequencePlayer 属性
    std::vector<UObject*> actors;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("LevelSequenceActor"), actors);
    for (UObject* actor : actors) {
        if (!is_ok(actor)) continue;
        UObject* player = nullptr;
        if (read_object(find_property(actor, "SequencePlayer"), actor, player) && player != nullptr) {
            players.push_back(player);
        }
    }

    std::sort(players.begin(), players.end());
    players.erase(std::unique(players.begin(), players.end()), players.end());
    report.found = static_cast<int>(players.size());

    std::string matched_class;
    for (UObject* player : players) {
        if (!is_ok(player)) continue;
        // 类默认对象（Default__...）不参与播放，显式排除避免脏命中
        if (narrow(player->GetFullName()).find("Default__") != std::string::npos) continue;
        // 暂停时找“正在播放”的；恢复时只恢复“处于暂停态”的（不去碰游戏自己停掉的）
        bool in_state = false;
        if (!invoke_bool(player, paused ? "IsPlaying" : "IsPaused", [&](ParamBlock&) { return true; }, in_state)) {
            continue;
        }
        if (!in_state) continue;
        ++report.matched;
        if (matched_class.empty()) matched_class = narrow(player->GetClassPrivate()->GetName());
        if (invoke(player, paused ? "Pause" : "Play", [&](ParamBlock&) { return true; })) {
            ++report.acted;
        }
    }

    // 剧情语音多半走 Wwise/普通 AudioComponent，暂停它们是另一条路；先记录游戏用哪种音频系统留证据
    const bool wwise = find_object("/Script/AkAudio.Default__AkGameplayStatics") != nullptr ||
                       find_object("/Script/Wwise.Default__AkGameplayStatics") != nullptr;
    char buf[280]{};
    std::snprintf(buf, sizeof(buf),
                  "剧情播放器[%s]：扫描 %d（直扫 %d + Actor 兜底 %d），命中 %d，执行 %d，类=%s，Wwise=%s，Ak组件=%d",
                  paused ? "暂停" : "恢复", report.found, direct_count, report.found - direct_count,
                  report.matched, report.acted, matched_class.empty() ? "-" : matched_class.c_str(),
                  wwise ? "有" : "无", wwise_component_count());
    log(LogLevel::Info, buf);
    if (paused && report.matched > report.acted) {
        log(LogLevel::Warn, "存在命中的播放器执行失败，请把这行日志反馈给作者");
    }
    if (paused && report.found == 0) {
        log(LogLevel::Info, "未发现 Sequencer 剧情播放器；若剧情中动作/声音仍继续，请带上 UE4SS.log 反馈");
    }
    return report;
}

// ---- 冻结期残余微动治理（1.0.41）----
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

    FProperty* modifiers_prop = find_property(manager, "CameraModifiers");
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
        log(LogLevel::Info, std::string("相机管理器无 CameraModifiers 数组") + detail + "，管理器=" +
                                manager_class + "；shake 停用=" + (shakes_stopped ? "成功" : "不可用"));
    } else {
        camera_modifier_states_.clear();
        log(LogLevel::Warn, "相机管理器无 CameraModifiers 数组，修饰器还原跳过");
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
    UObject* statics = kismet_material_library();
    UObject* scene = world();
    if (!is_ok(statics) || !is_ok(scene)) return 0;
    int written = 0;
    for (const MpcCollectionSnapshot& snapshot : mpc_snapshots_) {
        if (!is_ok(snapshot.collection)) continue;
        for (const MpcParamValue& param : snapshot.params) {
            const bool ok = param.is_vector
                                ? mpc_set_vector(statics, scene, snapshot.collection, param.name, param.value)
                                : mpc_set_scalar(statics, scene, snapshot.collection, param.name, param.value[0]);
            if (ok) ++written;
        }
    }
    return written;
}

void Ue4ssPort::mpc_release_snapshot() {
    mpc_snapshots_.clear();
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
            FProperty* modifiers_prop = find_property(manager, "CameraModifiers");
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

// Wwise 音频组件：对话语音走 Wwise 时世界暂停拦不住。这里只做两件事：
// 数量诊断（每次冻结顺带）与可选的一次性静音（stop_wwise_on_freeze，停止后不可恢复）。
int Ue4ssPort::wwise_component_count() {
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), components);
    probe_wwise_components(components);
    return static_cast<int>(components.size());
}

// 对话语音双保险（两层都做，静音不依赖任何单一通道）：
//   第一层 截获的 PlayingID → 游戏自有 PBZPausePlayingID / PBZResumePlayingID
//   （Wwise 原生暂停，解冻后当前句从原位置续播）；
//   第二层 组件音量归零/恢复——盖住没截获到的声音（钩子装好前已在播的、
//   绕过子系统直接走集成层的）。
int Ue4ssPort::pause_wwise_voices(bool pause) {
    int total = 0;
    if (!playing_ids_.empty()) {
        UObject* soundfx = nullptr;
        {
            std::vector<UObject*> subsystems;
            RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZSoundFXSubsystem"), subsystems);
            for (UObject* candidate : subsystems) {
                if (is_ok(candidate)) {
                    soundfx = candidate;
                    break;
                }
            }
        }
        if (soundfx != nullptr) {
            const char* fn = pause ? "PBZPausePlayingID" : "PBZResumePlayingID";
            UFunction* fn_ptr = find_function(soundfx, fn);
            int acted = 0;
            for (int32_t id : playing_ids_) {
                if (invoke_pause_id(soundfx, fn, id, 0.0, world())) ++acted;  // FadeTime=0：立即暂停/立即恢复
            }
            if (acted == 0 && fn_ptr != nullptr && !pause_signature_logged_) {
                pause_signature_logged_ = true;
                log(LogLevel::Warn, std::string(fn) + " 参数签名=" + describe_params(fn_ptr) +
                                        "（全部调用被签名校验拦下，回落音量兜底）");
            }
            log(LogLevel::Info, std::string(pause ? "对话语音暂停" : "对话语音恢复") + "（PlayingID）：截获 " +
                                    std::to_string(playing_ids_.size()) + "，执行 " + std::to_string(acted));
            total += acted;
        }
    }

    // 第二层（兜底）：组件音量归零/恢复——盖住没截获到的声音
    std::vector<UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), components);
    probe_wwise_components(components);
    const double volume = pause ? 0.0 : 1.0;
    int acted = 0;
    for (UObject* component : components) {
        if (!is_ok(component)) continue;
        if (invoke_set_number(component, "SetOutputBusVolume", volume)) ++acted;
    }
    log(LogLevel::Info, std::string(pause ? "Wwise 组件静音（音量兜底）" : "Wwise 组件恢复（音量兜底）") +
                            "：组件 " + std::to_string(components.size()) + "，执行 " + std::to_string(acted));
    total += acted;
    return total;
}

// 钩住 PBZSoundFXSubsystem 的 Post 系列函数：Wwise 每次播放都会从这里返回 PlayingID，
// 记下来冻结时才能真暂停。游戏加载后尽早挂（mod_app 的引擎 tick 里每 2 秒重试）。
bool Ue4ssPort::install_voice_hooks() {
    if (voice_hooks_installed_) return true;
    UObject* soundfx = nullptr;
    {
        std::vector<UObject*> subsystems;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZSoundFXSubsystem"), subsystems);
        for (UObject* candidate : subsystems) {
            if (is_ok(candidate)) {
                soundfx = candidate;
                break;
            }
        }
    }
    if (soundfx == nullptr) return false;

    int hooked = 0;
    for (const char* name : {"PBZPostEventOnAkComponent", "PBZPostEventOnAkComponent_SoftRef",
                             "PBZPostEventOnAkComponent_Animation", "PBZPostEventOnAkComponent_Animation_SoftRef",
                             "PBZPostEventOnActor", "PBZPostEventOnActor_SoftRef", "PBZPostEventGlobal",
                             "PBZPostEventGlobal_SoftRef", "PBZPostEventAtLocation", "PBZPostEventAtLocation_SoftRef",
                             "PBZPostAssociatedEvent"}) {
        UFunction* function = find_function(soundfx, name);
        if (function == nullptr) continue;
        const auto ids = RC::Unreal::UObjectGlobals::RegisterHook(
            function,
            nullptr,
            [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
                auto* self = static_cast<Ue4ssPort*>(custom);
                if (self != nullptr) self->on_voice_post(context.RESULT_DECL);
            },
            this);
        if (ids.first >= 0 || ids.second >= 0) ++hooked;
    }

    // 集成层入口兜底：绕过游戏子系统、直接走 Wwise BP 的声音也截获
    // （UAkGameplayStatics 的全局/按位置 post、UAkComponent 的成员 post——类级钩子对所有实例生效）
    UObject* ak_statics = find_object("/Script/AkAudio.Default__AkGameplayStatics");
    if (is_ok(ak_statics)) {
        for (const char* name : {"PostEvent", "PostEventAtLocation", "PostAndWaitForEndOfEvent"}) {
            UFunction* function = find_function(ak_statics, name);
            if (function == nullptr) continue;
            const auto ids = RC::Unreal::UObjectGlobals::RegisterHook(
                function,
                nullptr,
                [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
                    auto* self = static_cast<Ue4ssPort*>(custom);
                    if (self != nullptr) self->on_voice_post(context.RESULT_DECL);
                },
                this);
            if (ids.first >= 0 || ids.second >= 0) ++hooked;
        }
    }
    {
        std::vector<UObject*> ak_components;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("AkComponent"), ak_components);
        for (UObject* component : ak_components) {
            if (!is_ok(component)) continue;
            bool component_hooked = false;
            for (const char* name : {"PostAkEvent", "PostAssociatedAkEvent"}) {
                UFunction* function = find_function(component, name);
                if (function == nullptr) continue;
                const auto ids = RC::Unreal::UObjectGlobals::RegisterHook(
                    function,
                    nullptr,
                    [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
                        auto* self = static_cast<Ue4ssPort*>(custom);
                        if (self != nullptr) self->on_voice_post(context.RESULT_DECL);
                    },
                    this);
                if (ids.first >= 0 || ids.second >= 0) {
                    ++hooked;
                    component_hooked = true;
                }
            }
            if (component_hooked) break;  // 类级函数挂一次即覆盖所有实例
        }
    }

    if (hooked == 0) return false;
    voice_hooks_installed_ = true;
    log(LogLevel::Info, "对话语音钩子已挂：" + std::to_string(hooked) + " 个 Post 函数（PBZSoundFXSubsystem），冻结将走真暂停");
    // 顺手把暂停/恢复接口的真实参数签名打出来：调用被拦时不用再猜
    for (const char* name : {"PBZPausePlayingID", "PBZResumePlayingID"}) {
        if (UFunction* fn = find_function(soundfx, name)) {
            log(LogLevel::Info, std::string(name) + " 参数签名=" + describe_params(fn));
        }
    }
    return true;
}

void Ue4ssPort::on_voice_post(void* result_decl) {
    if (result_decl == nullptr) return;
    const int32_t id = *static_cast<const int32_t*>(result_decl);
    // Wwise PlayingID 是从 1 递增的正数；过界值说明该函数返回的不是 PlayingID（如 bool），丢弃
    if (id <= 0 || id > 10000000) return;
    if (!playing_ids_seen_.insert(id).second) return;
    playing_ids_.push_back(id);
    if (playing_ids_.size() > 1024) {
        // 只留最近 512 个：已结束的播放 ID 会被 Wwise 回收复用，截获列表要保持新鲜；
        // 上限给足是让长命的环境/音乐循环也在名单里（发得早就被记下了）
        const size_t keep = 512;
        playing_ids_.erase(playing_ids_.begin(), playing_ids_.end() - static_cast<long>(keep));
        playing_ids_seen_.clear();
        for (int32_t id_kept : playing_ids_) playing_ids_seen_.insert(id_kept);
    }
}

void Ue4ssPort::probe_wwise_components(const std::vector<UObject*>& components) {
    if (wwise_probe_done_) return;
    wwise_probe_done_ = true;
    if (components.empty()) {
        log(LogLevel::Info, "Wwise 组件诊断：当前场景没有 AkComponent 实例（仅探测一次）");
        return;
    }
    std::string available;
    for (const char* candidate : {"Stop", "AkStopAll", "AkStop", "StopAll", "Pause", "AkPause"}) {
        bool exists = false;
        for (UObject* component : components) {
            if (is_ok(component) && find_function(component, candidate) != nullptr) {
                exists = true;
                break;
            }
        }
        if (exists) available += available.empty() ? candidate : std::string(",") + candidate;
    }
    log(LogLevel::Info, "Wwise 组件诊断：AkComponent=" + std::to_string(components.size()) +
                            "，可用函数=" + (available.empty() ? "无" : available) + "（仅探测一次）");
    dump_audio_objects_once();
}

// 一次性音频对象普查：把场景里所有疑似音频/语音/对话相关的类找出来。
// 背景：AkComponent 上的 Stop 只能拦“挂在组件上”的声音；对话经常是 Wwise 直接
// post 在游戏对象上（没有组件），那部分要靠游戏自己的管理器类去停——这里把
// 候选管理器和它们可用的控制函数列出来，给下一版做可恢复的音频暂停定目标。
void Ue4ssPort::dump_audio_objects_once() {
    if (audio_dump_done_) return;
    audio_dump_done_ = true;

    struct AudioClassInfo {
        int count{0};
        UObject* sample{nullptr};
    };
    std::map<std::string, AudioClassInfo> found;
    RC::Unreal::UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) -> RC::LoopAction {
        if (!RC::Unreal::UObjectGlobals::IsValidObjectForFindXOf(object)) return RC::LoopAction::Continue;
        UClass* cls = object->GetClassPrivate();
        if (cls == nullptr) return RC::LoopAction::Continue;
        const std::string class_name = narrow(cls->GetName());
        if (!is_audio_related_name(class_name)) return RC::LoopAction::Continue;
        AudioClassInfo& info = found[class_name];
        ++info.count;
        if (info.sample == nullptr) info.sample = object;
        return RC::LoopAction::Continue;
    });

    if (found.empty()) {
        log(LogLevel::Info, "音频对象普查：未发现 Ak/Audio/Sound/Voice/Dialog 相关类（仅一次）");
        return;
    }
    for (const auto& entry : found) {
        log(LogLevel::Info, "音频对象普查：" + entry.first + " x" + std::to_string(entry.second.count));
    }
    // 全局静态库（UAkGameplayStatics）：Wwise 的 BP 入口都在这，直接列全量函数
    if (UObject* ak_statics = find_object("/Script/AkAudio.Default__AkGameplayStatics")) {
        if (is_ok(ak_statics)) {
            log(LogLevel::Info, "AkGameplayStatics 函数：" + list_all_functions(ak_statics, 80));
        }
    }

    // 优先看“能管全局”的类：子系统 > 函数库 > 任务/对话图/导演 > 管理器/系统 > 组件形态
    auto priority_of = [](const std::string& lower) -> int {
        if (lower.find("subsystem") != std::string::npos) return 0;
        if (lower.find("statics") != std::string::npos || lower.find("library") != std::string::npos) return 1;
        if (lower.find("task") != std::string::npos || lower.find("graph") != std::string::npos ||
            lower.find("director") != std::string::npos) {
            return 2;
        }
        if (lower.find("manager") != std::string::npos || lower.find("controller") != std::string::npos ||
            lower.find("system") != std::string::npos) {
            return 3;
        }
        if (lower.find("component") != std::string::npos) return 4;
        return 9;
    };
    std::vector<std::pair<int, const std::map<std::string, AudioClassInfo>::value_type*>> ranked;
    for (const auto& entry : found) {
        ranked.emplace_back(priority_of(lower_ascii(entry.first)), &entry);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    int dumped_classes = 0;
    for (const auto& item : ranked) {
        if (dumped_classes >= 12) break;
        const AudioClassInfo& info = item.second->second;
        if (info.sample == nullptr || !is_ok(info.sample)) continue;
        if (item.first >= 9) continue;  // 形态不像“能管东西”的类（事件资产等）不转储
        ++dumped_classes;
        // 子系统/静态库用全量函数表（它们最可能藏着“按 ID 暂停/枚举播放”这类接口），
        // 其余（任务/对话图/组件）仍按关键词筛
        const bool full_list = item.first <= 1;
        const std::string functions = full_list ? list_all_functions(info.sample, 120)
                                                : list_audio_functions(info.sample);
        log(LogLevel::Info, "音频类函数：" + item.second->first + " → " + functions);
    }
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
