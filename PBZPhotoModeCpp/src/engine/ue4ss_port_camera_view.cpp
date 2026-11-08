// =============================================================================
//  Ue4ssPort 成员函数实现：相机与 ViewTarget / 输入接管
// =============================================================================
#ifdef PBZPM_WITH_UE4SS

#include "engine/ue4ss_port_internal.h"

#include <cstring>

#include "core/input_logic.h"
#include "core/tuning.h"

namespace pbzpm {
using namespace detail;

// ------------------------------------------------------------------ EnginePort

bool Ue4ssPort::world_valid() { return is_ok(world()); }

// APlayerCameraManager::CameraCache（FMinimalViewInfo）存着这一帧最终视角。
// 5.4 及以前 FOV 是直接成员，5.5 起挪进 Lens（FieldOfView）；两处都试。
static auto read_camera_cache_fov(UObject* manager, double& out) -> bool {
    if (!is_ok(manager)) return false;
    FProperty* cache = find_property(manager, "CameraCache");
    if (cache == nullptr || !cache->IsA<FStructProperty>()) return false;
    void* base = struct_member_base(cache, manager);
    // 5.5+ 相机 Lens 重构后引擎读 Lens.FieldOfView；旧 FOV 可能还在结构里但已不是渲染读点，
    // 所以先取 Lens，取不到再退回旧字段。
    FProperty* lens_fov = struct_member(struct_member(cache, "Lens"), "FieldOfView");
    if (base != nullptr && lens_fov != nullptr && read_number(lens_fov, struct_member_base(struct_member(cache, "Lens"), base), out) &&
        out > 1.0) {
        return true;
    }
    return read_struct_number(cache, manager, "FOV", out) && out > 1.0;
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

    probe_camera_backends();

    // 优先 CinematicCamera 插件的 CineCameraActor：有它才能做真实的焦距/Filmback 联动。
    // Shipping 未打进该插件时退回普通 CameraActor，并在报告里如实说明“长焦压缩只是 FOV 视觉裁剪”。
    UClass* camera_class =
        RC::Unreal::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                                             STR("/Script/CinematicCamera.CineCameraActor"));
    camera_is_cine_ = camera_class != nullptr;
    if (camera_class == nullptr) {
        camera_class =
            RC::Unreal::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.CameraActor"));
    }
    if (camera_class == nullptr) {
        log(LogLevel::Warn, "找不到 CameraActor 类，无法生成摄影相机");
        return false;
    }
    log(camera_is_cine_
            ? LogLevel::Info
            : LogLevel::Warn,
        camera_is_cine_ ? "相机后端：CineCameraActor 可用（FOV 会同步 CurrentFocalLength/Filmback）"
                        : "相机后端：包内没有 CineCameraActor（CinematicCamera 插件未打进 Shipping），"
                          "改用 CameraActor；长焦压缩只是 FOV 视野裁剪，不是真实焦距");

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
    // SpawnActor 已按 pose 放置，但真机实测 apply_pose() 之前首次 attach_view 读回的位姿
    // 与期望值对不上（引擎 spawn 后还会做一次组件变换整理），会被误报“位姿已被改写”。
    // 这里标记“位姿暂不可比”，等 apply_pose() 真正写入后再置为可比较。
    last_applied_pose_ = pose;
    last_applied_pose_known_ = false;
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
    // 刚 spawn / apply_pose 尚未写入时位姿不可比，不能把“偏离”当成确凿失效
    const bool pose_comparable = last_applied_pose_known_ && is_ok(camera_actor_);
    const bool conclusive = view_loss_is_conclusive(readable, observed_other, pose_comparable, !pose_ok);
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
    // EngineTickPost 在 RedrawViewports 之后：这里写 POV 赶不上当帧渲染，真正的写点在
    // write_takeover_pov_now()（GameViewportClient Tick 之后、RedrawViewports 之前）。
    // 这里只做两件事：1) 回读本帧渲染实际用的 POV，验证接管是否真的生效；
    // 2) 继续写一次，保证游戏下一帧 UpdateCamera 之前缓存里也是我们的值（多为兜底）。
    if (takeover_active_) {
        verify_takeover_pov();
        write_takeover_pov();
        return;
    }
    UObject* pc = controller();
    if (!is_ok(pc) || !is_ok(camera_actor_)) return;
    set_view_target_cached(pc, camera_actor_);
}

void Ue4ssPort::write_takeover_pov_now() {
    if (!takeover_active_) return;
    if (!write_takeover_pov()) return;
    if (!takeover_write_point_logged_) {
        takeover_write_point_logged_ = true;
        log(LogLevel::Info,
            "接管当前相机：POV 写点已切到 GameViewportClient Tick 之后（UpdateCamera 之后、"
            "RedrawViewports 之前），本帧渲染应使用本 Mod 的位姿");
    }
}

bool Ue4ssPort::restore_view() {
    // 接管模式从未切换 ViewTarget，没有任何东西可还原；由 leave_camera_takeover 负责停止覆盖。
    if (takeover_active_) return true;
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
    if (takeover_active_) {
        takeover_pose_ = pose;
        takeover_pose_known_ = true;
        return write_takeover_pov();
    }
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
    if (takeover_active_) {
        takeover_fov_ = fov;
        return write_takeover_pov();
    }
    UObject* component = camera_component();
    if (!is_ok(component)) return false;
    const bool ok = write_number(find_property(component, "FieldOfView"), component, fov);
    // CineCameraComponent：同步真实焦距。CurrentFocalLength 由 Filmback.SensorWidth 与水平 FOV 换算；
    // 读不到 Filmback 时用 35mm 全画幅近似，并只在首次告警一次。
    if (camera_is_cine_ && ok) {
        double sensor_width = 35.0;
        (void)read_struct_number(find_property(component, "Filmback"), component, "SensorWidth", sensor_width);
        const double focal = focal_length_from_fov(sensor_width, fov);
        if (focal > 0.0) {
            write_number(find_property(component, "CurrentFocalLength"), component, focal);
        }
    }
    return ok;
}

bool Ue4ssPort::destroy_camera() {
    // 画面效果用的自建后处理体积不随相机走：进入失败/退出时一并清掉。
    destroy_post_volume();
    // 接管模式没有本 Mod 生成的 Actor；退出接管（停止覆盖 + 还原后处理快照）即可。
    if (takeover_active_) leave_camera_takeover();
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

// =============================================================================
// UUU 式「接管当前相机」+ 景深/手动对焦 + Cine 后端探测
//
// 设计边界（与 docs/README.md 一致）：
//   * 实时取景始终走 ViewTarget/POV（独立相机或接管当前相机），绝不用 SceneCapture 当取景器。
//   * 接管模式不 spawn Actor、不切 ViewTarget，只在 GameViewportClient::Tick 之后
//     （游戏 PlayerController::UpdateCameraManager 重算完 CameraCache.POV、RedrawViewports 之前）
//     覆盖 APlayerCameraManager 本帧的 CameraCache.POV。UE 的帧顺序是：
//     World->Tick（所有 actor tick）→ UpdateCameraManager → GameViewportClient->Tick → RedrawViewports。
//     EngineTickPost 已经在 RedrawViewports 之后，那里写 POV 永远赶不上当帧渲染（1.0.75 的回归原因）。
//   * 只写“每帧由相机管理器重算”的 POV 缓存，不写游戏相机组件，因此不存在持久污染；
//     退出时仍显式写回进入前后处理/FOV/位姿快照，做到双保险。
// =============================================================================

namespace {

using namespace pbzpm::detail;


// POV 的 FOV 在不同版本里可能是直接成员，也可能在 Lens.FieldOfView（UE5.5+ 相机 Lens 重构）。
// 5.5+ 两个字段可能同时存在，而引擎只认其中一个——判定/写回都优先 Lens.FieldOfView。
auto pov_fov_property(FProperty* pov_property) -> FProperty* {
    if (FProperty* lens_fov = struct_member(struct_member(pov_property, "Lens"), "FieldOfView"); lens_fov != nullptr) {
        return lens_fov;
    }
    return struct_member(pov_property, "FOV");
}

auto pov_fov_base(FProperty* pov_property, void* pov_base) -> void* {
    if (struct_member(struct_member(pov_property, "Lens"), "FieldOfView") != nullptr) {
        return struct_member_base(struct_member(pov_property, "Lens"), pov_base);
    }
    return pov_base;
}

// 写 POV.FOV：两个字段都存在时都写（本作 UE5.7 只认 Lens.FieldOfView 的可能性更高，
// 但旧 FOV 若仍是渲染读点也不能留旧值）。
auto write_pov_fov(FProperty* pov_property, void* pov_base, double fov) -> bool {
    bool any = false;
    FProperty* lens = struct_member(pov_property, "Lens");
    FProperty* lens_fov = struct_member(lens, "FieldOfView");
    if (lens_fov != nullptr) {
        any = write_number(lens_fov, struct_member_base(lens, pov_base), fov) || any;
    }
    FProperty* legacy = struct_member(pov_property, "FOV");
    if (legacy != nullptr) {
        any = write_number(legacy, pov_base, fov) || any;
    }
    return any;
}

}  // namespace

bool Ue4ssPort::resolve_pov_view(PovView& out) {
    out = PovView{};
    UObject* manager = camera_manager();
    if (!is_ok(manager)) return false;

    FProperty* cache = find_property(manager, "CameraCache");
    if (cache == nullptr) cache = find_property(manager, "CameraCachePrivate");
    if (cache == nullptr || !cache->IsA<FStructProperty>()) return false;

    void* cache_base = struct_member_base(cache, manager);
    if (cache_base == nullptr) return false;

    // UE4.27/UE5.x：CameraCache 是 FCameraCacheEntry { Timestamp, POV }；
    // 部分分支/包装直接把 FMinimalViewInfo 放在 CameraCache 上。两种布局都支持。
    FProperty* pov = struct_member(cache, "POV");
    if (pov != nullptr && pov->IsA<FStructProperty>()) {
        void* pov_base = struct_member_base(pov, cache_base);
        if (pov_base == nullptr) return false;
        out.pov = pov;
        out.base = pov_base;
        out.layout = "FCameraCacheEntry.POV";
    } else {
        out.pov = cache;
        out.base = cache_base;
        out.layout = "CameraCache(FMinimalViewInfo)";
    }
    return pov_fov_property(out.pov) != nullptr;
}

bool Ue4ssPort::verify_takeover_pov() {
    if (!takeover_active_ || !takeover_pose_known_) return true;

    PovView pv;
    if (!resolve_pov_view(pv)) return true;  // 暂时读不到：先不判失败，避免误伤
    FProperty* location = struct_member(pv.pov, "Location");
    FProperty* rotation = struct_member(pv.pov, "Rotation");
    FProperty* fov_property = pov_fov_property(pv.pov);
    if (location == nullptr || rotation == nullptr || fov_property == nullptr) return true;

    double x = 0.0, y = 0.0, z = 0.0, pitch = 0.0, yaw = 0.0, roll = 0.0, fov = 0.0;
    const bool readable =
        read_struct_number(location, pv.base, "X", x) && read_struct_number(location, pv.base, "Y", y) &&
        read_struct_number(location, pv.base, "Z", z) && read_struct_number(rotation, pv.base, "Pitch", pitch) &&
        read_struct_number(rotation, pv.base, "Yaw", yaw) && read_struct_number(rotation, pv.base, "Roll", roll) &&
        read_number(fov_property, pov_fov_base(pv.pov, pv.base), fov);
    if (!readable) return true;

    const bool matches =
        std::abs(x - takeover_pose_.loc.x) <= kTakeoverPovLocationTolerance &&
        std::abs(y - takeover_pose_.loc.y) <= kTakeoverPovLocationTolerance &&
        std::abs(z - takeover_pose_.loc.z) <= kTakeoverPovLocationTolerance &&
        std::abs(pitch - takeover_pose_.rot.pitch) <= kTakeoverPovRotationToleranceDeg &&
        std::abs(yaw - takeover_pose_.rot.yaw) <= kTakeoverPovRotationToleranceDeg &&
        std::abs(roll - takeover_pose_.rot.roll) <= kTakeoverPovRotationToleranceDeg &&
        std::abs(fov - takeover_fov_) <= kTakeoverPovFovTolerance;
    if (matches) {
        takeover_pov_mismatch_streak_ = 0;
        return true;
    }

    ++takeover_pov_mismatch_streak_;
    if (takeover_pov_mismatch_streak_ >= kTakeoverPovMismatchLimit && !takeover_render_lost_) {
        takeover_render_lost_ = true;
        log(LogLevel::Warn,
            "接管当前相机未生效：本帧渲染前的 CameraCache.POV 仍是游戏自己的位姿（连续 " +
                std::to_string(takeover_pov_mismatch_streak_) +
                " 帧被覆盖），说明写点没有赶在 RedrawViewports 之前；请把 config.ini [camera] 的 "
                "takeover_current_camera 设为 false，本次将自动回退独立相机");
    }
    return false;
}

void Ue4ssPort::probe_camera_backends() {
    if (camera_backend_probed_) return;
    camera_backend_probed_ = true;

    auto class_path = [](const char* path) -> UClass* {
        return RC::Unreal::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, widen(path).c_str());
    };

    auto* cine_actor_class = class_path("/Script/CinematicCamera.CineCameraActor");
    auto* cine_component_class = class_path("/Script/CinematicCamera.CineCameraComponent");
    auto* cine_capture_class = class_path("/Script/CineCameraSceneCapture.CineCaptureComponent2D");
    auto* engine_capture_class = class_path("/Script/Engine.SceneCaptureComponent2D");

    std::string report;
    report += "CineCameraActor=";
    report += cine_actor_class != nullptr ? "yes" : "no";
    report += " CineCameraComponent=";
    report += cine_component_class != nullptr ? "yes" : "no";

    // Cine 组件的真实字段（CurrentFocalLength / Filmback.SensorWidth）是否存在，决定②能否联动。
    if (cine_component_class != nullptr) {
        const bool has_focal =
            find_property(static_cast<UStruct*>(cine_component_class), widen("CurrentFocalLength")) != nullptr;
        const bool has_filmback = find_property(static_cast<UStruct*>(cine_component_class), widen("Filmback")) != nullptr;
        report += " CurrentFocalLength=";
        report += has_focal ? "yes" : "no";
        report += " Filmback=";
        report += has_filmback ? "yes" : "no";
    }

    // CineCameraSceneCapture 插件：优先按类路径判定；路径不对时再扫全量 UClass，
    // 区分「插件在包里但类名不同」与「插件根本没打进 Shipping」。
    bool plugin_classes_present = false;
    std::string plugin_class_sample;
    if (cine_capture_class == nullptr) {
        std::vector<UObject*> classes;
        RC::Unreal::UObjectGlobals::FindAllOf(STR("Class"), classes);
        int scanned = 0;
        for (UObject* candidate : classes) {
            if (candidate == nullptr || !is_ok(candidate)) continue;
            if (++scanned > 200000) break;
            const std::string full = narrow(candidate->GetFullName());
            if (full.find("CineCameraSceneCapture") == std::string::npos) continue;
            plugin_classes_present = true;
            if (plugin_class_sample.empty()) plugin_class_sample = narrow(candidate->GetName());
        }
    }

    report += " | CineCaptureComponent2D=";
    report += cine_capture_class != nullptr ? "yes" : "no";
    if (cine_capture_class != nullptr) {
        report += "(a: 类存在可用)";
    } else if (plugin_classes_present) {
        report += "(b: 插件已打进包但类名不同, 同类=" + plugin_class_sample + ")";
    } else {
        report += "(c: 插件 CineCameraSceneCapture 未打进 Shipping)";
    }
    report += " | SceneCaptureComponent2D=";
    report += engine_capture_class != nullptr ? "yes" : "no";

    // 只在首次进入时打一条，不做常驻刷屏。
    log(LogLevel::Info, "相机后端探测：" + report);
}

// ----------------------------------------------------------------- 自建后处理体积
//
// 「画面效果」写在这个体积的 Settings 上（见下面的 ensure_post_volume / set_look）。
// 为什么需要自建：UE 的最终后处理是「相机/POV 的 PostProcessSettings 打底，再按优先级把场上
// 每个 APostProcessVolume 的 bOverride_* 字段盖上」。相机 PP 只是基座、没有优先级，关卡里的
// 体积只要覆盖了同名项就会把我们写的值吃掉；自建体积无界且 Priority 很高，能压过它们。

bool Ue4ssPort::takeover_current_camera(const CameraPose& pose) {
    if (takeover_active_) return true;
    probe_camera_backends();

    PovView pv;
    if (!resolve_pov_view(pv)) {
        log(LogLevel::Warn,
            "接管当前相机失败：读不到 APlayerCameraManager.CameraCache 或 POV 布局不认识");
        return false;
    }
    FProperty* location = struct_member(pv.pov, "Location");
    FProperty* rotation = struct_member(pv.pov, "Rotation");
    if (location == nullptr || rotation == nullptr) {
        log(LogLevel::Warn, "接管当前相机失败：POV 上找不到 Location/Rotation 字段");
        return false;
    }


    takeover_pose_ = pose;
    takeover_pose_known_ = true;
    takeover_fov_ = pose.fov;
    takeover_active_ = true;

    // 自检：先把当前位姿写进 POV 再立刻读回。写不进（属性是只读/内存不可写/管理器换代）
    // 就在这里判定失败，由 core 在同一次 enter 内回退到独立相机。
    if (!write_takeover_pov()) {
        takeover_active_ = false;
        takeover_pose_known_ = false;
        log(LogLevel::Warn, "接管当前相机失败：CameraCache.POV 的位姿/FOV 写入失败");
        return false;
    }
    double x = 0.0, y = 0.0, z = 0.0, pitch = 0.0, yaw = 0.0, roll = 0.0, fov = 0.0;
    FProperty* read_loc = struct_member(pv.pov, "Location");
    FProperty* read_rot = struct_member(pv.pov, "Rotation");
    const bool readable = read_struct_number(read_loc, pv.base, "X", x) &&
                          read_struct_number(read_loc, pv.base, "Y", y) &&
                          read_struct_number(read_loc, pv.base, "Z", z) &&
                          read_struct_number(read_rot, pv.base, "Pitch", pitch) &&
                          read_struct_number(read_rot, pv.base, "Yaw", yaw) &&
                          read_struct_number(read_rot, pv.base, "Roll", roll) &&
                          read_number(pov_fov_property(pv.pov), pov_fov_base(pv.pov, pv.base), fov);
    const bool matches =
        readable && std::abs(x - pose.loc.x) <= kViewPoseLocationTolerance &&
        std::abs(y - pose.loc.y) <= kViewPoseLocationTolerance && std::abs(z - pose.loc.z) <= kViewPoseLocationTolerance &&
        std::abs(pitch - pose.rot.pitch) <= kViewPoseRotationToleranceDeg &&
        std::abs(yaw - pose.rot.yaw) <= kViewPoseRotationToleranceDeg &&
        std::abs(roll - pose.rot.roll) <= kViewPoseRotationToleranceDeg && std::abs(fov - pose.fov) <= 0.1;
    if (!matches) {
        takeover_active_ = false;
        takeover_pose_known_ = false;
        log(LogLevel::Warn, "接管当前相机失败：POV 写入后回读不一致（位姿/FOV 实际不可控）");
        return false;
    }

    takeover_write_fail_streak_ = 0;
    takeover_write_fail_logged_ = false;
    takeover_write_point_logged_ = false;
    takeover_pov_mismatch_streak_ = 0;
    takeover_render_lost_ = false;
    log(LogLevel::Info, "已接管当前相机：POV 布局=" + pv.layout +
                            "，在 GameViewportClient Tick 之后覆盖位姿/FOV（不 spawn、不换 ViewTarget）");
    return true;
}

bool Ue4ssPort::write_takeover_pov() {
    if (!takeover_active_) return true;
    // 运行中写失败只做诊断（连续多次告警一次），不在此处断开接管：是否回退由进入时自检决定，
    // 中途丢接管需要退出重进才能重建，避免在游戏相机逻辑运行中再 spawn 一台相机。
    auto note_fail = [&]() {
        if (++takeover_write_fail_streak_ >= 3 && !takeover_write_fail_logged_) {
            takeover_write_fail_logged_ = true;
            log(LogLevel::Warn,
                "接管当前相机运行中连续写 POV 失败（画面可能不再跟随输入）；退出重进摄影模式可重建接管，"
                "或在 config.ini [camera] 关闭 takeover_current_camera 改用独立相机");
        }
        return false;
    };
    if (!takeover_pose_known_) return note_fail();
    PovView pv;
    if (!resolve_pov_view(pv)) return note_fail();

    FProperty* location = struct_member(pv.pov, "Location");
    FProperty* rotation = struct_member(pv.pov, "Rotation");
    FProperty* fov_property = pov_fov_property(pv.pov);
    if (location == nullptr || rotation == nullptr || fov_property == nullptr) return note_fail();

    const bool pose_ok = write_struct_number(location, pv.base, "X", takeover_pose_.loc.x) &&
                         write_struct_number(location, pv.base, "Y", takeover_pose_.loc.y) &&
                         write_struct_number(location, pv.base, "Z", takeover_pose_.loc.z) &&
                         write_struct_number(rotation, pv.base, "Pitch", takeover_pose_.rot.pitch) &&
                         write_struct_number(rotation, pv.base, "Yaw", takeover_pose_.rot.yaw) &&
                         write_struct_number(rotation, pv.base, "Roll", takeover_pose_.rot.roll) &&
                         write_pov_fov(pv.pov, pv.base, takeover_fov_);

    if (!pose_ok) return note_fail();
    takeover_write_fail_streak_ = 0;
    return true;
}


// 列出 PostProcessSettings 里符合关键字的真实字段名。实机反复「写了没反应」，必须先确认
// 属性名在这个 build 里到底叫什么（UE5.7 可能把某些分组重命名/移进子结构）。
auto collect_pp_member_names(FProperty* pp_property, const char* const* keywords, std::string& out) -> void {
    if (pp_property == nullptr || !pp_property->IsA<FStructProperty>()) {
        out = "<不可读>";
        return;
    }
    UStruct* owner = static_cast<FStructProperty*>(pp_property)->GetStruct();
    int count = 0;
    std::string names;
    for (TFieldIterator<FProperty> it(owner); it; ++it) {
        FProperty* property = *it;
        if (property == nullptr) continue;
        const std::string name = narrow(property->GetName());
        const std::string lower = lower_ascii(name);
        bool keep = false;
        for (const char* const* keyword = keywords; *keyword != nullptr; ++keyword) {
            if (lower.find(*keyword) != std::string::npos) {
                keep = true;
                break;
            }
        }
        if (!keep) continue;
        if (!names.empty()) names += ",";
        names += name;
        if (++count >= 32) {
            names += ",…";
            break;
        }
    }
    out = names.empty() ? "<无>" : names;
}

// ----------------------------------------------------------------- 自建后处理体积
//
// 「画面效果」（暗角/色差/胶片颗粒/饱和/对比/泛光/冷暖）就写在这个体积的 Settings 上。
// 为什么需要自建：UE 的最终后处理是「相机/POV 的 Settings 打底，再按优先级把场上每个
// APostProcessVolume 的 bOverride_* 字段盖上」。相机 PP 只是基座、没有优先级，关卡里的体积
// 只要覆盖了同名项就会把我们写的值吃掉；自建体积无界且 Priority 很高，能压过它们。
// 实测：本作 gameplay 视图里只有这条体积通道会被采纳（写相机 POV 的 PP 无效）。
// 体积是本 Mod 生成的：关画面效果 / 退出摄影模式时销毁；世界换代时引擎已随关卡销毁它，只清指针。
bool Ue4ssPort::ensure_post_volume() {
    if (is_ok(post_volume_)) return true;
    post_volume_ = nullptr;
    UObject* scene = world();
    if (!is_ok(scene)) return false;

    UClass* volume_class = RC::Unreal::UObjectGlobals::StaticFindObject<UClass*>(
        nullptr, nullptr, STR("/Script/Engine.PostProcessVolume"));
    if (volume_class == nullptr) {
        if (!post_volume_fail_logged_) {
            post_volume_fail_logged_ = true;
            log(LogLevel::Warn, "画面效果：包内没有 PostProcessVolume 类，后处理效果无法生效");
        }
        return false;
    }

    FVector location(0.0, 0.0, 0.0);
    FRotator rotation(0.0, 0.0, 0.0);
    auto* as_world = static_cast<UWorld*>(scene);
    AActor* actor = as_world->SpawnActor(volume_class, &location, &rotation);
    if (actor == nullptr) {
        if (!post_volume_fail_logged_) {
            post_volume_fail_logged_ = true;
            log(LogLevel::Warn, "画面效果：SpawnActor(PostProcessVolume) 返回空，后处理效果无法生效");
        }
        return false;
    }

    post_volume_ = actor;
    // 无界 + 极高优先级 + 全权重：覆盖整张画面并压过关卡自带的体积。
    const bool enabled_ok = write_bool(find_property(actor, "bEnabled"), actor, true);
    const bool unbound_ok = write_bool(find_property(actor, "bUnbound"), actor, true);
    const bool priority_ok = write_number(find_property(actor, "Priority"), actor, 10000.0);
    const bool weight_ok = write_number(find_property(actor, "BlendWeight"), actor, 1.0);
    // 体积是否真的成为「无界 + 启用」：bUnbound 写失败时 AVolume 没有 Brush，范围为空，
    // Settings 里写什么都进不了画面（日志会看到「回读正常但画面无变化」）。这条诊断把该情况分开。
    double enabled = -1.0, unbound = -1.0, priority = -1.0, weight = -1.0;
    (void)read_number(find_property(actor, "bEnabled"), actor, enabled);
    (void)read_number(find_property(actor, "bUnbound"), actor, unbound);
    (void)read_number(find_property(actor, "Priority"), actor, priority);
    (void)read_number(find_property(actor, "BlendWeight"), actor, weight);
    if (!post_volume_diag_logged_) {
        post_volume_diag_logged_ = true;
        char buf[220]{};
        std::snprintf(buf, sizeof(buf),
                      "画面效果：自建后处理体积 enabled=%.0f unbound=%.0f priority=%.0f blend=%.2f"
                      "（写入=%d/%d/%d/%d）",
                      enabled, unbound, priority, weight, enabled_ok ? 1 : 0, unbound_ok ? 1 : 0,
                      priority_ok ? 1 : 0, weight_ok ? 1 : 0);
        log(LogLevel::Info, buf);
        if (unbound != 1.0 || enabled != 1.0) {
            log(LogLevel::Warn, "画面效果：后处理体积的 bEnabled/bUnbound 回读不是 1，体积范围为空，画面效果可能不生效");
        }
    }
    return true;
}

void Ue4ssPort::destroy_post_volume() {
    if (!is_ok(post_volume_)) {
        post_volume_ = nullptr;
        return;
    }
    static_cast<AActor*>(post_volume_)->K2_DestroyActor();
    post_volume_ = nullptr;
    log(LogLevel::Debug, "画面效果：后处理体积已销毁");
}

bool Ue4ssPort::set_look(const LookOptions& options) {
    look_current_ = options;
    const bool was_active = look_active_;
    look_active_ = options.any_active();
    if (!options.any_active() && !was_active) return true;
    // 复用景深那个自建后处理体积：本作 gameplay 视图里体积通道是唯一被证实能出画面的后处理路径
    // （探针第 2 段往它写 VignetteIntensity=3，用户看到「四周变暗」）。
    if (!ensure_post_volume()) return false;
    FProperty* settings = find_property(post_volume_, "Settings");
    void* base = struct_member_base(settings, post_volume_);
    if (settings == nullptr || base == nullptr) return false;

    std::string missing;
    auto note_missing = [&](const char* name) {
        if (!missing.empty()) missing += ",";
        missing += name;
    };

    auto set_scalar = [&](const char* override_name, const char* value_name, bool on, double value) {
        FProperty* override_prop = struct_member(settings, override_name);
        FProperty* value_prop = struct_member(settings, value_name);
        if (override_prop == nullptr || value_prop == nullptr) {
            note_missing(value_name);
            return;
        }
        (void)write_bool(override_prop, base, on);
        (void)write_number(value_prop, base, on ? value : 0.0);
    };

    // ColorSaturation / ColorContrast 在 UE5 里是 FVector4 = TVector4<double>，成员名是 X/Y/Z/W；
    // FLinearColor（SceneColorTint）才是 R/G/B/A。hotfix29~31 一直按 R/G/B/A 找，三项全是 nullptr，
    // 这两项从头到尾一个字节都没写进去（hotfix31 日志「回读 饱和=<读不到>」就是它）。
    auto vector_channels = [&](FProperty* value_prop, const char* (&names)[4]) {
        const bool rgba = struct_member(value_prop, "R") != nullptr;
        if (!rgba && !look_vector_named_logged_) {
            look_vector_named_logged_ = true;
            log(LogLevel::Info,
                "画面效果：ColorSaturation/ColorContrast 的成员名是 X/Y/Z/W（UE5 的 FVector4），按这个写");
        }
        names[0] = rgba ? "R" : "X";
        names[1] = rgba ? "G" : "Y";
        names[2] = rgba ? "B" : "Z";
        names[3] = rgba ? "A" : "W";
    };
    // off_rgb：失活时 RGB 三通道写回的中性值（乘算字段=1，加算字段=0）；
    // w_value：第 4 通道的值。乘算字段（饱和/对比/增益/伽马）引擎默认 (1,1,1,1)，W=1 正确；
    // 加算字段（ColorOffset 黑位）引擎默认 (0,0,0,0)，必须写 W=0——preview1 曾对偏移也写
    // W=1.0（与方向无关的常量），实机正负两方向都发白，是该常量的头号嫌疑（preview3 验证：
    // 若 W=0 后仍双向发白，则游戏自定义烘焙对偏移另有滥用如 abs()，届时黑位判死收成两件）。
    auto set_vector = [&](const char* override_name, const char* value_name, bool on, double value,
                          double off_rgb = 1.0, double w_value = 1.0) {
        FProperty* override_prop = struct_member(settings, override_name);
        FProperty* value_prop = struct_member(settings, value_name);
        if (override_prop == nullptr || value_prop == nullptr) {
            note_missing(value_name);
            return;
        }
        void* vector_base = struct_member_base(value_prop, base);
        const char* channels[4]{};
        vector_channels(value_prop, channels);
        (void)write_bool(override_prop, base, on);
        const double channel_value = on ? value : off_rgb;
        for (int i = 0; i < 3; ++i) {
            (void)write_number(struct_member(value_prop, channels[i]), vector_base, channel_value);
        }
        (void)write_number(struct_member(value_prop, channels[3]), vector_base, w_value);
    };

    set_scalar("bOverride_VignetteIntensity", "VignetteIntensity", options.vignette_active(), options.vignette);
    set_scalar("bOverride_BloomIntensity", "BloomIntensity", options.bloom_active(), options.bloom);
    set_vector("bOverride_ColorSaturation", "ColorSaturation", options.saturation_active(), options.saturation);
    set_vector("bOverride_ColorContrast", "ColorContrast", options.contrast_active(), options.contrast);

    // 调色·进阶三件（2026-11-08 批次）：ColorGrading LUT 每通道家族，与饱和/对比同一 pass，
    // 实机验证有效。同批十项（分区调色/胶片曲线/白平衡/泛光阈值软膝）当日探针判死移除
    //（字段缺失或写入成功引擎不消费，取证在 tuning.h）。
    // 中性值语义见 look.h：滑条在默认值时 *_active()=false，set_vector 会把 bOverride_ 写回
    // false（值字段同时归一/归零，仅本模组自己的体积，不影响游戏侧）。
    set_vector("bOverride_ColorGain", "ColorGain", options.color_gain_active(), options.color_gain);
    set_vector("bOverride_ColorGamma", "ColorGamma", options.color_gamma_active(), options.color_gamma);
    // 黑位是加算字段：激活写 (v,v,v,0)，失活写回引擎默认 (0,0,0,0)——W 通道不写 1（见 set_vector 注释）
    set_vector("bOverride_ColorOffset", "ColorOffset", options.color_offset_active(), options.color_offset,
               0.0, 0.0);

    // 冷暖：SceneColorTint（FLinearColor 乘算）。t>0 偏暖（压蓝），t<0 偏冷（压红）。
    {
        FProperty* override_prop = struct_member(settings, "bOverride_SceneColorTint");
        FProperty* tint_prop = struct_member(settings, "SceneColorTint");
        if (override_prop == nullptr || tint_prop == nullptr) {
            note_missing("SceneColorTint");
        } else {
            const bool on = options.temperature_active();
            (void)write_bool(override_prop, base, on);
            void* tint_base = struct_member_base(tint_prop, base);
            const double t = on ? options.temperature : 0.0;
            (void)write_number(struct_member(tint_prop, "R"), tint_base, 1.0 + 0.18 * t);
            (void)write_number(struct_member(tint_prop, "G"), tint_base, 1.0 - 0.02 * t);
            (void)write_number(struct_member(tint_prop, "B"), tint_base, 1.0 - 0.22 * t);
            (void)write_number(struct_member(tint_prop, "A"), tint_base, 1.0);
        }
    }

    // 颜色分级（饱和度/对比度 + 调色·进阶的增益/伽马/黑位/分区/白平衡）由引擎的「颜色分级 LUT」
    // 承载：本作若把 ColorGradingIntensity 压成 0，LUT 整个不参与合成，这些项写得再准也不会进画面。
    // 只在用户真动其中任一项时把强度钉成 1（LUT 内容仍由引擎按当前设置生成，不是外挂一张自己的 LUT）。
    if (options.color_grading_active()) {
        FProperty* override_prop = struct_member(settings, "bOverride_ColorGradingIntensity");
        FProperty* value_prop = struct_member(settings, "ColorGradingIntensity");
        if (override_prop == nullptr || value_prop == nullptr) {
            note_missing("ColorGradingIntensity");
        } else {
            (void)write_bool(override_prop, base, true);
            (void)write_number(value_prop, base, 1.0);
        }
    }

    if (!missing.empty() && !look_missing_logged_) {
        look_missing_logged_ = true;
        log(LogLevel::Warn, "画面效果：这些字段在本 build 里不存在，对应项不会生效 → " + missing);
    }

    look_active_ = options.any_active();
    if (look_active_ && !look_logged_) {
        look_logged_ = true;
        char buf[256]{};
        std::snprintf(buf, sizeof(buf),
                      "画面效果：已写进自建后处理体积（暗角=%.2f 饱和=%.2f 对比=%.2f 泛光=%.2f "
                      "冷暖=%.2f），退出摄影模式还原",
                      options.vignette, options.saturation, options.contrast, options.bloom,
                      options.temperature);
        log(LogLevel::Info, buf);
    }

    // 值每次都真的下发，但「写进去」不等于「引擎肯画」：hotfix31 实机里饱和度/对比度曾因
    // FVector4 成员名写错（R/G/B/A 而非 X/Y/Z/W）而一个字节都没进体积，日志却只显示「下发成功」。
    // 所以每次值变化都回读一遍体积里的实际值，把「没写进去」和「写进去了引擎不画」分开。
    if (!look_written_valid_ || !look_options_equal(look_written_last_, options)) {
        look_written_valid_ = true;
        look_written_last_ = options;
        char buf[320]{};
        std::snprintf(buf, sizeof(buf),
                      "画面效果：下发 暗角=%.2f 饱和=%.2f 对比=%.2f 泛光=%.2f 冷暖=%.2f",
                      options.vignette, options.saturation, options.contrast, options.bloom,
                      options.temperature);
        log(LogLevel::Info, buf);

        auto pair_text = [](const char* label, double flag, double value) {
            char part[64]{};
            if (flag < 0.0) {
                std::snprintf(part, sizeof(part), " %s=<读不到>", label);
            } else {
                std::snprintf(part, sizeof(part), " %s=%.2f(覆盖%d)", label, value, flag > 0.5 ? 1 : 0);
            }
            return std::string(part);
        };
        std::string text = "画面效果：回读";
        double flag = -1.0;
        double value = -1.0;
        // 标量项：bOverride_* + 浮点值
        auto read_scalar = [&](const char* label, const char* override_name, const char* value_name) {
            flag = -1.0;
            value = -1.0;
            double o = 0.0;
            double v = 0.0;
            if (read_number(struct_member(settings, override_name), base, o) &&
                read_number(struct_member(settings, value_name), base, v)) {
                flag = o;
                value = v;
            }
            text += pair_text(label, flag, value);
        };
        // 向量项：bOverride_* + R 通道（R=G=B，A 恒 1）
        auto read_vector = [&](const char* label, const char* override_name, const char* value_name) {
            flag = -1.0;
            value = -1.0;
            FProperty* vector_prop = struct_member(settings, value_name);
            double o = 0.0;
            double r = 0.0;
            if (vector_prop != nullptr && read_number(struct_member(settings, override_name), base, o)) {
                void* vector_base = struct_member_base(vector_prop, base);
                const char* channels[4]{};
                vector_channels(vector_prop, channels);
                if (read_number(struct_member(vector_prop, channels[0]), vector_base, r)) {
                    flag = o;
                    value = r;
                }
            }
            text += pair_text(label, flag, value);
        };
        read_scalar("暗角", "bOverride_VignetteIntensity", "VignetteIntensity");
        read_scalar("泛光", "bOverride_BloomIntensity", "BloomIntensity");
        read_vector("饱和", "bOverride_ColorSaturation", "ColorSaturation");
        read_vector("对比", "bOverride_ColorContrast", "ColorContrast");
        read_scalar("颜色分级", "bOverride_ColorGradingIntensity", "ColorGradingIntensity");
        // 调色·进阶三件回读（写进去了 ≠ 引擎肯画；回读只回答前半个问题）
        read_vector("增益", "bOverride_ColorGain", "ColorGain");
        read_vector("伽马", "bOverride_ColorGamma", "ColorGamma");
        read_vector("黑位", "bOverride_ColorOffset", "ColorOffset");
        log(LogLevel::Info, text);
    }

    return true;
}

// 画面效果的「相机 POV」重申通道。
// 为什么需要第二通道：UE 的最终后处理顺序是
//   后处理体积（UWorld::AddPostProcessingSettings）
//   → 相机 POV 的 PostProcessSettings（ULocalPlayer::CalcSceneView 里 weight=1 的 OverridePostProcessSettings，最后覆盖体积）
//   → FSceneView::EndFinalPostprocessSettings（按 r.BloomQuality / r.Tonemapper.Quality / ShowFlags 做最后清零）
// 本作的暗角/泛光强度在画面里始终存在（Phantom Tweaks 等工具也是通过改游戏自己的画质/镜头参数
// 来关掉它们）；若强度来自相机 POV 的 PostProcessSettings，只写自建体积就会出现「体积里读回
// 2.00/10.00 都成功，但渲染前被 POV 同名字段盖回去」的现象。即使强度不在 POV 上，POV 也是
// 体积之后最后生效的一份设置，写这里能保证覆盖任何体积。POV 在正常运行时每帧被 UpdateCamera
// 整份重建，因此必须在本 Mod 已有的 GameViewportClient Tick 之后写点（UpdateCamera 之后、
// RedrawViewports 之前）每帧重申；但摄影模式定格/独立相机下 UpdateCamera 不跑、POV 不重建，
// 所以失活（重置/开关/滑回 0）后本函数还要把上一帧写下的 覆盖1+旧值 显式回写成 覆盖0，
// 否则残留值永远压过体积（2026-11-07 实机：重置后体积回读全对、画面停在旧值）。
void Ue4ssPort::apply_look_to_camera_pov() {
    // 本帧目标状态 + 残留判定：定格/独立相机下 POV 不被 UpdateCamera 重建（这正是需要每帧
    // 重申的原因），因此失活后重申一停，上一帧写下的「覆盖1+旧值」会永远钉在 POV 上压过
    // 体积——重置/开关按钮画面不动的根因。所以失活后还要继续跑到 POV 把 bOverride 写回
    // false 撤残留，撤干净（两个闩锁都落下）才允许早退。
    const bool want_vignette = look_active_ && look_current_.vignette_active();
    const bool want_bloom = look_active_ && look_current_.bloom_active();
    // 调色·进阶诊断（一次性）：即使用户没动暗角/泛光，只要用了调色项也要放行过这个早退，
    // 读一遍游戏 POV 对三件调色字段的覆盖标志（只读，不写；读完即记，后续帧恢复早退）。
    const bool pro_diag_pending =
        !look_pov_pro_logged_ && look_active_ && look_current_.color_pro_active();
    if (!want_vignette && !pov_vignette_latched_ && !want_bloom && !pov_bloom_latched_ && !pro_diag_pending) {
        return;
    }

    PovView pv;
    if (!resolve_pov_view(pv)) return;
    FProperty* settings = struct_member(pv.pov, "PostProcessSettings");
    void* base = struct_member_base(settings, pv.base);
    if (settings == nullptr || base == nullptr) return;

    // 首帧先把游戏相机自己的值记下来：如果这里显示暗角/泛光的 bOverride=1 且值明显偏离 0，
    // 就能直接证明「体积被相机 POV 覆盖」这个根因（只读，不写）。
    double game_vignette = -1.0, game_bloom = -1.0, game_vignette_override = -1.0, game_bloom_override = -1.0;
    double pov_blend_weight = -1.0;
    if (!look_pov_logged_) {
        (void)read_number(struct_member(settings, "VignetteIntensity"), base, game_vignette);
        (void)read_number(struct_member(settings, "BloomIntensity"), base, game_bloom);
        (void)read_number(struct_member(settings, "bOverride_VignetteIntensity"), base, game_vignette_override);
        (void)read_number(struct_member(settings, "bOverride_BloomIntensity"), base, game_bloom_override);
        (void)read_number(struct_member(pv.pov, "PostProcessBlendWeight"), pv.base, pov_blend_weight);
    }

    // 调色·进阶诊断（一次性，只读）：游戏 POV 对三件存活字段的 bOverride_* 标志 + 原值。
    // 2026-11-08 实机判据已用掉一轮：当时 13 项全部 覆盖0（排除 POV 压制），十项判死定案；
    // 保留三件的一次性日志供游戏更新后复查（字段若被游戏 override，需按暗角/泛光的
    // 闩锁模式扩 POV 重申通道）。
    if (pro_diag_pending && !look_pov_pro_logged_) {
        // FVector4 项取首通道（X 或 R，与 set_look 的 vector_channels 同语义，只读无日志副作用）。
        auto first_channel_value = [&](const char* value_name, double& out) {
            FProperty* vector_prop = struct_member(settings, value_name);
            if (vector_prop == nullptr) return false;
            void* vector_base = struct_member_base(vector_prop, base);
            const bool rgba = struct_member(vector_prop, "R") != nullptr;
            return read_number(struct_member(vector_prop, rgba ? "R" : "X"), vector_base, out);
        };
        struct ProField {
            const char* label;
            const char* override_name;
            const char* value_name;
        };
        static const ProField kProFields[] = {
            {"增益", "bOverride_ColorGain", "ColorGain"},
            {"伽马", "bOverride_ColorGamma", "ColorGamma"},
            {"黑位", "bOverride_ColorOffset", "ColorOffset"},
        };
        std::string text = "画面效果·进阶：游戏 POV 覆盖标志（覆盖1=游戏镜头自己 override 该项，"
                           "体积写入会被压住）";
        for (const ProField& f : kProFields) {
            double o = 0.0;
            double v = 0.0;
            const bool flag_ok = read_number(struct_member(settings, f.override_name), base, o);
            const bool value_ok = first_channel_value(f.value_name, v);
            if (flag_ok && value_ok) {
                char part[48]{};
                std::snprintf(part, sizeof(part), " %s=%.2f(覆盖%.0f)", f.label, v, o);
                text += part;
            } else {
                text += std::string(" ") + f.label + "=<读不到>";
            }
        }
        look_pov_pro_logged_ = true;
        log(LogLevel::Info, text);
    }

    auto set_scalar = [&](const char* override_name, const char* value_name, bool on, double value) {
        FProperty* override_prop = struct_member(settings, override_name);
        FProperty* value_prop = struct_member(settings, value_name);
        if (override_prop == nullptr || value_prop == nullptr) return false;
        const bool override_ok = write_bool(override_prop, base, on);
        const bool value_ok = write_number(value_prop, base, on ? value : 0.0);
        return override_ok && value_ok;
    };

    bool wrote = false;
    bool cleared = false;
    // 失活那一帧走的是「撤残留」分支：把上一帧的 覆盖1+旧值 回写成 覆盖0（体积通道的
    // set_look 里同名 lambda 就是这个语义，照搬）。写成功才落闩，失败下一帧重试。
    const bool clear_vignette = !want_vignette && pov_vignette_latched_;
    const bool clear_bloom = !want_bloom && pov_bloom_latched_;
    if (want_vignette || clear_vignette) {
        if (set_scalar("bOverride_VignetteIntensity", "VignetteIntensity", want_vignette,
                       look_current_.vignette)) {
            pov_vignette_latched_ = want_vignette;
            wrote = true;
            cleared = clear_vignette;
        }
    }
    if (want_bloom || clear_bloom) {
        if (set_scalar("bOverride_BloomIntensity", "BloomIntensity", want_bloom, look_current_.bloom)) {
            pov_bloom_latched_ = want_bloom;
            wrote = true;
            cleared = cleared || clear_bloom;
        }
    }
    if (wrote && !look_pov_logged_) {
        look_pov_logged_ = true;
        double v = -1.0, b = -1.0, vf = -1.0, bf = -1.0;
        (void)read_number(struct_member(settings, "VignetteIntensity"), base, v);
        (void)read_number(struct_member(settings, "BloomIntensity"), base, b);
        (void)read_number(struct_member(settings, "bOverride_VignetteIntensity"), base, vf);
        (void)read_number(struct_member(settings, "bOverride_BloomIntensity"), base, bf);
        char buf[480]{};
        std::snprintf(buf, sizeof(buf),
                      "画面效果：相机 POV 通道已启用（每帧写 CameraCache.POV.PostProcessSettings，优先于体积）；"
                      "游戏 POV 原值 暗角=%.2f(覆盖%.0f) 泛光=%.2f(覆盖%.0f) PP权重=%.2f → "
                      "回读 暗角=%.2f(覆盖%.0f) 泛光=%.2f(覆盖%.0f)",
                      game_vignette, game_vignette_override, game_bloom, game_bloom_override, pov_blend_weight,
                      v, vf, b, bf);
        log(LogLevel::Info, buf);
    }
    if (cleared && !pov_look_clear_logged_) {
        pov_look_clear_logged_ = true;
        log(LogLevel::Info,
            "画面效果：暗角/泛光失活，已把相机 POV 上的残留覆盖回写为 覆盖0"
            "（定格/独立相机下 POV 不被 UpdateCamera 重建，残留必须显式撤除，否则压过体积）");
    }
}

bool Ue4ssPort::leave_camera_takeover() {
    if (!takeover_active_) return true;
    takeover_active_ = false;
    takeover_pose_known_ = false;
    takeover_fov_ = 90.0;
    takeover_write_fail_streak_ = 0;
    takeover_write_fail_logged_ = false;
    takeover_write_point_logged_ = false;
    takeover_pov_mismatch_streak_ = 0;
    takeover_render_lost_ = false;
    log(LogLevel::Info, "已退出接管当前相机：停止每帧覆盖 POV，游戏相机恢复自身逻辑");
    return true;
}

bool Ue4ssPort::read_ignore_flag(bool look, bool& out) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    bool value = false;
    if (!invoke_bool(pc, look ? "IsLookInputIgnored" : "IsMoveInputIgnored",
                     [](ParamBlock&) { return true; }, value)) {
        return false;
    }
    out = value;
    return true;
}

// IgnoreLookInput / IgnoreMoveInput 是 uint8 堆叠计数，且不是 UPROPERTY，反射层读不到计数，
// 只能通过 Is*InputIgnored() 读布尔锁状态。进入前若游戏自己已持有同类锁，配对的一次
// Set(false) 只会把 2 减成 1，退出后仍被判定为“忽略输入”——视角完全不能转。
// 这里按进入前/当前布尔状态校正：必要时 Reset* 清掉全部残留，或补回进入前那把锁。
bool Ue4ssPort::reconcile_ignore_flag(bool look, UObject* pc) {
    const bool saved = look ? input_look_state_saved_ : input_move_state_saved_;
    if (!saved) return true;
    const bool was_ignored = look ? input_look_was_ignored_ : input_move_was_ignored_;
    const char* label = look ? "Look" : "Move";
    if (!is_ok(pc)) {
        log(LogLevel::Warn, std::string("恢复输入屏蔽：") + label + " 目标对象已失效，无法校正锁状态");
        return false;
    }

    bool now_ignored = false;
    if (!invoke_bool(pc, look ? "IsLookInputIgnored" : "IsMoveInputIgnored",
                     [](ParamBlock&) { return true; }, now_ignored)) {
        // 读不到状态时不能赌：直接 Reset* 兜底，宁可清掉一把可能的游戏锁，也不能让玩家视角永久卡死。
        log(LogLevel::Warn, std::string("恢复输入屏蔽：读不到 ") + label + " 锁状态，执行 Reset 兜底");
        const bool reset_ok =
            invoke(pc, look ? "ResetIgnoreLookInput" : "ResetIgnoreMoveInput", [](ParamBlock&) { return true; });
        if (!reset_ok) return false;
        if (look) {
            input_look_state_saved_ = false;
        } else {
            input_move_state_saved_ = false;
        }
        return true;
    }

    const IgnoreLockRestore action = plan_ignore_lock_restore(was_ignored, now_ignored);
    bool ok = true;
    if (action == IgnoreLockRestore::Reset) {
        ok = invoke(pc, look ? "ResetIgnoreLookInput" : "ResetIgnoreMoveInput", [](ParamBlock&) { return true; });
    } else if (action == IgnoreLockRestore::Relock) {
        ok = invoke(pc, look ? "SetIgnoreLookInput" : "SetIgnoreMoveInput", [&](ParamBlock& params) {
            if (look) {
                return params.set_bool("bNewLookInput", true) || params.set_bool("bIgnore", true) ||
                       params.set_bool("NewLookInput", true);
            }
            return params.set_bool("bNewMoveInput", true) || params.set_bool("bIgnore", true) ||
                   params.set_bool("NewMoveInput", true);
        });
    }
    if (!ok) {
        log(LogLevel::Warn, std::string("恢复输入屏蔽：") + label + " 锁校正失败，保留待重试");
        return false;
    }
    if (action != IgnoreLockRestore::None) {
        log(LogLevel::Info, std::string("恢复输入屏蔽：") + label + " 锁残留已校正（进入前=" +
                                (was_ignored ? "锁定" : "未锁") + "，退出时=" +
                                (now_ignored ? "锁定" : "未锁") + "）");
    }
    if (look) {
        input_look_state_saved_ = false;
    } else {
        input_move_state_saved_ = false;
    }
    return true;
}

bool Ue4ssPort::set_ignore_move_input(bool ignore) {
    UObject* pc = controller();
    if (!is_ok(pc)) return false;
    if (ignore) {
        bool pre_ignored = false;
        const bool pre_read = read_ignore_flag(false, pre_ignored);
        const bool invoked = invoke(pc, "SetIgnoreMoveInput", [&](ParamBlock& params) {
            return params.set_bool("bNewMoveInput", true) || params.set_bool("bIgnore", true) ||
                   params.set_bool("NewMoveInput", true);
        });
        if (invoked && !input_move_state_saved_) {
            // 读不到 IsMoveInputIgnored 时按“进入前未锁”记录，退出时若仍锁定就 Reset 兜底。
            input_move_was_ignored_ = pre_read ? pre_ignored : false;
            input_move_state_saved_ = true;
            if (!pre_read) {
                log(LogLevel::Warn, "屏蔽输入：读不到 IsMoveInputIgnored（退出时将 Reset 兜底）");
            } else if (pre_ignored) {
                log(LogLevel::Info, "屏蔽输入：进入前 Move 已被游戏锁定（退出时会恢复为锁定）");
            }
        }
        return invoked;
    }

    const bool invoked = invoke(pc, "SetIgnoreMoveInput", [&](ParamBlock& params) {
        return params.set_bool("bNewMoveInput", false) || params.set_bool("bIgnore", false) ||
               params.set_bool("NewMoveInput", false);
    });
    if (!invoked) return false;
    return reconcile_ignore_flag(false, pc);
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
        bool look_ok = invoke(record.object, "SetIgnoreLookInput", [&](ParamBlock& params) {
            return params.set_bool("bNewLookInput", false) || params.set_bool("bIgnore", false) ||
                   params.set_bool("NewLookInput", false);
        });
        // 单次 Set(false) 只把堆叠计数 -1：进入前游戏已有锁时会残留 1。按进入前布尔状态校正。
        if (look_ok) look_ok = reconcile_ignore_flag(true, record.object);
        ok = look_ok && ok;
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
        const bool had_look_bit = (input_block_pc_.written_bits & 0x4) != 0;
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
        // 没有实际加过 Look 锁（或记录已因对象换代被跳过）时，清掉可能遗留的保存态，
        // 避免污染下一次进入的“进入前状态”。
        if (!had_look_bit || input_block_pc_.written_bits == 0) input_look_state_saved_ = false;
        return ok;
    }

    bool touched = false;
    bool fallback_any = false;
    // bBlockInput 直写失败的项先收集，等兜底结果出来再决定日志级别：
    // 兜底成功只是“换了一条路”，不该按 Error/Warn 刷屏；兜底也失败才是真问题。
    std::vector<std::string> direct_failures;

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
            direct_failures.push_back(std::string(slot.label) + " 的 bBlockInput");
        }

        UObject* component = nullptr;
        if (read_object(find_property(owner, "InputComponent"), owner, component) && is_ok(component)) {
            if (write_bool(find_property(component, "bBlockInput"), component, true)) {
                touched = true;
                remember_blocked_object(*slot.component_record, component, owner, 0x1);
            } else {
                direct_failures.push_back(std::string(slot.label) + " 的 InputComponent");
            }
        }
    }

    // 2) 视角轴一起关掉：否则游戏仍会消耗鼠标位移、把玩家的控制旋转算进去
    UObject* pc = controller();
    if (is_ok(pc)) {
        // 先记进入前的布尔锁状态（游戏暂停/过场可能已经持有一把锁，不能当成 0）。
        bool pre_look_ignored = false;
        const bool pre_look_read = read_ignore_flag(true, pre_look_ignored);
        if (invoke(pc, "SetIgnoreLookInput", [&](ParamBlock& params) {
                return params.set_bool("bNewLookInput", true) || params.set_bool("bIgnore", true) ||
                       params.set_bool("NewLookInput", true);
            })) {
            touched = true;
            fallback_any = true;
            remember_blocked_object(input_block_pc_, pc, pc, 0x4);
            if (!input_look_state_saved_) {
                // 读不到 IsLookInputIgnored 时按“进入前未锁”记录，退出时若仍锁定就 Reset 兜底。
                input_look_was_ignored_ = pre_look_read ? pre_look_ignored : false;
                input_look_state_saved_ = true;
                if (!pre_look_read) {
                    log(LogLevel::Warn, "屏蔽输入：读不到 IsLookInputIgnored（退出时将 Reset 兜底）");
                } else if (pre_look_ignored) {
                    log(LogLevel::Info, "屏蔽输入：进入前 Look 已被游戏锁定（退出时会恢复为锁定）");
                }
            }
        }

        // 3) 最后一道闸：把 PC 与 Pawn 的 InputComponent 直接停用。
        //    bBlockInput 只管“输入栈是否继续往下走”，蓝图里某些动作可能绕过它；
        //    DisableInput 是官方语义上的“这个 Actor 不再接收输入”。
        if (invoke(pc, "DisableInput", [&](ParamBlock& params) { return params.set_object("PlayerController", pc); })) {
            touched = true;
            fallback_any = true;
            remember_blocked_object(input_block_pc_, pc, pc, 0x8);
        }
        UObject* pawn = player_pawn();
        if (is_ok(pawn) &&
            invoke(pawn, "DisableInput", [&](ParamBlock& params) { return params.set_object("PlayerController", pc); })) {
            touched = true;
            fallback_any = true;
            remember_blocked_object(input_block_pawn_, pawn, pawn, 0x8);
        }
    }

    // 直写失败汇总：兜底成功 → Info（只报一条，不再每次进入两条 Warn）；
    // 兜底也全部失败 → Warn（此时输入栈可能真的没被屏蔽）。
    if (!direct_failures.empty()) {
        std::string joined;
        for (const std::string& item : direct_failures) {
            if (!joined.empty()) joined += "、";
            joined += item;
        }
        if (fallback_any) {
            log(LogLevel::Info, "屏蔽输入：" + joined + " 写入失败，已由 SetIgnoreLookInput/DisableInput 兜底");
        } else {
            log(LogLevel::Warn, "屏蔽输入：" + joined + " 写入失败，且兜底全部失败：游戏键鼠输入可能仍在生效");
        }
    }
    return touched;
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
