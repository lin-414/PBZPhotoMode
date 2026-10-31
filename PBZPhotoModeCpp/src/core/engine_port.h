#pragma once

#include <string>

#include "core/config.h"
#include "core/math3.h"

namespace pbzpm {

struct CameraPose {
    Vec3 loc{};
    Rot rot{};
    double fov{90.0};
    bool operator==(const CameraPose& o) const {
        return loc == o.loc && rot == o.rot && fov == o.fov;
    }
};

// 剧情过场播放器（Sequencer/LevelSequence）一次扫描/操作的结果（诊断用）
struct SequenceFreezeReport {
    int found{0};    // 扫到的播放器实例总数
    int matched{0};  // 处于目标状态的数量（暂停时=正在播放；恢复时=处于暂停态）
    int acted{0};    // 实际成功执行暂停/恢复的数量
    bool ok{false};  // 扫描链路是否跑通（false = 反射查询不可用）
};

// 引擎侧的抽象端口。core 只通过它说话，因此状态机可以在没有游戏的情况下单测。
// 实现在 src/engine/ue4ss_port.cpp（C++/UE4SS 反射），测试里用 tests/fake_engine.h。
class EnginePort {
public:
    virtual ~EnginePort() = default;

    virtual bool world_valid() = 0;
    virtual bool read_pov(CameraPose& out) = 0;

    virtual bool spawn_camera(const CameraPose& pose) = 0;
    virtual bool attach_view() = 0;
    virtual bool restore_view() = 0;
    virtual bool apply_pose(const CameraPose& pose) = 0;
    virtual bool apply_fov(double fov) = 0;
    virtual bool destroy_camera() = 0;

    // 输入屏蔽：只动“移动输入”，不动“视角输入”（否则会连带屏蔽鼠标轴）
    virtual bool set_ignore_move_input(bool ignore) = 0;

    // 彻底吞掉游戏自己的键鼠输入：左右键攻击、交互、技能都不要触发。
    // 做法是给 PC/Pawn 的 InputComponent 打上 bBlockInput，让输入栈在最高优先级处中断；
    // 与 set_ignore_move_input 互补（那个只挡移动轴，挡不住动作绑定）。
    virtual bool set_game_input_blocked(bool blocked) = 0;

    virtual bool set_player_hidden(bool hidden) = 0;
    virtual bool set_hud_visible(bool visible) = 0;

    // 交互提示（可互物体旁的 E 键图标）：进入摄影模式时隐藏、退出还原。
    // 它是 PBZInteractComponent 挂的世界空间 UMG：SetHideUIForPV 不覆盖它，
    // 而且暂停下游戏 UI 不再刷新，已经画出来的提示必须隐藏控件组件本身才会消失。
    virtual bool set_interact_prompts_visible(bool visible) = 0;

    // 敌人（怪物）血条：SetHideUIForPV 与 HUD 布局根控件都不覆盖它们（头顶血条不在
    // W_FrontEndHUDLayout 子树下）。游戏侧唯一的总开关是设置项 MonsterHealthBarMode
    // （EPBZMonsterHealthBarMode：AlwaysShow/OnlyLockedTarget/HideFullHealth）——
    // 摄影模式下没有锁定目标，切到 OnlyLockedTarget 即全部隐藏；进入时保存原值，退出还原。
    virtual bool set_enemy_health_bars_visible(bool visible) = 0;

    // 对话字幕：SetHideUIForPV 不管它们。双路径：游戏设置开关 bSubtitlesEnabled
    // （SetSubtitlesEnabled，GameUserSettings 家族）管住之后新出的字幕；GameSubtitles
    // 插件的 USubtitleDisplay 控件直接 Collapsed 管住此刻已经画出来的——
    // freeze_mode=pause 下控件 Tick 冻住，光改设置对已显示的字幕不会立即生效。
    virtual bool set_subtitles_visible(bool visible) = 0;

    // 超宽屏（21:9/32:9）支持：游戏把画幅锁在 16:9，超宽屏两侧补黑边。
    // 进入摄影模式时解除锁定（实现内部保存原值），退出时按进入前的状态还原。
    // 失败只告警不阻断进模式（游戏可能根本没锁，或属性不在预期位置）。
    virtual bool unlock_aspect_ratio() = 0;
    virtual void restore_aspect_ratio() = 0;

    virtual double get_time_dilation() = 0;
    virtual bool set_time_dilation(double dilation) = 0;
    virtual bool set_paused(bool paused) = 0;

    // 世界暂停只读回读（冻结期每秒校验用；剧情等场景游戏可能擅自解除暂停）
    virtual bool world_paused() = 0;

    // 剧情过场（Sequencer）播放器：冻结时一并暂停，解冻/退出时恢复。
    // 剧情系统常常绕过世界暂停与时间缩放自顾自更新（画面像冻住了、动作声音还在走），
    // 必须把 SequencePlayer 单独压住。找不到播放器时返回 found=0，不算失败。
    virtual SequenceFreezeReport pause_sequences(bool paused) = 0;

    // Wwise 音频组件数量（对话语音走 Wwise 时世界暂停拦不住，用于诊断）
    virtual int wwise_component_count() = 0;
    // Wwise 组件静音（mute=true，音量归零，声音继续无声播放、句子回调照常）与
    // 解除（mute=false，音量恢复）。组件类没有音量函数时静音方向回落一次性 Stop。
    // 对话语音真暂停/恢复：优先用钩子截获的 PlayingID 调游戏自有
    // PBZPausePlayingID / PBZResumePlayingID（Wwise 原生暂停，解冻原位置续播）；
    // 没有截获到 ID 时回落组件音量归零/恢复。返回执行的调用量。
    virtual int pause_wwise_voices(bool pause) = 0;

    virtual bool save_postfx() = 0;
    virtual void restore_postfx() = 0;

    virtual void log(LogLevel level, const std::string& message) = 0;
};

}  // namespace pbzpm
