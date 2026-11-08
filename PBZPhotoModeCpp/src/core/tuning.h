// 跨模块共用的调参常量：集中一处，避免同一数值在 core / 引擎适配层各写一份。
// 只做集中，不改数值与语义；新增常量优先放这里，并保留原有注释说明。
#pragma once

namespace pbzpm::tuning {

// ---- 引擎景深：取证结论 + 2.1 轮复活路径 ----
// 【旧结论（hotfix2~32，仍然成立）】gameplay 渲染档位里没有 DOF pass——四条后处理通道实测
//（每条都写入成功、回读正确，画面零变化）：
//   ① 相机 CameraCache.POV 的 PostProcessSettings
//   ② 自建的无界 PostProcessVolume（priority=10000）
//   ③ 关卡自带的 PostProcessVolume（一定在 UWorld::PostProcessVolumes 里）
//   ④ APlayerCameraManager::ViewTarget.POV（每帧重申）
// 用 fstop=0.1 / 对焦 300cm / 传感器 200mm（≈88px）写入后画面依旧完全不糊，而同一体积上的
// VignetteIntensity 明确生效 —— 说明不是值/通道/覆盖问题，而是这个视图里根本没有 DOF pass。
// 伪虚化（径向模糊替代方案）也失败：40 个后处理体积的 WeightedBlendables 全为空。
//
// 【2.1 复活（2026-11，由 Soul's Cinematic Studio 的 JS 实现在同构建上实证）】当年缺的不是
// 通道而是「渲染档位开关」，共两把，都在游戏自己的 BP 库里：
//   ① PBZLevelSequenceManager.SetNLPProfileCinematicEnable(token, true)——游戏的「过场渲染档」
//     （NLP profile），传瞬态 LevelSequence 作所有权标记（不播放）。DOF/SSS 等 pass 只在该
//     档位下存在；GetActiveSequenceNLPProfileName() 可回读档位名。
//   ② PBZBlueprintFunctionLibrary.SetConsoleVariableTagged / UnsetConsoleVariablesWithTag——
//     游戏自带的「带标签 cvar 覆盖」。能写 ShowFlag.DepthOfField=1（当年反射拿不到的那把
//     总开关）、r.DepthOfFieldQuality 等全部 DOF 闸门，且不受 console 命令不执行的限制；
//     每次写入都必须用 KismetSystemLibrary 的 GetConsoleVariable*Value 回读校验。
// 渲染通道：spawn 出的 CineCameraActor 设为 ViewTarget，景深字段写其 CineCameraComponent 的
// PostProcessSettings（SetPostProcessBlendWeight=1），并把管理器 POV 的 PostProcessSettings
// 整份拷过去保留游戏原调色；参数变化后调 PlayerController::ForceUpdateCameraView。
// 引擎实现：engine/ue4ss_port_dof.cpp；参数与纯换算：core/dof_math.h。旧四通道结论保留作历史：
// 在 gameplay 档位下写后处理确实换不来景深，不要再假设是参数问题。
//
// 【2026-11-08：NLP 档并入景深总开关（用户决策，原 [dof] dof_nlp_enable 键登记 Deprecated）】
// 当年留给独立取证开关的假设「仅靠 ShowFlag/画质闸门 DOF 可能就能渲染」被当日实机证伪：
// gates-only 无任何景深画面（与旧四通道结论自洽——pass 只在档位下存在）。同日另一实锤：
// 档位是全局渲染档，开着它游戏原生过场景深（游戏自己的对焦逻辑、随镜头移动）就在画面里、
// 本 mod 参数管不到——曾因胶片（色差）建控顺手开档造成「总开关关着却有景深」，已修为档位
// 严格随 need_dof（总开关）请求/释放。安全网不变：开启失败一次 exhausted 闩（重进重新探测）、
// AV 闩、解冻窗口纪律、退出兜底释放。bokeh_gate 取证开关保留（只影响散景形状，不产生误导状态）。

// ---- 景深/对焦参数（[dof] 段；距离单位 cm，沿用旧版语义）----
inline constexpr double kDofDefaultFocalCm = 300.0;
inline constexpr double kDofFocalMinCm = 5.0;        // 0.05m（564 同款下限）
inline constexpr double kDofFocalMaxCm = 100000.0;   // 1000m
inline constexpr double kDofFstopDefault = 2.8;
inline constexpr double kDofFstopMin = 0.1;
inline constexpr double kDofFstopMax = 32.0;
inline constexpr double kDofSensorDefaultMm = 36.0;
inline constexpr double kDofSensorMinMm = 4.0;
inline constexpr double kDofSensorMaxMm = 200.0;
inline constexpr double kDofFocusStepDefaultCm = 25.0;
inline constexpr double kDofFocusStepMinCm = 1.0;
inline constexpr double kDofFocusStepMaxCm = 5000.0;
// 对焦比例步进下限：实际步进 = max(配置步进, 当前距离 × 10%)（hotfix31 语义：最小步进），
// 远距离 10 步内可追上目标，近距离保持配置步进的精确。
inline constexpr double kDofFocusStepProportion = 0.1;
inline constexpr double kDofApertureStepDefault = 0.1;
inline constexpr double kDofApertureStepMin = 0.01;
inline constexpr double kDofApertureStepMax = 8.0;
// apply_dof 组件写入失败后的重试退避（秒）：SEH 拦下的写入路径不每帧重入，到期再试最新参数
inline constexpr double kDofApplyRetryBackoffSec = 0.5;

// ---- 色差（[look] 段；走 CineCamera 组件通道 + 胶片闸门。胶片颗粒同轮实测无画面变化，已下线）----
inline constexpr double kChromaticAberrationMax = 5.0;

// ---- 渲染闸门目标值（与 Soul's Cinematic Studio 实测值一致；写入后必须回读校验）----
inline constexpr int kDofGateShowFlag = 1;               // ShowFlag.DepthOfField（仅当当前≠1 时写）
inline constexpr int kDofGateQuality = 2;                // r.DepthOfFieldQuality（仅当 ≤0 时写；2 继承视口 flag）
inline constexpr double kDofGateKernelRadius = 0.025;    // r.DOF.Kernel.MaxForeground/BackgroundRadius（≤0 时写）
inline constexpr int kDofGateBokehSettings = 1;          // r.DOF.Gather/Recombine.EnableBokehSettings
inline constexpr int kDofGateRecombineQuality = 2;       // r.DOF.Recombine.Quality（低于时写）
inline constexpr int kFilmGateTonemapperQuality = 5;     // r.Tonemapper.Quality（低于时写；色差 pass 的档位）
inline constexpr int kFilmGateLutSize = 64;              // r.LUT.Size（低于时写）
inline constexpr int kFilmGateDitherOverride = 8;        // r.BackbufferQuantizationDitheringOverride（仅 SDR）

// ---- 锐化 / 超采样：tagged cvar 通道整线判死（2026-11-08 三轮实机终局，通道与键已移除）----
// 通道曾 = PBZBlueprintFunctionLibrary.SetConsoleVariableTagged（胶片闸门同款），摄影模式内
// 常驻覆盖 r.Tonemapper.Sharpness / r.ScreenPercentage。三轮实机裁决（UE4SS.log 13:23 / 14:16 /
// 14:32 三个会话）：
//   【锐化】tagged 写入全部「写入未生效（当前=0 目标=0.25~0.83）」——GetConsoleVariableFloatValue
//     恒回 0（cvar 不存在或被游戏从内部状态钉死），与 FG 抑制判死同型。
//   【超采样】写入有效（290% 回读 verified、tag 持续顶住游戏动态分辨率基线 66.7），但
//     **画面无可见增益**——本作的最终上采样/DRS 管线不消费该 cvar 的有效值；写入有效 ≠ 有
//     画面价值，判死。
// 【通道学费（游戏更新后重启取证时照抄）】
//   ① 模型必须「写一次靠 tag 顶住」（DOF 闸门同模型）——渲染对照的「每帧回读+漂移重写」
//     在动态分辨率基线（66.7）上 = 每帧 66.7↔目标拉锯 = 全场景渲染目标逐帧重分配，实机亚 1 帧。
//   ② UnsetConsoleVariablesWithTag 对模组 tag **确定性无效**（Set 参数命中/SEH 无异常/写入
//     verified，但 Unset 跨会话每次解不掉、Has 恒真）——释放只能「同一 tag 写回快照原值 +
//     回读校验」（neutralize），tag 留在注册表里复用，别再指望解绑。
//   ③ 动态分辨率基线即原值快照，写入前必须先读（66.7 非常量）。

// ---- 暗角（后处理体积通道，已实测本作 gameplay 视图里生效）----
// 探针第 2 段就是往自建体积写 VignetteIntensity=3，用户看到「四周变暗」，这条链是可用的。
constexpr double kVignetteMin = 0.0;
constexpr double kVignetteMax = 4.0;
constexpr double kVignetteDefault = 0.0;

// ---- 调色·进阶（[look] 段；后处理体积通道，与饱和/对比同一 ColorGrading LUT pass）----
// 2026-11-08 批次先上 13 项、同日实机探针后收缩为 3 项。存活的只有 ColorGrading LUT 的
// 每通道经典参数三件（增益/伽马/黑位），与已验证的饱和/对比同族——本 build 的分级管线
// 只消费这五件（饱和/对比/伽马/增益/偏移）。
//
// 【判死记录（2026-11-08 实机，UE4SS.log 01:02~01:06 会话）】
// ① 反射层字段不存在（写入时 note_missing 一次性 Warn，日志 884 行）：
//    ColorCorrectionHighlights / ColorCorrectionMidtones / ColorCorrectionShadows、BloomSoftKnee。
// ② 字段存在、写入成功、回读正确、游戏镜头 POV 全部 覆盖0（排除压制），但画面零变化：
//    FilmSlope（斜率 0.45~2.0 共 975 次覆盖1 写入）/ FilmToe（912 次）/ FilmShoulder（877 次）/
//    WhiteTemp（2199~12693K 双向 833 次）/ WhiteTint（774 次）/ BloomThreshold（-0.9~7.9 共 691 次，
//    7.9 已足够掐灭一切 bloom 仍无变化）。结论：本 build 的 tonemapper/LUT 烘焙不消费胶片曲线、
//    白平衡与泛光阈值参数（shipping 裁剪，与 DOF pass 判死同型：写入成功 ≠ 引擎肯画）。
//    当日按 hotfix34 先例整线移除，游戏更新后可按本记录重启取证。
// 【黑位「发白」排查史】preview1 实机：正偏移发白（当时误判为抬黑发灰的物理行为，范围收到
// ±0.2）；preview2 实机复验：**正负两方向都发白**——方向无关 → 指向写入端常量而非值本身。
// 头号嫌疑已修（preview3）：set_vector 曾对所有 FVector4 恒写 W=1.0，而 ColorOffset 是加算
// 字段、引擎默认 (0,0,0,0)，激活时写进 (v,v,v,1) 的那个常量 W 正是双向发白的候选根因；
// 现加算字段写 W=0、失活写回 (0,0,0,0)。若 preview3 仍双向发白，则游戏自定义烘焙对偏移
// 另有滥用（如 abs()），届时黑位判死、调色·进阶收成两件（增益/伽马）。
inline constexpr double kColorGainMin = 0.0;        // ColorGain（FVector4，RGB 等值，A 恒 1）
inline constexpr double kColorGainMax = 4.0;
inline constexpr double kColorGainDefault = 1.0;    // 中性=1（恒等乘算）
inline constexpr double kColorGammaMin = 0.25;      // ColorGamma
inline constexpr double kColorGammaMax = 4.0;
inline constexpr double kColorGammaDefault = 1.0;
inline constexpr double kColorOffsetMin = -0.2;     // ColorOffset（黑位；-压黑 / +提灰）
inline constexpr double kColorOffsetMax = 0.2;
inline constexpr double kColorOffsetDefault = 0.0;

// ---- 色差 / 胶片颗粒：旧判死结论 + 2.1 复活与再下线（当日终态：仅色差保留）----
// 【旧结论（hotfix33/34，当时成立）】同一个自建体积上，暗角 / 泛光 / 冷暖 / 饱和度 / 对比度
// 都实测生效，唯独这两项不行：它们的 pass 由引擎的「色调映射器档位」管辖，而本作把那几个
// cvar 钉在低档：r.Tonemapper.Quality=2、r.SceneColorFringeQuality=0、
// r.Tonemapper.GrainQuantization=0（BaseScalability.ini 的 PostProcessQuality@0/@1）。
// 当时试过的路全部失败：
//   · UGameUserSettings::SetPostProcessingQuality（ECVF_SetBy 优先级低于钉住它们的 ini，被丢弃）；
//   · console 命令在本作不执行；UE4SS 头文件没有 IConsoleManager；
//   · 补 FilmGrainTexture 噪声纹理 + 强度 6.85 仍无颗粒。
// 【2.1 复活（仅色差）】当年缺的仍是「写入通道」：PBZBlueprintFunctionLibrary.SetConsoleVariableTagged
// 是游戏自带的带标签 cvar 覆盖，不受上面三条限制；Soul's Cinematic Studio 在同构建上用它把
// r.Tonemapper.Quality 抬到 5（另抬 r.LUT.Size、SDR 下抬回环抖动）。本作走同一机制 + CineCamera
// 组件通道写 SceneFringeIntensity，闸门目标值见上方 kFilmGate* 常量；每次写入回读校验。
// 【颗粒再下线（同日用户决策）】色差复活后，颗粒走同通道 + GrainQuantization 闸门实测仍是
// 画面零变化（组件 FilmGrainIntensity 全写成功）——第二次证伪后整线移除，不再保留配置键与写入。


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
