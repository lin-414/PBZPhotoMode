# 冻结重影/闪烁：渲染管线侧机制审计（TAA/TSR · MotionBlur · FrameGeneration · WPO/Niagara/Skeletal/Cloth）

> 任务：task-2《审计渲染侧：TAA/TSR/运动模糊/帧生成导致的重影与闪烁机制》
> 日期：2026-11-03　分析对象：影之刃零（PBZ / Phantom Blade Zero，UE 5.7 Shipping）
> 方法：只读代码/文档/安装目录；不改源码、不 build。结论按「证据 → 推断 → 置信度」标注。

---

## 0. 结论先行（TL;DR）

用户描述的「冻结处于运动状态的物体 → 物体出现重影、闪烁」，在渲染侧不是一个单因问题，而是**至少三层叠加**：

1. **TSR/TAA 的时间历史在「匀速运动 → 突然暂停」瞬间必然产生一次「双影/拖影」**。这不是 bug，是任何带 jitter + history 的时域 AA 的正常收敛过程：停止后的前 **8–16 帧**（60fps 约 0.13–0.27s）历史权重里仍保留旧位置内容。**如果只在冻结瞬间看到半秒内衰减的鬼影，属于这一层，可不用修。**
2. **如果鬼影持续 >1 秒、相机完全不动也不消失，最可能是 UE5.6+ 的「WPO / Nanite WPO / Groom（HairCards）/ GPU Skin / Niagara 顶点工厂上一帧时间/顶点缓存不是 pause-aware」引擎缺陷**：这些顶点工厂在暂停后仍把「上一帧时间/上一帧顶点位置」当成仍在推进，向 TSR 输出**假运动向量**，TSR 依此错误重投影 → 持续 motion-blur 样拖影；**关 MotionBlur（r.MotionBlurQuality=0 / r.MotionBlur.Amount=0）也不会消失；DLSS 因自带 optical flow 校正通常不受影响**。Epic 官方论坛 2025-09 帖已确认并开内部 JIRA（见 §2.1 与参考文献）。
3. **`UWorld::bIsCameraMoveableWhenPaused` 在本仓库从未被设置**。Tom Looman 的经典做法要求同时设置 `PlayerController::bShouldPerformFullTickWhenPaused=true` 与 `UWorld::bIsCameraMoveableWhenPaused=true`；后者不负责让相机可动，但**修复暂停时 TAA/MotionBlur 的旧矩阵 glitch**。本仓库只做了前者（`ue4ss_port_time_pause.cpp:236-244`），因此「相机一移动就整体拖影/边缘闪烁」可能叠加这一缺失标志。

**一句话判别（游戏内可做）**：冻结后**相机完全不动等 1–2 秒**——
- 鬼影在 0.3s 内消失 → 正常 TSR/TAA 历史瞬态；
- 1–2 秒后仍清晰存在 → 先关 TSR（`r.AntiAliasingMethod=0`）或切 DLSS：**消失 = TSR + 顶点工厂假速度（机制 #2）**；仍存在 → 查 FrameGeneration / MotionBlur / 真实残余动画；
- 只有「相机移动才糊、停相机就干净」→ 优先补 `UWorld::bIsCameraMoveableWhenPaused=true` 验证（机制 #3）。

**排序（按对本用户症状的解释力）**：
1. **TSR + WPO/Groom/GPU Skin/Niagara pause-unaware 假运动向量（持续型，最符合“运动物体冻结后重影”）**——高置信度（Epic 官方帖 + 本机 UE5.7/TSR 默认配置吻合）。
2. **`UWorld::bIsCameraMoveableWhenPaused` 缺失（相机运动型旧矩阵/TAA-MB glitch）**——高置信度代码缺口，作为叠加项/前置项，不能单独解释“相机静止仍鬼影”。
3. **TSR/TAA 历史瞬态（数帧到十几帧）**——确定存在，但只解释短促双影，不解释持续。
4. **MotionBlur `gq.MotionBlurQualityLevel=1`（低档 tile gather、边缘泄漏）**——中置信度，只在相机移动/冻结过渡 1–2 帧明显；关 MB 后仍鬼影即可排除。
5. **FSR3 / DLSS FrameGeneration 插帧 ghost**——中低置信度；典型是 1–2 个生成帧的中间位置/边缘 halo，持续型需同时有相机运动或与 TSR 叠加。
6. **残余 Material Time WPO / MPC / 风 / 相机 shake**——中置信度；本仓库 `freeze_wind`/`freeze_mpc`/`stop_camera_shakes_on_freeze` 已压制大部分，但硬编码 `Time` 节点的布料仍无法在 pause 下停住（`PROGRESS.md:332`），`freeze_mode=dilation` 是兜底对照。

---

## 1. 本机事实与证据基线

### 1.1 用户环境（本机实测配置，只读）

`C:\Users\Administrator\AppData\Local\ProjectPBZ\Saved\Config\Windows\GameUserSettings.ini`：

- `ResolutionSizeX=2560 / ResolutionSizeY=1440`；`FullscreenMode=1`（无边框全屏）。
- `sg.AntiAliasingQuality=3`、`gq.AntiAliasQualityLevel=3`（最高档；UE5 中 AA Quality=3 即 Epic 档，默认 temporal upscaler 为 **TSR**）。
- `gq.MotionBlurQualityLevel=1`（PBZ 图形质量自定义档；引擎 `r.MotionBlurQuality` 语义为 0=off、1=low、2=medium、3=high(default)、4=very high）。
- `AdapterName=NVIDIA GeForce RTX 5060 Laptop GPU`、`VRAMGB=8`、驱动 `621.01`。
- `PBZLocalSetting.sav` 为加密存档，帧生成（DLSS/FSR/XeSS）具体档位无法从 ini 直接读出 → 必须按 §5 的实验或游戏设置页确认。

### 1.2 安装目录帧生成插件（两套都在）

- FSR：`ProjectPBZ/Plugins/FSR/Source/fidelityfx-sdk/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll`（约 40 MB）、`amd_fidelityfx_loader_dx12.dll`、`amd_fidelityfx_upscaler_dx12.dll`。
- DLSS/Streamline：`Engine/Plugins/Runtime/Nvidia/StreamlineCore/Binaries/ThirdParty/Win64/nvngx_dlssg.dll`、`sl.dlss_g.dll`、`sl.interposer.dll`、`sl.reflex.dll`。
- PBZ.exe 内嵌 `PBZFrameGenerationStartup.cpp` 的仲裁日志字符串：
  `[PBZ][FG] native startup selection: StreamlineRHI=%d StreamlineCore=%d DLSSReady=%d DLSS=%d FSR=%d XeFG=%d selected=%s(%d)`；
  `Startup-selected frame generation backend: 0=None, 1=DLSS, 2=FSR, 3=XeSS.`；
  `cr.PBZ.FrameGeneration.Backend`；
  `r.PBZ.SuppressFrameGen`（"When non-zero, suppress frame generation at runtime without changing the saved user preference"）。
  → 游戏在启动时**锁定一个** FG 后端（"session locked: backend=%s(%d) ... no runtime backend switching"），两套 DLL 同时存在不代表两套都在插帧。

### 1.3 本模组冻结链路（渲染侧相关）

| 事实 | 出处 |
|---|---|
| `freeze_mode=pause` 走 `SetPause`，失败回退 `SetGamePaused`；成功后 `IsGamePaused=true` | `PBZPhotoModeCpp/src/engine/ue4ss_port_time_pause.cpp:218-341` |
| 冻结时仅把 `PlayerController::bShouldPerformFullTickWhenPaused` 写 true（让相机管理器继续 Tick），**从未写 `UWorld::bIsCameraMoveableWhenPaused`** | `ue4ss_port_time_pause.cpp:236-244`；全仓库 grep `bIsCameraMoveableWhenPaused` **0 命中** |
| 冻结顺序：相机 shake/Modifier → 风源归零 → MPC 快照；解冻逆序 | `PBZPhotoModeCpp/src/core/photo_mode.cpp:471-498` |
| 冻结期每帧 `reassert_view_target()`（EngineTickPost），每秒再 `attach_view()` 校验 | `PBZPhotoModeCpp/src/mod_app.cpp:166-169`；`ue4ss_port_camera_view.cpp:450-454`；`photo_mode.cpp:825-835` |
| `freeze_diag=true` 每 10s 输出：风源、34 个 MPC 集合/347 参数变化、修饰器/shake、**视角微动（POV 逐帧最大位移/旋转）** | `PBZPhotoModeCpp/src/engine/ue4ss_port_diagnostics.cpp:11-187`（POV 在 177-185）；`ue4ss_port_micro_motion.cpp:307-354` |
| 代码注释明确：冻结=停世界，但渲染与相机管理层每帧照跑；每帧变化源被 TSR/TAA 放大成边缘闪烁 | `PBZPhotoModeCpp/src/engine/ue4ss_port_micro_motion.cpp:11-16`；`PROGRESS.md:316-327` |
| 硬编码 Time 节点的材质无法靠反射关掉材质时钟，布料类只能 `freeze_mode=dilation` 兜底 | `PROGRESS.md:332` |
| 进程级全线程冻结（PBZPause）连渲染线程一起挂起，画面才真正停在最后一帧；但相机/F2 也会死，不能用于摄影模式 | `_ref/pbzpause/ANALYSIS.md:13-20` |

---

## 2. 关键外部证据：Epic 官方论坛 2025-09 的暂停 WPO 缺陷

### 2.1 帖子原文（Rendering 版块，2025-09-29）

链接：https://forums.unrealengine.com/t/setgamepaused-aplayercontroller-setpause-causes-motionblur-like-artifacts-on-objects-animated-with-worldpostionoffset-even-if-motion-blur-is-disabled/2681490

核心结论（逐条摘录并翻译）：

1. **触发条件**：游戏通过 `SetGamePaused` / `APlayerController::SetPause()` 暂停，且相机在暂停期间可移动（帖子后续修复条件写作 `World->bIsCameraMoveableWhenPaused && World->IsPaused()`）。
2. **现象**：用 **World Position Offset（WPO）** 动画的物体，如果暂停瞬间正处于运动中，会出现 **motion-blur-like 残影/拖影**；**即使把 MotionBlur 完全关掉**（`r.MotionBlurQuality 0`、`r.MotionBlur.Amount 0`）也仍然存在。
3. **不是所有“暂停”都复现**：把游戏改成“极慢速”（extreme slowdown）而不是真暂停时，残影不出现 → **这是 pause 专属路径，不是单纯 TSR 历史收敛**。
4. **TSR vs DLSS**：问题只在 TSR 下明显；**DLSS 因自带 optical flow 组件校正 motion vector，不会出现同样模糊**。残影程度与 **upscaling 幅度**近似成正比（作者用 `r.ScreenPercentage 66` 放大复现）。
5. **Pix 证据**：TSR 第一个后处理 pass 的 `TSR.Velocity.HoleFilled : DilatedVelocityOutput` 中，**即使相机自暂停后已经移动**，仍能看到与残影对应的 motion vector——说明 WPO 物体仍在输出“像游戏没暂停一样”的速度。
6. **Epic 回复（Tiantian，2025-10-27）**：*“I was able to run your level and see that the WPO material does output velocity. This causes confusion to TSR that relies on motion vector. DLSS has an optical flow component inside to correct motion vectors, so it is expected to not cause blurring. We have fired an internal JIRA…”* → **官方确认这是 TSR 依赖 motion vector + WPO 输出速度造成的缺陷，已开内部 JIRA。**
7. **用户自修复细节（Pavel，2025-10-23 / 2025-11-03）**：
   - `sin(Time)` 之类 WPO 材质在 shader compiler 里会**复制整个材质图求上一帧 WPO，并把所有 Time 表达式替换成上一帧时间**；这套逻辑 **不是 pause-aware**。修复点在 `SceneView.cpp`：检测 `World->bIsCameraMoveableWhenPaused && World->IsPaused()`，把 `ViewUniformShaderParameters.PrevFrameGameTime` 和 `PrevFrameRealTime` 替换成当前时间。
   - Groom（带 skinning binding 的头发）、Chaos-enabled 衣服：**上一帧顶点位置存在 vertex buffer 里**，也不是 pause-aware；需要在 `VelocityShader.usf` 检测 pause 并用 `TranslatedWorldPosition` 替代 `PrevTranslatedWorldPosition`。
   - 最终 C++ 侧修复：在 `World->bIsCameraMoveableWhenPaused && World->IsPaused()` 时设置 `WorldIsPaused`；shader 侧加检查，涉及 **HairCardsVertexFactory、GpuSkinVertexFactory、NiagaraSpriteVertexFactory、NiagaraMeshVertexFactory，以及 Nanite WPO**——“None of them were pause-aware.”
8. **尝试无效**：Epic 建议的 `r.MotionBlur.AllowExternalVelocityFlatten 0`，作者实测**不解决问题**。

> 这条证据与用户“冻结处于运动状态的物体 → 重影/闪烁”高度吻合，且解释了两个关键反直觉点：**关 MotionBlur 仍重影**、**DLSS 下可能正常**。本报告把它列为**首要机制之一**（机制 A）。

### 2.2 Tom Looman：`bIsCameraMoveableWhenPaused` 与 TAA/MotionBlur 旧矩阵

链接：https://tomlooman.com/unreal-engine-moving-camera-paused/

原文要点：

~~~cpp
PlayerController::bShouldPerformFullTickWhenPaused = true; // Allows Camera Updates during PlayerController Tick.
UWorld::bIsCameraMoveableWhenPaused = true;              // Fixes TXAA/MotionBlur glitches.
~~~

> “`bIsCameraMoveableWhenPaused` doesn’t actually allow rotation itself but **fixes TAA and MotionBlur while paused**.”

本仓库只做了第一项（`ue4ss_port_time_pause.cpp:236-244`），**没有第二项**。这意味着：
- 相机虽然能被摄影相机 Actor 直接 `K2_SetActorLocation/Rotation` 推动（`ue4ss_port_camera_view.cpp:480-513`），但引擎内部仍可能把它当作“暂停时不可移动相机”，导致 **PrevViewMatrices / PrevFrameGameTime 与当前帧不一致** → TAA/TSR 用旧矩阵重投影、MotionBlur 旧速度 → 相机一移动就整体拖影/边缘闪烁。
- 这也解释了为什么单纯设 `bShouldPerformFullTickWhenPaused` 后相机“能动”，却不等于“渲染正确”。

### 2.3 两者关系：一个缺失标志 vs WPO pause bug

**结论：不能把二者当成同一个原因；它们是两层，缺一不可，但修复优先级不同。**

| 层 | 机制 | 是否由缺 `bIsCameraMoveableWhenPaused` 解释 | 典型表现 | 验证 |
|---|---|---|---|---|
| L1 相机/视图矩阵层 | 世界暂停时引擎未按“相机可移动”更新上一帧视图矩阵/时间；TAA/MB 旧矩阵重投影 | **是**；补 `World->bIsCameraMoveableWhenPaused=true` 可能直接修复 | 相机一移动，全画面/所有边缘拖影；相机停 1–2 帧内干净 | 设 flag true 前后对比；相机静止 vs 移动对比 |
| L2 图元顶点工厂层 | WPO/Nanite WPO/Groom/HairCards/GPU Skin/Niagara 的上一帧时间或顶点缓存位置不是 pause-aware → 假 velocity 输入 TSR | **否**；即使 flag=true，vanilla UE5.7 仍缺 shader 修复，鬼影仍在（Epic 帖作者按旧帖设了 flag 仍残留） | 相机完全静止，布料/头发/角色/粒子仍持续重影；关 MB 无效；关 TSR / 换 DLSS 才消失 | TSR off/DLSS 对比；静态网格 vs WPO 物体同屏对比；Pix 看 `TSR.Velocity.HoleFilled` |
| L3 内容层 | Material Time（`bIgnorePause`）、风/MPC 驱动 WPO、Niagara 持续 tick、Cloth/Hair 模拟 | 否 | 布料/植被边缘持续摆动+闪烁；`freeze_diag` 可见 MPC 变化或风非零 | `freeze_wind`/`freeze_mpc` 开关对照；`freeze_mode=dilation` 对照 |

**判断规则**：
- 补 `bIsCameraMoveableWhenPaused=true` 后**全部干净** → 主要是 L1。
- 补 flag 后**相机运动型拖影改善，但静止相机下 WPO/布料鬼影仍在** → L2 是主因（最常见）。
- 设 flag 反而让鬼影更明显 → 说明相机“可移动”路径被真正激活，L2 的 pause-unaware 顶点工厂被暴露；这本身就是 L2 的证据。
- `freeze_mode=dilation` 下鬼影消失 → 与 `SetGamePaused` 的 pause 路径强相关（L1/L2 均可能）；若 dilation 下仍有鬼影 → 更偏 L3（Time/MPC/风）或 TSR 历史本身。

**为什么 L2 在 UE5.7 上仍可能存在**：Epic 帖是 2025-09/10 的 UE5.6 复现，Epic 2025-10-27 才开内部 JIRA；PBZ 是 UE 5.7 Shipping（UE4SS 日志 `Found EngineVersion: 5.7`，`PBZ.exe` 内嵌 5.7 的 TSR cvar 帮助）。**5.7 是否已包含该 JIRA 修复无法从安装包字符串证明**；从用户症状与 TSR 默认配置看，应按“未修复/部分修复”对待，并用 §7 的实验确认。

---

## 3. TAA / TSR：history + motion vector 在“匀速运动 → 突然暂停”时的行为

### 3.1 基本管线

UE5 的时域 AA（TAA/TSR）每帧做四件事：

1. **Jitter**：每帧用不同子像素偏移的投影矩阵渲染（TAA 默认 `r.TemporalAASamples=8`；TSR 也用 jitter 序列）。
2. **History**：上一帧（或多帧）的颜色/深度/速度历史缓冲。TSR 默认 `r.TSR.History.SampleCount=16`（5.7 帮助文本：最小 8、最大 32、默认 16；值越高静态画面越稳，但“may introduce additional ghosting on fireflies style VFX”）。
3. **Motion vector / velocity**：每个像素的屏幕空间运动矢量（物体运动 + 相机运动）。TSR 用它把历史重投影到当前帧位置。
4. **Rejection / 收敛**：当前帧与重投影历史差异过大时拒绝历史（shading rejection、depth/normal rejection、thin geometry 等），否则按权重混合。

**关键点：历史缓冲的“旧内容”只有靠正确的 motion vector 才能被搬到正确位置。速度错 → 历史被搬到错位置 → 重影。**

### 3.2 「匀速运动 → 突然暂停」逐帧推演

设物体在屏幕空间以速度 v 匀速运动，帧 N-1 时在位置 P(N-1)，帧 N 因 `SetGamePaused` 突然停在 P(N)：

| 帧 | 物体实际位置 | 写入 velocity | TSR 行为 | 观察 |
|---|---|---|---|---|
| N-1 | P(N-1) | v | 历史正常累积 | 正常运动模糊/AA |
| N（暂停首帧） | P(N)（=P(N-1)+v） | **0**（transform 不再变化） | 用 velocity=0 重投影历史 → 把 N-1 帧在 P(N-1) 的旧内容直接叠到 P(N) 同屏幕坐标附近；历史样本仍按 16 帧权重混合 | **双影/拖影**：旧位置 P(N-1) 的残影 + 新位置 P(N) 的实像 |
| N+1 … N+k | 不动 | 0 | 每帧继续混入新样本，旧样本权重按约 1/N 衰减 | 鬼影逐帧变淡；约 8–16 帧（60fps 0.13–0.27s）基本消失 |
| N+16 后 | 不动 | 0 | 历史被当前帧完全占据 | 干净（除非有残余动画/错误速度） |

**为什么有时“只持续数帧”**：这正是上表 N→N+16 的正常收敛。TSR 的 `r.TSR.Velocity.WeightClampingSampleCount`（默认 4）会在**输出像素速度较大时把历史样本钳到 4**，所以运动快的物体在暂停后收敛更快；但**如果速度被错误写成 0，这个“高速度钳制”不会触发**，反而可能保留 16 帧历史 → 鬼影更久。这解释了“同样是暂停，有的物体 2–3 帧就干净，有的十几帧还在”。

**为什么有时“持续存在”**：只要某类图元在暂停后仍在每帧改变顶点位置、却仍向 TSR 报“上一帧位置/上一帧时间”的速度，TSR 就一直把旧内容重投影到新位置 → 鬼影不收敛。Epic 帖的 WPO/Groom/GPU Skin/Niagara 就是这一类（§2.1）。**这不是 TSR 的参数能调好的**：只要 velocity 是假的，TSR 越高质量（历史越长）反而越明显。

### 3.3 为什么“边缘闪烁”而不仅是“重影”

TSR 对薄几何/高频细节有专门 heuristic，AA Quality=3 时全部开启：

- `r.TSR.ThinGeometryDetection`（发丝/植被等薄几何，默认在 Nanite Foliage 下开启；`ShadingRange=1` 包含 hair）：对薄几何放宽历史拒绝，减少闪烁，但**代价是更容易留下鬼影**。
- `r.TSR.ShadingRejection.Flickering`（High/Epic/Cinematic 默认开启）：检测到像素亮度规律性抖动时，**主动“允许 ghost”来稳定画面**。其 5.7 cvar 帮助原文明确警告：
  > “**any opaque geometry with incorrect motion vector can make a pixel look identically flickery quicking this heuristic in and leaving undesired ghosting effects on the said geometry.** When that happens, it is highly encourage to verify the motion vector through the VisualizeMotionBlur show flag and how these motion vectors are able to reproject previous frame with the VisualizeReprojection show flag.”
  —— 这几乎是对“冻结后 WPO 物体持续鬼影 + 边缘闪烁”的逐字描述。
- `r.TSR.History.ScreenPercentage`：AA Quality=3（Epic）默认 **200**（历史分辨率 2×输出），细节保留更好、静态更稳，但错误速度造成的鬼影也更“清晰可见”。
- `r.TSR.Velocity.WeightClampingSampleCount` 默认 4：高速度时钳制历史样本；对“假零速度”无效。

**现象判别**：
- **重影（double image）**：两个清晰位置副本，通常在冻结瞬间出现、0.2–0.3s 内收敛。
- **拖影/ghost trail**：沿物体运动方向的长尾，持续存在，常见于 WPO/布料/头发/粒子。
- **边缘闪烁（shimmer）**：高频细边（发丝、布料边缘、植被）在相机微动或残余动画下闪烁；TSR 的 thin geometry/flickering heuristic 会放大成“边缘爬动”。
- **整体糊/所有边缘拖**：更像相机/视图矩阵层（L1）或 MotionBlur，而不是单个物体的顶点速度。

### 3.4 TSR vs TAA 的差别

- **TSR**：历史样本更多（默认 16）、空间重投影更复杂、对错误 motion vector 更敏感；高质量模式下“静态最稳、错误速度最毒”。本机 AA Quality=3 默认 TSR（`r.DefaultFeature.AntiAliasingMethod` 5.7 帮助：0 off、1 FXAA、2 TAA、3 MSAA、4 **TSR (Default)**、5 SMAA）。
- **TAA**：`r.TemporalAASamples=8`，历史权重更短，鬼影收敛更快但闪烁更多；如果用户/工程把 AA 改成 TAA，持续型鬼影会短一些，但仍会受错误速度影响。
- **DLSS/FSR 超分**：若游戏当前 upscaler 不是 TSR（`gq.Ind.UpscaleMode` / 游戏设置里的 DLSS/FSR/XeSS 超分），则时域历史由 DLSS/FSR 承担；DLSS 自带 optical flow 可校正错误 motion vector（Epic 帖明确说 DLSS 不会同样模糊）。**所以“切 DLSS 就正常”不等于“TSR 参数问题”，而是 DLSS 对 WPO 假速度更鲁棒**。

---

## 4. Motion Blur（gq.MotionBlurQualityLevel=1）

### 4.1 机制

UE MotionBlur 是后处理 pass：用 **velocity buffer**（每像素屏幕空间速度）+ 深度做 tile-based gather/scatter，对场景做方向性模糊。关键点：

- velocity 来自 **Velocity pass**（物体 transform 变化 + 相机运动）；不是“画面看起来在动”就有，必须被 velocity pass 写入。
- TSR 为了性能，会用 `r.MotionBlur.AllowExternalVelocityFlatten`（默认 1）**把 TSR 自己算的 velocity 压平后提供给 MotionBlur**；这也是 Epic 帖里建议关掉该项的原因（但作者实测无效）。
- 质量档：`r.MotionBlurQuality` 0=off、1=low、2=medium、3=high(default)、4=very high。低档使用更粗的 tile/半分辨率 gather，**在物体轮廓、前后景速度差大的边缘更容易泄漏**，表现为物体边缘外扩的光晕/拖尾。

### 4.2 在「暂停 + 相机可动」下

| 情形 | MotionBlur 行为 | 是否造成用户症状 |
|---|---|---|
| 暂停瞬间，物体刚从高速运动停下 | 首帧 velocity 仍来自上一帧高速运动；下一帧变 0 | 最多 1–2 帧的“最后一段拖影”，随后消失 |
| 暂停后相机静止、物体残余 WPO/粒子运动 | 若 velocity 被写为 0（WPO 未输出正确速度）→ MotionBlur 看不到；若被写为假速度 → 产生拖影 | 可能有贡献，但关 MB 后仍存在 → 主因不是 MB |
| 暂停后相机移动 | 相机速度使整个画面产生正确 motion blur；低质量档在物体边缘泄漏更明显 | 相机一移动就糊，停相机 1–2 帧干净 → MB 贡献明显 |
| 暂停后相机静止、画面仍持续拖影 | 若 velocity=0，MotionBlur 不可能产生持续拖影 | **排除 MotionBlur**；指向 TSR 历史/假速度或 FG |

### 4.3 结论

- `gq.MotionBlurQualityLevel=1`（引擎 low 档）会**放大**冻结瞬间和相机移动时的边缘拖影/光晕，但不是“相机静止仍持续重影”的原因。
- **判别实验**：游戏设置把 Motion Blur 关掉，或临时 `r.MotionBlurQuality=0`、`r.MotionBlur.Amount=0`。如果鬼影完全消失 → MB 是主因/放大器；如果鬼影仍在（尤其物体是布料/头发/粒子）→ MB 不是根因，按 §3/§2 查 TSR。
- Epic 帖作者实测 `r.MotionBlurQuality 0` + `r.MotionBlur.Amount 0` 仍重影，这是“不是 MotionBlur”的直接证据。

---

## 5. Frame Generation：FSR3 与 DLSS FG

### 5.1 机制与差异

| 项目 | FSR3 Frame Generation | DLSS Frame Generation |
|---|---|---|
| 实现 | `amd_fidelityfx_framegeneration_dx12.dll`（FidelityFX SDK） | `nvngx_dlssg.dll` + Streamline `sl.dlss_g.dll`/`sl.interposer.dll` |
| 输入 | 真实帧颜色 + 深度 + **motion vectors** + optical flow（FidelityFX Optical Flow） | 真实帧颜色 + 深度 + **engine motion vectors** + **optical flow**（NGX/Streamline） |
| 输出 | 在真实帧之间插入 1 个（或更多）生成帧；swapchain 包装 | 同左；要求 Reflex；通常 1 个生成帧 |
| 对本问题的鲁棒性 | 依赖游戏提供的 motion vector；WPO 假速度会污染光流/插帧 → 边缘 halo/拖影 | 自带 optical flow 校正 motion vector（Epic 帖明确说 DLSS 不会像 TSR 那样被 WPO 假速度骗）→ 通常更干净 |
| 典型伪影 | 快速运动/遮挡边缘的 halo、UI/字幕边缘抖动、暂停切换 1–2 帧“中间态” | 类似但更轻；UI 边缘、细发丝、快速转头处可能出现轻微 ghost |

PBZ 的 `PBZFrameGenerationStartup.cpp` 在启动时仲裁并**锁定**一个后端（0=None、1=DLSS、2=FSR、3=XeSS），运行中不切换；同时存在 FSR 与 DLSS 插件只是安装事实，不代表两者都启用。

### 5.2 在「暂停/突然停顿」下

- **过渡帧**：真实帧 N-1（运动）与 N（静止）之间，FG 可能生成 1 个“物体位于中间位置”的帧 → 看起来像 1–2 帧的双影/拖影。这是插帧的正常过渡，不是持续 bug。
- **相机持续移动**：FG 用相机运动 + 深度做外推，遮挡边界（物体前后景）容易出现 halo/拖影；相机停下后，真实帧相同，生成帧也相同 → 应立即干净。
- **与 TSR 叠加**：FG 的输入是 TSR 输出。若 TSR 历史已有 WPO 假速度鬼影，FG 会把鬼影进一步插值/放大，表现为“开 FG 更严重”；关 FG 只能减轻，不能根治 TSR 侧假速度。
- **帧率特征**：FG 开启时显示帧率约为真实渲染帧率 ×2；鬼影若每隔一帧出现/闪烁，符合 FG 生成帧特征。可用高帧率录屏逐帧看。

### 5.3 如何判定用户开的是哪一种

按可靠性从高到低：

1. **游戏设置页**（最直接）：图像/显示 → “帧生成 / Frame Generation”下拉，当前项会写 **DLSS Frame Generation / FSR Frame Generation / XeSS Frame Generation / 关**。让用户截图这一项即可。
2. **启动日志**：若能看到 `[PBZ][FG] session locked: backend=...` 或 `[PBZ][FG] native startup selection: ... selected=...`，直接读到后端名；但本机 `ProjectPBZ.log` 是 Krash 加密的，普通用户无法直接读。
3. **运行时 CVar**：`cr.PBZ.FrameGeneration.Backend`（0=None、1=DLSS、2=FSR、3=XeSS）；`r.PBZ.FrameGeneration.Ready`；`r.PBZ.SuppressFrameGen` 可在不改用户保存设置的前提下抑制 FG（适合做 A/B）。
4. **模块/文件**：`amd_fidelityfx_framegeneration_dx12.dll`、`nvngx_dlssg.dll`、`sl.dlss_g.dll`、`libxess_fg.dll` 的存在只能证明“装了”，不能证明“当前启用”；需结合设置页/CVar。
5. **行为差异**：切 DLSS 后 WPO 鬼影明显减轻、切 FSR 后加重 → 说明当前是 FSR 或 FG 与 TSR 叠加；但这只是旁证。

> 本仓库历史记录（`PBZ-Framework-Menu/README.md:18-20,49`、`PROGRESS.md:877-917`）按 **FSR3 帧生成环境**做过实机验证，用户也确认“帧生成必开”；但本机是 RTX 5060 Laptop，PBZ 启动仲裁**可能**选 DLSS（NVIDIA 原生优先）。**不要预设 FSR3；必须先看设置页/CVar。** 这是 §7 实验的第一步。

---

## 6. Material Time WPO / Niagara / Skeletal Mesh / Cloth 在 World Pause 下

### 6.1 World Pause 到底停什么

- `SetGamePaused` 让 `UWorld` 进入 `LEVELTICK_PauseTick`：**默认只有 `bTickEvenWhenPaused=true` 的 Actor/Component TickFunction 继续跑**；普通角色、动画、粒子组件默认停 Tick。
- 本模组额外设 `PlayerController::bShouldPerformFullTickWhenPaused=true`，让 **PlayerController/相机管理器**继续完整 Tick（相机可动），但**不会让世界里的其他 Actor 恢复 Tick**。
- **渲染线程不因世界暂停而停**：每帧仍出图；TSR jitter、材质求值、后处理、FG 都继续。这是“画面看起来还在刷新”的根本原因。
- **材质 `Time` 节点**：`UMaterialExpressionTime` 有 `bIgnorePause` 属性（PBZ.exe 反射属性表中可见）。勾选 Ignore Pause 的材质使用 **RealTime**，世界暂停也继续走；未勾选的用游戏时间，理论上暂停会停，但 vertex factory 的“上一帧时间”仍可能不是 pause-aware（§2.1），且在暂停首帧仍可能输出假速度。**美术硬编码 `sin(Time)` 的布料/植被 WPO 是最典型的持续源**；本仓库 `PROGRESS.md:332` 明确说“反射层关不掉材质时钟，这类布料用 `freeze_mode=dilation` 兜底”。

### 6.2 各系统的 pause 行为与 velocity 写入

| 系统 | World Pause 下是否继续 | 是否写 velocity / 问题 |
|---|---|---|
| **Static Mesh（普通 transform 动画）** | 默认不 Tick；若由 Sequencer/蓝图强制 Tick 则可能继续 | 普通 transform 有 prev transform，velocity 正确；暂停后下一帧归零 → TSR 短暂双影后收敛 |
| **Material Time WPO（静态/骨骼网格材质）** | **是**：材质在渲染时每帧重新求值；`bIgnorePause` 时 Time 继续 | **通常不写或写错 velocity**：UE 的 WPO 材质若开启 Output Velocity，会按“上一帧时间”复制材质图求上一帧 WPO；该 previous time 不是 pause-aware → **假速度**（Epic 帖）。这是持续鬼影头号来源。 |
| **Nanite WPO** | 是（GPU 驱动） | 同上；Nanite WPO 的上一帧位置/时间在 GPU 侧，pause 检查缺失 → 假速度（Epic 帖点名）。 |
| **Groom / HairCards（带 skinning binding）** | Groom 模拟/绑定可能仍更新；普通 hair 有 `HairStrandsVelocity` pass | 上一帧顶点位置存在 vertex buffer，不是 pause-aware → 假速度；Epic 帖点名 **HairCardsVertexFactory**。发丝细边 + TSR thin geometry → 重影+闪烁。 |
| **GPU Skin（GpuSkinVertexFactory）** | 骨骼动画默认停；但 GPU skin 缓存上一帧顶点位置 | Epic 帖点名 **GpuSkinVertexFactory** 不是 pause-aware；标准 Skinned Mesh 作者说“fine”，但 GPU skin 路径不一定。 |
| **Niagara（Sprite / Mesh 渲染器）** | Niagara 组件默认不 Tick；但部分系统设了 `bTickEvenWhenPaused`、或由 Sequencer/蓝图强制 Tick，仍会更新 | Epic 帖点名 **NiagaraSpriteVertexFactory / NiagaraMeshVertexFactory** 上一帧位置不是 pause-aware → 假速度；即使粒子模拟停了，最后一帧的速度也可能被 TSR/FG 放大。粒子/拖尾是持续鬼影/闪烁高发区。 |
| **Chaos Cloth / 布料（Chaos-enabled clothes）** | 物理/布料模拟可能在 Physics 线程继续（需实测）；渲染顶点每帧更新 | 上一帧顶点位置存 vertex buffer，不是 pause-aware → 假速度；Epic 帖作者说“chaos-enabled clothes 情况更复杂”。 |
| **Skeletal Mesh（普通蒙皮）** | 默认不 Tick（动画停） | 标准骨骼 velocity 由 bone matrix 计算，Epic 帖作者说“Skinned mesh assets are fine”；若角色 BP 给组件设了 `bTickEvenWhenPaused`，则可能继续。 |
| **相机 Modifier / CameraShake** | 用真实时间驱动，暂停照跑（本模组已停用） | 只影响相机 POV 微动 → TSR 全屏边缘闪烁，不产生物体局部鬼影。 |
| **Wind / MPC 驱动的 WPO** | 材质每帧求值；风源参数可能被游戏每帧改写 | 本模组 `freeze_wind` 把 Strength/Speed/MinGust/MaxGust 归零；`freeze_mpc` 4Hz 写回 347 参数（日志实测生效）。仍无法停“硬编码 Time”与“其他未覆盖的实时驱动”。 |

### 6.3 为什么标准 Skeletal Mesh 反而可能没事

Epic 帖作者的实测结论：**“Skinned mesh assets are fine”**。原因是普通骨骼动画在暂停时停 Tick，骨骼矩阵不再变化，velocity 自然归零；而 WPO/Groom/GPU skin/Niagara 的上一帧顶点数据在 vertex buffer/材质时间里，pause 时没有同步更新 → 假速度。**因此“用户说运动物体冻结后重影”如果物体是角色，重点看其布料/头发/披风/武器拖尾，而不是骨骼本体。**

---

## 7. 现象 → 最可能机制 → 判别实验（游戏内可做）

> 实验原则：一次只改一个变量；每次冻结同一个“运动中物体”（推荐：有布料/头发/披风的角色，或带 WPO 的旗帜/植被）；用 60fps 录屏，截取冻结后 0s / 0.3s / 1s / 3s 四帧对比。

| # | 现象 | 最可能机制 | 判别实验（游戏内/配置） | 判读 |
|---|---|---|---|---|
| 1 | 冻结瞬间物体出现两个位置的重影，0.2–0.3s 内变淡消失，之后干净 | TSR/TAA 历史 16 样本 + 速度归零（正常收敛） | 冻结后相机完全不动，录 3s；对比第 1 帧与第 30 帧 | 第 30 帧干净 = 正常瞬态，不需修；若仍脏 → 看 #2/#3 |
| 2 | 相机完全静止，1–3s 后物体仍有清晰拖影/鬼影；物体是布料/头发/披风/角色/粒子 | **WPO/Groom/GPU Skin/Niagara pause-unaware 假速度 + TSR**（Epic 2025-09 bug） | **关 TSR**：`r.AntiAliasingMethod=0`（或游戏设置切 DLSS 超分）；再开 `r.MotionBlurQuality=0` 对照 | TSR off 后鬼影消失/大幅减轻 = 机制 A 确认；关 MB 无效 = 排除 MotionBlur |
| 3 | 相机一移动，整个画面/所有边缘拖影；相机停下 1–2 帧内干净 | **`UWorld::bIsCameraMoveableWhenPaused` 缺失 / TAA-MB 旧矩阵** | 相机静止 vs 缓慢平移对比；若能用 UE4SS 把 `UWorld.bIsCameraMoveableWhenPaused` 置 true，再对比 | 设 flag 后明显改善 = L1 确认；仍剩下物体局部鬼影 = L2 |
| 4 | 只有相机移动时物体轮廓外扩、光晕/拖尾；相机停下即消失 | MotionBlur 低质量档（gq=1）边缘泄漏 | 游戏设置关 Motion Blur；或 `r.MotionBlurQuality=0`、`r.MotionBlur.Amount=0` | 关 MB 消失 = MB；仍存在 = TSR/假速度 |
| 5 | 开帧生成时多出 1–2 帧“中间位置”双影，或 UI/字幕边缘抖动；显示帧率约翻倍 | FSR3 / DLSS FrameGeneration 插帧 | 游戏设置关 FG；或 `r.PBZ.SuppressFrameGen=1`；再对比 | 只改善过渡帧 = FG 正常；持续改善 = FG 放大 TSR；换 DLSS/FSR 后端对比可判当前后端 |
| 6 | 布料/植被边缘高频闪烁、无清晰双影；相机微动或风/MPC 变化时更明显 | TSR thin geometry/flickering heuristic + 残余 WPO/风/MPC/相机微动 | `freeze_diag=true` 看「视角微动」「MPC 变化」「风源」；`freeze_wind=false`/`freeze_mpc=false` 对照；`r.TSR.Visualize 7` 看 flickering 区域 | 视角微动=0 且 MPC 变化=无 → 剩余为材质 Time/WPO；有变化 → 对应治理项没压住 |
| 7 | 冻结后角色仍在动（骨骼/布料明显继续演），但相机不动 | Actor/Component `bTickEvenWhenPaused`、Niagara 特殊 Tick、Sequencer | 看 UE4SS.log 是否有冻结成功；对比 `freeze_mode=dilation`；用静态物体 vs 角色 | 只有角色动 = 内容层 tick 未被暂停，需按资产/BP 处理（本仓库已 freeze_sequences） |
| 8 | 整个画面像蒙了旧帧，所有边缘都糊，且 `freeze_mode=pause` 才有；`dilation` 正常 | pause 专属路径（L1/L2） | **`freeze_mode=dilation` 对比**（本仓库 config）；或把暂停改为极慢速 | dilation 干净 = pause 专属 bug；dilation 仍脏 = Time/MPC/TSR 本身 |
| 9 | 同屏静态网格干净，只有 WPO/布料/头发物体脏 | 顶点工厂假速度（L2） | 同场景找一面普通墙/箱子与角色同框冻结；对比 | 静态网格干净 = L2 确认（不是全屏 TSR 问题） |
| 10 | 切换超分分辨率（`r.ScreenPercentage 66` vs 100）鬼影随放大倍数增加 | TSR 重投影/历史（Epic 帖也报告 blur ∝ upscaling） | 临时改 `r.ScreenPercentage` 或游戏超分档位对比 | 随 upscaling 放大 = TSR 路径；不变 = 其他 |

### 7.1 一步判别（给用户的最小操作）

**冻结后不要碰鼠标/键盘，等 2 秒，看鬼影是否还在。**

- **2 秒后干净** → 正常 TSR/TAA 历史瞬态（最多加一点 MotionBlur 过渡帧），不用处理。
- **2 秒后仍在** → 打开游戏设置把 **帧生成关掉** + **运动模糊关掉** 再试：
  - 仍存在 → **TSR 的 WPO/顶点工厂假速度**（机制 A）最可能；下一步关 TSR 或切 DLSS 确认。
  - 消失 → 是 FrameGeneration/MotionBlur 的叠加，按 #4/#5 继续分。
- **只有相机移动才糊** → 优先验证 `bIsCameraMoveableWhenPaused`（机制 L1）。

### 7.2 推荐实验顺序（由便宜到贵）

1. **确认当前超分/FG 后端**：游戏设置页截图（DLSS/FSR/XeSS/关；FG 档位）。
2. **开 `freeze_diag=true`**（本仓库 config；默认安装为 false）：冻结 20s，看 UE4SS.log 的「冻结诊断」：
   - `视角微动:位移0.000 旋转0.00` → 相机 POV 真静止，排除相机 shake/modifier；
   - `MPC=34集/347参 变化:无`、`风源=1[强度0.00/速度0.00]` → 风/MPC 已压住，剩余不是它们；
   - 若仍有鬼影 → 指向材质 Time/WPO（无法反射关闭）或 TSR 假速度。
3. **关 MotionBlur**（游戏设置或 `r.MotionBlurQuality=0`）：排除 MB。
4. **关 FrameGeneration**（游戏设置或 `r.PBZ.SuppressFrameGen=1`）：排除 FG。
5. **关 TSR / 切 DLSS**（`r.AntiAliasingMethod=0` 或游戏设置切 DLSS 超分）：**最关键的一步**，直接判 TSR。
6. **`freeze_mode=dilation` 对比**：判 pause 专属 vs 通用。
7. **`bIsCameraMoveableWhenPaused=true` 对照**（需 UE4SS/反射写入；见 §8 建议）：判 L1 vs L2。
8. **同屏静态 vs WPO 物体**：判 L2 具体图元。

---

## 8. 代码/配置层建议（只读分析结论，不在本任务改源码）

### 8.1 立即可做的（不改代码）

- **在 `PBZPhotoModeCpp/config.ini` 开 `freeze_diag=true`**：当前安装配置是 false（`.../Mods/PBZPhotoModeCpp/config.ini:140`），打开后才能看到 POV 微动/MPC 变化，这是最便宜的现场证据。
- **实验用 CVar**（写入 `%LOCALAPPDATA%\ProjectPBZ\Saved\Config\Windows\Engine.ini` 的 `[SystemSettings]`，或通过可用的控制台）：
  - `r.AntiAliasingMethod=0`（关 TSR/TAA，最强判别；仅诊断用）
  - `r.MotionBlurQuality=0` / `r.MotionBlur.Amount=0`
  - `r.PBZ.SuppressFrameGen=1`（运行时抑制 FG，不改用户设置）
  - `r.ScreenPercentage=66`（验证 upscaling 相关性；也可用游戏超分档位）
  - `r.TSR.ShadingRejection.Flickering=0`（验证 flickering heuristic 是否把鬼影“锁住”）
- **设置页确认 FG 后端**：DLSS / FSR / XeSS / 关，避免把 FSR3 当成唯一可能。

### 8.2 若后续要改代码（供 Lead/开发排期）

1. **补 `UWorld::bIsCameraMoveableWhenPaused=true`**（冻结时写、解冻还原），与现有 `bShouldPerformFullTickWhenPaused` 成对；这是 Tom Looman 方案的第二半，成本极低、可立即 A/B。注意：该字段是 `UWorld` 属性，需在 `ue4ss_port_time_pause.cpp` 的 `set_paused(true/false)` 路径写入并快照恢复（与现有 PauseSnapshot 同思路）。
2. **把 `bIsCameraMoveableWhenPaused && IsPaused` 暴露给渲染侧 pause-aware shader**：这是 Epic 帖的 C++ 侧修复思路（设置 `WorldIsPaused`，shader 用当前帧时间/顶点位置替代上一帧）。**5.7 引擎源码不在本仓库，需确认 PBZ 的引擎构建是否已有该 JIRA 修复；若没有，只能靠引擎升级或自研 shader patch（超出本模组范围）。**
3. **短期规避**：给用户提供「以 `freeze_mode=dilation` 替代 pause」的选项（本仓库已支持）：dilation 模式下 Time 随全局时间缩放走，WPO 材质时钟被大幅减慢，通常可显著减轻 WPO 鬼影；代价是相机/世界的暂停感略弱（时间仍在极慢速流动）。
4. **对摄影模式的产品建议**：如果目标是“干净截图”，可在冻结后**自动做一次性 TSR history reset**（或让用户等 0.3–0.5s 再拍）；但这只能解决瞬态，不解决 WPO 假速度。

---

## 9. 最终结论（回答任务第 6 点）

### 9.1 用户“冻结处于运动状态的物体”时最可能看到的是哪一种？

**最可能的组合是：**

1. **冻结瞬间的 TSR/TAA 历史双影（必然发生，数帧到十几帧）** +
2. **如果物体含 WPO / 布料 / 头发 / 粒子 / GPU skin，则叠加 UE5.6+ pause-unaware 顶点工厂假速度造成的持续 motion-blur-like 鬼影**（相机静止也不消失；关 MB 无效；DLSS 或关 TSR 可验证）+ 
3. **如果相机在暂停中移动，则再叠加 `bIsCameraMoveableWhenPaused` 缺失导致的旧视图矩阵拖影/边缘闪烁**。

**排序（本用户症状）**：
- **第 1 位（最符合“运动物体冻结后持续重影/闪烁”）**：**TSR + WPO/Groom/GPU Skin/Niagara pause-unaware 假运动向量**（机制 A）。依据：Epic 官方 2025-09 帖、官方回复“WPO does output velocity…confuses TSR”、DLSS 免疫、关 MotionBlur 无效、用户 AA Quality=3 默认 TSR/16 样本历史全部吻合。
- **第 2 位（叠加/前置）**：**`UWorld::bIsCameraMoveableWhenPaused` 缺失**（机制 L1）。依据：Tom Looman 明确该 flag 修复 TAA/MotionBlur glitch；本仓库 grep 0 命中，只设了 `bShouldPerformFullTickWhenPaused`。它不能解释“相机静止仍鬼影”，但能解释“相机一动全屏糊/边缘闪烁”。
- **第 3 位**：TSR/TAA 历史瞬态（正常，数帧）。
- **第 4 位**：MotionBlur 低档（放大过渡帧/相机移动）。
- **第 5 位**：FSR3/DLSS FrameGeneration（1–2 帧过渡；相机移动时 halo）。
- **第 6 位**：残余 Material Time WPO / MPC / 风 / CameraShake（本仓库已治理大部分，但硬编码 Time 仍是漏网）。

### 9.2 为什么单靠 SetGamePaused 不可能得到“逐像素静止”的冻结帧

1. **渲染线程不暂停**：世界暂停只停游戏逻辑 Tick，渲染每帧照出；TSR/TAA 每帧 jitter + 历史混合，静止场景也不是逐像素相同。
2. **TSR/TAA 历史本身在动**：16 帧历史、每帧 jitter 序列、shading rejection/flickering heuristic，使像素值在“静止”场景里仍有微小时间变化。
3. **材质 Time / WPO 仍求值**：`bIgnorePause` 的 Time 走 RealTime；即使不走，WPO 的上一帧时间/顶点缓存不是 pause-aware（引擎 bug）→ 假运动。
4. **Niagara/Groom/Hair/Cloth 可能继续 tick 或保留上一帧顶点位置**：这些系统的速度/上一帧数据不在世界暂停的统一控制下。
5. **相机管理层继续运行**：`bShouldPerformFullTickWhenPaused=true` 让 PlayerController/相机管理器完整 Tick；CameraShake、Modifier、ViewTarget 重设、POV 微动都可能每帧改变视图。
6. **后处理/帧生成继续**：MotionBlur、Bloom、DOF、曝光、FilmGrain、FSR3/DLSS FG 都会让“同一帧”不逐像素一致。
7. **唯一真正的逐像素静止**是把渲染线程也挂起（PBZPause 式进程级全线程 suspend），但那样相机/F2/摄影模式输入全部失效（`_ref/pbzpause/ANALYSIS.md:13-20`），不适合摄影模式。
8. 所以摄影模式的正确目标不是“像素静止”，而是**“逻辑冻结 + 视觉上稳定”**：至少要 0.3–0.5s 让 TSR 历史收敛，并对 WPO/Groom/Niagara/布料做 pause-aware 速度或改用 DLSS/dilation。

---

## 10. 证据索引（文件/行号）

| 主题 | 证据 |
|---|---|
| SetPause/SetGamePaused 与 bShouldPerformFullTickWhenPaused | `PBZPhotoModeCpp/src/engine/ue4ss_port_time_pause.cpp:218-341`（236-244 写 PC flag） |
| 冻结流程/治理顺序 | `PBZPhotoModeCpp/src/core/photo_mode.cpp:429-561`；`471-498` 相机→风→MPC |
| 冻结自检与 freeze_diag | `PBZPhotoModeCpp/src/core/photo_mode.cpp:839-892` |
| 冻结期残余变化源说明（渲染/相机每帧跑） | `PBZPhotoModeCpp/src/engine/ue4ss_port_micro_motion.cpp:11-16` |
| 相机 shake/Modifier 治理 | `ue4ss_port_micro_motion.cpp:18-83` |
| 风源归零 | `ue4ss_port_micro_motion.cpp:85-160` |
| MPC 快照/4Hz 写回 | `ue4ss_port_micro_motion.cpp:162-296` |
| POV 微动监测 | `ue4ss_port_micro_motion.cpp:307-354`；`ue4ss_port_diagnostics.cpp:177-185` |
| 每帧 reassert_view_target | `PBZPhotoModeCpp/src/mod_app.cpp:166-169`；`ue4ss_port_camera_view.cpp:450-454` |
| 相机位姿写入 | `ue4ss_port_camera_view.cpp:480-513` |
| 配置项（freeze_diag 默认、freeze_wind/MPC 等） | `PBZPhotoModeCpp/config.ini.example:121-149`；安装配置 `.../Mods/PBZPhotoModeCpp/config.ini:140`（当前 freeze_diag=false） |
| 硬编码 Time 材质无法反射关闭 | `PBZPhotoModeCpp/PROGRESS.md:332` |
| 1.0.41 冻结残余微动治理结论 | `PBZPhotoModeCpp/PROGRESS.md:316-335` |
| 全线程冻结=渲染也停、相机不可用 | `_ref/pbzpause/ANALYSIS.md:13-20` |
| FSR3 FG 环境（框架 README） | `_ref/PBZ-Framework-Menu/README.md:18-20,49` |
| 本机分辨率/AA/MB/GPU | `C:\Users\Administrator\AppData\Local\ProjectPBZ\Saved\Config\Windows\GameUserSettings.ini`（sg.AntiAliasingQuality=3、gq.MotionBlurQualityLevel=1、2560x1440、RTX 5060 Laptop） |
| UE5.7 内嵌 cvar 帮助：TSR 16 样本/200% history/flickering/velocity clamp | `PBZ.exe` UTF-16 字符串（r.TSR.History.SampleCount、r.TSR.History.ScreenPercentage、r.TSR.ShadingRejection.Flickering、r.TSR.Velocity.WeightClampingSampleCount） |
| UE5.7 内嵌 cvar 帮助：r.MotionBlurQuality 0-4、r.DefaultFeature.AntiAliasingMethod 4=TSR(default) | `PBZ.exe` 字符串（偏移 174548800 / 188278848） |
| PBZ FG 仲裁与 cvar | `PBZ.exe` 字符串：PBZFrameGenerationStartup、cr.PBZ.FrameGeneration.Backend、r.PBZ.SuppressFrameGen |

---

## 11. 参考链接（外部资料）

- Epic 官方论坛（2025-09-29）：[SetGamePaused / APlayerController::SetPause() causes MotionBlur-like artifacts on Objects animated with WorldPostionOffset even if motion blur is disabled](https://forums.unrealengine.com/t/setgamepaused-aplayercontroller-setpause-causes-motionblur-like-artifacts-on-objects-animated-with-worldpostionoffset-even-if-motion-blur-is-disabled/2681490)
- Tom Looman：[Moving Unreal Engine Camera While Game Is Paused](https://tomlooman.com/unreal-engine-moving-camera-paused/)
- Epic 旧帖（2017，`bIsCameraMoveableWhenPaused` 建议）：[Set Game Paused - Horrible rendering artifacts?](https://forums.unrealengine.com/t/set-game-paused-horrible-rendering-artifacts/91249)

> 注：外部链接内容为社区/官方论坛讨论，属于外部资料；本报告引用其结论作为机制假设，最终以本机 §7 实验为准。

---

## 12. 版本与复现记录

- 分析基于工作区当前 `PBZPhotoModeCpp` 源码与 `D:\Steam\steamapps\common\PBZ` 安装目录；未修改、未 build。
- 实机日志 `platform/steam/ue4ss/UE4SS.log`（2026-11-03 13:30–13:31）确认：`游戏暂停成功 | IsGamePaused=true（bShouldPerformFullTickWhenPaused=true）`、`MPC 快照：34 个集合、347 个参数`、`相机治理（冻结）：shake停用=成功，修饰器 2 个、本次停用 2 个`、`风源归零：风源 1 个、归零 1 个`。
- 当前安装的 `config.ini` 中 `freeze_diag=false`，因此日志没有「冻结诊断」行；建议做 §7 实验前改为 true。
