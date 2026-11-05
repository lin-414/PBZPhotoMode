# 冻结链路引擎侧审计：pause 后哪些系统仍在改变画面

- 审计对象：PBZPhotoModeCpp 1.0.75-cpp（UE 5.7 / Steam build 26266538；UE4SS.log:75 记录 EngineVersion: 5.7）
- 现象：冻结处于运动状态的物体时，物体出现重影、闪烁
- 审计方式：只读源码与 D:/Steam/steamapps/common/PBZ/platform/steam/ue4ss/UE4SS.log，不修改源码、不 build
- 说明：本工作区 HEAD 对应 1.0.75 发布版；当前工作树另有未提交的 MPC hotfix（见 §2.2）。本文以 1.0.75 实机二进制 = HEAD 为主线，工作树修改单独标注。

---

## 0. 结论摘要

1. **最可能原因 #1（机制高置信度，需实机 A/B 确认是本例主因）：缺少 UWorld.bIsCameraMoveableWhenPaused=true。**
   本仓库只设置了 APlayerController.bShouldPerformFullTickWhenPaused（ue4ss_port_time_pause.cpp:236-244），全仓库从未写入 UWorld.bIsCameraMoveableWhenPaused（源码搜索 0 命中）。于是暂停时引擎按“相机不可移动”处理（UWorld::IsCameraMoveable() 返回 false），而模组实际让摄影相机每帧移动/转向：TAA/TSR 的 pause 路径、运动模糊与历史帧会拿“静止相机”的旧视角矩阵去重投影，产生拖影/重影/边缘闪烁。Tom Looman 的标准做法是 **两个 flag 一起设**，并明确说后者用于修 TAA/MotionBlur 在暂停时的 glitch；Epic 论坛亦有多例（详见 §3.1 证据）。
2. **最可能原因 #2（代码+日志实锤）：1.0.75 的 freeze_mpc 写回实际全部失败，MPC 钉住是空操作。**
   实机日志唯一一条 UFUNCTION 参数名未命中（候选: Value）（UE4SS.log:931）来自 mpc_set_vector 的 params.prop({"Value"})（HEAD ue4ss_port_internal.h:1001）；mpc_set_scalar 用 params.set_number("Value", ...)（HEAD :958）没有告警、直接静默失败。MPC 快照：34 个集合、347 个参数（UE4SS.log:810）只证明读取成功，不证明写回成功。冻结期由 MPC 驱动的 WPO（布料、植被、天气、部分角色材质）仍在每帧变化，会被 TSR/TAA 放大为边缘闪烁/重影。当前工作树的未提交 hotfix 已改为按 Value/ParameterValue/第 4 参数定位（ue4ss_port_internal.h:956-974、1038-1110），但 1.0.75 发布包没有这个修复。
3. **候选原因 #3（中置信度，待 freeze_diag 实测）：每帧 reassert_view_target 与游戏自身每帧设置 ViewTarget 竞争。** on_engine_tick_post 每帧调用 reassert_view_target()（mod_app.cpp:166-169 → ue4ss_port_camera_view.cpp:450-454），即每帧 SetViewTargetWithBlend(摄影相机, BlendTime=0)。同目标时 APlayerCameraManager::SetViewTarget 会 early-out，通常不会重置 CameraCache；但如果游戏在 PlayerCameraManager::UpdateCamera 之后改写 ViewTarget，本帧 CameraCache 会落后一帧（UE4SS.log:927 显示本作 PC 侧 ViewTarget 不可读，无法用日志确认）。风险低于 #1/#2，但不能排除。
4. **可排除项（本次日志高置信度）：** 8 次冻结全程没有 检测到暂停被游戏解除、冻结已失效、冻结状态已恢复 记录（全文 0 命中），每次 游戏暂停成功 | IsGamePaused=true（如 UE4SS.log:807/975/1041/1107/1181/1214/1266/1332）。本日志中“暂停被游戏解除”不是变量；若用户现场仍见重影，应优先看 #1/#2 与渲染侧速度缓冲。

---

## 1. 冻结链路：代码事实（freeze_mode=pause）

### 1.1 进入/冻结顺序

PhotoMode::enter() 在 auto_pause_time=true（默认）时直接调用 toggle_freeze()（photo_mode.cpp:191-195；实机 UE4SS.log:806）。toggle_freeze() 中 use_pause=true 的处理链：

1. time_pause_.set_paused(true)（photo_mode.cpp:437-444）
2. 成功才把 frozen_=true、frozen_with_pause_=true（photo_mode.cpp:462-463）；失败则回退 set_time_dilation(cfg_.freeze_dilation)（photo_mode.cpp:441-449）
3. 冻结治理顺序：相机 shake/修饰器 → 风源 → MPC 快照（photo_mode.cpp:471-498）
4. Sequencer 剧情播放器暂停（photo_mode.cpp:500-504）
5. 对话 Flow 控件改 Collapsed 停 Slate Tick（photo_mode.cpp:506-516，仅 hide_subtitles=true 时）
6. Wwise 逐 PlayingID 真暂停；无 ID 才整引擎 Suspend 兜底（photo_mode.cpp:518-551）

### 1.2 set_paused(true) 实际做了什么、没做什么

ue4ss_port_time_pause.cpp:218-341：

| 动作 | 代码 | 备注 |
|---|---|---|
| 快照 APlayerController.bShouldPerformFullTickWhenPaused | :235-239 | 只有属性可读才采样；读不到不写 |
| 原值 false 时写 true | :240-244 | 保证暂停期相机管理器/PC 仍完整 Tick |
| 快照并在必要时写 AGameModeBase.bPauseable=true | :248-263 | 游戏禁止暂停时的兜底 |
| PlayerController.SetPause(true) | :267 | 失败再走 UGameplayStatics.SetGamePaused（:269-277） |
| 回读 IsGamePaused | :280-295 | 日志 IsGamePaused=true 是真实回读（UE4SS.log:807） |

注意：日志行尾“（bShouldPerformFullTickWhenPaused=true，相机仍可移动）”是**固定文案**（ue4ss_port_time_pause.cpp:335-339），不代表该属性一定写成功；若属性名读不到，代码静默跳过。判据只能看相机是否真的可动。

### 1.3 freeze_mode=pause 期间仍在运行的每帧路径

| 路径 | 频率 | 代码 | 实机日志 |
|---|---|---|---|
| 摄影模式 tick（相机输入、HUD 重试、每秒 ViewTarget 校验、冻结自检） | 每帧（EngineTickPre） | mod_app.cpp:128-141、461-643；photo_mode.cpp:617-893 | 正常运行 |
| on_engine_tick_post → reassert_view_target() | **每帧**（EngineTickPost） | mod_app.cpp:143-169；ue4ss_port_camera_view.cpp:450-454 | 无逐帧日志 |
| mpc_hold_snapshot() | 每冻结帧调用，内部限频 4Hz | photo_mode.cpp:839-841；ue4ss_port_micro_motion.cpp:253-262 | 1.0.75 实际未写入（§2.2） |
| pov_watch_tick() | 每冻结帧，但受 freeze_diag 门控 | photo_mode.cpp:842-844；ue4ss_port_micro_motion.cpp:310-354 | 本日志无（freeze_diag=false） |
| 冻结状态每秒校验 | 1Hz | photo_mode.cpp:845-891 | 无失守记录 |
| PlayerController 全 Tick → PlayerCameraManager::UpdateCamera | 每帧 | bShouldPerformFullTickWhenPaused=true 的引擎语义（见 §1.4） | 相机可动即间接证明 |
| 渲染线程（TSR/TAA/MotionBlur/材质 WPO/GPU Niagara） | 每帧 | 与 World 暂停无关（见 §1.4） | 需渲染侧判据 |

### 1.4 UE 5.7 暂停语义（哪些 tick/不 tick）

以下为代码语义判断；UE5.7 未随仓库提供引擎源码，能由本机反射信息佐证的已标注出处：

- **World Tick**：SetPause 后 UWorld::Tick 走 LEVELTICK_PauseTick；普通 Actor/Component 只有在 FTickFunction::bTickEvenWhenPaused=true 时才 tick；UWorld::TimeSeconds 不再推进。UWorld::bIsCameraMoveableWhenPaused 成员在 5.7 中存在：UE4SS.log:616 的 MemberVariableLayout 记录 UWorld::bIsCameraMoveableWhenPaused = 0x18E。
- **PlayerController**：APlayerController 的 PrimaryActorTick.bTickEvenWhenPaused 默认 true；bShouldPerformFullTickWhenPaused=false 时暂停仅走最小 Tick（输入+空 Tick），为 true 时走完整 PlayerTick（含相机管理器更新）。本模组依赖的正是后者（ue4ss_port_time_pause.cpp:230-244）。
- **PlayerCameraManager**：自身不是独立“暂停特例”，随 PlayerController 的完整 Tick 调 UpdateCamera(DeltaTime)；它每帧重置 ViewTarget.POV 的 FOV/后处理等字段，调用 ViewTarget.Target->CalcCamera()，再 ApplyCameraModifiers()，最后 FillCameraCache()（UE 源码同源实现：PlayerCameraManager.cpp 的 UpdateViewTarget/UpdateCamera）。摄影相机 Actor 静止时 POV 应逐帧一致；只要相机管理器仍有代码推 POV 或修饰器未禁用，就会每帧变。
- **渲染线程**：World 暂停不会停渲染；每帧仍会生成 ViewFamily、TSR/TAA 历史、速度缓冲、材质 WPO 的上一帧顶点位置。默认 Material Time 节点的 bIgnorePause=false，走 GameTime（暂停即停）；若材质勾了 Ignore Pause 或使用 RealTime/自定义 MPC 参数则继续。WPO/Nanite/Groom/HairCards/Chaos Cloth 的“上一帧顶点/速度”是否 pause-aware 是独立问题（见 §3.6）。
- **Niagara**：CPU 粒子随组件 Tick，World 暂停默认停；GPU Sim 由渲染线程推进，存在暂停期继续推进的可能，需实机对比（中低置信度）。
- **Slate/UMG**：Slate Tick 不依赖 World 暂停，UMG 动画可继续；本模组已把 HUD 隐藏，并把对话 FlowDialog 改 Collapsed（photo_mode.cpp:506-516），但本日志无 FlowDialog 命中（0 个），本次不是变量。
- **Physics**：世界暂停会停止游戏侧物理步进；Chaos Cloth/Groom 的“模拟已停但渲染速度缓冲仍引用上一帧”属于渲染侧残余（论坛案例见 §3.6）。
- **UE4SS EngineTick 钩子**：RegisterEngineTickPre/PostCallback 挂在 UEngine::Tick（_ref/ue4ss-src/deps/first/Unreal/src/UnrealInitializer.cpp:1168-1236；Hooks.cpp:240-247），每帧都派发，与 World 是否暂停无关；实机钩子注册见 UE4SS.log:689-690。因此 on_engine_tick_post 的每帧 reassert 在冻结期确实一直执行。

---

## 2. 1.0.75 实机日志核对（D:/Steam/steamapps/common/PBZ/platform/steam/ue4ss/UE4SS.log）

### 2.1 冻结段落概况

- 加载版本：1.0.75-cpp（UE4SS.log:691）；引擎 5.7（:75）
- 共 8 次 时间已冻结（:914/984/1050/1116/1190/1223/1275/1341），每次 游戏暂停成功 | IsGamePaused=true（:807/975/1041/1107/1181/1214/1266/1332）
- 每段冻结期间日志几乎只有进入时的治理行与离开时的恢复行；无逐帧冻结诊断（因为配置关闭）
- 无 检测到暂停被游戏解除、冻结已失效、冻结状态已恢复、时间缩放被游戏改写（全文 0 命中）→ **本日志没有暂停被抢回的实锤**

### 2.2 MPC 写回失败警告（关键实锤）

- 日志原文（唯一一条，无 [PBZPhotoMode] 前缀，来自 ParamBlock 的 emit() 旁路）：
  UE4SS.log:931 UFUNCTION 参数名未命中（候选: Value），按 M-7 不再按声明位置退化；若功能异常请把日志反馈给作者
- 对应 1.0.75（HEAD）代码：
  - ue4ss_port_internal.h:379-389 的旧版 warn_param_name_miss 只打印候选名（与日志逐字一致）；当前工作树已扩成带函数名/实际参数（ue4ss_port_internal.h:423-444），所以该日志证明实机 DLL 是 HEAD 旧版。
  - mpc_set_vector：FProperty* value_prop = params.prop({"Value"});（HEAD ue4ss_port_internal.h:1001）→ 参数名不命中 → 返回 nullptr → 所有向量参数不写；并触发上面一次性警告。
  - mpc_set_scalar：if (!params.set_number("Value", value)) return false;（HEAD ue4ss_port_internal.h:958）→ find_property 找不到 Value 直接返回 false，**无日志**，所有标量参数不写。
  - 本作实际签名是 ParameterValue（工作树注释 ue4ss_port_internal.h:951-954；1.0.49 实机也记录过 Get... 返回 ReturnValue）。
- 因此 1.0.75 的 MPC 快照：34 个集合、347 个参数（冻结期每帧写回）（UE4SS.log:810 等）**只说明快照读取成功**；mpc_hold_snapshot() 的 written 计数从未打日志，无法证明任何一次 Set 成功。结论：**1.0.75 的 freeze_mpc 对画面无实际约束作用**，MPC 驱动的 WPO/布料/天气/植被在冻结期继续演化。
- 当前工作树 hotfix（未提交）：mpc_value_property_iterate 依次找 Value/ParameterValue，再退第 4 个参数（ue4ss_port_internal.h:956-965），mpc_set_scalar/vector 改为用它（:1038-1110），并新增 MPC 签名诊断 日志（:1006-1018）。这是修复方向，但不在 1.0.75 发布包内，故本次日志不能作为“MPC 已压住”的证据。

### 2.3 有无 freeze_diag / POV 数据

- 配置：安装目录 .../Mods/PBZPhotoModeCpp/config.ini:140 freeze_diag = false（默认值同 core/config.h:93）。
- 全文搜索 冻结诊断、视角微动、POV：**0 条**；因此：
  - 没有 视角微动:位移x 旋转y°，无法判断暂停期相机管理器最终 POV 是否仍在动；
  - 没有 MPC 变化:...，无法判断 MPC 参数是否在冻结期被游戏改写；
  - 没有 修饰器=x(停用y) shake=z 的每秒状态（只有进入时一条 相机治理（冻结）...，UE4SS.log:808）。
- 结论：对“相机微动 vs 材质侧变化”的二分，本日志**不能作证**；必须按 §4 的 E1 开 freeze_diag=true 复测。

### 2.4 reassert_view_target 每帧调用与 ViewTarget/CameraCache 抖动判断

- 代码事实：on_engine_tick_post() 在 mode_->active() 时无条件调用 port_->reassert_view_target()（mod_app.cpp:166-169），后者每帧 set_view_target_cached(pc, camera_actor_)（ue4ss_port_camera_view.cpp:313-333/450-454）。注释也自认“reassert_view_target 每帧一次”（ue4ss_port_camera_view.cpp:311）。
- 稳定性判断：
  - APlayerCameraManager::SetViewTarget 在同一 NewTarget == ViewTarget.Target 且无 Pending 时，只有 else 分支把 PendingViewTarget.Target=NULL，不会重设 ViewTarget/POV/CameraCache；BlendTime=0 走瞬时赋值。**正常情况下每帧重复调用是 no-op，不会自身制造 CameraCache 抖动**（UE4 同源源码；5.7 未逐行核对）。
  - 风险点 A：若游戏 BP/PC 每帧先设回游戏相机、PlayerCameraManager::UpdateCamera 已按游戏相机填过 CameraCache，而 EngineTickPost 才把 ViewTarget 改回摄影相机，则当帧渲染可能用上一目标算出的 CameraCache → 1 帧错位。mod_app.cpp:143-144 注释明确写“压过游戏自己每帧设置 ViewTarget 的行为”，说明游戏确有每帧抢占行为；这是本候选的现实基础。
  - 风险点 B：UE4SS.log:927/997/1063/... 每次进入都 读不到 PlayerController.ViewTarget.Actor（校验跳过），本作 PC 侧 ViewTarget 不可读，无法用 PC 字段做逐帧确认；管理器侧 ViewTarget=CameraActor（UE4SS.log:926）只证明某一时刻正确。
  - 风险点 C：若存在 Director/Sequencer 轨道，APlayerController::SetViewTarget 每次调用都会写 Director->OldViewTarget，可能改变演出结束后的回退目标（副作用，非抖动）。
- 结论：**“每帧 reassert 本身”造成 CameraCache 抖动的概率低（约 10-20%），但“游戏每帧抢 ViewTarget + reassert 在 UpdateCamera 之后”这一组合值得实测（中置信度）**。判据：开 freeze_diag 看 视角微动；或加 debug_input_diag.txt 看每秒 管理器视角 与 我们的相机 是否持续偏差（见 §4 E1/E2）。

### 2.5 其它日志事实

- 管理器 ViewTarget=CameraActor ...（UE4SS.log:926）→ 进入后视角确实指向摄影相机。
- ViewTarget 回读暂未确认（同帧未生效或属性不可读），继续每秒重试接管（:932）只出现一次（M-1 已知现象），不是逐帧抖动。
- 8 次冻结的 MPC 快照 参数数在 347/344 间波动（:810/978/1044/1110/1184/1217/1269/1335），说明集合枚举有少量变化；不影响“写回失败”的结论。
- 语音位置校验均为 暂停期位置未变 N 个，前进 0 个（:938 等）→ Wwise 语音暂停有效，排除音频侧“声音继续走”造成的错觉（但音频不是画面重影来源）。

---

## 3. 残余变化源清单（证据 → 推断 → 置信度）

| # | 变化源 | 机制 | 证据 | 置信度 | 是否解释重影/闪烁 |
|---|---|---|---|---|---|
| 1 | **缺少 UWorld.bIsCameraMoveableWhenPaused** | 只设 PC 全 Tick，不告诉引擎“暂停中相机可移动”；UWorld::IsCameraMoveable() 为 false → ViewFamily bWorldIsPaused=true/TAA pause 修正按静止相机工作；实际摄影相机在动 → TAA/TSR 历史与视图矩阵错配，产生拖影/重影/边缘闪烁 | 全仓库 0 命中（PBZPhotoModeCpp/src 搜索 bIsCameraMoveableWhenPaused=0）；UE4SS.log:616 证明 5.7 成员存在；Tom Looman 文章明确“两个 flag 一起设，后者修 TAA/MotionBlur glitches”；Epic 论坛多例（含 5.0.3、UE5.6 WPO 案例） | **高（机制）**；对应用例为**中高（需 A/B）** | **是，最优先验证** |
| 2 | **1.0.75 MPC 写回全失败** | freeze_mpc 名义钉住 347 参数，实际 SetScalar/SetVector 均因参数名 Value 不匹配而不写；MPC 驱动的 WPO/布料/植被/天气每帧继续变 | UE4SS.log:931；HEAD ue4ss_port_internal.h:958/1001；快照行 :810 | **实锤（代码+日志）**；对具体物体贡献取决于材质 | 是（布料/植被/天气/角色材质类） |
| 3 | **每帧 reassert_view_target** | 每帧 SetViewTargetWithBlend(...,0)；同目标通常 no-op；若游戏每帧先抢 ViewTarget 且 UpdateCamera 已填 CameraCache，则当帧 POV 可能落后一帧 | mod_app.cpp:166-169；ue4ss_port_camera_view.cpp:313-333/450-454/311；UE4SS.log:927 PC 侧不可读 | 低-中（抖动风险）；**代码事实高** | 可能（相机移动/视角跳变时） |
| 4 | 相机修饰器/自定义相机管理器 POV | StopAllCameraShakes(true) + 禁用 2 个修饰器已执行；但 PBZPlayerCameraManager 自身代码可能在暂停期仍推 POV；POV 未测 | ue4ss_port_micro_motion.cpp:18-83；UE4SS.log:808；freeze_diag=false 无 POV 数据 | 中（需 E1） | 可能（全画面边缘 shimmer） |
| 5 | PlayerController 全 Tick 中的游戏 BP 逻辑 | bShouldPerformFullTickWhenPaused=true 让 BP_PlayerController_C 的 Tick/输入/相机管理每帧执行；可能改 ControlRotation、ViewTarget、后处理 | ue4ss_port_time_pause.cpp:230-244；BP_PlayerController_C 见 UE4SS.log:917 | 中低（需逐帧 POV/ViewTarget 实验） | 可能 |
| 6 | WPO / Nanite / Groom / HairCards / Chaos Cloth 速度缓冲 | 暂停瞬间物体处于运动，上一帧顶点/WPO 时间仍在速度缓冲里；TSR 把旧速度当新运动 → motion-blur-like ghost；Epic 2025 论坛案例：WPO mid-motion 暂停必现，TSR 的 TSR.Velocity.HoleFilled 含“像没暂停”的速度；修复涉及 SceneView 的 World->bIsCameraMoveableWhenPaused && World->IsPaused() 分支及 HairCards/GpuSkin/NiagaraSprite/NiagaraMesh/Nanite WPO 多个 shader | Epic 论坛 setgamepaused-...-worldpostionoffset...（2025-09/10/11）；UE4SS.log:75 5.7 | **高（机制）**；具体资产需实机 | 是（最符合“运动物体重影”） |
| 7 | Material Time 节点 | 默认 bIgnorePause=false 走 GameTime，暂停会停；勾 Ignore Pause=true 或用 RealTime/自定义参数则继续。不能一概说“材质时钟暂停拦不住” | ueHow/UE 文档：Time 节点 Ignore Pause 默认 False；工作树历史注释（PROGRESS.md:232-239）曾把 Time 当主嫌，需按材质资产区分 | 中（按资产） | 可能（仅 IgnorePause/自定义材质） |
| 8 | Niagara | CPU 粒子随组件 Tick，默认停；GPU Sim 渲染线程推进可能继续；本日志无对象级证据 | 引擎常识；UE4SS.log 无 Niagara 记录 | 低-中（需实机） | 可能（特效物体） |
| 9 | Slate/UMG | Slate Tick 独立于 World；HUD 已隐藏，FlowDialog 本日志命中 0；不是本次主因 | photo_mode.cpp:506-516；UE4SS.log 无 FlowDialog | 低（本用例） | 否/很小 |
| 10 | Physics/布料模拟 | World 暂停停物理步进；模拟停后渲染速度缓冲仍可能引用上一帧（与 #6 同源） | 引擎语义；Epic 论坛案例 | 中（归入 #6） | 是（布料类物体） |
| 11 | 暂停被游戏解除/时间缩放被改写 | 冻结自检每秒 world_paused()/dilation 检查；本次日志 8 次均无失守 | photo_mode.cpp:845-891；UE4SS.log 0 条相关告警 | **高（可排除）** | 否（本次） |
| 12 | Wwise/音频 | 逐 PlayingID 暂停成功，语音位置前进 0ms；与画面重影无关 | UE4SS.log:938 等 | 高（可排除） | 否 |

### 3.1 关于 bIsCameraMoveableWhenPaused 的补充证据链

- 成员存在：UE4SS.log:616 UWorld::bIsCameraMoveableWhenPaused = 0x18E（UE4SS 从本作 5.7 反射生成的 MemberVariableLayout）。
- 引擎语义（UE4 同源源码）：UWorld::IsCameraMoveable() { return (!IsPaused() || bIsCameraMoveableWhenPaused || IsPlayingReplay()); }（LevelTick.cpp）；FSceneViewFamily::bWorldIsPaused 注释：GetWorld->IsPaused() && !Simulate，“Simulate is excluded as the camera can move which invalidates motionblur”（SceneView.h）。
- 实践证据：Tom Looman《Moving Unreal Engine Camera While Game Is Paused》：PlayerController::bShouldPerformFullTickWhenPaused(true) + UWorld::bIsCameraMoveableWhenPaused(true)，后者“doesn’t actually allow rotation itself but fixes TAA and MotionBlur while paused”。
- Epic 论坛：Set Game Paused - Horrible rendering artifacts? 中“SetGamePaused/SetPause 对 WPO 动画物体产生 MotionBlur-like artifact，即使关掉 motion blur”；回复“设置 UWorld->bIsCameraMoveableWhenPaused=true 有帮助”“fixes both TAA and MotionBlur”。2025 年 UE5.6 案例进一步指出 TSR velocity / WPO/Groom 上一帧数据问题，修复条件正是 World->bIsCameraMoveableWhenPaused && World->IsPaused()。
- 与本模组结合：本模组在 pause 下让相机可动（bShouldPerformFullTickWhenPaused + 每帧 apply_pose），但没有把世界标志置 true；如果 FSceneViewFamily::bWorldIsPaused 走 !IsCameraMoveable()，渲染器会以为相机静止，正好落入上述 glitch 条件。**结论：这是最符合“暂停+摄影相机移动+物体重影/闪烁”的引擎侧缺口。**

---

## 4. 判别实验（不改代码，配置/日志即可）

> 建议顺序：E1 → E2 → E3 → E4 → E5；每项只改一个变量，保留 UE4SS.log 与短视频/截图。

### E1：开 freeze_diag，冻结后 10 秒不碰键鼠，读 视角微动
- 操作：config.ini 改 freeze_diag = true；进摄影模式自动冻结；**完全不碰鼠标/键盘/手柄** 10 秒以上；退出后看日志 冻结诊断: 行的 视角微动:位移x 旋转y°。
- 判读：
  - 位移=0.000 旋转=0.00° → 相机管理器 POV 静止，剩余重影在材质/速度缓冲侧（#2/#6）；
  - 非零（哪怕 0.01°/0.001）→ 暂停期仍有代码推 POV（#3/#4/#5 之一），优先查 ViewTarget/自定义管理器。
- 同时确认 MPC 变化: 是否 无；若仍有变化且物体是布料/植被/天气 → #2 实锤。

### E2：相机静止 vs 轻推相机（5 秒 A/B）
- 操作：同一冻结画面，先静止录 5 秒/截两帧；再按住 Alt 轻转 5° 后停住录 5 秒；对比截图/录屏边缘。
- 判读：只在“相机移动后”出现拖影/闪烁 → 强烈指向 #1（TAA/TSR 用旧视角矩阵/pause 修正）或 #3（ViewTarget 竞争）；相机完全静止也闪 → #2/#6（物体速度/材质仍在变）。

### E3：TAA/运动模糊开关对照（控制台 CVar，不改代码）
- 操作：暂停画面下依次执行并各录 5 秒：
  1. r.TemporalAAPauseCorrect 0（默认 1；UE 5.4-5.8 均有此 CVar，作用是“Correct temporal AA in pause”）
  2. r.AntiAliasingMethod 0（临时关 TAA/TSR，改 FXAA/无 AA；执行前记录原值）
  3. r.MotionBlurQuality 0（用户当前 q.MotionBlurQualityLevel=1，先彻底关运动模糊）
- 判读：任一开关让重影明显消失/减弱 → 坐实 TAA/TSR/运动模糊路径（配合 #1 的 pause 标志检查）；都不变 → 更可能是材质 WPO/顶点动画本身仍在变（#2/#6）。
- 注意：r.AntiAliasingMethod 改后可能影响画质/性能，实验后按原值恢复；这是诊断，不是推荐设置。

### E4：freeze_mode=dilation 短时对照
- 操作：config.ini freeze_mode = dilation，freeze_dilation = 0.0001；进入后立即冻结，录 5 秒（本作有代码每秒把 TimeDilation 写回 1，可能坚持不了几秒，只做短时对照）。
- 判读：dilation 下重影/闪烁明显减轻或消失 → 说明是 pause 特有路径（#1/#3/pause TAA 修正）；dilation 下仍一样 → 材质/资产自身动画或 TSR 速度缓冲（#6），与 pause 实现无关。

### E5：freeze_mpc/freeze_wind 单变量对照（判断 #2 的真实占比）
- 操作（需重进模式生效）：分别记录 freeze_mpc=false、freeze_wind=false 与全开三组，同一场景同一机位录 5 秒；同时开 freeze_diag=true 看 MPC 变化/风源数值。
- 判读：1.0.75 上 freeze_mpc=true 与 false 应几乎无差别（写回全失败），若两者有差别说明该物体不由 MPC 主导；freeze_wind=false 时若布料明显更抖 → 风源路径仍在（风源归零代码是好的，UE4SS.log:809 风源 1 个归零 1 个）；若全开仍闪且 MPC 变化:无 → 剩余为 #6 的 WPO/顶点/速度缓冲。
- 补充：若拿到 hotfix 构建（工作树修好 MPC 后），此项可直接验证 MPC 快照 是否伴随 MPC 签名诊断 且参数变化被压住。

---

## 5. 审计局限与后续建议

- UE 5.7 引擎源码不在本工作区；§1.4/§3 的引擎行为来自 UE4 同源源码（Epic 官方仓库镜像）、Epic 文档/论坛与 UE4SS 反射，不是 5.7 逐行确认。已在每条标注置信度，最终以实机 E1-E4 为准。
- 建议下一版（不在本任务范围内，仅记录）：在 set_paused(true) 成功时同时读取/保存/写 UWorld.bIsCameraMoveableWhenPaused=true（退出/解冻恢复原值），与 bShouldPerformFullTickWhenPaused 成对管理；这是 Tom Looman/Epic 论坛给出的标准组合，也是本审计认为最可能直接消除“暂停+移动相机”类重影的引擎侧修复。
- 建议把 mpc_hold_snapshot() 的 written 值纳入日志（哪怕每 10 秒一条），否则“MPC 快照 347 参数”会继续误导为“已写回 347 参数”。

---

### 附：本文引用文件清单

- 仓库源码：PBZPhotoModeCpp/src/core/photo_mode.cpp、src/engine/ue4ss_port_time_pause.cpp、src/engine/ue4ss_port_micro_motion.cpp、src/engine/ue4ss_port_camera_view.cpp、src/engine/ue4ss_port_diagnostics.cpp、src/mod_app.cpp、src/core/config.h、src/engine/ue4ss_port_internal.h（HEAD 与工作树差异见 §2.2）
- 实机日志：D:/Steam/steamapps/common/PBZ/platform/steam/ue4ss/UE4SS.log（行号如上）
- 实机配置：D:/Steam/steamapps/common/PBZ/platform/steam/ue4ss/Mods/PBZPhotoModeCpp/config.ini
- UE4SS 源码：_ref/ue4ss-src/deps/first/Unreal/src/UnrealInitializer.cpp:1168-1236、.../Hooks/Hooks.cpp:240-247
- 外部参考（不构成游戏内事实，仅机制对照）：
  - Tom Looman, Moving Unreal Engine Camera While Game Is Paused：https://tomlooman.com/unreal-engine-moving-camera-paused/
  - Epic 论坛 Set Game Paused - Horrible rendering artifacts?：https://forums.unrealengine.com/t/set-game-paused-horrible-rendering-artifacts/91249
  - Epic 论坛 SetGamePaused/SetPause causes MotionBlur-like artifacts on Objects animated with WPO...：https://forums.unrealengine.com/t/setgamepaused-aplayercontroller-setpause-causes-motionblur-like-artifacts-on-objects-animated-with-worldpostionoffset-even-if-motion-blur-is-disabled/2681490
  - Unreal Directive, r.TemporalAAPauseCorrect：https://unrealdirective.com/resources/console-variables/r-temporalaapausecorrect/
