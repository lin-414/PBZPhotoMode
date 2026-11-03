// 跨模块共用的调参常量：集中一处，避免同一数值在 core / 引擎适配层各写一份。
// 只做集中，不改数值与语义；新增常量优先放这里，并保留原有注释说明。
#pragma once

namespace pbzpm::tuning {

// 引擎 GetInputMouseDelta 给的是「像素 ÷ 游戏鼠标灵敏度」，量级只有 0.1~0.2；
// 乘这个系数放回像素量级，再交给 core 的 look_sensitivity（度/单位）换算成角度。
inline constexpr double kEngineMouseScale = 40.0;

// 方向键 / 手柄十字键上下的俯仰速度（度/秒）。
// 注意这与 config 的 pitch_speed 默认值独立：这里是不读配置时的固定积分步长。
inline constexpr double kPitchSpeedDegPerSec = 30.0;

// 单帧 dt 上限：掉帧 / 切窗口回来时避免相机瞬移，也避免把多次重试压成一帧。
inline constexpr double kMaxFrameDt = 0.25;

// 延迟隐藏类设置（HUD / 交互提示 / 敌人血条 / 锁定标记 / 字幕）的每秒重试上限。
// 重试本身要全对象扫描，拖帧比隐藏失败更伤体验。
inline constexpr int kMaxVisibilityRetries = 10;

// 退出后恢复失败的有限重试：1s → 2s → 4s → 8s → 8s，最多 5 次。
// 恢复动作要动反射/全对象扫描，不能每帧重压；5 次仍失败说明环境已坏（换图/GC/接口缺失），
// 继续重试只会拖帧，保留 Error 日志交给玩家重启处理。
inline constexpr int kMaxRecoveryAttempts = 5;
inline constexpr double kRecoveryMaxDelaySec = 8.0;

// 冻结诊断日志持续期上限：状态没翻转时每 10 秒最多一条（诊断要做反射扫描，不能每秒都打）
inline constexpr double kFreezeDiagIntervalSec = 10.0;

// 对话语音钩子重试上限（每 2 秒一次，300 次 = 10 分钟）：每次重试做两遍全对象数组扫描，
// 挂不上就说明环境有问题，无限重试等于周期性卡顿。
inline constexpr int kMaxVoiceHookRetries = 300;

}  // namespace pbzpm::tuning
