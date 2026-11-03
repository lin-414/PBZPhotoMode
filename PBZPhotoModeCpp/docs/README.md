# PBZ Photo Mode — 影之刃零摄影模式（C++）

基于 UE4SS 的 C++ 摄影模式 Mod：核心逻辑与引擎适配层分离，`src/core/` 可在没有游戏与 UE4SS 的环境下运行单元测试。

| 项目 | 状态 |
| --- | --- |
| 版本 | v1.0.75 |
| 核心层测试 | 当前断言总数见 [test_count.txt](test_count.txt)，全部通过 |
| 实机验证 | F 键冻结、HUD 隐藏、进入自动暂停、动作冻结稳定已验证；**1.0.36 语音静音双保险（ID 暂停 38/38 + 音量兜底 38/38，解冻成对恢复）实机通过**；**1.0.66 移动视角卡顿修复实机通过（2026-11-02）：全日志无「时间偏移」，Wwise 整引擎 Suspend/Wakeup 成对出现**；超宽屏/DInput 手柄/字幕压制各项待回归 |

## 项目结构

```
src/core/      纯逻辑，不碰引擎 —— 已完成
  config        配置解析（含容错、范围夹紧、默认配置生成）
  vk            键名 -> 虚拟键码（F1..F12 / ADD / SUBTRACT / OEM_PLUS / 修饰键…，含 MOUSE_LEFT/RIGHT/MIDDLE/X1/X2）
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

不需要游戏、不需要 UE4SS，只需要 VS 2022 生成工具。实际断言总数由 `tools\update_test_count.bat` 跑完测试后写入 [test_count.txt](test_count.txt)（单一来源；运行时断言次数，不是独立用例数）。

## 键位

| 操作 | 按键 |
| --- | --- |
| 进入/退出摄影模式 | `F2` |
| 自由转视角 | 按住 `Alt` + 移动鼠标 |
| 前后左右平移 | `W` `A` `S` `D` |
| 方向键转向/俯仰 | 方向键 左/右转视角（`yaw_speed`，默认 90 度/秒）、上/下俯仰（`pitch_speed`，默认 30 度/秒） |
| 升降 | 默认左键上移 / 右键下移（`key_vertical_up` / `key_vertical_down` 可改成鼠标中键、侧键或键盘键；translate 模式；手柄 RT / LT） |
| 滚转（旋转画面） | `Q` / `E`（E 顺时针 / Q 逆时针；`key_roll_left` / `key_roll_right` 可改键，`roll_speed` 可调）；手柄 `RB` / `LB` |
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

## 已知缺口（1.0.75）

- **`log_level` 当前未真正生效**（M-6 未修）：配置项会被解析，但日志输出级别没有按它过滤。
- **已弃用键**：`interval_ms`、`ui_scale`、`time_offset_compensation` 接受但忽略，每个键首次出现时告警一次；不写则行为与默认一致。
- **配置越界值**：现在 clamp 到 `min/max` 边界（M-5），不再回落默认值；想让某项恢复默认请删掉该行或改成默认值。
- **锁定/血条/字幕等隐藏**：依赖游戏内反射查找，换世界后缓存统一失效并重新扫描，个别版本/场景可能扫不到（日志会告警）。

## 配置文件

`config.ini`（示例见 `config.ini.example`）。写错不会崩：非法值保留默认、并在日志里给一条 warning；数值越界会 clamp 到边界。

- **注释规则**：`;` 或 `#` 在引号外即视为注释起点（独占一行或以空白开头最易读，但解析器不强制要求前置空白，`fov = 50;备注` 也有效）；引号内的 `;`/`#` 属于值本身，不当作注释。`key_toggle = "G"` 这类带引号写法同样适用。
- **已弃用键**：`interval_ms`、`ui_scale`、`time_offset_compensation` 接受但忽略，每键告警一次。
- **`log_level`**：当前解析后未真正生效，见「已知缺口」。

## 构建 DLL 的前置条件

1. **UE4SS 源码** —— 自行克隆 RE-UE4SS，用 `tools\build_dll.bat <路径>` 指定
2. **UEPseudo 依赖** —— RE-UE4SS 的私有子模块，需要 GitHub 账号被 Epic 授权
3. **Rust 工具链** —— UE4SS 的构建链路需要
4. **CMake + VS 2022 C++ 工具链**

具体步骤见 `docs/BUILD.md`，API 核对清单见 `docs/P1_CHECKLIST.md`。

## 游戏内设置菜单（PBZ-Framework-Menu，可选）

1.0.42 起本模组是 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu)（游戏内 ImGui 菜单框架，DirectComposition 覆盖窗实现，不碰游戏交换链、不影响 FSR3 帧生成）的**第一个客户端模组**。两个模组各自解压进 `ue4ss\Mods\`（enabled.txt 启用，互不改 mods.txt）后：

- 游戏内按 PBZFM 的呼出键（默认 `F8`）打开菜单，侧栏出现「**PBZ Photo Mode**」分区（1.0.43 起两页均由本模组自绘，ImGui 版本与框架 1.92.1 对齐）
- **「设置 · Settings」页**：首行「语言」下拉——**跟随 PBZFM 菜单** / 中文 / English / 日本語 / 한국어 / 外部词典，当帧生效并写回 `config.ini` 的 `[ui] menu_language`；旁边「重新载入词典」按钮；其余为键盘键位（菜单内捕获式改键）、行为开关（进入时暂停时间 / 冻结方式 / 屏蔽输入 / 超宽屏解锁 / 手柄支持）、隐藏开关（角色 / HUD / 交互提示 / 敌人血条 / 字幕）、冻结增强（Wwise 整引擎挂起 / 逐语音兜底 / 剧情暂停 / 相机震动 / 风源 / MPC / 诊断日志）、相机与移动参数、手柄参数——改动**实时生效并写回 `config.ini`**
- **「状态 · Status」页**：OFF / LIVE / FROZEN 状态、位置与朝向读数、键位速查；FOV 滑条（实时变焦）与「进入/退出摄影模式」「冻结/恢复时间」「重置视角」「重新载入 config.ini」按钮
- **多语言**：内置 zh-CN / en-US / ja-JP / ko-KR；**外部词典**放 `PBZPhotoModeCpp\languages\<id>.json`——UTF-8 扁平 JSON，key=中文原文、value=译文，可选 `__display` 当显示名；同名覆盖内置、新文件名即新语言，游戏内热重载（点按钮或等 2 秒）。翻译缺失的条目回落中文。韩文渲染用自建「雅黑 + Malgun Gothic」合并字体，不受框架启动字体限制
- 手柄组合键绑定（多按钮同按）在菜单里表达不了，仍写在 `config.ini` 的 `[gamepad]` 节

**值的流向**：`config.ini` 是唯一真源。模组启动时用 config.ini 现值播种菜单；菜单里的改动写回 config.ini 并实时应用；手动改过 config.ini 后点菜单里的「重新载入 config.ini」，菜单显示值会跟着文件走。卸载 PBZFM 后模组照常工作（纯快捷键，日志提示「未检测到 PBZFM 菜单框架」）。

实现：`src/pbzfm_client.*`（运行时 GetProcAddress 接入 PBZFM.dll，零链接依赖）+ `src/pbzfm_schema_map.*`（菜单 ↔ Config 字段映射表，随核心层单测）。菜单回调在框架线程只提交数据，所有引擎交互回到引擎 Tick 线程执行。

## 路线图

- **P0**：核心逻辑 + 单元测试 → 完成
- **P1**：引擎适配层与 DLL → 完成，已实机运行
- **P2**：PostProcess 快照还原、滤镜、连拍、录像

## 1.0.75 这次改了什么（Q/E 滚转键可在 config.ini 配置）

- 问题：用户反馈 Q/E 旋转视角（滚转）快捷键在 `config.ini` 里改不了——检查发现 1.0.74 及以前 Q/E 硬编码在 `input_logic.cpp` 的 `build()` 里（`kQ`/`kE` 常量），`[keys]` 段只有 `key_roll_reset`（滚转归零），没有滚转左右键配置项。
- 修复：新增 `[keys] key_roll_left` / `key_roll_right`（默认 `Q` / `E`，左=逆时针、右=顺时针），解析进 `KeyMap` 并由 `InputLogic` 使用；`config_ini_text` / `config.ini.example` / PBZFM 菜单「快捷键（键盘）」页同步，PBZFM 改键后写回 `config.ini`。
- 兼容：不写新键名时默认值仍是 Q/E，旧配置无需改动；`key_roll_reset`（默认 Z，滚转归零）保持不变。
- 其他：控制台诊断面板的滚转提示改为显示当前实际键位；四语言词典补 `#key_roll_left/right`。
- 核心层测试 2400 → **2529 项全绿**；DLL 编译 exit 0。

## 1.0.74 这次改了什么（过场对话 Flow 冻结/诊断：保留相机，不挂线程）

- 背景：用户提出「全线程冻结」；经 ANALYSIS.md / 线程模型核对，直接挂起所有线程会连渲染线程和 game thread 一起停，摄影相机与 F2 都会失效，只能得到静帧、无法取景；因此采用方案 A：保留相机线程与渲染，只冻结对话 Flow。
- 1.0.73 实机日志（19:02）：`语音位置校验：可读 41 个，暂停期位置未变 41 个，前进 0 个（最大 +0 ms）`——Wwise 播放头已冻结，剩余疑点在对话 Flow 继续 Tick（`WBP_FlowDialog_C` 仍为 Hidden、Slate 仍 Tick）。
- 新增配置 `freeze_dialogue_flow`（默认 `true`，仅 `hide_subtitles=true` 时生效）：冻结时把 `WBP_FlowDialog_C` 从 `Hidden` 改为 `Collapsed`（ESlateVisibility=1，停止 Slate Tick），解冻恢复 `Hidden`（2，仍隐藏但继续 Tick）；`leave()` 退出时按原可见性恢复。
- 新增 `PlayAudio_C.ForceFinishAudio` / `StopAudio_C.ExecuteUbergraph_StopAudio` 只读诊断钩子：冻结期被调用时打日志（前 8 次），只确认对话 Flow 是否提前结束语音，不改播放行为。
- 不挂线程、不碰 QPC/IAT、不改变 Wwise/相机逻辑；无对话/普通场景 FlowDialog=0 不产生新日志，配置关闭或 `hide_subtitles=false` 时行为回退 1.0.73。
- 核心层测试 2306 → **2400 项全绿**；DLL 编译 exit 0。

## 1.0.73 这次改了什么（过场/对话中进入摄影模式后语句仍不续播）

- 实机反馈：1.0.72 装后，过场/对话中按 F2 进入摄影模式再退出，**被截断的语句仍不能续播**。
- 根因（1.0.72 遗留）：`AK::SoundEngine::Suspend(true,false)` 的 render-anyway 只是“不输出”，音频线程和语音时间轴照常推进；即使逐 PlayingID `PBZPausePlayingID` 执行成功（日志 `对话语音暂停（PlayingID）：截获 47，执行 47`），当前句也会在冻结期间无声播完，解冻后无法从原位置续播。1.0.36 只用逐 PlayingID + 组件音量时实机验证可续播。
- 修复①（整引擎 Suspend 降级为无 ID 兜底）：有逐语音 PlayingID 时**跳过整引擎 Suspend**，逐 PlayingID 真暂停（`PBZPausePlayingID`/`PBZResumePlayingID`）恢复为唯一常规路径；整引擎 `Suspend(true,false)` 仅在 `pause_wwise_voices` 返回 0（没有截获到 ID）时作为静音兜底（`wwise_engine_suspend` 仍默认 true，但只在无 ID 时生效）。
- 修复②：新增 `GetPlayingIDPositionMs` 语音位置校验（只读诊断）：暂停时记录每个 PlayingID 的位置，恢复前再读一次，日志 `语音位置校验：可读 N 个，暂停期位置未变 N 个，前进 N 个（最大 +X ms）`；前进超过 100ms 会 Warn `语音暂停未冻住：当前句在冻结期继续前进`。
- 修复③：`pause_wwise_voices` 返回值只统计实际执行的 PlayingID 数（组件音量兜底不计入），避免 core 误判「逐语音已生效」；补 `对话类函数：…` 一次性诊断。
- 验证：有 ID 时日志应出现 `逐语音真暂停已执行 N 个 PlayingID，跳过整引擎 Suspend`，且**不再出现** `Wwise 音频引擎已静音挂起`；无 ID 时才出现 `无逐语音 PlayingID，整引擎 Suspend 作为静音兜底`。
- 核心层测试 2301 → **2306 项全绿**；DLL 重编通过。

## 1.0.72 这次改了什么（过场动画进入/退出摄影模式后语句断句不续播）

- 实机反馈：过场动画（剧情演出）中按 F2 进入摄影模式、再退出后，**正在播的语句被截断，不能从原位置续播**。
- 根因①（音频）：1.0.66 起冻结走 `AK::SoundEngine::Suspend(false,false)` 整引擎硬挂起。Wwise 2024.1 的签名是 `Suspend(bool in_bRenderAnyway, bool in_bFadeOut)`，`(false,false)` 表示音频完全不处理（连 `RenderAudio` 也不处理），音频线程被硬停、事件队列与语音流状态丢失，解冻后播到一半的语句无法续播；同时绕过了 1.0.36 实机验证过的逐 PlayingID `PBZPausePlayingID`/`PBZResumePlayingID` 路径。
- 根因②（镜头）：退出摄影模式时 `restore_view()` 固定还原到玩家 Pawn，过场动画期间的 ViewTarget（过场相机）被切走，正在播的对话/语音被打断。
- 根因③（对话 UI）：字幕文本画在 `WBP_FlowDialog_C` 等对话控件上；`Collapsed` 会停止控件 Tick、让对话 Flow/语句推进停摆；进入时关闭 `bEnableSubtitles` 也会截断当前语句。
- 修复①：整引擎改 `AK::SoundEngine::Suspend(true,false)`（render-anyway：音频继续处理但不输出，保住语音流状态）；冻结顺序恢复为「先逐 PlayingID 真暂停 → 再整引擎 render-anyway 静音」，解冻逆序「先 `WakeupFromSuspend` 恢复输出 → 再 `PBZResumePlayingID` 恢复语音」。
- 修复②：进入时保存相机管理器的原 `ViewTarget`，退出优先还原它（日志 `进入前 ViewTarget 已记录` / `已还原进入前 ViewTarget`），记录不可用时才回退玩家 Pawn。
- 修复③：FlowDialog 家族控件用 `ESlateVisibility=2`（Hidden，保留 Tick），其他字幕控件仍用 `Collapsed`；扫描到 FlowDialog 时保留 `bEnableSubtitles` 原值、只隐藏控件（日志 `检测到对话 UI（FlowDialog），保留字幕设置开关不写…`）。
- 配置说明：`stop_wwise_on_freeze` 仍默认 `true`；`wwise_engine_suspend` 仍默认 `true`，但现在是**第二层 render-anyway 静音**（音频继续处理、不输出），逐 PlayingID 暂停始终先行，不再被整引擎替代。
- 核心层测试 2294 → **2301 项全绿**；DLL 重编通过。

## 1.0.67 这次改了什么（清理两处旧日志提示）

- **PostProcess 快照不再误报失败**：本版不修改后期处理设置，`save_postfx()` 没有需要保存的内容；此前返回 `false` 导致每次进摄影模式都打 `PostProcess 快照失败`。现在按“无需还原”处理并返回成功，退出时 `restore_postfx()` 为空操作，不再刷警告。（1.0.75 收口批次：改为如实返回 `false` 并日志「PostFX 快照暂不支持」，不再宣称“已保存、退出会还原”；后续版本已把该死接口整体移除）
- **相机修饰器列表改用 UE5 正确字段名**：`APlayerCameraManager` 的运行时列表在 UE5 叫 `ModifierList`，旧代码只查 `CameraModifiers`（旧名/部分分支）导致找不到数组、冻结时跳过修饰器治理。现在按 `ModifierList` → `CameraModifiers` → `CameraModifierList` 顺序尝试；找到后正常停用/恢复，确实不存在时才打一次信息日志（恢复阶段也不再无条件警告）。

## 1.0.66 这次改了什么（修复移动视角卡顿）

- **现象**：1.0.65 相比 1.0.64 没有明显改善，反而在冻结状态下移动视角时频繁卡顿。
- **根因（时间偏移补偿）**：1.0.65 的 IAT Hook 把 `PBZ.exe` 和 `MRTCEngine.dll` 的 `QueryPerformanceCounter` 也换成了“冻结期返回常量”的钩子。PBZPause 能这么做，是因为它把进程内所有线程（包括渲染线程）都挂起了；摄影模式必须保留渲染线程和相机 Tick，QPC 被冻住后渲染线程的帧计时/引擎 delta 变成 0，画面就会一顿一顿。这个前提在本项目不成立，套用后只有副作用。
- **修复**：整段移除时间偏移补偿（IAT Hook、`time_offset_begin/end`、配置项、菜单项与四语言标签全部删除）；旧 `config.ini` 若保留 `time_offset_compensation = true` 会收到“已移除并忽略”的 warning，不影响启动。摄影模式仍用 `SetGamePaused` / 时间缩放冻结，不依赖 QPC 钩子。
- **同时修复 Wwise 整引擎 Suspend 的静默失效**：当前 PBZ 把 Wwise 静态链接进 `PBZ.exe`，`AkSoundEngineDLL.dll` 并不会被加载；1.0.65 只查 DLL，导致 `AK::SoundEngine::Suspend` 从未真正调用、每次都回落逐语音。1.0.66 优先从主程序导出表解析 `Suspend` / `WakeupFromSuspend` / `IsInitialized`，失败才回落 DLL 与逐语音，并在日志中写明接口来源；同时修正 `AKRESULT` 成功值判断（`AK_Success = 1`，旧代码误把 1 当失败）。
- 核心层测试 **2294 项全绿**（移除时间偏移用例，保留 Wwise 整引擎优先/回落/关闭用例）。

## 1.0.65 这次改了什么（修复 1.0.64 冻结崩溃）

- **根因**：1.0.64 的 `PatchModuleImports` 在解析导入函数名时把 `IMAGE_THUNK_DATA.AddressOfData`（RVA）直接当指针传给 `strcmp`，没有加模块基址。PBZ.exe 的 RVA 是 `0xD9CEABE`，`strcmp` 访问 `0xD9CEAC0` 触发 `0xC0000005`，所以一按 F2 自动冻结就崩。
- **修复**：按 PBZPause 原实现改为 `module_base + AddressOfData + sizeof(WORD)`，并对 `FirstThunk` / `OriginalFirstThunk` / 名称 RVA 增加模块 `SizeOfImage` 边界校验，坏模块/怪 import 表直接跳过而不是让 `strcmp` 读飞。
- 本次崩溃 dump：`crash_2026_11_02_13_17_56.3389705.dmp`，异常地址 `ucrtbase.dll+0xED8B0`，栈顶回到 `PBZPhotoModeCpp.dll!patch_module_time_imports`。

## 1.0.64 这次改了什么（Wwise 整引擎 Suspend + 时间偏移补偿）

- **Wwise 整引擎挂起**：冻结时优先调用 `AkSoundEngineDLL.dll` 的 `AK::SoundEngine::Suspend(false,false)`，解冻 `WakeupFromSuspend(0)`——一次覆盖环境/音乐/语音/UI 全部声音，解冻从冻结位置继续；引擎未初始化或调用失败自动回落原有逐 `PlayingID` + 组件音量兜底（`stop_wwise_on_freeze` 仍总开关，新配置 `wwise_engine_suspend` 默认 true）。
- **时间偏移补偿**：借鉴 PBZPause 的 IAT Hook 方案，冻结前把主程序与游戏侧 DLL（MRTCEngine/AkSoundEngineDLL/Media Foundation/audioclient）的 `QueryPerformanceCounter`、`GetTickCount`、`GetTickCount64`、`timeGetTime` 换成钩子；冻结期间返回暂停瞬间值，解冻时把本次暂停时长累加进偏移——动画/物理/粒子/计时器不会在解冻瞬间跳时间。新配置 `time_offset_compensation` 默认 true，Hook 失败自动降级并告警；不 patch 本模组自身的 CRT，摄影相机 dt 不受影响。
- **安全边界**：只改 IAT 不挂线程；钩子只用原子变量、不分配/不加锁；`Ue4ssPort` 析构时恢复所有 IAT 槽，避免 DLL 卸载后悬空。
- 核心层测试 **2372 项全绿**（新增 Wwise 整引擎优先/回落、时间偏移时序/失败降级/关闭用例）。
- **后续（1.0.66）**：实测确认时间偏移补偿会冻结渲染线程 QPC、导致移动视角卡顿，已整段移除；Wwise 整引擎 Suspend 保留并从 PBZ.exe 静态导出解析。

## 1.0.63 这次改了什么（升降键可配置）

- **需求**：右键/左键是升降键，但 config.ini 里没有键位项，无法改成其它鼠标键或键盘键。
- **实现**：新增 `[keys] key_vertical_up` / `key_vertical_down`（默认 `MOUSE_LEFT` / `MOUSE_RIGHT`）。键名表新增 `MOUSE_LEFT` / `MOUSE_RIGHT` / `MOUSE_MIDDLE` / `MOUSE_X1` / `MOUSE_X2`（别名 `LMB` / `RMB` / `MMB` / `MOUSE_BACK` / `MOUSE_FORWARD`）；输入层把 5 个鼠标键并入 `GetAsyncKeyState` 轮询，`translate`（上下平移）与 `look`（俯仰）两种模式都按新键工作。
- PBZFM 菜单「快捷键（键盘）」页同步显示两项，改键捕获支持鼠标键；四语言词典新增对应标签。
- 核心层测试 **2216 项全绿**（新增鼠标键名解析、默认值、交换键位、键盘键替代与 PBZFM 字段往返用例）。

## 1.0.71 这次改了什么（字幕/交互提示反复进出后仍消失）

- 实机反馈：1.0.70 不再崩溃，但进入相机模式再退出几次后字幕和 E 交互图标一直消失。
- 日志证据：只有 `UI 设置修复：等待玩家进入游戏世界…`，20 次后 `UI 设置修复多次重试未成功…跳过`（修复从未执行）；每次进出都是 `字幕设置开关原值=关（读取=GetSubtitlesEnabled()）`、`场景交互提示开关原值=关（读取=GetSceneInteractPromptVisible()）`。
- 根因：① `find_live_player_pawn()` 要求 Player 属性 + Pawn + `bBegunPlay` 全部命中，实机 Player 属性/世界属性读不到导致永远失败；② 设置原值 getter 优先读到了未同步旧副本，退出把「本来开着」还原成关。1.0.34 实机证明 `bEnableSubtitles` / `bSceneInteractPromptVisible` 属性才是生效值。
- 修复：`find_live_player_pawn()` 只要求 Pawn 可读、`bBegunPlay` 仅在明确 false 时拦截；修复只在非摄影模式的 Tick 执行。`read_bool_setting()` 改为属性优先、getter 兜底，回读校验按属性；写仍走 setter + Pending + 属性三路。
- 核心层 2294 项测试全绿；DLL 重编通过。

## 1.0.70 这次改了什么（进入游戏时崩溃）

- 实机反馈：安装 1.0.69 后进入游戏即崩溃（`crash_2026_11_02_15_36_45.7956922.dmp`）。
- minidump 取证：异常 `0xC0000005` 读地址 `0xFFFFFFFFFFFFFFFF`，RIP=`PBZPhotoModeCpp.dll+0x198A3`（`pbzpm::find_property`，ue4ss_port.cpp:117）；调用链 `recover_ui_settings_once` → `player_pawn`（ue4ss_port.cpp:939）→ `find_property(UObject*)`（:126）→ `find_property(UStruct*)`（:117）。
- 根因：1.0.69 的 UI 一次性修复启动 3 秒就调用 `player_pawn()`；`controller()` 缓存的 PlayerController 在换图/读档时已被销毁，`GetClassPrivate()` 读成 `0xFFFFFFFFFFFFFFFF`，旧 `is_ok` 只判非空，`find_property` 把 -1 当 `UStruct*` 构造 `TFieldIterator` 读地址 -1 崩溃。
- 修复：`is_ok()` 增加指针范围/对齐 + SEH 保护的 `GetClassPrivate` + `FUObjectItem` 对象数组反查；`find_property`/`find_function` 同样前置校验。UI 修复改用新增的 `find_live_player_pawn()`——每次重新枚举 PlayerController，要求 Player+Pawn 都在且 Pawn 所在 `UWorld.bBegunPlay=true`。
- 核心层 2294 项测试全绿；DLL 重编通过。

## 1.0.69 这次改了什么（字幕/交互提示退出后一直消失）

- 实机反馈：1.0.68 白点/血条正常了，但退出摄影模式后**字幕与交互按钮一直消失**，重进重出也不行。日志证据：首次进入即「字幕设置开关已关闭（原值=关，回读=关）」——两个开关在测试前就已是关闭态（旧版本 1.0.30 时代遗留：还原静默失败被游戏存盘）。
- **一次性修复**：新配置 `repair_ui_settings_once`（默认开）——引擎 Tick 起每 3 秒试一次（上限 20 次/约 1 分钟），把字幕/场景交互提示两个设置强制打开、回读校验、调用 `PBZSettingsShared::SaveSettings()` 立即存盘，成功后写 `ui_settings_repair_done.flag` 只执行一次。若你本来就关闭字幕，把它设为 false。
- **设置读写加固**：字幕/提示开关改为 getter 优先读（`GetSubtitlesEnabled`/`AreSubtitlesEnabled`/`GetSceneInteractPromptVisible`）、setter + Pending + 属性三路写、回读校验，日志记录原值与回读，不再静默写坏玩家设置。

## 1.0.68 这次改了什么（锁定白点与锁定血条一起残留）

- 实机反馈：锁定敌人后进摄影模式，**血条与白点一起残留**。日志证据：原血条模式=OnlyLockedTarget（切档无效——该模式只为当前锁定目标显示血条，锁定目标不清就永远在）；关键词扫描零命中（白点不是普通 UMG/可视化组件）。
- exe 反射字符串取证找到游戏自己的锁定系统 `TargetingComponent`（PBZ 里锁定 = Focus 家族）：`LockWidget` / `SecondaryLockIndicators`（锁定/副目标指示器控件）、`SetShowLockIndicator(bool)`、`HasLockTarget` / `GetLockTarget` / `SetLockTarget` / `StopLockTarget`、`TargetingHandlers`。
- 本版（`hide_lockon_marker` 默认开）：运行时定位锁定系统对象 → `LockWidget`/`SecondaryLockIndicators` 逐个 Collapsed（宿主 WidgetComponent 一并隐藏）→ `SetShowLockIndicator(false)` → **保存 `GetLockTarget` 后 `StopLockTarget` 清掉锁定目标**（血条根因）；退出时 `SetLockTarget(保存目标)` + `SetShowLockIndicator(true)` 成对恢复。
- 关键词补网：`focus`/`indicator`/`monsterhp`/`enemyhp`/`hpbar`/`healthbar`（实机可见清单 `WG_PBZFocus_C` 即疑似白点控件）；全名带 Monster/Enemy 的 WidgetComponent（敌人头顶血条/名牌）一并隐藏兜底。
- 诊断④：锁定系统对象清单（类/对象/有锁定/目标/成员表）；诊断①②上限 40→120。所有动作逐条记日志、退出按原值还原。

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
