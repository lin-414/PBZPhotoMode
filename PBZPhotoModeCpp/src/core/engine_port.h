#pragma once

#include <initializer_list>
#include <string>

#include "core/look.h"
#include "core/dof_math.h"
#include "core/config.h"
#include "core/math3.h"

namespace pbzpm {

// ------------------------------------------------------------------ 可单测的纯判定
// 以下判定不依赖 UE4SS / 反射，放在 core 侧便于单测；引擎实现只负责采集输入。

// 日志级别过滤：configured 是用户设定阈值，message 是这条日志自身级别。
// 枚举顺序 Error(0) < Warn(1) < Info(2) < Debug(3)，数值越小越严重；
// 只有 message 严重程度不低于阈值才输出（error 只留 Error，debug 全留），Error 永不被过滤。
inline bool log_level_enabled(LogLevel configured, LogLevel message) {
    return static_cast<int>(message) <= static_cast<int>(configured);
}

// 反射参数名解析决策：只在候选名里找，返回命中的候选下标；全部未命中返回 -1。
// 调用方必须在 -1 时失败——不能按声明顺序退化到“类型相同但语义不同”的参数。
template <typename ExistsFn>
inline int resolve_param_name(std::initializer_list<const char*> candidates, ExistsFn&& exists) {
    int index = 0;
    for (const char* name : candidates) {
        if (name != nullptr && exists(name)) return index;
        ++index;
    }
    return -1;
}

// ViewTarget 回读结果：只有“读到了且就是摄影相机”才算接管确认。
enum class ViewTargetState {
    Match,      // 读到了，目标是摄影相机
    Mismatch,   // 读到了，但目标是别的对象
    Unreadable  // 属性/结构链路读不出来（不能据此认为接管成功）
};

inline ViewTargetState classify_view_target(bool readable, bool is_camera_actor) {
    if (!readable) return ViewTargetState::Unreadable;
    return is_camera_actor ? ViewTargetState::Match : ViewTargetState::Mismatch;
}

// M-1 收口：回读“没看到摄影相机”不等于接管失效。
// 真机 1.0.75 实测：SetViewTargetWithBlend 调用成功的同一帧里 ViewTarget.Target 读出 null，
// 要等相机管理器本帧 UpdateViewTarget 之后才有值。只有“读到明确的其他 Actor”或
// “摄影相机位姿被搬走”才算确凿失效；null / 属性不可读只能降级为告警并继续重试，
// 绝不能因此阻断进入摄影模式（1.0.75 的回归就是把它当成了硬性前置检查）。
// pose_comparable=false 表示“位姿暂不可比”（刚 spawn、apply_pose 还没写入）：
// 此时读到的任何位姿偏差都不能当成失效，否则每次进入首帧都会误报一次。
inline bool view_loss_is_conclusive(bool readable, bool observed_is_other_actor, bool pose_comparable,
                                    bool pose_mismatch) {
    if (pose_comparable && pose_mismatch) return true;
    return readable && observed_is_other_actor;
}

struct CameraPose {
    Vec3 loc{};
    Rot rot{};
    double fov{90.0};
    bool operator==(const CameraPose& o) const {
        return loc == o.loc && rot == o.rot && fov == o.fov;
    }
};

// 相机位姿是否仍在容差内：判定 ViewTarget 看似正确、但相机 Actor 已被别的系统搬走的情况。
// 角度用 wrap_angle_180 归一化，避免 359° 与 1° 被当成相差 358°。
inline bool camera_pose_within_tolerance(const CameraPose& expected, const CameraPose& actual,
                                         double loc_tolerance, double rot_tolerance_deg) {
    const double dx = actual.loc.x - expected.loc.x;
    const double dy = actual.loc.y - expected.loc.y;
    const double dz = actual.loc.z - expected.loc.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > loc_tolerance) return false;
    if (std::fabs(wrap_angle_180(actual.rot.pitch - expected.rot.pitch)) > rot_tolerance_deg) return false;
    if (std::fabs(wrap_angle_180(actual.rot.yaw - expected.rot.yaw)) > rot_tolerance_deg) return false;
    if (std::fabs(wrap_angle_180(actual.rot.roll - expected.rot.roll)) > rot_tolerance_deg) return false;
    return true;
}

// 焦距与 FOV 换算（CineCamera Filmback 联动用）：
// UE 的 FieldOfView 是水平视场角，给定传感器宽度（mm）即可换算等效焦距：
//   focal = (sensor_width / 2) / tan(fov / 2)
// 非法输入（<=0 或接近 180°）返回 0，调用方据此跳过写入，绝不能用 0 去写引擎字段。
inline double focal_length_from_fov(double sensor_width_mm, double fov_deg) {
    constexpr double kPi = 3.14159265358979323846;
    const double half_rad = fov_deg * kPi / 360.0;
    if (!(sensor_width_mm > 0.0) || !(half_rad > 0.0) || half_rad >= kPi * 0.5) return 0.0;
    return (sensor_width_mm * 0.5) / std::tan(half_rad);
}

inline double fov_from_focal_length(double sensor_width_mm, double focal_mm) {
    constexpr double kPi = 3.14159265358979323846;
    if (!(sensor_width_mm > 0.0) || !(focal_mm > 0.0)) return 0.0;
    const double half_rad = std::atan((sensor_width_mm * 0.5) / focal_mm);
    const double fov = half_rad * 2.0 * 180.0 / kPi;
    return fov;
}

// 剧情过场播放器（Sequencer/LevelSequence）一次扫描/操作的结果（诊断用）
struct SequenceFreezeReport {
    int found{0};    // 扫到的播放器实例总数
    int matched{0};  // 处于目标状态的数量（暂停时=正在播放；恢复时=处于暂停态）
    int acted{0};    // 实际成功执行暂停/恢复的数量
    bool ok{false};  // 扫描链路是否跑通（false = 反射查询不可用）
};

// 冻结期相机治理结果：相机管理器修饰器的扫描/停用/还原计数（诊断用）
struct CameraShakeFreezeReport {
    int found{0};    // CameraModifiers 数组里的修饰器数量
    int acted{0};    // 冻结=本次停用的修饰器数；恢复=按原状态写回的数量
    bool ok{false};  // 相机管理器可达（false = 反射链路不可用，core 不记账）
};

// 冻结期风源治理结果：WindDirectionalSource 扫描/归零/还原计数（诊断用）
struct WindFreezeReport {
    int found{0};    // 场景里的风源数量（排除类默认对象）
    int acted{0};    // 冻结=成功归零的风源数；恢复=成功还原的数量
    bool ok{false};  // 扫描链路是否跑通
};

// 冻结期布料模拟治理结果：SkeletalMeshComponent 的 clothing simulation 与
// ChaosClothComponent 的独立布料模拟都可能按真实时间继续跑，世界暂停拦不住。
struct ClothFreezeReport {
    int found{0};   // 扫到的“带布料”组件数（含原本就处于暂停态的）
    int acted{0};   // 冻结=本次挂起的数量；恢复=本次恢复的数量
    bool ok{false}; // 扫描/调用链路是否跑通（false = 反射接口不可用，core 不记账）
};

// 冻结期骨骼动画治理结果：角色/敌人的 AnimBP 若在暂停下仍 tick，cape 的骨骼驱动
// 会继续产生运动，表现成布料“流动”。
struct SkeletalAnimationFreezeReport {
    int found{0};   // 扫到的 SkeletalMeshComponent 数
    int acted{0};   // 冻结=本次暂停动画的数量；恢复=本次恢复的数量
    bool ok{false}; // 扫描/调用链路是否跑通
};

// 冻结期渲染对照（A/B 诊断）结果：临时改渲染 CVar，解冻/退出必须还原
struct RenderTuningReport {
    int applied{0};   // 本次实际改写的 CVar 数量（0=没有可改项或全部已是目标值）
    int restored{0};  // 本次恢复的 CVar 数量
    bool ok{false};   // 读/写 CVar 的反射链路是否可用；false 时 core 不记账
};

// 自定义布料/摆动系统冻结结果：PBZ 的 SwayComponent / SwayManager / r.Cloth.Enable 是
// 引擎布料模拟之外的一套角色衣摆驱动，世界暂停和普通 Cloth 挂起都拦不住。
struct CustomClothFreezeReport {
    int sway_found{0};      // 扫到的 Sway/WindSystem 组件数
    int sway_acted{0};      // 本次成功停用 tick 的数量
    int groom_found{0};     // 扫到的 Groom（毛发）组件数
    int groom_acted{0};     // 本次关闭毛发模拟的数量
    int niagara_found{0};   // 扫到的 Niagara 粒子组件数
    int niagara_acted{0};   // 本次暂停的粒子组件数
    bool cloth_global_ok{false};  // r.Cloth.Enable=0 是否生效
    bool ok{false};         // 反射链路是否跑通（false 时 core 不记账）
};

// WPO（WorldPositionOffset）评估冻结：材质顶点动画在暂停/TimeDilation 下仍会求值，
// 是布料/衣摆“流动式闪烁”的最后一条内容层路径；冻结=逐 SkeletalMesh 调用
// SetEvaluateWorldPositionOffset(false)，解冻恢复。
struct WpoFreezeReport {
    int found{0};            // 扫到的 SkeletalMeshComponent 数
    int acted{0};            // 本次关闭 WPO 评估的 SkeletalMesh 数量
    int primitive_found{0};  // 扫到的其他图元（StaticMesh/Instanced/ LandScape 等）数
    int primitive_acted{0};  // 本次关闭 WPO 评估的其他图元数量
    bool ok{false};          // 反射链路是否跑通
};

// 光照 A/B（Lumen / 屏幕空间光照）各子项：冻结时按开关临时改光照 CVar，解冻/退出还原。
// 与渲染对照分开记账，避免每 6 秒的阶段轮换把 AA/DLSS/FG 也一起开关。
struct LightingTuningOptions {
    bool screen_traces_off{false};   // Lumen GI/反射的 ScreenTraces=0（保留距离场追踪）
    bool short_range_ao_off{false};  // Lumen ShortRangeAO / DiffuseIndirect.SSAO / r.AmbientOcclusionLevels=0
    bool radiance_cache_off{false};  // Lumen ScreenProbeGather.RadianceCache=0
    bool fixed_jitter{false};        // Lumen FixedJitterIndex/FixedStateFrameIndex=0（固定逐帧随机采样）
    bool lumen_off{false};           // Lumen GI/反射总开关（DiffuseIndirect.Allow / Reflections.Allow / 方法 CVar）
};

// ------------------------------------------------------------------ 引擎端口（按职责拆分）
// core 只依赖它真正用到的子接口，看不到与摄影模式无关的引擎细节；
// Ue4ssPort / FakeEngine 统一实现聚合接口 EnginePort，由调用方一次性接线。
// 边界按 PhotoMode 的实际调用划分：凡是 PhotoMode 正在用的方法都留在对应的子接口里，
// 不额外加转发层。

// 世界状态：当前是否还有可读的世界（换图/读档期间为 false）
class WorldPort {
public:
    virtual ~WorldPort() = default;
    virtual bool world_valid() = 0;
};

// 相机与视图：进入/退出摄影相机、位姿与 FOV、画幅解锁
class CameraViewPort {
public:
    virtual ~CameraViewPort() = default;

    virtual bool read_pov(CameraPose& out) = 0;

    virtual bool spawn_camera(const CameraPose& pose) = 0;
    virtual bool attach_view() = 0;
    virtual bool restore_view() = 0;
    virtual bool apply_pose(const CameraPose& pose) = 0;
    virtual bool apply_fov(double fov) = 0;
    virtual bool destroy_camera() = 0;

    // 超宽屏（21:9/32:9）支持：游戏把画幅锁在 16:9，超宽屏两侧补黑边。
    // 进入摄影模式时解除锁定（实现内部保存原值），退出时按进入前的状态还原。
    // 失败只告警不阻断进模式（游戏可能根本没锁，或属性不在预期位置）。
    virtual bool unlock_aspect_ratio() = 0;
    virtual void restore_aspect_ratio() = 0;

    // 冻结/暂停期间游戏可能重新给相机管理器打开 16:9 画幅锁（暂停菜单、处决演出都会）。
    // 摄影模式每帧在 GameViewportClient Tick 之后（游戏改完、渲染之前）调用一次，把锁压回超宽。
    // 默认不做事；失败由实现侧限流告警，不影响其他功能。
    virtual void reassert_aspect_ratio() {}


    // 清除运行期函数查找缓存（GetInputMouseDelta / SetViewTargetWithBlend 等），下次调用重新探测。
    // 配置重载或重新进入摄影模式时调用，避免“函数暂时没就绪就永久标记缺失，只能重启”。
    virtual void refresh_function_caches() {}

    // attach_view() 回读确认连续失败次数是否已达退出阈值。
    // core 每 1 秒重试 attach_view；只有连续多次确认失败才真正 leave，避免一次瞬时抖动就退出。
    virtual bool view_confirmation_failed() const { return false; }

    // UUU 式「接管当前相机」：不 spawn、不切 ViewTarget，直接接管游戏当前相机。
    // 返回 false 表示当前环境接管不了（拿不到相机管理器/CameraCache、写不进 POV），
    // 调用方必须在同一次 enter 内回退到 spawn_camera + attach_view，绝不能因此拒绝进入。
    // 实现内部必须自己保存原始 POV/后处理快照，leave_camera_takeover 时原样写回。
    virtual bool takeover_current_camera(const CameraPose& pose) {
        (void)pose;
        return false;
    }
    virtual bool is_camera_takeover_active() const { return false; }
    virtual bool leave_camera_takeover() { return true; }

    // 接管是否真的生效：实现方必须在「游戏相机更新之后、渲染之前」写 POV，
    // 并在渲染结束点回读当前 POV 验证本帧渲染用的确实是写进去的位姿。
    // 连续若干帧发现 POV 被游戏覆盖时返回 true（接管失效）；core 据此自动回退独立相机。
    virtual bool takeover_view_lost() const { return false; }

    // 画面效果（暗角/饱和/对比/泛光/冷暖）：写进自建的后处理体积。
    // 这条通道在本作 gameplay 视图里实测生效（暗角探针证明），其余项走同一套 bOverride_ 机制。
    virtual bool set_look(const LookOptions& options) {
        (void)options;
        return true;
    }
};

// 景深/对焦/色差：渲染门控 + CineCamera 组件通道（2.1 复活；背景见 core/dof_math.h 与
// core/tuning.h 顶部）。与 set_look 的后处理体积通道是两条独立链路：
//   门控 = NLP 过场渲染档 + tagged cvar（写 ShowFlag.DepthOfField / r.DepthOfFieldQuality /
//          r.Tonemapper.Quality 等，全部回读校验）；
//   画面 = spawn 出的 CineCameraActor 的 CineCameraComponent.PostProcessSettings（权重 1），
//          进门控前先把管理器 POV 的 PostProcessSettings 整份拷过去保留游戏原调色。
class DofPort {
public:
    virtual ~DofPort() = default;

    // 确保渲染门控可用并保持打开。幂等；need_dof / need_film 分别声明要哪组闸门。
    // 返回 false = 门控不可用（游戏没提供 NLP/Tagged cvar 接口，或 CineCamera 类缺失），
    // core 据此拒绝启用并只告警一次；实现侧保证只打一次详细日志。
    virtual bool ensure_dof_gate(bool need_dof, bool need_film) {
        (void)need_dof;
        (void)need_film;
        return false;
    }

    // 释放门控（逆序：解 tagged cvar → 关 NLP 档）。幂等；全部释放干净返回 true，
    // 失败项由 core 按退避重试（与其它恢复步骤同一套语义）。
    virtual bool release_dof_gate() { return true; }

    // bokeh gate cvar 取证开关（2026-11-07 崩溃二分）：false=不写 r.DOF.{Gather,Recombine}.EnableBokehSettings
    // 与 r.DOF.Recombine.Quality——bokeh 路径是崩溃嫌疑首位（shipping 可能无实现，崩溃点=call rdx→全零页）。
    virtual void set_bokeh_gate_enabled(bool enabled) { (void)enabled; }

    // 把景深/胶片参数写进 CineCamera 组件通道。要求 ensure_dof_gate 已成功；值变化时才调用。
    virtual bool apply_dof(const DofParams& params) {
        (void)params;
        return false;
    }

    // 胶片 cvar 闸门（r.SceneColorFringeQuality 等）当前是否已建。首启建控时色差常为 0
    //（0=不覆盖），胶片组会被整块跳过；core 据此判断「胶片闸门还没建」要不要放行补建一次
    //（否则组件 PP 写入成功但引擎质量闸门关着，滑条动了画面没反应）。
    virtual bool film_gate_active() const { return false; }

    // NLP 过场渲染档当前是否开着（2026-11-08 主开关跟随同步用）。档位是全局渲染档：
    // 开着它，游戏原生过场景深（游戏自己的对焦逻辑，随镜头移动）就在画面里，本 mod
    // 参数管不到——所以它必须严格跟随景深主开关，见 apply_dof_if_needed 与 ensure_dof_ready。
    virtual bool nlp_profile_active() const { return false; }

    // NLP 开启方向本会话已失败过一次（接口缺失/类缺失/AV 闩，全是永久性失败）：
    // true = 不再尝试开启。core 的 nlp_owed 早退放行靠它收口，避免每帧解冻重请求。
    // 退出摄影模式（release_dof_gate）清零，重进重新探测。
    virtual bool nlp_enable_exhausted() const { return false; }

    // 释放 NLP 过场渲染档（景深主开关关掉时的每帧同步；幂等，未开时返回 true）。
    // 实现侧自带暂停态解冻窗口（与 release_dof_gate 同一纪律）；失败由 core 按退避重试，
    // 退出路径 release_dof_gate 也会兜底重试。
    virtual bool release_nlp_profile() { return true; }

    // 景深/胶片活动时的每帧钩子（GameViewportClient Tick 之后调用）：原生镜头/视图刷新依赖
    // 暂停期常规相机更新（ForceUpdateCameraView 在本环境已停用，见 ue4ss_port_dof.cpp），
    // 门控延迟回读探针也在这里消化。
    virtual void dof_frame_tick() {}
};


// 输入接管：只动摄影模式需要的键鼠输入，不碰其他系统
class InputPort {
public:
    virtual ~InputPort() = default;

    // 输入屏蔽：只动“移动输入”，不动“视角输入”（否则会连带屏蔽鼠标轴）
    virtual bool set_ignore_move_input(bool ignore) = 0;

    // 彻底吞掉游戏自己的键鼠输入：左右键攻击、交互、技能都不要触发。
    // 做法是给 PC/Pawn 的 InputComponent 打上 bBlockInput，让输入栈在最高优先级处中断；
    // 与 set_ignore_move_input 互补（那个只挡移动轴，挡不住动作绑定）。
    virtual bool set_game_input_blocked(bool blocked) = 0;
};

// HUD 与界面元素：玩家、HUD、交互提示、血条、锁定标记、字幕、对话 Flow
class HudPort {
public:
    virtual ~HudPort() = default;

    virtual bool set_player_hidden(bool hidden) = 0;
    virtual bool set_hud_visible(bool visible) = 0;

    // 交互提示（可互物体旁的 E 键图标）：进入摄影模式时隐藏、退出还原。
    // 它是 PBZInteractComponent 挂的世界空间 UMG：SetHideUIForPV 不覆盖它，
    // 而且暂停下游戏 UI 不再刷新，已经画出来的提示必须隐藏控件组件本身才会消失。
    virtual bool set_interact_prompts_visible(bool visible) = 0;

    // 敌人（怪物）血条：SetHideUIForPV 与 HUD 布局根控件都不覆盖它们（头顶血条不在
    // W_FrontEndHUDLayout 子树下）。游戏侧唯一的总开关是设置项 MonsterHealthBarMode
    // （EPBZMonsterHealthBarMode：AlwaysShow/OnlyLockedTarget/HideFullHealth）——
    // 切到 OnlyLockedTarget 只能压掉「非锁定目标」的血条；**锁定中的那个目标血条照常显示**
    //（1.0.67 实机：原模式本就是 OnlyLockedTarget，切档无效）。锁定目标由
    // set_lockon_marker_visible 按游戏接口清掉，两条路配合才干净。
    virtual bool set_enemy_health_bars_visible(bool visible) = 0;

    // 锁定标记（锁定敌人时其身上出现的白点）与锁定目标状态：SetHideUIForPV 与 HUD
    // 布局都不覆盖它，血条设置也只管血条不管点。1.0.68 起走游戏自己的锁定系统：
    // 找 TargetingComponent（LockWidget / SecondaryLockIndicators / SetShowLockIndicator /
    // HasLockTarget / GetLockTarget / SetLockTarget / StopLockTarget），藏指示器控件 + 关
    // 指示器开关 + 清锁定目标（血条只为锁定目标显示），退出全部成对恢复；另按类名/资产名
    // 关键词全量扫描 UMG 与可视组件兜底，零命中打一次性诊断清单。暂停下游戏 UI 不再刷新，
    // 事件驱动的标记不会自己消失，必须动渲染载体本身（同交互提示的道理）。
    virtual bool set_lockon_marker_visible(bool visible) = 0;

    // 对话字幕：SetHideUIForPV 不管它们。普通字幕走游戏设置开关 bSubtitlesEnabled
    // （SetSubtitlesEnabled）管新字幕 + USubtitleDisplay 控件 Collapsed 管已显示的；
    // 对话/过场中的 WBP_FlowDialog_C 文本改用 Hidden（保留 Tick），且不改字幕设置，
    // 避免当前语句被截断——freeze_mode=pause 下控件 Tick 冻住，只改设置对已显示文本不生效。
    virtual bool set_subtitles_visible(bool visible) = 0;

    // 对话 Flow 暂停：冻结时把 FlowDialog 控件设为 Collapsed（停止 Slate Tick），
    // 解冻恢复 Hidden（仍隐藏但继续 Tick）。返回实际操作控件数；0=当前没有 FlowDialog（不算失败）。
    virtual int set_dialogue_flow_tick_paused(bool paused) = 0;
};

// 时间与暂停：时间缩放、世界暂停、剧情播放器
class TimePausePort {
public:
    virtual ~TimePausePort() = default;

    virtual double get_time_dilation() = 0;
    virtual bool set_time_dilation(double dilation) = 0;
    // dilation 模式的每帧重申（不写日志）：该游戏会把 WorldSettings.TimeDilation
    // 每秒写回 1.0，只在 tick 里每秒重申会让角色在间隙按正常速度继续走。
    virtual bool reassert_time_dilation(double dilation) { (void)dilation; return true; }
    // 时间冻结的显式恢复入口：只回写本 Mod 确实改过的 WorldSettings 字段
    // （靠「写过的字段」位图判定，不再靠数值是否接近原值来猜）。
    // 世界换代后旧快照作废，直接返回 true（旧世界的改动已随关卡一起消失）。
    virtual bool restore_time_dilation(double original) = 0;
    virtual bool set_paused(bool paused) = 0;

    // 世界暂停只读回读（冻结期每秒校验用；剧情等场景游戏可能擅自解除暂停）
    virtual bool world_paused() = 0;

    // 退出摄影模式时调用：清理“处决/过场演出暂停”留给玩家角色的时间与动画残留。
    // 实机症状：处决动画中进入摄影模式再退出后，角色模型定格、只能重启游戏。
    // 只处理玩家 Pawn 及其骨骼网格（不碰场上本来静止的 NPC/场景物件），且必须幂等。
    virtual void recover_character_after_photo_mode() {}

    // 剧情过场（Sequencer）播放器：冻结时一并暂停，解冻/退出时恢复。
    // 剧情系统常常绕过世界暂停与时间缩放自顾自更新（画面像冻住了、动作声音还在走），
    // 必须把 SequencePlayer 单独压住。找不到播放器时返回 found=0，不算失败。
    virtual SequenceFreezeReport pause_sequences(bool paused) = 0;
};

// 音频与 Wwise：对话语音走 Wwise，世界暂停拦不住
class AudioPort {
public:
    virtual ~AudioPort() = default;

    // Wwise 组件静音（mute=true，音量归零，声音继续无声播放、句子回调照常）与
    // 解除（mute=false，音量恢复）。组件类没有音量函数时静音方向回落一次性 Stop。
    // 对话语音真暂停/恢复：优先用钩子截获的 PlayingID 调游戏自有
    // PBZPausePlayingID / PBZResumePlayingID（Wwise 原生暂停，解冻原位置续播）；
    // 没有截获到 ID 时回落组件音量归零/恢复。
    // 返回值 = 实际执行暂停/恢复的 PlayingID 数（0 = 无逐语音 ID，调用方可用整引擎静音兜底）；
    // 不含组件兜底动作数——组件音量只是附带兜底，不能据此跳过整引擎兜底。
    virtual int pause_wwise_voices(bool pause) = 0;

    // Wwise 整引擎静音/唤醒（AK::SoundEngine::Suspend(true,false) /
    // WakeupFromSuspend(0)）。2024.1 下 true=render-anyway：音频继续处理但不输出，
    // 事件队列/语音流状态保留；但语音时间轴仍会推进，已有逐 PlayingID 暂停时不得调用，
    // 仅在 pause_wwise_voices 返回 0（没有截获到 ID）时作为静音兜底。
    virtual bool suspend_wwise_engine(bool suspend) = 0;
};

// 冻结期残余微动治理：世界暂停拦不住的“每帧变化源”逐个掐掉
class FreezeMotionPort {
public:
    virtual ~FreezeMotionPort() = default;

    // 相机管理层：shake/修饰器用真实时间驱动，暂停期间照常推视角——冻结画面边缘
    // 抖动（脸部阴影爬动）的主变化源。冻结=立即结束全部 shake + 停用所有修饰器；
    // 恢复=按冻结前的 bDisabled 状态写回。ok=false 时 core 不记账、解冻不还原。
    virtual CameraShakeFreezeReport freeze_camera_shakes(bool frozen) = 0;

    // 风源：材质顶点偏移（布料/植被摆动）的幅度源，走材质时间/实时路径，暂停拦不住。
    // 冻结=把 Strength/Speed/阵风参数存原值后归零（钟还在走、幅度归零=位移恒零）；
    // 恢复=原值写回。找不到风源返回 found=0，不算失败。
    virtual WindFreezeReport freeze_wind(bool frozen) = 0;

    // 材质参数集合（MPC）钉住：实时驱动代码每帧改写参数集合推 WPO。冻结时抓快照，
    // 冻结期每帧写回（hold，必须每帧赢回写入权），解冻释放。take 返回快照参数数
    // （0=没抓到，core 不进入每帧写回）。
    virtual int mpc_take_snapshot() = 0;
    virtual int mpc_hold_snapshot() = 0;
    virtual void mpc_release_snapshot() = 0;

    // 布料模拟：角色/敌人的裙摆、披风在暂停下仍会动。SkeletalMeshComponent 的
    // clothing simulation 与 UChaosClothComponent 的独立模拟都可能由独立 tick/真实时间驱动，
    // 世界暂停与 TimeDilation 都拦不住；冻结=SuspendClothingSimulation / SuspendSimulation
    // （保留最后一次模拟姿态），恢复=Resume...。原本就处于暂停态的组件不重复处理。
    virtual ClothFreezeReport freeze_cloth(bool frozen) = 0;

    // 冻结期布料闸门（core 约每秒调用一次，仅 cloth 冻结生效期间）：
    // 游戏侧的布料管理按渲染可见性/距离翻转状态，可能在冻结期把布料重新恢复——
    // 活的 Chaos solver 对着被 bPauseAnims/bNoSkeletonUpdate 钉死的骨骼，
    // 飘带会被越拉越远（实机表现：部件离身数米、中间拉成直条，解冻后仍是坏姿态）。
    // 实现：复核冻结时登记的全部布料组件，发现仍在模拟的先重挂；
    // 重挂仍失败的停用其 Tick 兜底（恢复时还原），并把组件名记一次日志。
    // 返回本次发现「冻结期仍在模拟」的组件数（0=全部安分）。
    virtual int cloth_gate_tick() { return 0; }

    // 骨骼动画：世界暂停不一定停住 SkeletalMesh 的 AnimBP/骨骼 pose；角色 cape 若
    // 由骨骼驱动，会继续产生位移。冻结=对全部 SkeletalMesh 写 bPauseAnims=true、
    // bNoSkeletonUpdate=true（记录原值），解冻恢复；默认实现不做事。
    virtual SkeletalAnimationFreezeReport freeze_skeletal_animation(bool frozen) = 0;

    // 动态模糊：静止拍照不需要 motion blur；布料/骨骼的速度缓冲若有残留，
    // motion blur 会把它抹成“流动”拖影。冻结=r.MotionBlurQuality=0，解冻还原。
    virtual bool freeze_motion_blur(bool frozen) = 0;

    // 渲染对照（A/B 诊断）：冻结时按开关临时改渲染 CVar——
    //   disable_aa：r.AntiAliasingMethod=0（关 TSR/TAA，排查时域 AA 历史）
    //   disable_wpo_velocity：r.Velocity.EnableVertexDeformation / EnableLandscapeGrass /
    //                         r.HairStrands.ForceVelocityOutput 归零（排查 WPO 假速度）
    //   disable_framegen：r.PBZ.SuppressFrameGen=1 + 显式关 DLSSG/FSR-FI（排查帧生成）
    //   disable_dlss：r.NGX.DLSS.Enable=0（排查 DLSS 超分；默认 false 保留画质）
    // frozen=false 时按快照还原；默认实现不做事（FakeEngine/其他平台无需实现）。
    virtual RenderTuningReport freeze_render_tuning(bool frozen, bool disable_aa, bool disable_wpo_velocity,
                                                     bool disable_framegen, bool disable_dlss,
                                                     bool disable_tonemapper_dither = false) {
        (void)frozen;
        (void)disable_aa;
        (void)disable_wpo_velocity;
        (void)disable_framegen;
        (void)disable_dlss;
        (void)disable_tonemapper_dither;
        return {};
    }

    // 光照 A/B（Lumen / 屏幕空间光照）：frozen=true 按 options 应用；frozen=false 还原全部光照 CVar。
    // 与 freeze_render_tuning 使用各自的快照表；默认实现不做事（FakeEngine/其他平台无需实现）。
    virtual RenderTuningReport freeze_lighting(bool frozen, const LightingTuningOptions& options) {
        (void)frozen;
        (void)options;
        return {};
    }

    // 光照阶段自动轮换（诊断用）：冻结期每帧调用，enabled=true 时按 phase_seconds 自动
    // 切换「原始→屏幕追踪→短距AO→辐射缓存→固定抖动→Lumen全关」并循环。
    virtual void lighting_ab_tick(double dt, bool enabled, double phase_seconds) {
        (void)dt;
        (void)enabled;
        (void)phase_seconds;
    }

    // 阴影/后处理 A/B：frozen=true 记录状态并进入阶段 0；frozen=false 还原全部阴影/后处理 CVar。
    // 阶段轮换由 shadow_ab_tick 驱动（SMRT→VSM 缓存→VSM→全部阴影→接触阴影→体积雾→后处理）。
    virtual RenderTuningReport freeze_shadow_ab(bool frozen) {
        (void)frozen;
        return {};
    }
    virtual void shadow_ab_tick(double dt, bool enabled, double phase_seconds) {
        (void)dt;
        (void)enabled;
        (void)phase_seconds;
    }

    // 自定义布料/摆动：冻结 PBZ 自己的 SwayComponent tick、r.Cloth.Enable 全局开关、
    // pbz.Sway.SkipSkeletalKinematicBoneUpdate。解冻/退出按原值恢复。
    virtual CustomClothFreezeReport freeze_custom_cloth(bool frozen) {
        (void)frozen;
        return {};
    }

    // WPO 评估冻结（材质顶点动画）：冻结时逐 SkeletalMesh 关闭，解冻恢复。
    virtual WpoFreezeReport freeze_wpo(bool frozen) {
        (void)frozen;
        return {};
    }

    // 冻结期每帧重申渲染对照目标值（游戏可能把 FG/DLSS/AA/tonemapper 改回去）。
    virtual void render_reassert_tick() {}

    // 冻结诊断（每秒一条）：风源状态 + MPC 参数变化（与上一拍比对）+ 相机修饰器状态，
    // 定位布料与阴影闪烁还剩哪个变化源没压住
    virtual std::string freeze_diag_sample() = 0;

    // 冻结期 POV 微动监测（每帧调用，观察一个诊断周期内的最大逐帧偏差）：
    // 读相机管理器算出的最终 POV（ViewTarget.POV）。游戏的自定义相机管理器不挂引擎
    // modifier 数组（实机：PBZPlayerCameraManager 无 CameraModifiers），若它自己的代码
    // 在暂停期仍推视角，这里会出现非零偏差——全画面边缘 shimmer 的残余变化源。
    // camera_idle=false（用户在转相机）时不累计并清空上一帧样本——只测“没人动相机时的微动”
    virtual void pov_watch_tick(bool camera_idle) = 0;
};

// 日志：core 只通过它输出，引擎实现负责按级别过滤后转给 UE4SS 控制台
class LogPort {
public:
    virtual ~LogPort() = default;

    // 日志级别阈值（来自 config.log_level）。默认不做过滤，引擎实现按 log_level_enabled 过滤。
    virtual void set_log_level(LogLevel level) { (void)level; }

    virtual void log(LogLevel level, const std::string& message) = 0;
};

// 聚合接口：Ue4ssPort / FakeEngine 统一实现，mod_app 只在构造 PhotoMode 时使用。
class EnginePort : public WorldPort,
                   public CameraViewPort,
                   public DofPort,
                   public InputPort,
                   public HudPort,
                   public TimePausePort,
                   public AudioPort,
                   public FreezeMotionPort,
                   public LogPort {
public:
    ~EnginePort() override = default;
};

}  // namespace pbzpm
