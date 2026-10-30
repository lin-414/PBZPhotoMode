# PBZ Photo Mode — 影之刃零摄影模式（C++）

基于 UE4SS 的 C++ 摄影模式 Mod：核心逻辑与引擎适配层分离，`src/core/` 可在没有游戏与 UE4SS 的环境下运行单元测试。

| 项目 | 状态 |
| --- | --- |
| 版本 | v1.0.13 |
| 核心层测试 | 257 项，全部通过 |
| 实机验证 | F 键冻结时间已通过（游戏暂停模式）；v1.0.12 HUD 隐藏待验证；v1.0.13 已移除 F9 截图 |

## 项目结构

```
src/core/      纯逻辑，不碰引擎 —— 已完成，257 项断言全绿
  config        配置解析（含容错、范围夹紧、默认配置生成）
  vk            键名 -> 虚拟键码（F1..F12 / ADD / SUBTRACT / OEM_PLUS / 修饰键…）
  input_logic   按键/鼠标 -> Intent（含 Alt 门控、滚轮累积、按下沿）
  math3         前/右向量、俯仰夹紧、偏航回绕
  photo_mode    进/退/每帧状态机（含失败回滚、幂等退出、1 秒视角守护）
  menu_model    菜单数据与文字渲染（ImGui 与文字兜底共用）

src/engine/    引擎适配层 —— 已完成，随 DLL 在实机运行
  ue4ss_port    EnginePort 的 UE4SS 反射实现（所有 UE 调用集中在 12 个 /// P1 辅助函数里）

src/mod_app    CppUserModBase 子类：配置加载、输入绑定、每帧 tick、ImGui 绘制
src/ui_tab     ImGui 面板（滑块/按钮，纯展示 + 回传命令）
```

### 立刻可以验证的部分

```bat
tools\build_tests.bat
```

不需要游戏、不需要 UE4SS，只需要 VS 2022 生成工具。当前输出：`检查 257 项，失败 0 项`。

## 键位

| 操作 | 按键 |
| --- | --- |
| 进入/退出摄影模式 | `F2` |
| 自由转视角 | 按住 `Alt` + 移动鼠标 |
| 前后左右平移 | `W` `A` `S` `D` |
| 升降 | `E` / `Q`，或 左键上移 / 右键下移 |
| 加速 / 减速 | `Shift` / `Ctrl` |
| 调整 FOV | 鼠标滚轮（向上缩小 / 向下放大）、小键盘 `+` / `-` |
| 重置相机视角 | `R`（回到进入摄影模式时的位置/朝向/FOV） |
| 冻结时间 | `F` |
| 隐藏菜单 | `T` |

键位都能在 `config.ini` 里改（`[keys]` 段）。

## 配置文件

`config.ini`（示例见 `config.ini.example`）。写错不会崩：非法值保留默认、并在日志里给一条 warning。

## 构建 DLL 的前置条件

1. **UE4SS 源码** —— 自行克隆 RE-UE4SS，用 `tools\build_dll.bat <路径>` 指定
2. **UEPseudo 依赖** —— RE-UE4SS 的私有子模块，需要 GitHub 账号被 Epic 授权
3. **Rust 工具链** —— UE4SS 的构建链路需要
4. **CMake + VS 2022 C++ 工具链**

具体步骤见 `docs/BUILD.md`，API 核对清单见 `docs/P1_CHECKLIST.md`。

## 路线图

- **P0**：核心逻辑 + 单元测试 → 完成
- **P1**：引擎适配层与 DLL → 完成，已实机运行
- **P2**：PostProcess 快照还原、滤镜、连拍、录像

## 1.0.3 这次改了什么

1. **鼠标增量改从引擎取**：hook `PlayerController::InputAxis`，直接用引擎算好的 `MouseX`/`MouseY`/`MouseWheelAxis` 增量。
   系统层 Raw Input 只是兜底 —— 上一版日志显示 Raw Input 在游戏里会「装上但收不到事件」，这条路不可靠。
2. **进模式时打一份引擎对象清单**：PC / Pawn / HUD / MyHUD / 相机 Actor / 相机组件 / ViewTarget.Actor / PC 上和输入有关的函数名。
3. **每秒诊断行加了相机状态**：管理器视角坐标、我们相机的坐标与朝向、组件 FOV、ViewTarget 是不是我们的相机。
4. **ViewTarget 没接管就每秒重试**一次，并把 SpawnActor / SetViewTargetWithBlend 的失败明确写进日志（以前是静默返回）。
5. 菜单文本同时写进 UE 日志（bPrintToLog），用来判断 PrintString 到底有没有执行。

## 出问题怎么报

按这个顺序操作，然后把 `platform\steam\ue4ss\UE4SS.log` 发回来：

1. 进游戏、读取存档、按 F2 进入摄影模式（日志出现「进入摄影模式」）
2. 按住 Alt，鼠标左右各移动几秒
3. 鼠标滚轮上下各滚 3 格
4. 再按一次 F2 退出
5. 退出游戏

日志里这几类行是关键：

- 「---- 引擎对象 ----」之后那几行（PC/Pawn/HUD/相机/ViewTarget）
- 「PC 输入相关函数: ...」（决定引擎输入钩子能不能挂上）
- 「引擎输入钩子已启用」或「PlayerController 上没有 InputAxis/InputKey」
- 「相机接管确认」/「ViewTarget 校验失败」/「拿不到 UWorld」/「SpawnActor 返回空」
- 每秒一行「输入诊断 ... | 管理器视角=... 我们的相机=... 组件FOV=... ViewTarget=...」

## 1.0.4 这次改了什么（按你的日志逐条修）

日志证据 → 修法：

| 日志里的现象 | 真正原因 | 1.0.4 的改法 |
| --- | --- | --- |
| `PC 输入相关函数` 里没有 `InputAxis`/`InputKey` | 本作是 Enhanced Input，PlayerController 上没有这两个 UFUNCTION | 改用 `PlayerController.GetInputMouseDelta(DeltaX&, DeltaY&)`，直接读引擎这一帧的鼠标增量 |
| `raw=1 事件计数=0`、`raw失效=1` | Raw Input 注册成功但一个事件都收不到 | 滚轮改走 `WH_MOUSE_LL` 低级鼠标钩子（装在跑消息循环的那条线程上，只放行不拦截） |
| `HUD 上找不到 DrawHUD` | `AHUD::DrawHUD` 是 C++ 虚函数、不是 UFUNCTION | 改钩蓝图事件 `ReceiveDrawHUD(SizeX, SizeY)`，用 `AHUD::DrawText` + `GetTextSize` 画右下角菜单 |
| 我们的相机=(4972,280454) 朝向=(0,0)，管理器视角=(4795,280138) | 相机被生成在**角色原点**、朝向 0，等于卡在角色身体里 | 初始位姿改用相机管理器的真实视角 `GetCameraLocation`/`GetCameraRotation` |
| `读不到 PlayerController.ViewTarget.Actor` | 这个属性在本作读不出来 | 改读 `PlayerCameraManager.ViewTarget.Target`，能读到就打印当前视角目标是谁 |
| 按 W 不是向前走 | 视角没被摄影相机接管，相机朝向是 0，WASD 的参考方向就错了 | 上面两条修好后，移动基准跟着相机朝向走 |

另外：进模式时会打印「已屏蔽游戏键鼠输入」，退出时也会逐项还原并打日志。

## 1.0.9 这次改了什么（按 13:02 真机日志）

- **修 Alt + 鼠标上下反向**：引擎 `GetInputMouseDelta` / `InputAxis` 的 `MouseY` 是「向上为正」，
  而 core 的 `Intent.look_dy` 和 Win32 原始坐标是「向下为正」。旧代码直接把引擎 Y 喂给 core，
  所以鼠标上移被当成下移。现在引擎/轴输入统一取反 Y，`invert_y` 仍可再反转。
- **修滚轮 FOV 方向**：`Intent.wheel` 正值定义为滚轮向上；现在向上 `FOV -= wheel * fov_step`（拉近），
  向下则放大。旧代码是加的，方向相反。
- **新增 `R` 重置相机视角**：回到进入摄影模式时快照的位置/朝向/FOV（`key_reset` 可改键）。
- 顺手修了 `handle_intent` 和 `PhotoMode::tick` 重复调用 `toggle_freeze` 导致的“按 F 没反应/双翻转”。
- 核心层测试 236 → **252 项全绿**。


## 1.0.8 这次改了什么（按 12:25 真机日志）

- **修相机朝向永远 (0,0)**：根因不是输入，也不是 Mobility，而是 UE4SS 的 UEPseudo 里
  `AActor::K2_SetActorRotation` 包装按 float 往 `FRotator`（UE5 是 3 个 double）里写值，等于写 0。
  现在改为按反射属性写 `NewRotation.Pitch/Yaw/Roll`，自动按 FDoubleProperty 处理。
  相机位置能动、鼠标也进了 core，但画面不转的根因就是这一条。
- **修 WASD 与视角方向对不上**：这是上一条的连带现象 —— 移动方向用的是 core 里的 yaw，画面用的却是
  被写坏的相机旋转 (0,0)，两者自然不一致。旋转修好后两者同源。
- **修进入摄影模式没有菜单**：`KismetSystemLibrary::PrintString` 的参数名是小写 `duration`，
  旧代码写成 `Duration` 导致 duration=0，消息瞬间消失。现在 duration=1.0 并每 0.35 秒重推，
  按视口尺寸补空行/空格推到右下角；HUD Canvas 路径只作为命中 `ReceiveDrawHUD` 时的主路径。


## 1.0.10 这次改了什么（按 13:25 真机日志：F 键冻结时间不生效）

- 13:16:59 / 13:17:07 的日志里 F 键确实触发了 `时间已冻结` / `时间已恢复`，说明按键和状态机没问题；
  问题是 `UGameplayStatics::SetGlobalTimeDilation` 在这个作里只是“调用成功”，实际倍率没变。
- UE5 的 `AWorldSettings::SetTimeDilation` 会把目标值夹在 `MinGlobalTimeDilation` / `MaxGlobalTimeDilation` 之间；
  且 `GetEffectiveTimeDilation()` 在 `bAllowTimeDilation=false` 时直接返回 1.0。
- 现在直接写 `AWorldSettings.bAllowTimeDilation / TimeDilation / MinGlobalTimeDilation / MaxGlobalTimeDilation`，
  再调用一次 `SetGlobalTimeDilation` 并回读校验，解冻/退出时恢复原始字段。
- 进入摄影模式时日志会多一行 `时间设置：WorldSettings bAllowTimeDilation=... TimeDilation=... Min=... Max=...`，
  下次如果还没冻结成功，把这行发回来即可直接定位。

## 1.0.11 这次改了什么（15:50 日志：TimeDilation 已生效，但角色/敌人/特效仍在动）

- 15:50:15 的日志：`时间设置目标=0.0001 生效=0.0001`，说明 `AWorldSettings.TimeDilation` 确实写进去了；
  但你在游戏里看到角色/敌人/NPC 的动作和特效继续播放，说明本作的 SGame 玩法模拟并不吃全局时间膨胀。
- 因此 v1.0.11 把默认冻结方式改成 **pause（游戏暂停）**，不再依赖 `TimeDilation`：
  - 暂停时临时把 `APlayerController.bShouldPerformFullTickWhenPaused` 设为 true，让相机管理器继续 Tick，
    摄影相机仍可自由移动/转视角；
  - 如 GameMode 的 `bPauseable` 被关掉，也会临时打开，退出/解冻时原样恢复；
  - 解冻/退出用 `IsGamePaused` 回读确认。
- 想保留原来的时间膨胀行为，可在 `config.ini` 把 `freeze_mode` 改回 `dilation`。
- 版本 `1.0.11-cpp`；核心层测试 252 项全绿；打包为 `dist/PBZPhotoModeCpp_1.0.11-cpp.zip`。

## 1.0.11 实机验证

2026-10-30 16:04 用户日志确认：`游戏暂停成功 | IsGamePaused=true` → `时间已冻结`，暂停期间相机仍可移动；`IsGamePaused=false` → `时间已恢复`。F 键冻结时间已验证通过。

## 1.0.12 这次改了什么（进入摄影模式自动隐藏 HUD）

- 旧版 `hide_game_hud` 只是把 `PlayerController.MyHUD`（AHUD Actor）`SetActorHiddenInGame`，对本作纯 UMG 的 HUD 无效。
- 1.0.12 首先调用游戏自带的 `UPBZUIManagerSubsystem::SetHideUIForPV(true/false)` 总开关（它会广播 `OnHideUIForPVChanged`，
  Boss 血条、教程提示等订阅者一起隐藏/恢复）；同时找 UMG HUD 根控件 `W_FrontEndHUDLayout_C`
  （`UPBZHUDLayout` 的唯一蓝图子类，HP/快捷栏/武器/任务等挂在它下面），通过
  `UWidget::SetVisibility(ESlateVisibility::Collapsed)` 隐藏，退出时按进入前读到的原始 `Visibility` 值恢复。
- 如果进入模式时 HUD 根控件还没建好，每秒重试一次；成功后不再重复遍历对象数组，退出时清理记录。
- `hide_game_hud` 默认值改为 `true`；想保留游戏 HUD 可在 `config.ini` 改回 `false`。
- 进入摄影模式时的引擎对象日志会多出 `HUD控件=... Visibility=...`，用于确认根控件是否找到。
- 版本 `1.0.12-cpp`；核心层测试 258 项全绿；打包为 `dist/PBZPhotoModeCpp_1.0.12-cpp.zip`。

## 1.0.13 这次改了什么（移除 F9 截图）

- 曾尝试用 `HighResShot` 实现 F9 截图，但本作 `BP_PlayerController_C` 没有 `ConsoleCommand` UFunction；直接调用 `UGameViewportClient` 虚函数存在 ABI/崩溃风险，已全部移除。
- 截图请使用 NVIDIA App / GeForce Experience（默认 `Alt + F1`）或 Steam（`F12`）等系统工具。
- 版本 `1.0.13-cpp`；核心层测试 257 项全绿。
