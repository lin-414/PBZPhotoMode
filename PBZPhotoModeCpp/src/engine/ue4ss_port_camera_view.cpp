// =============================================================================
//  Ue4ssPort 成员函数实现：相机与 ViewTarget / 输入接管
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

namespace pbzpm {
using namespace detail;

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
    FProperty* result = params.prop({"ReturnValue"});
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

    // 记录进入前的 ViewTarget：过场动画/剧情演出期间相机管理器指向的是过场相机，
    // 退出时必须还原到它而不是玩家 Pawn，否则过场会被强行切回玩家视角、语音也被打断。
    if (UObject* manager = camera_manager(); is_ok(manager)) {
        UObject* current = nullptr;
        if (read_struct_object(find_property(manager, "ViewTarget"), manager, "Target", current) && is_ok(current) &&
            narrow(current->GetFullName()).find("Default__") == std::string::npos && current != camera_actor_) {
            original_view_target_ = current;
            original_view_target_saved_ = true;
            log(LogLevel::Info, std::string("进入前 ViewTarget 已记录：") + narrow(current->GetFullName()));
        }
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
    // SpawnActor 已按 pose 放置；记录期望位姿，供 attach_view 的位姿回读比对（M-1）
    last_applied_pose_ = pose;
    last_applied_pose_known_ = true;
    view_verify_fail_streak_ = 0;
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
    const std::string hook_target = std::string("PlayerController.") + narrow(function->GetName());
    const bool hooked = register_tracked_script_hook(
        hooks_, hook_target.c_str(), function, nullptr,
        [](RC::Unreal::UnrealScriptFunctionCallableContext& context, void* custom) {
            auto* self = static_cast<Ue4ssPort*>(custom);
            if (self != nullptr) self->on_axis_event(context);
        },
        this);
    if (!hooked) {
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
    // 每帧调用：函数查找走缓存（UFunction 挂在 PC 类上，进程内稳定）。
    // 缓存与 refresh_function_caches() 共用 mutex_，避免运行中重探与每帧读取竞争。
    UFunction* function = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mouse_delta_fn_missing_) return false;
        if (mouse_delta_fn_ == nullptr) {
            mouse_delta_fn_ = find_function(pc, "GetInputMouseDelta");
            if (mouse_delta_fn_ == nullptr) {
                mouse_delta_fn_missing_ = true;
                return false;
            }
        }
        function = mouse_delta_fn_;
    }

    ParamBlock params(function);
    if (!params.valid()) return false;
    pc->ProcessEvent(function, params.data());

    double out_x = 0.0;
    double out_y = 0.0;
    if (!read_number(params.prop({"DeltaX", "X"}), params.data(), out_x)) return false;
    if (!read_number(params.prop({"DeltaY", "Y"}), params.data(), out_y)) return false;
    dx = out_x;
    dy = out_y;
    return true;
}

// SetViewTargetWithBlend 的调用走缓存：reassert_view_target 每帧一次，
// find_function 每次都要走一遍函数链，缓存掉这步反射查找
bool Ue4ssPort::set_view_target_cached(UObject* pc, UObject* target) {
    if (!is_ok(pc)) return false;
    UFunction* function = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (set_view_target_fn_missing_) return false;
        if (set_view_target_fn_ == nullptr) {
            set_view_target_fn_ = find_function(pc, "SetViewTargetWithBlend");
            if (set_view_target_fn_ == nullptr) {
                set_view_target_fn_missing_ = true;
                return false;
            }
        }
        function = set_view_target_fn_;
    }
    ParamBlock params(function);
    if (!params.valid()) return false;
    if (!params.set_object("NewViewTarget", target) || !params.set_number("BlendTime", 0.0)) return false;
    pc->ProcessEvent(function, params.data());
    return true;
}

// M-1：回读 ViewTarget.Target。优先 PlayerController.ViewTarget，读不出来/指向仍不是
// 摄影相机时再看相机管理器（本作 PC 侧经常不可读）。把所有可读来源都扫一遍再判定，
// 避免“PC 侧读到一个空/旧 Target、管理器其实已经切好”被误判成接管失败。
bool Ue4ssPort::read_view_target_target(UObject*& observed, bool& readable) const {
    observed = nullptr;
    readable = false;
    auto consider = [&](bool ok, UObject* value) -> bool {
        if (!ok) return false;
        readable = true;
        if (observed == nullptr && value != nullptr) observed = value;
        return value != nullptr && value == camera_actor_;
    };

    if (UObject* pc = controller(); is_ok(pc)) {
        FProperty* view_target = find_property(pc, "ViewTarget");
        UObject* pc_target = nullptr;
        if (consider(read_struct_object_raw(view_target, pc, "Target", pc_target), pc_target)) return true;
        UObject* pc_actor = nullptr;
        if (consider(read_struct_object_raw(view_target, pc, "Actor", pc_actor), pc_actor)) return true;
    }
    if (UObject* manager = camera_manager(); is_ok(manager)) {
        UObject* manager_target = nullptr;
        if (consider(read_struct_object_raw(find_property(manager, "ViewTarget"), manager, "Target",
                                           manager_target),
                     manager_target)) {
            return true;
        }
    }
    return false;
}

// 相机 Actor 当前位姿是否仍接近本 Mod 最后写入的位姿。读不到就返回 true（不误判失效）。
bool Ue4ssPort::camera_pose_matches_last_apply() const {
    if (!last_applied_pose_known_ || !is_ok(camera_actor_)) return true;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
    if (!read_return_vector(camera_actor_, "K2_GetActorLocation", "X", "Y", "Z", x, y, z)) return true;
    if (!read_return_vector(camera_actor_, "K2_GetActorRotation", "Pitch", "Yaw", "Roll", pitch, yaw, roll)) {
        return true;
    }
    CameraPose actual{};
    actual.loc = Vec3{x, y, z};
    actual.rot = Rot{pitch, yaw, roll};
    return camera_pose_within_tolerance(last_applied_pose_, actual, kViewPoseLocationTolerance,
                                        kViewPoseRotationToleranceDeg);
}

bool Ue4ssPort::attach_view() {
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return false;

    const bool called = set_view_target_cached(pc, camera_actor_);
    if (called) view_attached_by_port_ = true;  // 已发出接管请求；真正是否生效由回读决定

    UObject* observed = nullptr;
    bool readable = false;
    const bool is_camera = read_view_target_target(observed, readable);
    const ViewTargetState state = classify_view_target(readable, is_camera);
    const bool pose_ok = camera_pose_matches_last_apply();
    if (called && state == ViewTargetState::Match && pose_ok) {
        view_verify_fail_streak_ = 0;
        view_unconfirmed_logged_ = false;
        if (!view_verified_) {
            view_verified_ = true;
            log(LogLevel::Info, "相机接管确认：ViewTarget 已是摄影相机");
        }
        return true;
    }
    view_verified_ = false;

    // 调用本身失败：这是真正的接管失败，按连续失败计数（core 侧到阈值后退出）。
    if (!called) {
        ++view_verify_fail_streak_;
        log(view_verify_fail_streak_ >= kViewVerificationFailLimit ? LogLevel::Error : LogLevel::Warn,
            "相机接管校验第 " + std::to_string(view_verify_fail_streak_) +
                " 次失败：SetViewTargetWithBlend 调用失败");
        return false;
    }

    // M-1：ProcessEvent 成功不等于本帧就能回读成功。真机实测同帧 ViewTarget.Target 是 null，
    // 要等相机管理器本帧 UpdateViewTarget 后才有值——这属于“回读尚未生效”，不是接管失败。
    // 只有读到明确的其他 Actor（视角被抢）或摄影相机位姿被搬走才算确凿失效；
    // 其余情况只告警一次、继续每秒重试，绝不阻断 enter()。
    const bool observed_other = is_ok(observed) && observed != camera_actor_;
    const bool conclusive = view_loss_is_conclusive(readable, observed_other, !pose_ok);
    if (!conclusive) {
        if (!view_unconfirmed_logged_) {
            view_unconfirmed_logged_ = true;
            log(LogLevel::Warn, "ViewTarget 回读暂未确认（同帧未生效或属性不可读），继续每秒重试接管");
        }
        return true;
    }

    ++view_verify_fail_streak_;
    if (view_verify_fail_streak_ == 1 || view_verify_fail_streak_ >= kViewVerificationFailLimit) {
        const std::string detail =
            observed_other
                ? std::string("ViewTarget 当前目标=") + narrow(observed->GetFullName()) + "，不是摄影相机"
                : "摄影相机位姿已被改写（偏离最后写入的位置/朝向）";
        log(view_verify_fail_streak_ >= kViewVerificationFailLimit ? LogLevel::Error : LogLevel::Warn,
            "相机接管校验第 " + std::to_string(view_verify_fail_streak_) + " 次失败：" + detail);
    }
    return true;
}

bool Ue4ssPort::view_confirmation_failed() const {
    return view_verify_fail_streak_ >= kViewVerificationFailLimit;
}

void Ue4ssPort::reassert_view_target() {
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return;
    set_view_target_cached(pc, camera_actor_);
}

bool Ue4ssPort::restore_view() {
    // 先同步世界身份：换代时 invalidate 会清掉旧相机/旧 ViewTarget 缓存，
    // 之后再 is_ok 校验，避免把视角还原到已销毁的旧对象（H-3）。
    (void)world();
    UObject* pc = controller();
    view_attached_by_port_ = false;  // 无论成败，本 port 都不再持有视角
    view_verify_fail_streak_ = 0;
    view_verified_ = false;
    if (!is_ok(pc)) return false;
    // 优先还原到进入前的 ViewTarget（过场相机）：过场中退出摄影模式时直接回玩家 Pawn
    // 会把演出镜头切走、正在播的语音/字幕状态也被打断。只有记录不可用时才回退 Pawn。
    if (original_view_target_saved_ && is_ok(original_view_target_) && original_view_target_ != camera_actor_) {
        const bool restored = set_view_target_cached(pc, original_view_target_);
        if (restored) {
            log(LogLevel::Info, std::string("已还原进入前 ViewTarget：") + narrow(original_view_target_->GetFullName()));
        } else {
            log(LogLevel::Warn, "还原进入前 ViewTarget 失败，回退玩家 Pawn");
            return set_view_target_cached(pc, player_pawn());
        }
        return restored;
    }
    return set_view_target_cached(pc, player_pawn());
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
        // 位姿没完整写进去：不再把旧期望值当作“当前应处位置”，避免把应用失败误报成视角被抢
        last_applied_pose_known_ = false;
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
        last_applied_pose_ = pose;
        last_applied_pose_known_ = true;
    }
    return moved && turned;
}

bool Ue4ssPort::apply_fov(double fov) {
    UObject* component = camera_component();
    if (!is_ok(component)) return false;
    return write_number(find_property(component, "FieldOfView"), component, fov);
}

bool Ue4ssPort::destroy_camera() {
    // 先同步世界身份：旧世界的相机已随关卡销毁，换代后缓存会被清空，不能再 K2_DestroyActor。
    (void)world();
    const bool had_view = is_ok(camera_actor_);
    if (had_view) {
        // 仅当视角仍由本 port 持有时才还原：leave() 已经 restore_view() 过，
        // 这里再还原一次会把刚还原好的过场相机/Spectator 又踢回玩家 Pawn。
        if (view_attached_by_port_) restore_view();
        if (auto* actor = static_cast<AActor*>(camera_actor_); actor != nullptr) {
            actor->K2_DestroyActor();
        }
    }
    camera_actor_ = nullptr;
    camera_component_cache_ = nullptr;
    controller_cache_ = nullptr;
    view_verified_ = false;
    view_verify_fail_streak_ = 0;
    last_applied_pose_known_ = false;
    original_view_target_ = nullptr;
    original_view_target_saved_ = false;
    view_attached_by_port_ = false;
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

// M-2：只在确实写成功时记录目标对象身份。name 用于恢复前二次确认（指针失效后再分配
// 到同地址时也不会误写）。
void Ue4ssPort::remember_blocked_object(BlockedObject& record, UObject* object, UObject* owner,
                                        std::uint8_t bits) {
    if (!is_ok(object) || bits == 0) return;
    if (record.written_bits == 0) {
        record.object = object;
        record.owner = owner != nullptr ? owner : object;
        record.name = narrow(object->GetFullName());
    }
    record.written_bits |= bits;
}

// 按位恢复；对象身份不一致时跳过并告警（不盲目写回新对象）。写失败如实返回 false，
// 保留位图等待 core 的重试机制再次调用。
bool Ue4ssPort::restore_blocked_object(BlockedObject& record, UObject* current_owner, const char* label) {
    if (record.written_bits == 0) return true;
    auto skip = [&](const char* why) {
        log(LogLevel::Warn, std::string("恢复输入屏蔽：") + label + " " + why +
                                "，跳过写回（该对象未被本 Mod 修改）");
        record = BlockedObject{};
        return true;
    };
    if (!is_ok(record.object)) return skip("对象已失效");
    if (!record.name.empty() && narrow(record.object->GetFullName()) != record.name) return skip("对象身份已变化");
    if (record.owner == record.object) {
        // PC/Pawn 自身记录：当前活动对象必须仍是进入时那一个
        if (!is_ok(current_owner) || current_owner != record.object) return skip("当前 PlayerController/Pawn 已更换");
    } else {
        // 组件记录：所属 PC/Pawn 必须仍是进入时那一个
        if (!is_ok(record.owner) || !is_ok(current_owner) || record.owner != current_owner) {
            return skip("所属 PlayerController/Pawn 已更换");
        }
    }

    bool ok = true;
    if ((record.written_bits & 0x1) != 0) {
        ok = write_bool(find_property(record.object, "bBlockInput"), record.object, false) && ok;
    }
    if ((record.written_bits & 0x4) != 0) {
        ok = invoke(record.object, "SetIgnoreLookInput", [&](ParamBlock& params) {
                 return params.set_bool("bNewLookInput", false) || params.set_bool("bIgnore", false) ||
                        params.set_bool("NewLookInput", false);
             }) &&
             ok;
    }
    if ((record.written_bits & 0x8) != 0) {
        UObject* pc = controller();
        ok = invoke(record.object, "EnableInput",
                    [&](ParamBlock& params) { return params.set_object("PlayerController", pc); }) &&
             ok;
    }
    if (!ok) {
        log(LogLevel::Warn, std::string("恢复输入屏蔽失败：") + label + " 仍有字段写回失败，保留状态等待重试");
        return false;
    }
    record = BlockedObject{};
    return true;
}

bool Ue4ssPort::set_game_input_blocked(bool blocked) {
    if (!blocked) {
        bool ok = true;
        const UObject* pc = controller();
        const UObject* pawn = player_pawn();
        // 注意：&& 右侧必须执行，不能短路（每个对象都要尝试恢复）
        if (!restore_blocked_object(input_block_pc_, const_cast<UObject*>(pc), "PlayerController")) ok = false;
        if (!restore_blocked_object(input_block_pawn_, const_cast<UObject*>(pawn), "Pawn")) ok = false;
        if (!restore_blocked_object(input_block_pc_component_, const_cast<UObject*>(pc),
                                    "PlayerController.InputComponent")) {
            ok = false;
        }
        if (!restore_blocked_object(input_block_pawn_component_, const_cast<UObject*>(pawn),
                                    "Pawn.InputComponent")) {
            ok = false;
        }
        return ok;
    }

    bool touched = false;

    // 1) 输入栈中断：PC / Pawn 上的 InputComponent 打 bBlockInput。
    //    动作类绑定（攻击、交互、技能）挂在 Pawn 的 InputComponent 上，
    //    SetIgnoreMoveInput 只管移动轴，挡不住它们。
    struct OwnerSlot {
        UObject* owner;
        BlockedObject* owner_record;
        BlockedObject* component_record;
        const char* label;
    };
    OwnerSlot slots[2] = {{controller(), &input_block_pc_, &input_block_pc_component_, "PlayerController"},
                          {player_pawn(), &input_block_pawn_, &input_block_pawn_component_, "Pawn"}};
    for (OwnerSlot& slot : slots) {
        UObject* owner = slot.owner;
        if (!is_ok(owner)) continue;
        if (write_bool(find_property(owner, "bBlockInput"), owner, true)) {
            touched = true;
            remember_blocked_object(*slot.owner_record, owner, owner, 0x1);
        } else {
            log(LogLevel::Warn, std::string("屏蔽输入：") + slot.label + " 的 bBlockInput 写入失败");
        }

        UObject* component = nullptr;
        if (read_object(find_property(owner, "InputComponent"), owner, component) && is_ok(component)) {
            if (write_bool(find_property(component, "bBlockInput"), component, true)) {
                touched = true;
                remember_blocked_object(*slot.component_record, component, owner, 0x1);
            } else {
                log(LogLevel::Warn, std::string("屏蔽输入：") + slot.label + " 的 InputComponent 写入失败");
            }
        }
    }

    // 2) 视角轴一起关掉：否则游戏仍会消耗鼠标位移、把玩家的控制旋转算进去
    UObject* pc = controller();
    if (is_ok(pc)) {
        if (invoke(pc, "SetIgnoreLookInput", [&](ParamBlock& params) {
                return params.set_bool("bNewLookInput", true) || params.set_bool("bIgnore", true) ||
                       params.set_bool("NewLookInput", true);
            })) {
            touched = true;
            remember_blocked_object(input_block_pc_, pc, pc, 0x4);
        }

        // 3) 最后一道闸：把 PC 与 Pawn 的 InputComponent 直接停用。
        //    bBlockInput 只管“输入栈是否继续往下走”，蓝图里某些动作可能绕过它；
        //    DisableInput 是官方语义上的“这个 Actor 不再接收输入”。
        if (invoke(pc, "DisableInput", [&](ParamBlock& params) { return params.set_object("PlayerController", pc); })) {
            touched = true;
            remember_blocked_object(input_block_pc_, pc, pc, 0x8);
        }
        UObject* pawn = player_pawn();
        if (is_ok(pawn) &&
            invoke(pawn, "DisableInput", [&](ParamBlock& params) { return params.set_object("PlayerController", pc); })) {
            touched = true;
            remember_blocked_object(input_block_pawn_, pawn, pawn, 0x8);
        }
    }
    return touched;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
