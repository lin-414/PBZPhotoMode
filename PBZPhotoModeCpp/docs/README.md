# PBZ Photo Mode — 影之刃零摄影模式（C++）

基于 UE4SS 的 C++ 摄影模式 Mod：核心逻辑与引擎适配层分离，`src/core/` 可在没有游戏与 UE4SS 的环境下运行单元测试。

| 项目 | 状态 |
| --- | --- |
| 版本 | v1.0.44 |
| 核心层测试 | 668 项，全部通过 |
| 实机验证 | F 键冻结、HUD 隐藏、进入自动暂停、动作冻结稳定已验证；**1.0.36 语音静音双保险（ID 暂停 38/38 + 音量兜底 38/38，解冻成对恢复）实机通过**；超宽屏/DInput 手柄/字幕压制各项待回归 |

## 项目结构

```
src/core/      纯逻辑，不碰引擎 —— 已完成
  config        配置解析（含容错、范围夹紧、默认配置生成）
  vk            键名 -> 虚拟键码（F1..F12 / ADD / SUBTRACT / OEM_PLUS / 修饰键…）
  input_logic   按键/鼠标 -> Intent（含 Alt 门控、滚轮累积、按下沿）
  math3         前/右向量、俯仰夹紧、偏航/滚转回绕
  photo_mode    进/退/每帧状态机（含失败回滚、幂等退出、1 秒视角守护）

src/engine/    引擎适配层 —— 已完成，随 DLL 在实机运行
  ue4ss_port    EnginePort 的 UE4SS 反射实现（相机/输入钩子/HUD 隐藏/时间冻结/画幅解锁）

src/mod_app    CppUserModBase 子类：配置加载、输入绑定、每帧 tick
src/ui_tab     UE4SS 控制台诊断面板（滑块/按钮，纯展示 + 回传命令；游戏内无菜单，纯快捷键操作）
```

### 立刻可以验证的部分

```bat
tools\build_tests.bat
```

不需要游戏、不需要 UE4SS，只需要 VS 2022 生成工具。当前输出：`检查 565 项，失败 0 项`。

## 键位

| 操作 | 按键 |
| --- | --- |
| 进入/退出摄影模式 | `F2` |
| 自由转视角 | 按住 `Alt` + 移动鼠标 |
| 前后左右平移 | `W` `A` `S` `D` |
| 方向键转向/俯仰 | 方向键 左/右转视角（`yaw_speed`，默认 90 度/秒）、上/下俯仰（`pitch_speed`，默认 30 度/秒） |
| 升降 | 左键上移 / 右键下移（translate 模式；手柄 RT / LT） |
| 滚转（旋转画面） | `Q` / `E`（E 顺时针 / Q 逆时针；`roll_speed` 可调）；手柄 `RB` / `LB` |
| 加速 / 减速 | `Shift` / `Ctrl` |
| 调整 FOV | 鼠标滚轮（向上缩小 / 向下放大）、小键盘 `+` / `-`（单击一格，按住约 0.35 秒后连发） |
| 重置相机视角 | `R`（回到进入摄影模式时的位置/朝向/FOV） |
| 滚转归零 | `Z`（只把滚转回 0，位置/朝向/FOV 不动）；手柄 `LB+RB` 同按（`key_roll_reset` / `pad_roll_reset` 可改） |
| 冻结时间 | `F`（进入时按 `auto_pause_time` 自动冻结） |

键位都能在 `config.ini` 里改（`[keys]` 段）。

## 手柄支持

- **Xbox 手柄**：XInput 读取（`XInputGetState` 轮询，强制加载 System32 的 DLL，绕开游戏目录代理）。
- **DualSense / DualShock4**：**不是 XInput 设备**，Windows 把它们枚举成 DirectInput 手柄——1.4.0 起由 DInput8 轮询后端读取（USB/蓝牙均可），与 XInput 翻译成同一份 `PadState`，键位写法与 Xbox 完全一致。XInput 槽位优先，避免 Xbox 手柄在两套后端重复计数。
- 映射（`core/gamepad.cpp`）：DIJOYSTATE2 按钮 0..13 → □╳○△/L1R1/L2R2/CREATE/OPTIONS/**L3=LS/R3=RS**/PS；轴 lX/lY/lZ/lRz=双摇杆（Y 取反）、lRx/lRy=L2/R2 模拟量；rgdwPOV=十字键。
- 固定键位：RT/LT 升降；**RB/LB 滚转**（RB 顺时针 / LB 逆时针，与键盘 Q/E 同向）；**L3/R3 变速**（LS 加速 / RS 减速；两杆同按是进出模式组合键，此时不触发变速）。
- 十字键：上/下俯仰（`pitch_speed`）、左/右转视角（`yaw_speed`，1.0.46 起从左右平移改来）；FOV 在 `A` / `B`（`pad_fov_up` / `pad_fov_down` 可改）：单击一格，按住约 0.35 秒后连发，松开即停。
- 接入诊断：`DInput 手柄已接入：<名>（VID_…&PID_…）首读：按钮=… 摇杆=… POV=…`（一次性，原始值，映射对不上真机时用它定位）；`手柄=<后端摘要>` 在每秒输入诊断行里。

## 配置文件

`config.ini`（示例见 `config.ini.example`）。写错不会崩：非法值保留默认、并在日志里给一条 warning。

## 构建 DLL 的前置条件

1. **UE4SS 源码** —— 自行克隆 RE-UE4SS，用 `tools\build_dll.bat <路径>` 指定
2. **UEPseudo 依赖** —— RE-UE4SS 的私有子模块，需要 GitHub 账号被 Epic 授权
3. **Rust 工具链** —— UE4SS 的构建链路需要
4. **CMake + VS 2022 C++ 工具链**

具体步骤见 `docs/BUILD.md`，API 核对清单见 `docs/P1_CHECKLIST.md`。

## 游戏内设置菜单（PBZ-Framework-Menu，可选）

1.0.42 起本模组是 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu)（游戏内 ImGui 菜单框架，DirectComposition 覆盖窗实现，不碰游戏交换链、不影响 FSR3 帧生成）的**第一个客户端模组**。两个模组各自解压进 `ue4ss\Mods\`（enabled.txt 启用，互不改 mods.txt）后：

- 游戏内按 PBZFM 的呼出键（默认 `F8`）打开菜单，侧栏出现「**PBZ Photo Mode**」分区（1.0.43 起两页均由本模组自绘，ImGui 版本与框架 1.92.1 对齐）
- **「设置 · Settings」页**：首行「语言」下拉——**跟随 PBZFM 菜单** / 中文 / English / 日本語 / 한국어 / 外部词典，当帧生效并写回 `config.ini` 的 `[ui] menu_language`；旁边「重新载入词典」按钮；其余为键盘键位（菜单内捕获式改键）、行为开关（进入时暂停时间 / 冻结方式 / 屏蔽输入 / 超宽屏解锁 / 手柄支持）、隐藏开关（角色 / HUD / 交互提示 / 敌人血条 / 字幕）、冻结增强（Wwise 静音 / 剧情暂停 / 相机震动 / 风源 / MPC / 诊断日志）、相机与移动参数、手柄参数——改动**实时生效并写回 `config.ini`**
- **「状态 · Status」页**：OFF / LIVE / FROZEN 状态、位置与朝向读数、键位速查；FOV 滑条（实时变焦）与「进入/退出摄影模式」「冻结/恢复时间」「重置视角」「重新载入 config.ini」按钮
- **多语言**：内置 zh-CN / en-US / ja-JP / ko-KR；**外部词典**放 `PBZPhotoModeCpp\languages\<id>.json`——UTF-8 扁平 JSON，key=中文原文、value=译文，可选 `__display` 当显示名；同名覆盖内置、新文件名即新语言，游戏内热重载（点按钮或等 2 秒）。翻译缺失的条目回落中文。韩文渲染用自建「雅黑 + Malgun Gothic」合并字体，不受框架启动字体限制
- 手柄组合键绑定（多按钮同按）在菜单里表达不了，仍写在 `config.ini` 的 `[gamepad]` 节

**值的流向**：`config.ini` 是唯一真源。模组启动时用 config.ini 现值播种菜单；菜单里的改动写回 config.ini 并实时应用；手动改过 config.ini 后点菜单里的「重新载入 config.ini」，菜单显示值会跟着文件走。卸载 PBZFM 后模组照常工作（纯快捷键，日志提示「未检测到 PBZFM 菜单框架」）。

实现：`src/pbzfm_client.*`（运行时 GetProcAddress 接入 PBZFM.dll，零链接依赖）+ `src/pbzfm_schema_map.*`（菜单 ↔ Config 字段映射表，随核心层单测）。菜单回调在框架线程只提交数据，所有引擎交互回到引擎 Tick 线程执行。

## 路线图

- **P0**：核心逻辑 + 单元测试 → 完成
- **P1**：引擎适配层与 DLL → 完成，已实机运行
- **P2**：PostProcess 快照还原、滤镜、连拍、录像

## 1.0.44 这次改了什么（隐藏锁定标记）

- 需求：锁定敌人后进摄影模式，敌人身上的**锁定白点**仍在——`SetHideUIForPV` 订阅者、HUD 布局、血条设置（`MonsterHealthBarMode` 只管血条）都不覆盖它。
- 实现（新配置 `hide_lockon_marker`，默认开，PBZFM 菜单「隐藏」页同名开关）：进入摄影模式全量扫描对象数组，类名/资产名命中锁定·目标·标记关键词（`_`/`-` 归一、`block` 排除）的 UMG 控件 Collapsed、可视组件（Widget/Niagara/Particle/Decal/Sprite/Mesh）bVisible=false，退出全部按原值还原；隐藏清单逐条记日志。
- 白点的真实类名离线证不了：零命中时直接确认完成（进模式后无法再锁定，重扫无益），并打一次性诊断（可见 UserWidget 清单 / 可视组件清单 / 玩家 pawn 名下锁定·目标组件清单）——白点仍在时把日志发回来收敛关键词。
- 隐藏失败（有目标藏不住）才按秒重试，上限 10 次（同 HUD/提示/血条）。

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

## 1.0.14 这次改了什么（进入摄影模式自动暂停时间）

- 需求：打开相机模式时自动暂停时间，用户可在 `config.ini` 中修改开关，默认开启。
- `[effects]` 新增 `auto_pause_time = true`：进入摄影模式成功后，自动按 `freeze_mode` 的方式冻结时间
  （`pause` = 游戏暂停，`dilation` = 全局时间变慢）；退出摄影模式时照常恢复。
- 关闭后（`auto_pause_time = false`）进入摄影模式时间照常流动，仍可按 `F` 手动冻结/恢复。
- 游戏内菜单新增「进入时暂停」状态行，UE4SS 的 ImGui 面板新增对应开关。
- 自动暂停失败只写 warning，不会阻止进入摄影模式。
- 版本 `1.0.14-cpp`；核心层测试 298 项全绿。

## 1.5.0 这次改了什么（超宽屏支持，消除两侧黑边）

- 需求：21:9 / 32:9 超宽屏用户反映，进入摄影模式后画面两侧仍有黑边。游戏官方支持超宽屏
  （超过 3:1 的比例，21:9/32:9 均在范围内），黑边应只出在摄影模式路径——拍照/演出常主动约束画幅：
  UE 里这把锁挂在 `APlayerCameraManager.bDefaultConstrainAspectRatio / DefaultAspectRatio`
  （对任何 ViewTarget 生效，换成摄影相机也锁），部分相机还会在组件上开 `bConstrainAspectRatio`；
  真机日志 `画幅锁=开/关(比例)` 会确认锁的位置。
- `[camera]` 新增 `ultrawide_unlock = true`（默认开启）：进入摄影模式时保存管理器原值并解除锁定，
  同时把摄影相机组件的 `bConstrainAspectRatio` 显式关掉；退出摄影模式时按进入前的值还原。
- 解锁结果写日志：`画幅解锁：已解除 16:9 锁定（原比例=1.778）`；管理器没锁会打
  `画幅：相机管理器未锁定画幅，无需解锁`；属性读不到/写不进去会打 warning（黑边若来自别的机制，
  把这条日志发回来）。解锁失败不阻断进入摄影模式。
- 每秒诊断行新增 `画幅锁=开/关(比例)`，真机验证解锁是否生效一眼可查。
- 想保留 16:9 构图（比如要裁成 16:9 输出）的用户可在 `config.ini` 里把 `ultrawide_unlock` 改为 `false`。
- 版本 `1.5.0-cpp`；核心层测试 503 → **527 项全绿**（新增画幅解锁进退/失败/热重载用例）。

## 1.0.30 这次改了什么（对话语音：游戏自有停止接口）

- 真机观察：冻结时对话推进已随世界冻结（说过的句子不会触发下一句），残留的是「正在播的当前句」。
- 普查在 `PBZSoundFXSubsystem` 找到游戏自有的 `PBZStopAkComponent`/`PBZStopAllSoundsOnActor`/`PBZPausePlayingID`/`PBZResumePlayingID` 等接口。冻结静音改为优先调用 `PBZStopAkComponent`（参数签名校验，失败回落组件 `Stop`）。
- `stop_wwise_on_freeze` 默认改为 **true**：冻结即静音（环境/对话/音乐一并停，解冻后由游戏自行重播）；不想静音改回 `false`。

## 1.0.35 这次改了什么（字幕压制扩到对话 UI 本体；对话语音按真实签名填参）

- **字幕**：1.0.34 实机确认设置开关已真关（`bEnableSubtitles` 回读=关）、字幕子控件已 Collapsed，但文本仍显示——文本画在对话 UI 本体 `WBP_FlowDialog_C` 上。压制范围扩到对话框家族（类名含 FlowDialog）：摄影模式里对话框+字幕一起隐藏，退出还原。
- 每 10 次重申做一次全量扫描：对话中途新出现的控件类也会被收编。
- 一次性「可见 UserWidget 清单」诊断：若压制后字幕仍显示，日志会列出场上所有可见控件类——把那行发回来即可精确指认。
- **对话语音**：`PBZPausePlayingID`/`PBZResumePlayingID` 按真实签名 `(int32 PlayingID, float FadeTime)` 填参（FadeTime=0 立即生效），冻结当前句真静音、解冻从停住位置续播。

## 1.0.33 这次改了什么（字幕隐藏改持续压制；对话语音见 PROGRESS）

- 1.0.32 反馈：进模式后字幕不消失。根因：对话 UI 只在创建时读一次字幕设置，运行中的对话由游戏逐条把控件重新显示——进入时藏一次会被下一条字幕盖回去。
- 修复：进入后**每秒重申**——已管控件被游戏改回可见就再压回 Collapsed，新实例（对话 UI 重建）自动收编并记录原值，退出统一还原；设置开关只在进入时写一次（避免重复写的未知副作用）。
- 进入/退出各做一次全量扫描并缓存字幕控件类名（`WBP_FlowSubTitle_C`），每秒重申只按类名查找，微秒级不拖帧。
- 已知边界：字幕被 1.0.30 写成关闭的游戏存档，仍需在游戏设置里手动开回一次。

## 1.0.31 这次改了什么（修复：字幕退出后永久消失）

- 1.0.30 实机 bug：进模式字幕消失正常，退出后字幕永久消失。根因：`PBZSettingsShared` 只有 `SetSubtitlesEnabled` 函数、没有 `bSubtitlesEnabled` 属性（属性在引擎 GlobalUserParam 上），旧代码只在属性读成功时记录原值，导致开关翻转后退出还原分支被整个跳过（且静默无日志）。
- 修复：原值改走 `AreSubtitlesEnabled` getter（读不到按开兜底）；还原条件改为「只要动过开关」，还原失败打 Warn；字幕控件收集扩到游戏自制控件 `GAA_Subtitles_C`（场上没有 USubtitleDisplay 插件控件）。
- **曾被子 1.0.30 卡掉字幕的，先在游戏设置里把字幕手动开回来，再装本包。**

## 1.0.30 这次改了什么（对话语音停住 + 隐藏对话字幕）

- **对话语音**：冻结静音升级走游戏自有接口（`PBZSoundFXSubsystem.PBZStopAkComponent`，签名校验通过才调，失败回落组件 `Stop`）；`stop_wwise_on_freeze` 默认改为 **true**（实测对话流程随世界冻结，停掉当前句不丢内容，不想静音可在 config.ini 改回 false）。
- **进入摄影模式自动隐藏对话字幕**（`hide_subtitles` 默认 `true`）：双路径——① 游戏字幕设置 `bSubtitlesEnabled` 关掉（管住之后新出的字幕，退出还原原状态）；② 字幕显示控件（GameSubtitles 插件 `USubtitleDisplay`，含 BP 子类）直接 Collapsed（管住此刻已经画出来的；暂停下控件不再刷新，光改设置对已显示的字幕不生效，退出按原可见性恢复）。失败每秒重试，不阻断进模式。
- 字幕控件一个都没找到时会打一次性 Warn 并列出场上类名带 Subtitle 的控件类——游戏若用自制字幕控件，把那行日志发回来定下一版。
## 1.0.25 这次改了什么（按真机日志：命中 1 执行 0 + Wwise 语音）

- 真机日志结论：动作冻结已稳定；但日志里多次「剧情播放器：命中 1，执行 0」——Sequencer 的 `Pause`/`Play` 是无参函数，参数块为空导致调用在 ProcessEvent 之前就放弃。已修复（缓冲区至少留 1 字节），剧情与场景后台播放器现在真正能暂停/恢复。
- 对话语音「有时拦得住有时拦不住」：序列音轨驱动的语音随上面的修复一起停；Wwise 直接 post 的语音由新增的 `stop_wwise_on_freeze`（默认 false，停止不可恢复）处理。
- 每会话探测一次并打日志：`Wwise 组件诊断：AkComponent=N，可用函数=…`——下一版据此决定是否做可恢复的音频暂停。
- **进入摄影模式自动隐藏敌人（怪物）血条**（`hide_enemy_health_bars` 默认 `true`）：游戏侧总开关是设置项 `MonsterHealthBarMode`（总是显示/仅锁定目标/满血隐藏），摄影模式没有锁定目标，切到"仅锁定目标"即全部隐藏，退出还原原模式；设置没就绪时每秒重试，失败只告警不阻断。

## 1.0.24 这次改了什么（剧情中冻结失效修复）

- 现象：游戏处于剧情时进摄影模式，自动冻结"看起来"生效（画面像冻住），但角色动作和声音还在继续。
- 修复分三层：① 冻结时同时**暂停 Sequencer 剧情播放器**（`FindAllOf(MovieSceneSequencePlayer)` 沿继承链匹配，`LevelSequenceActor.SequencePlayer` 兜底；暂停用 `Pause()`，解冻只恢复处于暂停态的 `Play()`；`freeze_sequences` 默认 `true`）；② **暂停被游戏拒绝时自动回退时间缩放**，不再只告警；③ **冻结期每秒回读自检**，被剧情/游戏改写就重新冻结，连续 3 次压不住停止对抗并告警。
- 已知边界：剧情语音若走 Wwise/独立音频组件，播放器暂停拦不住（日志里有 `Wwise=有/无` 探测），实机确认后单独处理。
- 每次冻结/解冻日志会打 `剧情播放器[暂停/恢复]：扫描 N，命中 M，执行 K`——剧情中冻结仍无效时把这行发回来即可定位。

## 1.6.0 这次改了什么（进入摄影模式隐藏交互提示）

- 需求：进入摄影模式后，对准场景互动物（铃铛/拾取点）时的“E”键提示图标不会消失。
- 根因：该提示是 `PBZInteractComponent` 挂的**世界空间 UMG**（组件属性 `InteractWidgetComponent`），
  不在 `SetHideUIForPV` / HUD 根控件的覆盖范围内；且 `freeze_mode=pause` 暂停下游戏 UI 不再刷新，
  已画出来的提示必须隐藏控件组件本身才会消失。
- 实现（双路径，退出都还原）：① 游戏自带设置开关 `bSceneInteractPromptVisible`（运行时按候选类探测，
  优先调 `SetSceneInteractPromptVisible`，参数名对不上直写属性）——管住之后新出现的提示；
  ② 枚举 `PBZInteractComponent` 的 `InteractWidgetComponent` 并 `SetVisibility(false)`——管住此刻已画出的。
- `[effects]` 新增 `hide_interact_prompt = true`（想保留提示可改 `false`）。
- 失败不阻断进入摄影模式：两路都失败只打一次性 Warn（真机排障把日志发回来）。
- 版本 `1.6.0-cpp`；核心层测试 527 → **544 项全绿**（新增交互提示隐藏用例）。
