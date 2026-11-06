# PBZ Photo Mode — 影之刃零摄影模式（C++）

基于 UE4SS 的 C++ 摄影模式 Mod：核心逻辑与引擎适配层分离，`src/core/` 可在没有游戏与 UE4SS 的环境下运行单元测试。

| 项目 | 状态 |
| --- | --- |
| 版本 | v2.0.2-cpp（2026-11-06，含 2.0.1 菜单输入修复与 2.0.2 处决/超宽屏暂停修复） |
| 核心层测试 | 当前断言总数见 [test_count.txt](test_count.txt)，全部通过 |
| 实机验证 | F 键冻结、HUD 隐藏、进入自动暂停、动作冻结稳定已验证；**1.0.36 语音静音双保险（ID 暂停 38/38 + 音量兜底 38/38，解冻成对恢复）实机通过**；**1.0.66 移动视角卡顿修复实机通过（2026-11-02）：全日志无「时间偏移」，Wwise 整引擎 Suspend/Wakeup 成对出现**；**1.0.75 收口批次：接管当前相机、HUD 恢复重构实机通过**；超宽屏/DInput 手柄/字幕压制各项待回归 |

## 项目结构

```
src/core/      纯逻辑，不碰引擎 —— 已完成
  config        配置解析（含容错、范围夹紧、默认配置生成）
  vk            键名 -> 虚拟键码（F1..F12 / ADD / SUBTRACT / OEM_PLUS / 修饰键…，含 MOUSE_LEFT/RIGHT/MIDDLE/X1/X2）
  input_logic   按键/鼠标 -> Intent（含 Alt 门控、滚轮累积、按下沿、对焦/光圈步进）
  gamepad       XInput / DInput 手柄映射与组合键
  math3         前/右向量、俯仰夹紧、偏航/滚转回绕
  photo_mode    进/退/每帧状态机（含失败回滚、幂等退出、接管失败自动回退、1 秒视角守护）
  mod_runtime   引擎侧编排（依赖注入，可用 FakeEngine 驱动完整进退路径）
  i18n_logic    词典加载/回退/热重载
  retry_policy  恢复失败的有限重试与退避常量
  tuning        手感/容差等遗留常量（数值不变）
  engine_port.h 按职责拆分的引擎子接口（World/CameraView/Input/Hud/TimePause/Audio/FreezeMotion/Log）

src/engine/    引擎适配层 —— 已完成，随 DLL 在实机运行
  ue4ss_port*   EnginePort 的 UE4SS 反射实现，按子系统分文件：
                ue4ss_port.cpp（入口/反射 helper）· camera_view（相机/接管/景深/Cine）
                hud_ui（HUD/字幕/提示/锁定）· time_pause（冻结）· micro_motion（微动治理）
                audio（Wwise）· diagnostics（诊断日志）· process_freeze（F7 定格：挂起游戏引擎线程/时间补偿）

src/mod_app    CppUserModBase 子类：配置加载、输入绑定、每帧 tick
src/ui_tab     UE4SS 控制台诊断面板（滑块/按钮，纯展示 + 回传命令；游戏内无菜单，纯快捷键操作）
```

### 立刻可以验证的部分

```bat
tools\build_tests.bat
```

不需要游戏、不需要 UE4SS，只需要 VS 2022 生成工具。实际断言总数由 `tools\update_test_count.bat` 跑完测试后写入 [test_count.txt](test_count.txt)（单一来源；运行时断言次数，不是独立用例数）。

## 键位

| 操作 | 按键 |
| --- | --- |
| 进入/退出摄影模式 | `F2`（`key_toggle`） |
| 自由转视角 | 按住 `Alt` + 移动鼠标（`key_free_look` 可改；`free_look_requires_alt = false` 可不按键） |
| 前后左右平移 | `W` `A` `S` `D`（`key_move_forward/back/left/right` 可改） |
| 方向键转向/俯仰 | 方向键 左/右转视角（`yaw_speed`，默认 90 度/秒）、上/下俯仰（`pitch_speed`，默认 30 度/秒） |
| 升降 | 默认左键上移 / 右键下移（`key_vertical_up` / `key_vertical_down` 可改成鼠标中键、侧键或键盘键；translate 模式；手柄 `pad_vertical_up/down`，默认 RT / LT） |
| 滚转（旋转画面） | `Q` / `E`（E 顺时针 / Q 逆时针；`key_roll_left` / `key_roll_right` 可改键，`roll_speed` 可调）；手柄 `pad_roll_right` / `pad_roll_left`（默认 RB / LB） |
| 加速 / 减速 | `Shift` / `Ctrl`（`key_boost` / `key_slow` 可改；手柄 `pad_boost` / `pad_slow`，默认 LS / RS） |
| 调整 FOV | 鼠标滚轮（`[control] wheel_fov` 可关、`wheel_fov_invert` 可反向）、小键盘 `+` / `-`（`key_fov_up/down`，单击一格，按住约 0.35 秒后连发） |
| 重置相机视角 | `R`（`key_reset`；回到进入摄影模式时的位置/朝向/FOV） |
| 滚转归零 | `Z`（`key_roll_reset`；只把滚转回 0，位置/朝向/FOV 不动）；手柄 `LB+RB` 同按（`pad_roll_reset` 可改） |
| 冻结时间 | `F`（`key_freeze`；进入时按 `auto_pause_time` 自动冻结） |
| 画面定格（内置冻结渲染帧） | `F7`（`key_frame_freeze`；再按恢复；定格期间相机不响应输入，安全阀默认 60 秒） |

所有键位都能在 `config.ini` 里改（`[keys]` 键盘 / `[gamepad]` 手柄组合键），没有写死的按键。鼠标移动、滚轮与手柄摇杆属于模拟轴：摇杆/鼠标位移不可改键，滚轮可用 `wheel_fov` 关闭或反向。

## 手柄支持

- **Xbox 手柄**：XInput 读取（`XInputGetState` 轮询，强制加载 System32 的 DLL，绕开游戏目录代理）。
- **DualSense / DualShock4**：**不是 XInput 设备**，Windows 把它们枚举成 DirectInput 手柄——1.4.0 起由 DInput8 轮询后端读取（USB/蓝牙均可），与 XInput 翻译成同一份 `PadState`，键位写法与 Xbox 完全一致。XInput 槽位优先，避免 Xbox 手柄在两套后端重复计数。
- 映射（`core/gamepad.cpp`）：DIJOYSTATE2 按钮 0..13 → □╳○△/L1R1/L2R2/CREATE/OPTIONS/**L3=LS/R3=RS**/PS；轴 lX/lY/lZ/lRz=双摇杆（Y 取反）、lRx/lRy=L2/R2 模拟量；rgdwPOV=十字键。
- 可配键位（`[gamepad]`）：`pad_vertical_up/down`（默认 RT / LT；单键绑定保留扳机模拟量行程）、`pad_roll_right/left`（默认 RB / LB）、`pad_boost/slow`（默认 LS / RS；两杆同按是进出模式组合键，此时不触发变速）。
- 十字键：上/下俯仰（`pitch_speed`）、左/右转视角（`yaw_speed`，1.0.46 起从左右平移改来）；FOV 在 `A` / `B`（`pad_fov_up` / `pad_fov_down` 可改）：单击一格，按住约 0.35 秒后连发，松开即停。
- 接入诊断：`DInput 手柄已接入：<名>（VID_…&PID_…）首读：按钮=… 摇杆=… POV=…`（一次性，原始值，映射对不上真机时用它定位）；`手柄=<后端摘要>` 在每秒输入诊断行里。

## 景深（本作不支持，功能已移除）

**这个游戏的 gameplay 视图不渲染引擎景深**，取证在 hotfix25~31 完成，hotfix32 把整个功能删掉了：

- 四条通道（相机 `CameraCache.POV` 的 PP / 自建无界体积 / 关卡自带体积 / `ViewTarget.POV` 每帧重申）
  全部「写入成功、回读正确、画面零变化」；89px 的强力参数也不糊，而同一体积的暗角照常生效。
- 唯一的引擎总闸 `ShowFlags.DepthOfField` 在反射层拿不到：console 命令在本作不执行，
  `0xB8` 那块内存整局一位不变（不是真的 flags）。
- 径向模糊替代方案也失败：40 个后处理体积的 `WeightedBlendables` 全空，
  自建槽位时 `FWeightedBlendables.Array` 取址失败，材质挂不上去。

因此 **PBZFM 的「景深（手动对焦）」页、`[dof]` 配置段、对焦/光圈按键、引擎侧写入与核心层状态机
已全部删除**（hotfix32），不再保留「参数记录」这种看着有用、实际没用的东西。
旧 `config.ini` 里残留的 `[dof]`、`key_focus_*`、`pad_aperture_*` 等键会被**静默忽略并给一条合并提示**，
可以整段删掉。

想给截图加虚化只能用外部后期：NVIDIA Ansel 的景深、或 ReShade 的 DOF。Mod 侧不会再碰这件事。

## 接管当前相机（UUU 式，`[camera] takeover_current_camera`）

- **默认开启（`true`）**：不 spawn 独立相机、不切 `ViewTarget`，每帧在 `GameViewportClient::Tick` 之后（游戏 `UpdateCameraManager` 重算完 `CameraCache.POV`、`RedrawViewports` 之前）覆写 `APlayerCameraManager.CameraCache.POV` 的位置/朝向/FOV。写点若晚于渲染（`EngineTickPost` 在 `RedrawViewports` 之后）就会表现为“镜头不动”，1.0.75 的回归即此。
- 只写“每帧由相机管理器重算”的 POV 缓存，不写游戏相机组件，因此不存在持久污染；退出时先还原接管相机的后处理/景深，再停止每帧写 POV，游戏相机从下一帧起恢复自身逻辑。
- **失败自动回退**：进入时读不到 `CameraCache`/POV 布局、或写入后回读不一致，会当场回退到独立相机（spawn + `SetViewTargetWithBlend`）；运行中连续 **15 帧**回读 POV 与目标位姿不一致，判定接管未生效并自动回退，日志 `已自动回退到独立相机（spawn + SetViewTargetWithBlend）`。
- **游戏内就能换后端（不用重进）**：PBZFM 菜单里的「接管当前相机」可即时在两条后端之间切换——
  关掉会当场拆掉接管、spawn 独立相机并把视角切过去；打开会当场还原视角、销毁独立相机、改为接管 POV。
  任何一步失败都会自动回滚到原来的后端（不会出现「点了开关没相机可用」），并打一条 Warn。
  同一项也可写进 `config.ini` 的 `[camera] takeover_current_camera`（`false` = 始终独立相机）。
- **两条后端怎么选**：接管的优点是**侵入最小**（不 spawn Actor、不切 ViewTarget，退出时无需还原任何东西，
  过场中进出也不会打断演出镜头）；独立相机的优点是**有真实焦距联动**（按 `Filmback.SensorWidth` 把 FOV 换算成
  `CurrentFocalLength`，长焦是真实光学压缩而不是单纯 FOV 裁剪）。要长焦压缩感用独立相机，要最不打扰游戏用接管。
- **接管有个硬前提**：必须挂上 UE4SS 的 `GameViewportClientTickPost` detour。挂不上时本 Mod 会
  **自动降级为独立相机**并打 Warn（早期版本只报 Error 让用户自己改配置，结果是镜头完全不动）。
- 运行中连续 3 次写 POV 失败只 Warn 一次；此时画面可能不再跟随输入，退出重进可重建接管。

## CineCamera 焦距联动与出图通道

- 包内存在 `CineCameraActor` / `CineCameraComponent` / `CurrentFocalLength` / `Filmback`：独立相机优先用 `CineCameraActor`，改 FOV 时按 `Filmback.SensorWidth`（读不到用 35mm 全画幅）换算并同步 `CurrentFocalLength`，长焦压缩是真实焦距而非纯 FOV 裁剪。
- `UCineCaptureComponent2D`（CineCameraSceneCapture 插件）**确认未打进 Shipping**。进入时日志 `相机后端探测：… CineCaptureComponent2D=no(c: 插件 CineCameraSceneCapture 未打进 Shipping)` 可复核，因此高品质无 UI 出图通道不存在。
- **设计边界**：实时取景始终走 `ViewTarget` / 相机组件（独立相机或接管 POV）；`SceneCapture2D` 仅作为独立出图通道，不用作取景器。

## HUD 恢复重构（1.0.75 收口批次）

- **粗粒度优先**：进入先调游戏自带 `UPBZUIManagerSubsystem::SetHideUIForPV(true)` 总开关，再关 `AHUD.bShowHUD`（PlayerController 或 MyHUD 上的反射属性；读不到原值就不写），最后把 UMG 根控件 `W_FrontEndHUDLayout_C` 设为 `Collapsed`。退出按原值成对恢复（PV 开关、`bShowHUD`、根控件 `Visibility`）。
- **仍保留细粒度（有理由）**：字幕（`WBP_FlowSubTitle_C` / `GAA_Subtitles_C`）、世界空间交互提示（`PBZInteractComponent.InteractWidgetComponent`）、锁定标记（`TargetingComponent.LockWidget` 等）、对话 Flow（`WBP_FlowDialog_C` 需 `Hidden` 保留 Tick）。它们不在 PV 总开关 / 根控件覆盖范围内，必须单独处理。
- 存活判定从“重扫列表 + `pointer_in`”改为 `is_ok()`（UE 对象数组反查 + 指针范围/对齐校验），不再要求“本轮重新扫描必须命中”。旧实现扫描漏项会让控件永远停在 `Collapsed`，这是字幕/提示反复消失的根因之一。
- 退出恢复失败仍走既有有限重试机制（不吞错），逐项保留未完成状态，后续 tick 继续重试。

## 已知缺口（1.0.75 收口批次）

- **取景链路边界**：实时取景始终走 ViewTarget + 相机组件；SceneCapture2D 仅作为独立出图通道，不用作取景器。
- **无高品质无 UI 出图通道**：`UCineCaptureComponent2D`（CineCameraSceneCapture 插件）未打进 Shipping；`SceneCapture2D` 也不作为取景器/出图替代。
- **`HighResShot` 不可用**：本作 `BP_PlayerController_C` 没有 `ConsoleCommand` UFunction，且 `HighResShot` 仅 Editor/standalone 可用，故不采用。
- **接管当前相机**：依赖本作 `CameraCache.POV` 布局（`FCameraCacheEntry.POV` 或直接 POV），布局不认识/回读不一致会自动回退独立相机；POV 的 FOV 在 UE5.5+ 可能位于 `Lens.FieldOfView`，代码两个字段都会写。
- **`log_level` 已生效**（M-6 已修）：`PhotoMode` 构造时与每条日志都会把 `cfg_.log_level` 下发给引擎侧，`Ue4ssPort::log` 按 `log_level_enabled()` 过滤（`Error` 级永不过滤）；热重载后立即按新级别生效。
- **已弃用键**：`interval_ms`、`ui_scale`、`time_offset_compensation` 接受但忽略，每个键首次出现时告警一次；不写则行为与默认一致。
- **配置越界值**：现在 clamp 到 `min/max` 边界（M-5），不再回落默认值；想让某项恢复默认请删掉该行或改成默认值。
- **锁定/血条/字幕等隐藏**：依赖游戏内反射查找，换世界后缓存统一失效并重新扫描，个别版本/场景可能扫不到（日志会告警）。

## 配置文件

`config.ini`（示例见 `config.ini.example`，由 `tools/gen_config.bat` 从代码默认值生成，**勿手改**）。写错不会崩：非法值保留默认、并在日志里给一条 warning；数值越界会 clamp 到边界。

- **`[camera]`**：`fov_default = 90` / `fov_min = 15` / `fov_max = 150` / `fov_step = 2`、`ultrawide_unlock = true`、`takeover_current_camera = true`（默认接管当前相机；`false` 固定走独立相机）。
- **`[keys]` 的可配项**：`key_toggle/freeze/frame_freeze/reset/roll_reset/fov_up/fov_down/pitch_up/pitch_down/yaw_left/yaw_right/roll_left/roll_right/vertical_up/vertical_down/move_forward/move_back/move_left/move_right/boost/slow/free_look/focus_near/focus_far/aperture_open/aperture_close`——**没有写死的键盘键**。
- **`[control]` 的滚轮**：`wheel_fov = true`（滚轮调 FOV）、`wheel_fov_invert = false`（向上缩小 / 向下放大，true 反向）。
- **`[gamepad]` 的可配项**：`pad_toggle/freeze/reset/roll_reset/fov_up/fov_down/pitch_up/pitch_down/yaw_left/yaw_right/roll_left/roll_right/boost/slow/vertical_up/vertical_down/focus_near/focus_far/aperture_open/aperture_close`；组合键用 `+` 连接，`NONE` 解绑。
- **注释规则**：`;` 或 `#` 在引号外即视为注释起点（独占一行或以空白开头最易读，但解析器不强制要求前置空白，`fov = 50;备注` 也有效）；引号内的 `;`/`#` 属于值本身，不当作注释。`key_toggle = "G"` 这类带引号写法同样适用。
- **已弃用键**：`interval_ms`、`ui_scale`、`time_offset_compensation` 接受但忽略，每键告警一次。
- **`log_level`**：`error` / `warn` / `info` / `debug` 四档，`Error` 级永不过滤；运行中热重载立即生效。

## 构建 DLL 的前置条件

1. **UE4SS 源码** —— 自行克隆 RE-UE4SS，用 `tools\build_dll.bat <路径>` 指定
2. **UEPseudo 依赖** —— RE-UE4SS 的私有子模块，需要 GitHub 账号被 Epic 授权
3. **Rust 工具链** —— UE4SS 的构建链路需要
4. **CMake + VS 2022 C++ 工具链**

具体步骤见 `docs/BUILD.md`，API 核对清单见 `docs/P1_CHECKLIST.md`。

## 游戏内设置菜单（PBZ-Framework-Menu，可选）

1.0.42 起本模组是 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu)（游戏内 ImGui 菜单框架，DirectComposition 覆盖窗实现，不碰游戏交换链、不影响 FSR3 帧生成）的**第一个客户端模组**。两个模组各自解压进 `ue4ss\Mods\`（enabled.txt 启用，互不改 mods.txt）后：

- 游戏内按 PBZFM 的呼出键（默认 `F8`）打开菜单，侧栏出现「**PBZ Photo Mode**」分区（1.0.43 起两页均由本模组自绘，ImGui 版本与框架 1.92.1 对齐）
- **「设置 · Settings」页**：首行「语言」下拉——**跟随 PBZFM 菜单** / 中文 / English / 日本語 / 한국어 / 外部词典，当帧生效并写回 `config.ini` 的 `[ui] menu_language`；旁边「重新载入词典」按钮；其余为键盘键位（菜单内捕获式改键）、行为开关（进入时暂停时间 / 冻结方式 / 屏蔽输入 / 超宽屏解锁 / 手柄支持）、隐藏开关（角色 / HUD / 交互提示 / 敌人血条 / 字幕）、冻结增强（Wwise 整引擎挂起 / 逐语音兜底 / 剧情暂停 / 相机震动 / 风源 / MPC / 布料模拟 / 诊断日志）、相机与移动参数、手柄参数——改动**实时生效并写回 `config.ini`**
- **「状态 · Status」页**：OFF / LIVE / FROZEN 状态、位置与朝向读数、键位速查；FOV 滑条（实时变焦）与「进入/退出摄影模式」「冻结/恢复时间」「重置视角」「重新载入 config.ini」按钮
- **多语言**：内置 zh-CN / en-US / ja-JP / ko-KR；**外部词典**放 `PBZPhotoModeCpp\languages\<id>.json`——UTF-8 扁平 JSON，key=中文原文、value=译文，可选 `__display` 当显示名；同名覆盖内置、新文件名即新语言，游戏内热重载（点按钮或等 2 秒）。翻译缺失的条目回落中文。韩文渲染用自建「雅黑 + Malgun Gothic」合并字体，不受框架启动字体限制
- 手柄组合键绑定（多按钮同按）在菜单里表达不了，仍写在 `config.ini` 的 `[gamepad]` 节

**值的流向**：`config.ini` 是唯一真源。模组启动时用 config.ini 现值播种菜单；菜单里的改动写回 config.ini 并实时应用；手动改过 config.ini 后点菜单里的「重新载入 config.ini」，菜单显示值会跟着文件走。卸载 PBZFM 后模组照常工作（纯快捷键，日志提示「未检测到 PBZFM 菜单框架」）。

实现：`src/pbzfm_client.*`（运行时 GetProcAddress 接入 PBZFM.dll，零链接依赖）+ `src/pbzfm_schema_map.*`（菜单 ↔ Config 字段映射表，随核心层单测）。菜单回调在框架线程只提交数据，所有引擎交互回到引擎 Tick 线程执行。

## 路线图

- **P0**：核心逻辑 + 单元测试 → 完成
- **P1**：引擎适配层与 DLL → 完成，已实机运行
- **P2**：PostProcess 快照还原、滤镜、连拍、录像

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

## 最近的版本

- **2.0.2-cpp**（2026-11-06）：**处决/过场演出残留修复 + 超宽屏暂停修复**。① 处决动画中进入摄影模式并在退出/解冻后角色模型定格：不再把 `TimeDilation` 的接近零演出值当成进入前原值；退出时对玩家 Pawn 清理 `CustomTimeDilation` / `SkeletalMeshComponent.bPauseAnims` / `bNoSkeletonUpdate` / `CinematicTimeDilation` 残留。② 21:9 超宽屏在暂停时被游戏重新锁成 16:9：进入时即使未锁也保存画幅原值，`GameViewportClient Tick` 之后每帧重申解锁，退出按进入前状态还原；来自过场/处决的瞬时 16:9 锁不写回。核心层 **5,155 项**全绿。

- **2.0.1-cpp**（2026-11-06）：**PBZFM 菜单打开期间暂停摄影模式输入**。PBZFM 覆盖菜单与游戏同进程，`foreground_is_ours()` 判不出它；菜单里的左键会命中 `key_vertical_up`（默认 `MOUSE_LEFT`）导致相机上升。本版每帧查询 `PBZFM_IsMenuOpen`，菜单可见时键鼠/手柄全部暂停，关闭后恢复（关闭首帧不产生按下边沿）。核心层 **5,133 项**全绿；发行包 `PBZPhotoModeCpp_2.0.1-cpp.zip`。
- **2.0.0-cpp 正式版**（2026-11-05）：hotfix1~35 的成果收进 2.0 正式版号（`PBZPM_HOTFIX 35 → 0`，版本串不带 `-hotfixNN`）。内容见 [CHANGELOG.md](CHANGELOG.md) 的「2.0.0-cpp 正式版」一节：摄影模式本体（世界冻结/帧冻结、键位全可配、接管相机与独立相机双后端、画幅解锁、出图）、**画面效果五项全部实机生效**（暗角 / 饱和 / 对比 / 泛光 / 冷暖）、PBZFM 可选图形菜单、四语言。**明确不支持并已从界面移除**：引擎景深、色差、胶片颗粒（取证结论在 `core/tuning.h` 顶部）。核心层 **5,133 项**全绿；发行包 `PBZPhotoModeCpp_2.0.0-cpp.zip`。
- **2.0.0-cpp-hotfix35**（2026-11-05）：**旧配置真清理**——hotfix34 删了色差/胶片颗粒，但旧 `config.ini` 里的残留键只是「提示一次然后忽略」，文件里那几行还在。本版给 `ConfigLoadResult` 加 `legacy_keys`，`PhotoModeMod::reload_config()` 拿到非空时用规范格式**原子重写一次 config.ini**（`.tmp` → rename），把 `[dof]` 段、`key_focus_*`/`key_aperture_*`/`pad_focus_*`/`pad_aperture_*`、`chromatic_aberration`、`film_grain` 从文件里物理删掉，并打一行 `配置清理：已从 config.ini 删除 N 个已废弃键并重写文件（…）`。告警文案改成「已忽略并将从 config.ini 清理」；**只在真命中废弃键时才写盘**（干净配置不会每次启动白重写，有单测盯着）；重写用 `config_ini_text()` 规范序列化，顺带把未知/拼错键也一并清掉。核心层 5,124 → **5,133 项**全绿（新增 `test_config_legacy_purge`）。
- **2.0.0-cpp-hotfix34**（2026-11-05）：**色差与胶片颗粒整条删除**（按景深那套做法）——PBZFM「画面效果」组里 `色差` / `胶片颗粒` 两条滑条、`[look]` 段的 `chromatic_aberration` / `film_grain` 两键（含 `Config` 成员 / schema / `default_config_text()` 输出）、四语言两个标签键、`set_look()` 里的 `SceneFringeIntensity` / `FilmGrainIntensity` / `ChromaticAberrationStartOffset` 写入与下发回读字段、`log_look_gates()` 取证函数、`LookOptions` 两个字段一并移除。取证结论改由 `core/tuning.h` 顶部注释承担（档位依据、抬档被 cvar 优先级丢弃、console 不执行、UE4SS 无 `IConsoleManager`、补噪声纹理后颗粒仍不出现）。`[look]` 现在只剩 **暗角 / 饱和 / 对比 / 泛光 / 冷暖** 五项，全部实测生效。核心层 5,291 → **5,124 项**全绿。
- **2.0.0-cpp-hotfix33**（2026-11-05）：**胶片颗粒判定「本作不支持」**——hotfix32 那套「补 `FilmGrainTexture`」的实测结果是：噪声纹理确实挂上了（`Texture2D /Engine/EngineMaterials/Good64x64TilingNoiseHighFreq`）、`FilmGrainTexelSize=1` 写了、`r.FilmGrain=1`（pass 总开关开着）、强度拉到 6.85，画面里一粒颗粒都没有 → 颗粒 pass 与色差 pass 同属被 `r.Tonemapper.Quality=2` 钉死的那一档。本版删除 `attach_film_grain_texture()` 及 `film_grain_texture_`/`film_grain_scanned_` 两个状态成员与 `FilmGrainTexture`/`FilmGrainTexelSize` 写入（实测证伪的东西不留），颗粒强度仍照常写入作参数记录，四语言菜单标签标注「本作不支持」，`config.ini.example` 注释同步。核心层 **5,291 项**全绿。
- **2.0.0-cpp-hotfix32**（2026-11-05）：**景深功能整体移除**——菜单「景深（手动对焦）」整页、`[dof]` 配置段、对焦/光圈键位、`Intent.*_nudge` 与连发状态、`PhotoMode::apply_depth_of_field` 与全部 `dof_*` 状态、`EnginePort` 三个景深接口、引擎侧 `write_post_process_dof`（含 SEH 包装）与 `takeover_pp_*` 快照、`core/dof_math.h`、四语言 11 个标签键、1,106 项单测断言——端到端删干净。自建后处理体积保留（改名 `ensure_post_volume`/`post_volume_`），画面效果七项不受影响。旧 `config.ini` 的 `[dof]` 与键位残留合并成一条提示后静默忽略。DLL -43,008 字节，核心层 **5,291 项**测试全绿。同版**修掉画面效果两处真 bug**：① `ColorSaturation`/`ColorContrast` 在 UE5 是 `FVector4`（成员名 **X/Y/Z/W**，不是 R/G/B/A），hotfix29~31 一直按 R/G/B/A 找 → 三个通道全 `nullptr`，饱和度/对比度**一个字节都没写进去**（日志里 `饱和=<读不到>` 就是它），现按 X/Y/Z/W 写入并回读；② 胶片颗粒当时以为缺 `FilmGrainTexture`，于是从场景纹理里挑一张 `grain/noise/perlin` 贴图挂上去（日志 `画面效果：胶片颗粒纹理 = …`）——**实测无效**，hotfix33 已删除该机制并标注不支持。色差则**判定为本作不支持**（`r.SceneColorFringeQuality` 被钉死在 0 且优先级高于任何游戏设置级写入，console 命令在本作不执行、UE4SS 不暴露 `IConsoleManager`），菜单标签照此标注、抬档代码删除。

- **2.0.0-cpp-hotfix31**（2026-11-05）：**输入手感 + 景深死代码清零 + 画面效果四项修复**——① 对焦/光圈也支持**按住连发**（与 FOV 同一套状态机：按下 1 格，0.35s 后每 0.08s 1 格，掉帧补格）；② `Shift` 粗调 / `Ctrl` 微调（×`boost_mult` / ×`slow_mult`，乘在比例步进之后）；③ 对焦**比例步进**：`max(配置步进, 当前距离×10%)`，100m 外一步 10m；④ 删除景深实验设施的 20 个函数/整块（通道③④⑤、ShowFlags 采样、console 闸门、探针、材质普查、MPC dump），`ue4ss_port_camera_view.cpp` 3,046 → 1,926 行、头文件 825 → 679 行、DLL -23,040 字节；⑤ 景深反射写入加 SEH 兜底；⑥ 世界换代时作废接管与后处理快照并回退独立相机（不再把旧世界的值写进新相机）；⑦ **画面效果四项失效修复**——色差/颗粒/饱和/对比在本作里不进画面，根因是后处理质量停在 `r.Tonemapper.Quality=2`、`r.SceneColorFringeQuality=0`（`BaseScalability.ini` 的 `PostProcessQuality@0/@1`），这些 pass 被引擎整段跳过，而暗角/泛光/冷暖不受档位管辖所以一直正常；本版加「写后回读」`画面效果：回读 …` 区分「没写进去」与「引擎不画」，补 `ColorGradingIntensity` / `ChromaticAberrationStartOffset` 兜底，并用游戏自己的 `UGameUserSettings.SetPostProcessingQuality(3)` + `ApplyNonResolutionSettings()` 临时抬档，**以 `r.Tonemapper.Quality` / `r.SceneColorFringeQuality` 回读判定成败**，抬不动立刻写回原档位，退出摄影模式自动还原。核心层 6,365 → **6,397 项**全绿。

- **2.0.0-cpp-hotfix30**（2026-11-05）：**景深收口，删除径向模糊实验**——hotfix28 的日志给出最后一格：40 个后处理体积的 `WeightedBlendables` 一个都不带条目（没槽位可借），自建槽位时 `Array` 取址失败，材质根本挂不上 → 这条替代路线在挂载这一步就断了。本版把 `attach_radial_blur_material` / `detach_radial_blur_material` / `scan_weighted_blendables` 及全部状态与日志整体删除（engine 层 -233 行、头文件 -21 行，连 `FMemory.hpp` 一并去掉），不留死代码；`[dof]` 段注释改写为「本作不渲染引擎景深、以下仅为记录」；`tuning::kDofExperimentalChannels` 注释补齐取证结论。`[look]` 画面效果走另一条已验证通道，不受影响。核心层测试 **6,365 项**全绿。

- **2.0.0-cpp-hotfix29**（2026-11-05）：**画面效果全套**——暗角/色差/胶片颗粒/饱和度/对比度/泛光/冷暖 7 项全部接进 `[look]` 段与 PBZFM「画面效果」菜单，走同一套已被证明有效的自建后处理体积通道（`bOverride_*` + 值）。中性值即关闭（暗角/色差/颗粒/泛光/冷暖=0，饱和/对比=1），关闭时把 `bOverride_` 置回 false 交还游戏。`ColorSaturation`/`ColorContrast` 写 FVector4（R=G=B），冷暖写 `SceneColorTint`（R=1+0.18t、G=1-0.02t、B=1-0.22t）；字段缺失只告警一次并列名。核心层测试 5,816 → **6,365 项**全绿。

- **2.0.0-cpp-hotfix28**（2026-11-05）：**自建 blendable 槽位没走到头 → 每一步加日志**——hotfix27 材质找到了，但自建槽位那段静默返回；本版把「取 `WeightedBlendables` → 取 `Array` → 取 `Inner` → 取 `Object/Weight` → 分配 → 写入」每一步都拆开打日志，失败时输出具体是哪一环（`找不到 WeightedBlendables` / `Array 不是 FArrayProperty` / `找不到 Object/Weight` …），拿到那一行就知道 5.7 里这个结构的真实字段名/类型。

- **2.0.0-cpp-hotfix28**（2026-11-05）：**新增屏幕暗角（Vignette）**——景深走不通，但同一轮探针第 2 段证明「写进自建 `PostProcessVolume` 的 `VignetteIntensity`」是本作 gameplay 视图里**唯一实测能出画面**的后处理通道。本版把它做成正式功能：新增 `[look]` 段 `vignette = 0`（0~4，0=关闭），PBZFM 菜单新增「画面效果（暗角）」滑条，拖完即时生效；走自建无界高优先级体积（与景深同一个），关闭时把 `bOverride_VignetteIntensity` 置回 false，退出摄影模式恢复。`EnginePort::set_vignette` + `PhotoMode::apply_look_if_needed`（值变化才下发、失败只告警一次并重试）。核心层测试 5,728 → **5,816 项**全绿。

- **2.0.0-cpp-hotfix27**（2026-11-05）：**借不到槽位 → 用引擎分配器造一个**——hotfix26 找到材质 `M_FX_PP_RadialBlur`，但 40 个后处理体积的 `WeightedBlendables` 全是空的。本版改用 UE4SS 暴露的 `FMemory::Malloc/Free`（游戏自己的 GMalloc，Binned2 只认它；CRT malloc 交给它 free 会崩）分配 1 个 `FWeightedBlendable`（16 字节），写入材质与权重 1.0，并把 `FScriptArray` 头设成 `{Data,1,1}`；退出时先清空数组头、再自己释放，避免二次释放。风险：效果强弱未定、极端情况下可能崩——把 `[dof] enabled` 改 false 即可完全绕开。

- **2.0.0-cpp-hotfix26**（2026-11-05）：**径向模糊伪虚化**——引擎景深已确认不渲染，但后处理体积的 `WeightedBlendables` 是走过引擎 PP 混合链的（暗角证明过）。本版把游戏自己的后处理材质 `M_FX_PP_RadialBlur` **借槽位**挂到某个已有一条 blendable 的体积上（记原值、退出还原；不自己分配 TArray），实现「中心清晰、四周发糊」。注意：这不是景深，前景背景会一起糊；日志会打印 `伪虚化诊断：N 个体积里带 WeightedBlendables 的有 M 个` 与挂载结果，若全为空则明确失败，再决定是否冒险自己分配内存。

- **2.0.0-cpp-hotfix25**（2026-11-05）：**收口：景深标注为「本作不支持」，关闭实验通道**——hotfix24 日志给出最后一块拼图：`ViewTarget.POV 的 fstop 在两次 tick 之间从 1.84 变回 40.00`，即便每帧重申，游戏仍会把 `fstop` 写回 `F40`（全合焦）。证据链完整：相机 POV 无效、自建体积只有暗角生效、关卡体积无效、ViewTarget.POV 每帧被写回、`r.DOF.*` 全健康但 console 命令在本作完全不执行、`ShowFlags@0xB8` 整局不变（不是真的 flags）、6864 个材质里无 DOF 后处理材质 → **本作 gameplay 视图不渲染引擎景深**。本版：① 四个景深标签在四种语言里追加「本作不支持」；② `tuning::kDofExperimentalChannels = false` 关闭四条实验通道/探针/采样/每帧重申（换不来画面变化，只会每帧写游戏对象）；③ 第一次下发景深时打一条一次性说明。配置项与滑条保留为参数记录。

- **2.0.0-cpp-hotfix24**（2026-11-05）：**通道⑤ 改为每帧重申**——hotfix23 结果：`ShowFlags@0xB8` 整局一次没变（说明那块内存不是真的 `FEngineShowFlags`，`bit20=1` 作废）；材质普查 6864 个里没有任何 DOF/bokeh 后处理材质（只有 `M_FX_PP_RadialBlur` 等径向模糊）。但审计发现 `ViewTarget.POV` 的 focal 两次进入分别为 221/362 → 这份 POV 被游戏反复重填，而通道⑤ 之前只在开启/拖滑条时写，写一次就被下一帧的 F40 抹掉。本版每帧重申（探针第 3 段期间用强力参数），并打印 `景深诊断：ViewTarget.POV 的 fstop 在两次 tick 之间从 … 变回 …` 直接回答「写进去的值能不能活过一帧」。

- **2.0.0-cpp-hotfix23**（2026-11-05）：**88px 打进四条通道仍不糊 → 收束核查**——hotfix22 把 fstop=0.1 / 对焦 300cm / 传感器 200mm（≈88px）同时写进相机 POV、自建体积、关卡自带体积、`ViewTarget.POV`（后两者写入成功），画面依旧完全不糊，而同一个体积的暗角是有效的 →「值不对 / 通道不对 / 被覆盖」全部排除，只剩「这个视图里没有 DOF pass」。本版：① 每帧采样 `UGameViewportClient+0xB8` 的 16 字节并记录变化，验证那块内存到底是不是真的 `FEngineShowFlags`；② 材质普查，列出所有名字含 blur/dof/bokeh/coc 的材质——找到就能用游戏自己的模糊材质做背景虚化（绕开引擎 DOF），找不到就把景深标注为本作不支持。

- **2.0.0-cpp-hotfix22**（2026-11-05）：**找到按着光圈的那个值：`ViewTarget.POV` 里写着 `fstop=F40`**——hotfix21 打印出`景深诊断：ViewTarget.POV 的景深原值 focal=221(override=1) fstop=F40.00(override=1) sensor=23.8(override=1)`：游戏在相机管理器的 `ViewTarget.POV` 里显式把光圈写成 F40（≈0.04px，等于全部合焦），而这份 POV 在**体积之后以 weight=1** 生效，于是写体积/相机 POV 的光圈全被按回去，只有暗角这类没被覆盖的字段能穿过。本版把**通道⑤ 加进探针第 3 段**：`fstop=0.1 / 对焦 300cm / 传感器 200mm`（≈88px）现在同时写进四条通道。第 3 段糊了 = 引擎 DOF 一直没问题，只是被 F40 按着；接下来只需调滑条默认值/范围（当前默认仅 ≈5px）。

- **2.0.0-cpp-hotfix21**（2026-11-05）：**通道⑤：相机管理器的 `ViewTarget.POV`**——hotfix20 的通道④没成（玩家 Pawn/PC 身上没有原生 `CameraComponent`）。改走 UE 里视图 PP 的另一条来源：`APlayerCameraManager::ViewTarget.POV`，它由 `UpdateCamera` 从相机组件填好，而摄影模式会暂停世界导致它不再重建、一直停在进入前的状态；它很可能带着 `bOverride_DepthOfFieldFocalDistance=1` + `focal=0`（关 gameplay DOF 的标准写法），且在体积之后以 weight=1 生效——正好解释「体积的暗角有效、体积的光圈无效」。本版快照并写入这份 POV 的景深三项，退出还原，并打印 `景深诊断：ViewTarget.POV 的景深原值 focal=…(override=…) …` 直接指认元凶。

- **2.0.0-cpp-hotfix20**（2026-11-05）：**新增通道④：玩家相机组件**——hotfix19 证实**console 命令在本作里完全不生效**（Kismet 与直连 `PlayerController::ConsoleCommand` 都改不动 `r.Tonemapper.Quality`），过去十几轮的 cvar/ShowFlag 操作全是空转。而探针显示「相机 POV 的 PP 无效、体积的暗角有效、体积的光圈无效」——只能解释为体积之后还有一步单独覆盖了 DOF 字段，即 `CalcSceneView` 时从 ViewTarget 的相机组件重填的 `ViewInfo.PostProcessSettings`（weight=1），那里极可能写着 `bOverride_DepthOfFieldFocalDistance=1` + `focal=0`（关 gameplay DOF 的标准写法）。本版遍历玩家 Pawn/PC 名下的 `CameraComponent`，快照并写入我们的焦点/光圈/传感器（退出还原），并打印 `景深诊断：玩家相机组件 … 的景深原值 focal=…(override=…) …` 直接指认元凶。

- **2.0.0-cpp-hotfix19**（2026-11-05）：**90px 强力景深照样不糊 → 转头查 console 命令到底执行了没有**——hotfix18 把 fstop=0.1 / 对焦 300cm / 传感器 200mm（约 90px）同时写进相机 POV、自建体积、关卡自带体积，画面依旧完全不糊，于是「通道不对」「参数太弱」都排除。但日志里 `r.DepthOfFieldQuality 写 3 回读=2` 与 `ShowFlag.DepthOfField 翻转后内存一位没变` 同时出现，最像的解释是**这些 console 命令根本没执行**（`ExecuteConsoleCommand` 返回成功 ≠ 执行）。本版用可回读的 `r.Tonemapper.Quality` 验证 exec 通道，不过就切到直连 `APlayerController::ConsoleCommand`，再在可用通道上重做 ShowFlag 翻转与 `sg.DepthOfFieldQuality 4`。

- **2.0.0-cpp-hotfix18**（2026-11-05）：**用「不可能看不见」的强力景深测通道**——hotfix16 探针经确认：相机 POV 写 `ColorSaturation` 毫无变化、自建体积写 `VignetteIntensity` 明确「四周变暗」→ **相机 POV 的 PostProcessSettings 在本作 gameplay 视图里不被采纳**。回看历史：过去所有极端光圈（F0.08 / 421cm / 164mm ≈ 200px）都写在死通道上，而现用参数 F1.8 / 678cm / 286mm 只有约 3px——难怪「写了没反应」。本版把探针第 3 段换成强力景深（fstop=0.1、对焦 300cm、传感器 200mm ≈ 90px），同时写进相机 POV / 自建体积 / 关卡自带体积，2.5 秒后还原。第 3 段糊了 = DOF 能跑，只是通道/参数问题；没糊 = 该视图里 DOF 被引擎关掉。

- **2.0.0-cpp-hotfix17**（2026-11-05）：**写错通道了：改用关卡自带体积 + 实证 ShowFlag 位索引**——hotfix16 日志显示（1）游戏画质「景深质量」=4、`r.DOF.*` 全是健康值、`r.DepthOfFieldQuality` 被钉在 2 → 质量闸门全部排除；（2）探针里相机 POV 的 `ColorSaturation` 毫无反应、自建体积的 `VignetteIntensity` 有明显反应 → **之前十几轮把景深写在「相机 CameraCache.POV 的 PostProcessSettings」上，而这条通道在本作 gameplay 视图里很可能没被采纳**；（3）深度模糊（不看光圈的分支）也没糊。本版：① 新增通道③——把关卡自带的 `PostProcessVolume`（一定在 `UWorld::PostProcessVolumes` 里）也写上当前光圈/焦距/传感器，退出按快照还原；② 用引擎自己的 `ShowFlag.DepthOfField 0/1` 翻一次并对比内存差异，**实测**真正的位索引与当前值，翻不动就明确写「这条路到此为止」，不再靠猜。

- **2.0.0-cpp-hotfix16**（2026-11-05）：**撤回 ShowFlags 内存写入 + 加一根「深度模糊」探针**——打包后做了一轮 UE 源码核对：① `FEngineShowFlags` 在 Shipping 下 `SHOWFLAG_FIXED_IN_SHIPPING` 类开关**没有存储**，「第一个 32 位字 bit20 = DepthOfField」的映射不成立，hotfix14/15 往那块内存写 bit20 有风险 → 本版**只读不写**，hex 留档、结论交给探针；hotfix15 的包作废。② 混合顺序是「体积 → 相机 POV（weight=1，最后生效）→ 相机动画 PP → `UpdatePhotographyPostProcessing`」，所以摘体积覆盖其实不必要，能顶掉 DOF 的只可能是 POV 之后那几步。③ `DepthOfFieldScale` 在 `DiaphragmDOF.cpp` 里出现 0 次（CircleDOF 专用），关不掉 DiaphragmDOF。新增探针第三段：`DiaphragmDOF::IsEnabled` 的第二条放行分支 `DepthBlurRadius > 0`（不看光圈），`DepthOfFieldDepthBlurRadius=400 / Amount=1.0` 同时写相机与体积——背景 3 米外糊掉 = 引擎 DOF 活着（问题在焦距/光圈被顶掉），三段都没反应 = 本作没跑 DiaphragmDOF（例如 `bSupportsDiaphragmDOF=0` 会让 `IsSupported()` 静默返回 false）。

- **2.0.0-cpp-hotfix15**（2026-11-05）：**闸门每 2 秒复核 + 两条后处理通道分开测**——hotfix14 日志里 `EngineShowFlags@0xB8 = 07FFFFFF FFFFFBFF FFFFFFFF FFFFFFFF`（bit20 本来就是 1，上一版却按 5.3 的声明顺序判形状、每次误报「形状不符」而不敢写）、`r.DepthOfFieldQuality 2 → 3 | 回读=2`（被钉住，写了等于没写）、`sg.DepthOfFieldQuality=0` 且包里存在 `gq.DepthOfFieldQualityLevel`（本作把「景深质量」做成独立画质档）三条同时成立。本版：① 闸门每 2 秒复核——一次性打印 11 个 cvar 普查行、`gq.DepthOfFieldQualityLevel` 低于 3 抬到 3 并补位（退出还原）、`r.DepthOfFieldQuality` 写不动只告警「被钉住」、`ShowFlags.DepthOfField` 若在进入之后被清掉立刻补位并告警；② 探针重做——A 段只去色（整屏黑白）、停顿 1 秒、B 段只加暗角（四周变暗），用肉眼就能分辨到底是「相机 POV」还是「自建体积」在起作用；③ ShowFlags 形状判定改成「位很密、四个字都不为 0」，不再误报。

- **2.0.0-cpp-hotfix14**（2026-11-05）：**最后一道闸门：`ShowFlags.DepthOfField` 位**——hotfix13 日志证实关卡体积确实覆盖了 `fstop`（原值 F7.1）且已被摘掉，但 focal=421cm/F0.08/sensor=164mm（公式算 203px 虚化）依然毫无虚化。后处理通道通、结构对、覆盖也摘了，只剩引擎不建 DOF pass 这一种可能。查 UE5 `ShowFlagsValues.inl`：`DepthOfField` 是 `ALWAYS_ACCESSIBLE` 的真实位（按声明序为第 20 位），默认全开，游戏可在运行时关掉。本版从 `UGameViewportClient + 0xB8` 读 `EngineShowFlags` 前 8 个字打印 hex、用「低 12 位全 1 / 12~16 为 0」的形状确认偏移，若 bit20 为 0 就置位并回读；形状不符则只留 hex 不写内存。另加 `r.DOF.Gather.RingCount`/`r.DOF.HalfRes`/`r.DOF.Recombine.Quality` 读数与抬档（<3 抬到 3，退出还原）。
- **2.0.0-cpp-hotfix13**（2026-11-05）：**暗角生效 = 后处理通道是活的 → 真凶是关卡体积盖掉了光圈**——hotfix12 探针证明写的是真正的 `FPostProcessSettings`（2208 字节/588 字段）、属性名基本都在，且 `VignetteIntensity=3` 让用户看到「四周变暗」，说明相机后处理确实进了渲染。唯一能同时解释「暗角生效、去色无效、景深无效」的机制是字段被更高优先级的覆盖顶掉：本关两个有界 `PostProcessVolume` 显式覆盖了 `DepthOfFieldFstop`。本版在开启景深时快照并摘掉这些体积的 DOF 覆盖（退出/关景深写回），让本 Mod 写的焦点/光圈/传感器成为唯一来源。
- **2.0.0-cpp-hotfix12**（2026-11-05）：**探针 A/B 都执行了但画面毫无变化 → 先证伪「我们写的到底是不是后处理结构」**——hotfix11 的两段探针（相机通道去色、自建体积染红）用户反馈完全没变化；结合 33px 虚化也没反应，必须排除更基础的可能：结构或属性名在这个 build 里不起作用。本版探针改为「自证 + 多重旋钮」：打印被写结构的字节数与字段数（真正的 `FPostProcessSettings` 有两百多个字段）、同时改 `ExposureCompensation+6`/`VignetteIntensity=3`/`ColorSaturation=0` 三个互不相关的旋钮、并在写之前核对每个 `bOverride_*`/值属性是否存在（缺了就打印缺失名 + 该结构里所有相关真实字段名）。
- **2.0.0-cpp-hotfix11**（2026-11-05）：**闸门全开、虚化算到 33px 仍无变化 → 改为分辨「哪条通道真的进了渲染」**——hotfix8 日志证明 `r.DepthOfFieldQuality=2`、CoC 半径 0.025、`override` 全 1、`blendWeight=1.00`、体积 priority=10000 且独立相机确实是 ViewTarget，按公式算到 16.6~33.2px@1920 却没变化。本版进摄影模式后做两段探针：A（1.2s）只把相机通道饱和度压 0 → 黑白；B（1.2s）只把自建体积 ColorGain 改强红。看到 A/B 的组合直接决定下一步（换写入位置 / 写进关卡体积 / 判 DOF 被关）。另加一次性 `MPC 景深候选参数` 日志，用于排查本作是否用自家后处理材质做景深。
- **2.0.0-cpp-hotfix10**（2026-11-05）：**景深配置独立成节点**——PBZFM 设置页原来把 `[dof]` 的六项（`dof_enabled`、对焦距离、光圈 F 值、传感器宽度、对焦步进、光圈步进）混在「相机与移动」分组里，现在拆出独立节点「景深（手动对焦）」（`#group_dof`），四个语言词典同步；纯菜单分组调整，不动配置键名与运行逻辑。
- **2.0.0-cpp-hotfix9**（2026-11-05）：**按键全量可配 + 删除 F9 截图**——原来写死的 `W/A/S/D` 平移、`Shift/Ctrl` 变速、`Alt` 自由视角门控、手柄 `RT/LT` 升降、`LB/RB` 滚转、`LS/RS` 变速全部进配置（`[keys]` / `[gamepad]` 新增对应项，默认值保持不变）；滚轮 FOV 增加 `wheel_fov` 开关与 `wheel_fov_invert` 反向。输入层改用 **1..255 全量 VK 轮询**（旧白名单漏了 `OEM_4/OEM_6`，默认 `[`/`]` 光圈键实际不触发，且任意改键都可能静默失效）。删除 **F9 无 UI 截图**整套逻辑（本作导出表无 `FScreenshotRequest::RequestScreenshot`，原本也只打一条不可用日志）。PBZFM 设置页键盘键位补全到 26 项（含 `F7` 画面定格、对焦/光圈键），状态页与 UE4SS 控制台速查改为显示当前生效键名。
- **2.0.0-cpp-hotfix8**（2026-11-05）：**真因是物理量：默认参数与上限根本虚不了**——hotfix7 自检证明引擎侧闸门全开（`r.DepthOfFieldQuality=2`、CoC 半径 0.025、`override` 全 1、`blendWeight=1.00`；`DepthOfFieldEnabled` 在本作不存在）。薄透镜换算显示玩家落点只有 0.4~2.5px、旧默认 0.42px、旧配置（对焦 513 米 + F18.4）0.00px —— 2px 以下肉眼等于没有。改法：光圈下限 0.5→0.05、传感器上限 100→400mm（光圈滑条改对数）、默认值改 300cm/F0.5/60mm（≈9.7px@1920）、日志直接打 `预计背景虚化≈X.Xpx`、旧配置进模式时一次性纠正并打印原值、加一次性黑白探针区分「虚化太弱」与「后处理通道没进渲染」；新增 `core/dof_math.h` 与 12 项断言。
- **2.0.0-cpp-hotfix7**（2026-11-05）：**「景深调数值没反应」：把引擎侧三道闸门一次调到可用**——既有证据说明字段写对了、后处理通道也通（AA/动态模糊/GI 覆盖每帧生效、自建 priority=10000 无界体积建得起来、关卡 6 个体积都不抢 DOF），剩余可能全在引擎闸门：`r.DepthOfFieldQuality<=0`（不建 DOF pass）、`ShowFlag.DepthOfField` 被关、`r.DOF.Kernel.Max*Radius` 把 CoC 夹没；另补写 UE5.4+ 的 per-view 总开关 `DepthOfFieldEnabled`。开景深时一次处理这三道闸门（各记基线，退出/关景深/世界换代还原，失败交给 1s/2s/4s 重试环），并新增一轮「景深自检」日志（CVar 读数 + 相机/体积两个通道的写后回读，读不到印 `<na>`）。
- **2.0.0-cpp-hotfix6**（2026-11-05）：**对焦距离滑条窗口直接收到 300~1500cm**（按用户建议）——菜单滑条只铺 3~15m（1 像素≈12cm，线性更直观），`[dof]` 配置与按键步进仍是 1..100000cm（微距/远景照旧可用 config 或 PAGE_UP 调）；窗口外的旧值打开菜单会贴在右端、一拖动即夹到 1500。`对焦步进` 保持对数。
- **2.0.0-cpp-hotfix5**（2026-11-05）：**修复「对焦距离滑条拖不到可用值、画面自然没景深」**——hotfix4 的诊断证明体积通道正常、关卡体积只覆盖了有界 `fstop`，问题在数值：1..100000cm 的线性滑条 1 像素≈500cm，可用的 300~1500cm 只占最左 0.3%，每次落点都是几万 cm + F16~20（物理上等于全清晰）。`对焦距离` 与 `对焦步进` 改为对数滑条（500cm 落在 54% 处），其余 Float 保持线性；并加断言锁死「对数字段下界>0、这两个字段必须对数」。hotfix6 按用户建议把该字段改成 300~1500cm 的线性窗口（更直观），对数刻度保留给 `对焦步进`。
- **2.0.0-cpp-hotfix4**（2026-11-05）：**修复「数值改得动、画面没有景深」**——UE 的最终后处理按优先级把场上 `PostProcessVolume` 的 `bOverride_*` 盖在相机/POV 设置之上，相机通道只是基座；关卡里只要有体积覆盖 DOF，写进去的值就进不了画面。新增**景深专用后处理体积**通道（`bUnbound + Priority=10000`，把 DOF 三项与 override 写进它的 `Settings`），相机/POV 通道保留、任一成功即可用；退出/关景深/切后端/换代时销毁。同时新增 Info 级诊断：体积建立日志、场上体积优先级与 DOF 覆盖清单、每次下发的 `focal/fstop/sensor` 实际值。
- **2.0.0-cpp-hotfix3**（2026-11-04）：**修复 PBZFM 菜单 Float 滑条拖不动/松手回弹**——自绘页每帧把草案从 `mirror_` 重建，而 Float 滑条是「拖动中只改草案、松手才提交」，中间值每帧被丢弃、松手提交的是旧值（`ImGui` 的拖拽为逐帧增量，宿主每帧重置绑定变量就只能动一帧）。改为草案跨帧写回 `mirror_`，并加 `mirror_generation_` 代次防止旧草案覆盖同帧的配置重载。相机/移动/手柄/冻结各组的所有 Float 滑条一并修好。
- **2.0.0-cpp-hotfix2**（2026-11-04）：**景深/手动对焦缺陷修复**——手柄调焦/调光圈不再连带滚转（四个组合键按住时肩上键只当修饰键）；景深写不进改按秒重探、环境恢复即自动可用（不再「一次失败整局对焦键失效」）；接管快照与运行值拆成两套变量，退出还原真正写回进入前的原值（「关→开→退出」也会重新快照）；后端热切换保留本次会话调好的对焦值，切换失败回滚也重新下发景深；独立相机后端关景深立即撤销 `bOverride_*`；景深写失败不再被误报成「应用相机位姿失败」；`[dof]` 合法范围集中到 `core/tuning.h`。核心层 4,498 项测试全绿，详见 [CHANGELOG.md](CHANGELOG.md)。
- **2.0.0-cpp-hotfix1**（2026-11-04）：**修复 F7 定格期间 Ctrl+P/PixPin 截图卡顿**——上版白名单挂起没有生效（`NtQueryInformationThread(ThreadQuerySetWin32StartAddress)` 在 `THREAD_QUERY_LIMITED_INFORMATION` 句柄上取不到起始地址，日志每次只显示「跳过 1 个」）。本版：枚举保持 limited 权限，查询起始地址时临时用 `THREAD_QUERY_INFORMATION` 句柄重试，并用 `VirtualQuery(...).AllocationBase` 解析线程所属模块；同时把 PBZFM、Steam/NVIDIA/Discord/RTSS 等已知覆盖层模块加入跳过名单。日志应看到「跳过 M 个非游戏/输入钩子线程」中 M 明显大于 1（正常为 PBZFM 的 2~3 个线程 + 本模组输入线程 + 其他覆盖层）。
- **2.0.0**（2026-11-04）：**冗余清理 + 缺陷修复 + 相机后端可热切换**。
  删掉 3,129 行冗余（含与 UE4SS 逐字节相同的 ImGui backends 2,763 行、变更史从 README 迁出、合并重复构建脚本），
  释放 1,833 MB 过期构建目录；修掉 7 处实际缺陷（解冻级联分叉导致布料/植被可能永久静止、手柄回退键位错 3 处、
  发行包配置与代码默认值差 10 项、菜单坏键名显示 `?`、4 个零消费者配置项、`.bat` 中文注释导致脚本半坏、
  PBZFM 菜单事件只注册不反注册）；相机后端（接管当前相机 ↔ 独立相机）改为**菜单内即时切换**；
  接管依赖的 `GameViewportClientTickPost` 缺失时**自动降级**为独立相机而不是镜头卡死。
- **1.0.76-cpp-hotfix29**（2026-11-04）：F7 定格改为**白名单挂起** —— 只挂 `PBZ.exe` / `MRTCEngine.dll` / `AkSoundEngineDLL.dll` 里的线程，第三方覆盖层与输入钩子线程全部保留，解决 F7 期间 Ctrl+P 截图卡顿。
- **1.0.76**（2026-11-04）：修复「进入摄影模式自动冻结后布料件整体换位」—— 不再写 `r.MorphTarget.Enable/ForceUpdate` 与 `pbz.Sway.SkipSkeletalKinematicBoneUpdate`，改为按扫描到的真实 Sway 类名停组件 tick。

## 完整变更史

1.0.3 → 1.0.76 共 62 节逐版本「这次改了什么」已整体迁到 **[CHANGELOG.md](CHANGELOG.md)**（正文未改动，只换文件）；
更详细的取证链（每次真机日志的根因分析、已判死路线）见 [PROGRESS.md](../PROGRESS.md)。
