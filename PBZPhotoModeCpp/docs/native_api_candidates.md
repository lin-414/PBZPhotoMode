# 游戏原生 API 候选对照表

数据来源：PBZModLoader 仓库（github.com/zoneoflkf/PBZModLoader）附带的游戏反射全量 SDK，
快照构建 `CL-302256_JKS-17280`（2026-11-04，EXE SHA256 见其 `scripts/REFLECTION.md`）。
本地克隆：`D:\Download\pbzml-review\PBZModLoader`（查询入口 `sdk/type-index.json` 与
`templates/ts-mod/vendor/game-api/types/generated/*.d.ts`）。

使用纪律：

1. **只作查阅参考，不拷代码**——该仓库整体 GPL-3.0，任何代码进入本仓库即传染许可；反射事实本身不受版权约束。
2. **adopt-on-pain**：已实机通过的实现不因发现新 API 而重构；候选只在回归失败、新功能、断裂适配时启用。
3. 每条「待验证」必须实机验证后回填状态列；SDK 是构建快照，游戏更新后整表需对照新导出复核。
4. 状态取值：`待验证` / `采用评估` / `已采用` / `已评估·不适用` / `参考` / `储备`。

## 已采用锚点（证明这条管线可用）

| 域 | API | 采用处 |
| --- | --- | --- |
| 景深渲染档 | `PBZLevelSequenceManager.SetNLPProfileCinematicEnable`（瞬态序列作所有权标记） | 工作区 2026-11-07 批次 |
| Tagged cvar | `PBZBlueprintFunctionLibrary.SetConsoleVariableTagged` / `UnsetConsoleVariablesWithTag` / `GetConsoleVariableFloatValue` | 同上（DOF/胶片 cvar 组） |
| 相机刷新 | `PBZPlayerController.ForceUpdateCameraView()` | 同上（DOF 活动期每帧） |
| 字幕/提示开关 | `bEnableSubtitles` / `bSceneInteractPromptVisible` 属性（getter `GetSubtitlesEnabled` 等，ProjectPBZ.d.ts:14930 起） | 1.0.34 取证、1.0.71 收口 |
| 音频挂起 | `Suspend/WakeupFromSuspend`（PBZ.exe 静态链接导出）+ `PBZPausePlayingID` 逐 ID 兜底 | 1.0.66 / 1.0.36 双保险 |
| 时间倍率语义 | `AWorldSettings.TimeDilation / CinematicTimeDilation` + `<0.1` 视为演出瞬时值 | 2.0.2① |

## 候选表

| # | 域 | 本模组落点 | 候选 / 相关 API | 出处 | 说明与假设 | 验证 | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 画幅/超宽屏 | camera_view + `reassert_aspect_ratio_if_needed()` | `PBZPlayerCameraManager.SetIgnoreCameraAspectRatioConstraint(bool)` | ProjectPBZ.d.ts:13200 | 游戏自己的约束忽略开关，与 `bDefaultConstrainAspectRatio` 同源（约束住在 CameraManager 上）；若成立可评估替代每帧重申 | 回归 T1 | 待验证 |
| 2 | 暂停中相机 | time_pause / photo_mode | `PBZPlayerController.SetCameraMoveableWhenPaused(bool)`、`IsPaused()`、`OnGamePausedChanged` 委托 | ProjectPBZ.d.ts:13232 | 原生"暂停中相机可动"；若成立可评估简化自研视角守护/暂停状态机 | 回归 T2 | 待验证 |
| 3 | 相机模式 | camera_view（接管） | `NBCamera.NBCameraComponent.OverrideCameraMode / SetCameraTarget / NotifyTeleported`（继承 ATPCCameraComponent） | NBCamera.d.ts | 游戏第三人称相机模式通道；接管/传送边界场景的备选杠杆，现行接管稳定故不动 | — | 参考 |
| 4 | HUD 显隐 | hud_ui（逐控件隐藏） | `PBZBlueprintFunctionLibrary.AddHudShowModeTag / RemoveHudShowModeTag(Tag)` | ProjectPBZ.d.ts:7371 / 7567 | 游戏原生 HUD 显隐通道；替代逐控件隐藏的前提是先枚举有效 GameplayTag 集 | 待定（先摸 Tag 集，本轮不做） | 待验证 |
| 5 | 输入模式 | 进入摄影模式的输入抑制 | `PBZBlueprintFunctionLibrary.AddInputModeTag / RemoveInputModeTag(ContextObject, Tag, Imposer)` | ProjectPBZ.d.ts:7372 / 7568 | 原生输入模式叠加通道（带 Imposer 所有权语义）；与 #4 同场考察 | 与 #4 同场 | 参考 |
| 6 | 字幕子系统 | hud_ui 字幕压制 | `GameSubtitles.SubtitleDisplaySubsystem`（GameInstanceSubsystem，仅 `SubtitleFormat` 成员） | GameSubtitles.d.ts | 成员极少；实机证据表明游戏字幕走 Slate 控件（WBP_FlowDialog_C），子系统通道大概率不适用 | — | 已评估·不适用（记录避免重复挖） |
| 7 | 声音淡出 | audio（进出过渡） | `PBZBlueprintFunctionLibrary.FadeOutAllSound / FadeInAllSound(ContextObject, 秒)` | ProjectPBZ.d.ts | 进出摄影模式的柔和过渡候选；与现行 Wwise Suspend 双保险并行观察，不替换 | 回归 T4 | 待验证（可选） |
| 8 | 冻结微动 | micro_motion | `PBZBlueprintFunctionLibrary.ClearSkeletalMeshMotionVector(Mesh, bResetSceneVelocity)`、`EnableClothTick(bEnable, SMC)` | ProjectPBZ.d.ts | 冻结期微动/布料残留的候选治理手段 | 回归 T5 | 待验证 |
| 9 | HitStop 范式 | process_freeze（F7 定格） | `PBZHitReactionComponent.FreezeFrameBegin(TimeDilation, DurationTime, DilationCurve, BlockTime) / FreezeFrameEnd()` | ProjectPBZ.d.ts:10587 | 战斗 hitstop 通道，非全画面定格；仅作游戏「冻结+曲线+时长+阻塞窗」范式参考 | — | 参考（暂不采用） |
| 10 | 世界流送 | World / 传送边界 | `PBZPlayerController.CloseWorldPartitionStreamingSource(b)` / `TempCloseWorldPartitionStreamingSource` 属性 | ProjectPBZ.d.ts:13209 起 | 流送源关闭开关；与 commandlet/流送类坑位备查 | — | 参考 |
| 11 | 手柄数据模型 | gamepad 核心层 | `CommonInput.CommonInputBaseControllerData`、`PBZGamepadNameLibrary` | CommonInput.d.ts / ProjectPBZ | 只影响游戏内按键图标显示；DInput8 轮询后端与其无关，不需要适配 | — | 参考 |
| 12 | 拍照系统 | 未来功能 | `TakePhotoSystem / TakePhotoAsset / TakePhotoWidget`、`FindPhotoStudioBPClassForFlow / …ForSequence(DialogueFlowAsset)` | ProjectPBZ.d.ts | 游戏自带影棚蓝图存在；内容未摸清，等需求触发再挖 | — | 储备 |
| 13 | 曝光 | 未来功能（564 已实证可行） | `PBZAutoExposureBlueprintLibrary.SetAutoExposureListeningEnabled / GetCurrentAutoExposureValue / SetStableTime` 等 | ProjectPBZ.d.ts | 曝光锁定类功能的原生通道；当前功能面不含曝光，仅储备 | — | 储备 |

## 维护

- 每次回归批次结束后回填「状态」「验证」两列；新增行注明发现来源（SDK 版本或实机取证）。
- 游戏更新后：对照新 SDK 快照逐行确认 API 仍在、签名未变，失配行降级为「待验证」并注明构建。
