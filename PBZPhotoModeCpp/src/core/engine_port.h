#pragma once

#include <initializer_list>
#include <string>

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

    // 清除运行期函数查找缓存（GetInputMouseDelta / SetViewTargetWithBlend 等），下次调用重新探测。
    // 配置重载或重新进入摄影模式时调用，避免“函数暂时没就绪就永久标记缺失，只能重启”。
    virtual void refresh_function_caches() {}

    // attach_view() 回读确认连续失败次数是否已达退出阈值。
    // core 每 1 秒重试 attach_view；只有连续多次确认失败才真正 leave，避免一次瞬时抖动就退出。
    virtual bool view_confirmation_failed() const { return false; }
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
    // 时间冻结的显式恢复入口：只回写本 Mod 确实改过的 WorldSettings 字段
    // （靠「写过的字段」位图判定，不再靠数值是否接近原值来猜）。
    // 世界换代后旧快照作废，直接返回 true（旧世界的改动已随关卡一起消失）。
    virtual bool restore_time_dilation(double original) = 0;
    virtual bool set_paused(bool paused) = 0;

    // 世界暂停只读回读（冻结期每秒校验用；剧情等场景游戏可能擅自解除暂停）
    virtual bool world_paused() = 0;

    // 剧情过场（Sequencer）播放器：冻结时一并暂停，解冻/退出时恢复。
    // 剧情系统常常绕过世界暂停与时间缩放自顾自更新（画面像冻住了、动作声音还在走），
    // 必须把 SequencePlayer 单独压住。找不到播放器时返回 found=0，不算失败。
    virtual SequenceFreezeReport pause_sequences(bool paused) = 0;
};

// 音频与 Wwise：对话语音走 Wwise，世界暂停拦不住
class AudioPort {
public:
    virtual ~AudioPort() = default;

    // Wwise 音频组件数量（诊断用）
    virtual int wwise_component_count() = 0;
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
