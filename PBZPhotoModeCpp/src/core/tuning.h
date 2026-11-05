// 跨模块共用的调参常量：集中一处，避免同一数值在 core / 引擎适配层各写一份。
// 只做集中，不改数值与语义；新增常量优先放这里，并保留原有注释说明。
#pragma once

namespace pbzpm::tuning {

// ---- 本作不支持引擎景深：取证结论（代码已收口，不要再走这条路）----
// 引擎景深在这款游戏的 gameplay 视图里不渲染——四条通道实测（每条都写入成功、回读正确）：
//   ① 相机 CameraCache.POV 的 PostProcessSettings
//   ② 自建的无界 PostProcessVolume（priority=10000）
//   ③ 关卡自带的 PostProcessVolume（一定在 UWorld::PostProcessVolumes 里）
//   ④ APlayerCameraManager::ViewTarget.POV（每帧重申）
// 用 fstop=0.1 / 对焦 300cm / 传感器 200mm（≈88px）写入后画面依旧完全不糊，而同一体积上的
// VignetteIntensity 明确生效 —— 说明不是值/通道/覆盖问题，而是这个视图里根本没有 DOF pass。
// 剩下的唯一开关 View.Family->EngineShowFlags.DepthOfField 在反射层拿不到：
//   - console 命令在本作完全不执行（Kismet 与直连 PlayerController::ConsoleCommand 都改不动 cvar）；
//   - 0xB8 那块内存整局一位都不变，已确认不是真的 FEngineShowFlags。
// 伪虚化（径向模糊替代方案）也试过并失败：40 个后处理体积的 WeightedBlendables 全为空，
// 自建槽位时 FWeightedBlendables.Array 取址失败，材质挂不上去。
//
// 因此整套景深设施（四条通道、ShowFlags 采样、console 闸门、探针、材质普查、MPC 参数 dump、
// 以及「参数记录」的写入路径）已全部删除：它们只会每帧改游戏对象却换不来任何画面变化。
// 菜单滑条、[dof] 配置段、对焦/光圈按键一并移除（hotfix32）。若日后真能找到打开引擎 DOF 的办法，
// 再按上面的通道重新验证，不要再假设是参数问题。

// ---- 暗角（后处理体积通道，已实测本作 gameplay 视图里生效）----
// 探针第 2 段就是往自建体积写 VignetteIntensity=3，用户看到「四周变暗」，这条链是可用的。
constexpr double kVignetteMin = 0.0;
constexpr double kVignetteMax = 4.0;
constexpr double kVignetteDefault = 0.0;

// ---- 本作不支持色差 / 胶片颗粒：取证结论（滑条与配置键已删，不要再加回来）----
// 同一个自建体积上，暗角 / 泛光 / 冷暖 / 饱和度 / 对比度 都实测生效，唯独这两项不行，
// 因为它们的 pass 由引擎的「色调映射器档位」管辖，而本作把那几个 cvar 钉在低档：
//   r.Tonemapper.Quality=2、r.SceneColorFringeQuality=0、r.Tonemapper.GrainQuantization=0
//（BaseScalability.ini 里 PostProcessQuality@0/@1 正是这三个值；@2/@3/@Cine 才是 1 / 5 / 1）
// 试过并失败的路：
//   · 抬「游戏后处理质量」：UGameUserSettings::SetPostProcessingQuality(3) +
//     ApplyNonResolutionSettings → Scalability::SetQualityLevels，回读 cvar 一个都没变 ——
//     UE5 的 cvar 带 ECVF_SetBy* 优先级，Scalability(0x01)/GameSetting(0x02) 都比钉住它们的
//     ConsoleVariablesIni(0x06)/DeviceProfile(0x05) 低，这种写入会被直接丢弃；
//   · console 命令在本作不执行（Kismet 与直连 PlayerController::ConsoleCommand 回读都不变）；
//   · UE4SS 头文件里没有 IConsoleManager（核对过），反射层拿不到 cvar 对象；
//   · 胶片颗粒还额外试过补噪声纹理：FilmGrainTexture 挂上
//     /Engine/EngineMaterials/Good64x64TilingNoiseHighFreq + TexelSize=1，r.FilmGrain=1、
//     强度拉到 6.85，画面里一粒颗粒都没有。
// 结论：这两项在 Mod 侧没有合法开关。滑条、[look] 配置键、四语言标签、引擎写入已全部移除
//（hotfix34），旧 config.ini 残留这两项时按 kLegacyLookKeys 合并成一条提示后忽略。


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
