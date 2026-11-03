// =============================================================================
//  UE4SS 反射层内部共享 helper
//
//  只被单个子系统使用的 helper 留在各自的 ue4ss_port_*.cpp；凡是跨文件使用的
//  反射/参数块/设置读写工具都集中在这里（namespace pbzpm::detail，inline 定义，
//  保证全工程只有一份实体，避免同名的 static 副本行为分叉）。
//  仅在 PBZPM_WITH_UE4SS 下参与编译。
// =============================================================================
#pragma once

#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port.h"

#include "core/version.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
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
namespace detail {

using namespace RC::Unreal;

// M-1：attach_view 的回读确认（ViewTarget 与相机位姿）连续失败几次才判定接管彻底失效。
// core 每秒重试一次，3 次≈3 秒，足够过滤切图/过场切换的瞬时抖动。
constexpr int kViewVerificationFailLimit = 3;
// 相机位姿比对容差：位置 10 单位、角度 2°。游戏侧正常不会主动搬动摄影相机，
// 超过这个量级说明视角已被别的系统改写。
constexpr double kViewPoseLocationTolerance = 10.0;
constexpr double kViewPoseRotationToleranceDeg = 2.0;

// ------------------------------------------------------------------ 基础工具

inline auto widen(const std::string& text) -> RC::StringType {
    if (text.empty()) return {};
    const int needed =
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};
    RC::StringType out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

inline auto narrow(const RC::StringType& text) -> std::string {
    if (text.empty()) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                             nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), 0);
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), needed, nullptr,
                          nullptr);
    return out;
}

inline auto emit(pbzpm::LogLevel level, const RC::StringType& text) -> void {
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

// 指针可信度：防止已释放对象（内存被覆写为 0xFF）被当成有效 UObject 继续解引用。
// 1.0.69 实机崩溃（crash_2026_11_02_15_36_45）：controller() 缓存的 PlayerController 在换图后
// 已释放，ClassPrivate 读出 0xFFFFFFFFFFFFFFFF；is_ok 只判非空 → find_property 的 TFieldIterator
// 直接读地址 -1 崩溃。这里三管齐下：指针地址范围 + 对象数组反查 + 类指针范围。
inline auto is_plausible_pointer(const void* pointer) -> bool {
    const auto value = reinterpret_cast<uintptr_t>(pointer);
    return value >= 0x10000 && value < 0x0000800000000000ULL && (value & 0x7) == 0;
}

// 对象已释放且内存不可读时，GetClassPrivate 本身也可能触发访问违例，用 SEH 隔离。
inline auto safe_class_of(const UObject* object) -> const UClass* {
    if (!is_plausible_pointer(object)) return nullptr;
    __try {
        return object->GetClassPrivate();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

inline auto is_ok(const UObject* object) -> bool {
    if (!is_plausible_pointer(object)) return false;
    const UClass* object_class = safe_class_of(object);
    if (!is_plausible_pointer(object_class)) return false;
    // 对象数组反查：GC 释放/复用槽位后的旧指针在这里暴露（GetObjectItem 会按 InternalIndex 查表）
    __try {
        const FUObjectItem* item = const_cast<UObject*>(object)->GetObjectItem();
        if (item == nullptr || item->GetUObject() != object || !item->IsValid(true)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// 对象已释放时 GetWorld 也可能读非法地址（H-3），与 safe_class_of 同风格隔离。
inline auto safe_get_world(UObject* object) -> UObject* {
    if (!is_plausible_pointer(object)) return nullptr;
    __try {
        return object->GetWorld();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ------------------------------------------------------------------ 反射小工具

// 在 UStruct/UClass/UFunction 的字段链里按名字找属性。
// TFieldIterator 会沿字段链读属性数组/偏移；owner 已被 GC 时直接解引用会崩
//（1.0.69 crash 的同类路径），所以遍历本身包进 SEH（H-3）。
// SEH 只包遍历 helper：本函数内不能出现带析构的局部对象（/EHsc 下 __try 报 C2712）。
inline auto find_property_iterate(const UStruct* owner, const RC::StringType& name) -> FProperty* {
    for (TFieldIterator<FProperty> it(const_cast<UStruct*>(owner)); it; ++it) {
        FProperty* candidate = *it;
        if (candidate != nullptr && candidate->GetName() == name) return candidate;
    }
    return nullptr;
}

inline auto find_property(const UStruct* owner, const RC::StringType& name) -> FProperty* {
    if (!is_plausible_pointer(owner)) return nullptr;
    __try {
        return find_property_iterate(owner, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

inline auto find_property(UObject* owner, const char* name) -> FProperty* {
    if (!is_plausible_pointer(owner)) return nullptr;
    const UClass* owner_class = safe_class_of(owner);
    if (!is_plausible_pointer(owner_class)) return nullptr;
    return find_property(owner_class, widen(name));
}

inline auto find_function(UObject* owner, const char* name) -> UFunction* {
    if (!is_ok(owner)) return nullptr;
    return owner->GetFunctionByNameInChain(widen(name).c_str());
}

inline auto value_ptr(FProperty* property, void* container) -> void* {
    if (property == nullptr || container == nullptr) return nullptr;
    return property->ContainerPtrToValuePtr<void>(container);
}

// 数值型：float / double / byte / int / bool 全自适应
inline auto write_number(FProperty* property, void* container, double value) -> bool {
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

inline auto read_number(FProperty* property, void* container, double& out) -> bool {
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

inline auto write_object(FProperty* property, void* container, UObject* value) -> bool {
    if (property == nullptr || !property->IsA<FObjectProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    *static_cast<UObject**>(address) = value;
    return true;
}

inline auto read_object(FProperty* property, void* container, UObject*& out) -> bool {
    if (property == nullptr || !property->IsA<FObjectProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    out = *static_cast<UObject**>(address);
    return out != nullptr;
}

inline auto write_bool(FProperty* property, void* container, bool value) -> bool {
    if (property == nullptr) return false;
    if (property->IsA<FBoolProperty>()) {
        static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(container, value);
        return true;
    }
    return write_number(property, container, value ? 1.0 : 0.0);
}

inline auto write_text(FProperty* property, void* container, const std::string& text) -> bool {
    if (property == nullptr || !property->IsA<FStrProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    const RC::StringType wide = widen(text);
    *static_cast<FString*>(address) = FString(wide.c_str());
    return true;
}

inline auto write_fname(FProperty* property, void* container, const char* text) -> bool {
    if (property == nullptr || !property->IsA<FNameProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    const RC::StringType wide = widen(text);
    *static_cast<FName*>(address) = FName(wide.c_str(), FNAME_Add);
    return true;
}

// 结构体成员（一层）：FVector 的 X/Y/Z、FRotator 的 Pitch/Yaw/Roll、FLinearColor 的 R/G/B/A
inline auto struct_member_base(FProperty* struct_property, void* container) -> void* {
    if (struct_property == nullptr || !struct_property->IsA<FStructProperty>()) return nullptr;
    return value_ptr(struct_property, container);
}

inline auto struct_member(FProperty* struct_property, const char* member) -> FProperty* {
    if (struct_property == nullptr || !struct_property->IsA<FStructProperty>()) return nullptr;
    auto* as_struct = static_cast<FStructProperty*>(struct_property);
    UScriptStruct* script_struct = as_struct->GetStruct();
    return find_property(static_cast<UStruct*>(script_struct), widen(member));
}

inline auto write_struct_number(FProperty* struct_property, void* container, const char* member, double value)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return write_number(struct_member(struct_property, member), base, value);
}

inline auto read_struct_number(FProperty* struct_property, void* container, const char* member, double& out)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_number(struct_member(struct_property, member), base, out);
}

inline auto read_name(FProperty* property, void* container, std::string& out) -> bool {
    if (property == nullptr || !property->IsA<FNameProperty>()) return false;
    void* address = value_ptr(property, container);
    if (address == nullptr) return false;
    out = narrow(static_cast<FName*>(address)->ToString());
    return true;
}

inline auto read_struct_name(FProperty* struct_property, void* container, const char* member, std::string& out)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_name(struct_member(struct_property, member), base, out);
}

inline auto read_struct_object(FProperty* struct_property, void* container, const char* member, UObject*& out) -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    return read_object(struct_member(struct_property, member), base, out);
}

// 可读性判定版本：即使成员是 null 也算“读成功”，用于区分“ViewTarget 指向空”与
// “整条反射链路读不出来”。M-1 的接管确认不能把后者当成成功。
inline auto read_struct_object_raw(FProperty* struct_property, void* container, const char* member, UObject*& out)
    -> bool {
    void* base = struct_member_base(struct_property, container);
    if (base == nullptr) return false;
    FProperty* member_property = struct_member(struct_property, member);
    if (member_property == nullptr || !member_property->IsA<FObjectProperty>()) return false;
    void* address = value_ptr(member_property, base);
    if (address == nullptr) return false;
    out = *static_cast<UObject**>(address);
    return true;
}

// ------------------------------------------------------------------ 参数块

// M-6：参数块告警不经过 Ue4ssPort::log（ParamBlock 是自由函数），用同一阈值做一次过滤，
// 避免用户设 log_level=error 时仍被 M-7 的一次性告警打扰。
inline std::atomic<int> g_param_warn_threshold{static_cast<int>(LogLevel::Info)};

// M-7：参数名未命中时不再按声明位置退化（相邻参数类型常常相同、语义却不同）。
// 同一组候选名只告警一次，避免每秒调用把日志刷爆。
inline void warn_param_name_miss(std::initializer_list<const char*> names) {
    if (!log_level_enabled(static_cast<LogLevel>(g_param_warn_threshold.load()), LogLevel::Warn)) return;
    static std::mutex warn_mutex;
    static std::set<std::string> warned_groups;
    std::string key;
    for (const char* name : names) {
        if (name == nullptr) continue;
        if (!key.empty()) key += "|";
        key += name;
    }
    {
        std::lock_guard<std::mutex> lock(warn_mutex);
        if (!warned_groups.insert(key).second) return;
    }
    std::string text = "UFUNCTION 参数名未命中（候选: ";
    text += key.empty() ? "<空>" : key;
    text += "），按 M-7 不再按声明位置退化；若功能异常请把日志反馈给作者\n";
    emit(LogLevel::Warn, widen(text));
}

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

    // 参数名在不同版本里可能被改（InString→Text 之类）。只在候选名里找；
    // 全部未命中就失败并告警一次——绝不按声明顺序退化到“类型相同但语义不同”的位置参数。
    auto prop(std::initializer_list<const char*> names) -> FProperty* {
        const int hit = resolve_param_name(names, [this](const char* name) {
            return property(name) != nullptr;
        });
        if (hit < 0) {
            warn_param_name_miss(names);
            return nullptr;
        }
        return property(names.begin()[hit]);
    }

    auto set_object(const char* name, UObject* value) -> bool {
        return write_object(property(name), data(), value);
    }
    auto set_number(const char* name, double value) -> bool {
        return write_number(property(name), data(), value);
    }
    // 属性名大小写敏感（FProperty::GetName() 保留原始大小写），
    // 引擎里 PrintString 的参数就叫小写 duration，这里允许候选名。
    auto set_number_any(std::initializer_list<const char*> names, double value) -> bool {
        return write_number(prop(names), data(), value);
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

// 无返回值 UFUNCTION 的封装：调用成功即返回 true；绝不读取/猜测 ReturnValue。
template <typename Fn>
inline auto invoke_void(UObject* object, const char* function_name, Fn&& fill) -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!fill(params)) return false;
    object->ProcessEvent(function, params.data());
    return true;
}

// 建参数块 + 执行（历史名称保留；有返回值的函数请用 invoke_bool）
template <typename Fn>
inline auto invoke(UObject* object, const char* function_name, Fn&& fill) -> bool {
    return invoke_void(object, function_name, std::forward<Fn>(fill));
}

// 调用返回 bool 的 UFUNCTION：ProcessEvent 成功不代表游戏逻辑接受，
// 例如 GameMode 的 bPauseable=false 时 PlayerController::SetPause 会返回 false。
// M-7：必须读到 ReturnValue 才算成功；读不到就不再默认 true（无返回值函数请走 invoke_void）。
template <typename Fn>
inline auto invoke_bool(UObject* object, const char* function_name, Fn&& fill, bool& result) -> bool {
    if (!is_ok(object)) return false;
    UFunction* function = find_function(object, function_name);
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!fill(params)) return false;
    object->ProcessEvent(function, params.data());
    double value = 0.0;
    if (!params.get_number("ReturnValue", value)) {
        // 签名不是 bool 返回（或反射读不到）：如实失败，让调用方走后备路径
        return false;
    }
    result = value != 0.0;
    return true;
}

// AActor::K2_SetActorRotation 是 UEPseudo 手写的包装，内部把 FRotator 的
// Pitch/Yaw/Roll 当成 float 写（UE5 里 FRotator 是 3 个 double，24 字节）。
// 结果只能写进 double 的低 4 字节，实际角度变成约 1e-315，等价于 0：
// 相机位置能动、朝向却永远 (0,0)。这里改成按反射属性写，自动识别 FDoubleProperty。
inline auto set_actor_rotation(UObject* actor, const FRotator& rotation, bool teleport) -> bool {
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

inline auto find_object(const char* path) -> UObject* {
    if (path == nullptr) return nullptr;
    return RC::Unreal::UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widen(path).c_str());
}

inline auto gameplay_statics() -> UObject* {
    return find_object("/Script/Engine.Default__GameplayStatics");
}

inline auto lower_ascii(std::string text) -> std::string {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// Wwise 集成类都以 Ak 开头；游戏自己的音频/语音/对话类从名字里认
inline auto is_audio_related_name(const std::string& name) -> bool {
    if (name.rfind("Ak", 0) == 0) return true;
    const std::string lower = lower_ascii(name);
    return lower.find("audio") != std::string::npos || lower.find("sound") != std::string::npos ||
           lower.find("voice") != std::string::npos || lower.find("dialog") != std::string::npos ||
           lower.find("wwise") != std::string::npos;
}

// 把一个类里可疑的音频控制函数列出来（Stop/Pause/Mute/Volume/Voice/Dialog…）
inline auto list_audio_functions(UObject* sample) -> std::string {
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
inline auto list_all_functions(UObject* sample, int cap) -> std::string {
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
inline auto invoke_set_number(UObject* target, const char* fn, double value) -> bool {
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
inline auto describe_params(UFunction* function) -> std::string {
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
inline auto invoke_pause_id(UObject* target, const char* fn, int32_t id, double fade, UObject* world) -> bool {
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

// 数值型属性判定（float/double/int32/uint32），语音位置接口签名校验用
inline auto is_numeric_property(FProperty* property) -> bool {
    return property != nullptr &&
           (property->IsA<FFloatProperty>() || property->IsA<FDoubleProperty>() ||
            property->IsA<FIntProperty>() || property->IsA<FUInt32Property>());
}

// 严格签名的「GetPlayingIDPositionMs」调用（1.0.73 只读诊断，不改变播放行为）：
// 要求唯一的 PlayingID 数值入参（参数名含 playingid）+ 可选 WorldContextObject 对象参数；
// 返回值可以是数值型 ReturnValue，或唯一一个数值型 out 参数。其余参数类型一律视为签名
// 不符，返回 false 且不调用——宁可没有诊断，也不往错误参数位写值/误调游戏函数。
inline auto read_playing_id_position_ms(UObject* target, int32_t id, UObject* world, double& out) -> bool {
    if (!is_ok(target)) return false;
    UFunction* function = find_function(target, "GetPlayingIDPositionMs");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    FProperty* ret = function->GetReturnProperty();
    const bool ret_numeric = is_numeric_property(ret);
    bool filled_id = false;
    FProperty* numeric_out = nullptr;
    for (TFieldIterator<FProperty> it(function); it; ++it) {
        FProperty* prop = *it;
        if (prop == nullptr || prop == ret) continue;
        const std::string name = lower_ascii(narrow(prop->GetName()));
        if (is_numeric_property(prop)) {
            if (name.find("playingid") != std::string::npos) {
                if (filled_id) return false;  // 多个 PlayingID 入参，签名不认识
                if (!write_number(prop, params.data(), static_cast<double>(id))) return false;
                filled_id = true;
            } else if (ret_numeric) {
                return false;  // 已有数值返回值还带别的数值参数，签名不认识
            } else {
                if (numeric_out != nullptr) return false;  // 多个数值 out，无法确定读哪个
                numeric_out = prop;
            }
        } else if (prop->IsA<FObjectProperty>() && name.find("worldcontext") != std::string::npos) {
            write_object(prop, params.data(), world);
        } else {
            return false;  // 其他类型参数（bool 入参、字符串、结构体…）一律不猜
        }
    }
    if (!filled_id) return false;
    FProperty* out_prop = ret_numeric ? ret : numeric_out;
    if (out_prop == nullptr) return false;
    target->ProcessEvent(function, params.data());
    return read_number(out_prop, params.data(), out);
}

// ------------------------------------------------------------------ 冻结期残余微动治理

// 带签名校验的单 bool 参数调用（StopAllCameraShakes(bool) / DisableModifier(bool)）：
// 唯一参数必须是 bool 型。参数名对不上时绝不照调——DisableModifier 写不进参数
// 就等于发 bDisable=0（启用），方向正好相反。
inline auto invoke_set_bool(UObject* target, const char* fn, bool value) -> bool {
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

// ------------------------------------------------------------------ 游戏设置开关（当前值 + Pending）读写

// PBZSettingsShared 上这类开关统一是「GetX()/SetX() 当前值 + GetPendingX()/SetPendingX()
// 待定值 + bX 属性」三件套（字幕 / 场景交互提示 / 锁定标记 / 怪物血条模式…）。
// 历史教训（1.0.30 实机）：只写属性或只写 setter 可能只改到副本，退出还原静默失败
// 会把玩家设置永久写坏。这里统一：getter 优先读、setter + pending + 属性三路写、
// 写完由调用方用 getter 回读校验并把结果写日志。
struct BoolSettingSpec {
    const char* label;
    const char* const* getters;          // 以 nullptr 结尾
    const char* const* setters;          // 以 nullptr 结尾
    const char* const* pending_setters;  // 以 nullptr 结尾
    const char* const* properties;       // 以 nullptr 结尾
};

inline auto fill_bool_setting_param(ParamBlock& params, bool value) -> bool {
    return params.set_bool("Value", value) || params.set_bool("bValue", value) || params.set_bool("bEnabled", value) ||
           params.set_bool("bInEnabled", value) || params.set_bool("bNewValue", value) ||
           params.set_bool("bNewEnabled", value) || params.set_bool("bVisible", value) ||
           params.set_bool("bNewVisible", value) || params.set_bool("bNewVisibility", value) ||
           params.set_bool("bInVisibility", value) || params.set_bool("InVisible", value) ||
           params.set_bool("bShow", value) || params.set_bool("bInValue", value) || params.set_bool("InValue", value);
}

inline auto read_bool_setting(UObject* owner, const BoolSettingSpec& spec, bool& out, std::string* via = nullptr) -> bool {
    if (!is_ok(owner)) return false;
    // 属性优先，getter 兜底（1.0.34 实机结论：设置类上的 bEnableSubtitles / bSceneInteractPromptVisible
    // 才是当前生效值；1.0.70 实机日志出现 GetSubtitlesEnabled()=关 但字幕仍在显示——
    // getter 可能返回未同步的旧副本，拿它当原值会在退出时把「本来开着」还原成关，永久消失）。
    for (const char* const* name = spec.properties; *name != nullptr; ++name) {
        double raw = 0.0;
        if (read_number(find_property(owner, *name), owner, raw)) {
            out = raw != 0.0;
            if (via != nullptr) *via = *name;
            return true;
        }
    }
    for (const char* const* name = spec.getters; *name != nullptr; ++name) {
        bool value = false;
        if (invoke_bool(owner, *name, [](ParamBlock&) { return true; }, value)) {
            out = value;
            if (via != nullptr) *via = std::string(*name) + "()";
            return true;
        }
    }
    return false;
}

inline auto write_bool_setting(UObject* owner, const BoolSettingSpec& spec, bool value) -> bool {
    if (!is_ok(owner)) return false;
    bool any = false;
    for (const char* const* name = spec.setters; *name != nullptr; ++name) {
        if (invoke(owner, *name, [&](ParamBlock& params) { return fill_bool_setting_param(params, value); })) any = true;
    }
    for (const char* const* name = spec.pending_setters; *name != nullptr; ++name) {
        if (invoke(owner, *name, [&](ParamBlock& params) { return fill_bool_setting_param(params, value); })) any = true;
    }
    for (const char* const* name = spec.properties; *name != nullptr; ++name) {
        if (write_bool(find_property(owner, *name), owner, value)) any = true;
    }
    return any;
}

inline const char* const kPromptSettingGetters[] = {"GetSceneInteractPromptVisible", nullptr};
inline const char* const kPromptSettingSetters[] = {"SetSceneInteractPromptVisible", nullptr};
inline const char* const kPromptSettingPending[] = {"SetPendingSceneInteractPromptVisible", nullptr};
inline const char* const kPromptSettingProps[] = {"bSceneInteractPromptVisible", nullptr};
inline const BoolSettingSpec kPromptSettingSpec{"场景交互提示开关", kPromptSettingGetters, kPromptSettingSetters,
                                                kPromptSettingPending, kPromptSettingProps};

inline const char* const kSubtitleSettingGetters[] = {"GetSubtitlesEnabled", "AreSubtitlesEnabled", nullptr};
inline const char* const kSubtitleSettingSetters[] = {"SetSubtitlesEnabled", nullptr};
inline const char* const kSubtitleSettingPending[] = {"SetPendingSubtitlesEnabled", nullptr};
inline const char* const kSubtitleSettingProps[] = {"bEnableSubtitles", "bSubtitlesEnabled", nullptr};
inline const BoolSettingSpec kSubtitleSettingSpec{"字幕开关", kSubtitleSettingGetters, kSubtitleSettingSetters,
                                                  kSubtitleSettingPending, kSubtitleSettingProps};

inline auto kismet_material_library() -> UObject* {
    return find_object("/Script/Engine.Default__KismetMaterialLibrary");
}

// 数组属性判定：IsA<FArrayProperty> 走 UE4SS 的属性类注册表（FFieldClassRegistrar），
// 注册失败时 IsA 对该类型永远 false 且无任何报错——1.0.46 实机 ModifierList/CameraModifiers 与
// MPC 的 ScalarParameters/VectorParameters 全部因此被误判为“不是数组”。
// 字段类名读的是引擎数据（FFieldClassVariant::GetFName），作权威判定；
// 类名 "ArrayProperty" 即 FArrayProperty，此时 static_cast 是安全的。
inline auto as_array_property(FProperty* prop) -> FArrayProperty* {
    if (prop == nullptr) return nullptr;
    if (prop->IsA<FArrayProperty>()) return static_cast<FArrayProperty*>(prop);
    const std::string field_class = narrow(prop->GetClass().GetFName().ToString());
    if (field_class == "ArrayProperty") return static_cast<FArrayProperty*>(prop);
    return nullptr;
}

// APlayerCameraManager 的运行时修饰器列表在 UE5 叫 ModifierList；旧版/部分分支叫
// CameraModifiers。两个名字都试，避免再因为猜错属性名而跳过整个修饰器治理。
inline auto camera_modifiers_property(UObject* manager) -> FProperty* {
    if (!is_ok(manager)) return nullptr;
    for (const char* name : {"ModifierList", "CameraModifiers", "CameraModifierList"}) {
        FProperty* prop = find_property(manager, name);
        if (as_array_property(prop) != nullptr) return prop;
    }
    return nullptr;
}

// 读 UMaterialParameterCollection 的 ScalarParameters / VectorParameters 参数名
//（TArray<FCollectionScalarParameter/FCollectionVectorParameter>，元素结构体取 ParameterName）
inline auto mpc_read_param_names(UObject* collection, const char* array_name, std::vector<std::string>& out)
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

// ------------------------------------------------------------------ MPC 安全调用

// 本作部分 native UFUNCTION（例如 SetScalarParameterValue）在集合/参数失效时会直接在游戏代码里
// 读 0xFFFFFFFFFFFFFFFF，不拦就是整局崩（crash_2026_11_02_14_42_23 实测即此）。这里把 ProcessEvent
// 统一包进 SEH：捕获到访问违例就返回 false，由调用方隔离该参数。
// 注意：此函数内不能有带析构的 C++ 对象，否则 /EHsc 下 __try 会报 C2712。
inline auto seh_process_event(UObject* object, UFunction* function, void* params) -> bool {
    if (object == nullptr || function == nullptr || params == nullptr) return false;
    __try {
        object->ProcessEvent(function, params);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// 写回前复核集合类（含 BP 子类）；FindAllOf 按父类匹配，冻结期间对象也可能被卸载/替换，
// 快照里存的是裸指针，不能假定它现在还是 MaterialParameterCollection。
inline auto mpc_is_collection_class(UClass* cls) -> bool {
    if (cls == nullptr) return false;
    static const RC::Unreal::FName target = RC::Unreal::FName(widen("MaterialParameterCollection"));
    for (UStruct* node = cls; node != nullptr; node = node->GetSuperStruct()) {
        if (node->GetNamePrivate().Equals(target)) return true;
    }
    return false;
}

// Kismet 的 Set* 签名核对：ParameterName 必须是 FName，Value 必须是（标量=数值 / 向量=结构体）。
// 签名不符（换版本/换函数）就整条不写，避免把值写进错误参数位。
inline auto mpc_value_property(UFunction* function) -> FProperty* {
    if (function == nullptr) return nullptr;
    if (FProperty* by_name = find_property(function, widen("Value")); by_name != nullptr) return by_name;
    int index = 0;
    for (TFieldIterator<FProperty> it(function); it; ++it, ++index) {
        if (index == 3) return *it;
    }
    return nullptr;
}

inline auto mpc_set_signature_ok(UFunction* function, bool want_vector) -> bool {
    if (function == nullptr) return false;
    FProperty* name_prop = find_property(function, widen("ParameterName"));
    if (name_prop == nullptr || !name_prop->IsA<FNameProperty>()) return false;
    FProperty* value_prop = mpc_value_property(function);
    if (value_prop == nullptr) return false;
    if (want_vector) return value_prop->IsA<FStructProperty>();
    return value_prop->IsA<FFloatProperty>() || value_prop->IsA<FDoubleProperty>() ||
           value_prop->IsA<FIntProperty>() || value_prop->IsA<FByteProperty>() ||
           value_prop->IsA<FUInt32Property>();
}

// KismetMaterialLibrary 的 MPC 读写：走世界的 CollectionInstance，材质 uniforms 同步更新，
// 这是运行时改 MPC 的正路（直接改资产默认值不影响运行中的实例）。
inline auto mpc_get_scalar(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    double& out) -> bool {
    UFunction* function = find_function(statics, "GetScalarParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    if (!seh_process_event(statics, function, params.data())) return false;
    // 实机签名（1.0.48 诊断）：object,object,other:ParameterName,float:ReturnValue(ret)——
    // 该版本把唯一的出参直接作为返回值（属性名 ReturnValue），旧签名才是 bool + OutValue 出参
    return params.get_number("OutValue", out) || params.get_number("ReturnValue", out);
}

inline auto mpc_set_scalar(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    double value, bool& faulted) -> bool {
    faulted = false;
    UFunction* function = find_function(statics, "SetScalarParameterValue");
    if (function == nullptr) return false;
    if (!mpc_set_signature_ok(function, false)) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    if (!params.set_number("Value", value)) return false;
    if (!seh_process_event(statics, function, params.data())) {
        faulted = true;
        return false;
    }
    return true;
}

inline auto mpc_get_vector(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    float out[4]) -> bool {
    UFunction* function = find_function(statics, "GetVectorParameterValue");
    if (function == nullptr) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    if (!seh_process_event(statics, function, params.data())) return false;
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

inline auto mpc_set_vector(UObject* statics, UObject* world, UObject* collection, const std::string& name,
                    const float in[4], bool& faulted) -> bool {
    faulted = false;
    UFunction* function = find_function(statics, "SetVectorParameterValue");
    if (function == nullptr) return false;
    if (!mpc_set_signature_ok(function, true)) return false;
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("WorldContextObject", world) || !params.set_object("Collection", collection) ||
        !params.set_name("ParameterName", name.c_str())) {
        return false;
    }
    FProperty* value_prop = params.prop({"Value"});
    if (value_prop == nullptr) return false;
    static const char* kMembers[4] = {"R", "G", "B", "A"};
    for (int i = 0; i < 4; ++i) {
        if (!write_struct_number(value_prop, params.data(), kMembers[i], in[i])) return false;
    }
    if (!seh_process_event(statics, function, params.data())) {
        faulted = true;
        return false;
    }
    return true;
}

// 注册脚本钩子并立刻记账（H-5）。已挂上的 target 直接返回 true（热重载/重试幂等），
// 注册抛异常或两个 id 都无效时记 Failed；注销所需的 UFunction* 与 id 一并存进 registry。
inline auto register_tracked_script_hook(HookRegistry& registry, const char* target, UFunction* function,
                                 UnrealScriptFunctionCallable pre, UnrealScriptFunctionCallable post,
                                 void* custom) -> bool {
    if (target == nullptr || function == nullptr) return false;
    if (registry.installed(target)) return true;
    try {
        const auto ids = RC::Unreal::UObjectGlobals::RegisterHook(function, std::move(pre), std::move(post), custom);
        return registry.record_install(target, HookKind::Script, function, ids.first, ids.second);
    } catch (...) {
        // RegisterHook 在不支持的函数上会抛 runtime_error；这里只记账，日志由调用方决定
        registry.record_failure(target, HookKind::Script);
        return false;
    }
}

// ---- 以下 helper 被多个子系统共用，从原文件后段提到共享头 ----

// setter 参数名离线确认不了，按候选名试；全不中直接失败并告警一次（M-7，不按位置退化）
inline auto fill_bar_mode_param(ParamBlock& params, double value) -> bool {
    return params.set_number_any({"Mode", "InMode", "NewMode", "InMonsterHealthBarMode",
                                  "NewMonsterHealthBarMode", "Value"},
                                 value);
}

inline auto get_widget_visibility(UObject* widget, int& out) -> bool {
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
inline auto find_hud_layout_widgets(std::vector<UObject*>& out) -> void {
    RC::Unreal::UObjectGlobals::FindAllOf(STR("W_FrontEndHUDLayout_C"), out);
    if (out.empty()) {
        RC::Unreal::UObjectGlobals::FindAllOf(STR("PBZHUDLayout"), out);
    }
    if (out.empty()) {
        // 最后的 UMG 兜底：整个 CommonUI 根布局（包含所有 Layer 栈）。
        RC::Unreal::UObjectGlobals::FindAllOf(STR("WB_OverLayerOut_C"), out);
    }
}

}  // namespace detail
}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
