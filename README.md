# PBZ Photo Mode — 影之刃零摄影模式 Mod（C++）

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS C++ Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV；进入时自动暂停时间，`F` 可随时冻结/恢复。纯快捷键操作，游戏内无菜单界面。

## 功能

- **自由相机**：`F2` 进入 / 退出
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；左键上移 / 右键下移；`Q` / `E` 滚转（E 顺时针 / Q 逆时针）；手柄 `RB` / `LB` 同样滚转
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **时间冻结**：进入摄影模式自动暂停时间（默认开启，可在 `config.ini` 关闭）；`F` 随时冻结 / 恢复（默认使用游戏暂停，真正冻结角色、敌人与特效）；剧情/过场播放器一并暂停（解冻恢复），冻结状态每秒自检、被游戏改写时自动重压；对话语音走 Wwise 时可选一次性静音（`stop_wwise_on_freeze`，默认关闭）
- **超宽屏支持**：进入摄影模式自动解除 16:9 画幅约束（游戏官方支持 21:9 / 32:9 超宽屏），两侧黑边消除，退出时还原
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **隐藏 HUD**：进入摄影模式自动隐藏游戏 HUD（血条/快捷栏/任务提示等），退出时恢复原可见性
- **隐藏交互提示**：进入摄影模式自动隐藏场景互物体旁的“E”键图标（世界空间 UMG），退出时恢复
- **隐藏敌人血条**：进入摄影模式自动隐藏敌人（怪物）头顶血条（切到游戏自带"仅锁定目标"血条显示模式，退出还原原模式）
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `config.ini` 里

## 当前版本

**1.0.37-cpp** —— 已编译：

- **方向键/十字键减速**：左右平移不再与 WASD/左摇杆同速，改为 `move_speed` × `arrow_pan_mult`（新配置项，默认 **0.5** 半速，只影响方向键与十字键）；上下俯仰 `pitch_speed` 默认 60 → **30 度/秒**
- **Ctrl/右摇杆视角精调**：此前按住 Ctrl 只放慢移动、完全不影响视角；现在按住 Ctrl（手柄为按下右摇杆 RS）时鼠标与右摇杆的视角速度一起乘 `slow_look_mult`（新配置项，默认 **0.1** 十分之一速），大幅降低精调视角时的转速
- **右摇杆视角减速**：`pad_look_speed` 默认 180 → **60 度/秒**（全行程）；UE4SS 诊断面板新增「Ctrl视角倍率」「方向键平移倍率」滑条。旧 config.ini 若显式写了 `pitch_speed`/`pad_look_speed` 需同步改小（或删行用内置默认）
- **语音静音双保险（回答「按 ID 暂停其他角色没效果吗」）**：钩子挂在游戏音频总入口 `PBZSoundFXSubsystem` 上，**所有**角色的声音（枪声/脚步/环境/各角色语音）发出的 ID 都会被记录、冻结时一起暂停——不是只管对话。这轮再把覆盖面封死：① 加挂 Wwise 集成层入口（`UAkGameplayStatics::PostEvent`/`PostEventAtLocation`、`UAkComponent::PostAkEvent` 等类级钩子），绕过子系统的声音也截获；② ID 名单上限 256→1024（开局音乐这类长命声音不脱名单）；③ 冻结时**两层同开**——ID 真暂停 + 组件音量归零，解冻时恢复，静音不再依赖任何单一通道
- **字幕压制扩到对话 UI 本体（1.0.34 实机反馈）**：设置开关 `bEnableSubtitles` 已验证关闭（回读=关）、字幕子控件也已 Collapsed 但文本仍显示——文本画在对话 UI 本体 `WBP_FlowDialog_C` 上。压制范围扩到对话框家族（类名含 FlowDialog），并把「可见 UserWidget 清单」作为一次性诊断打进日志（若仍显示可从清单直接指认真身）。每 10 次重申做一次全量扫描，对话中途新出现的控件类也会被收编
- **修复：真暂停按真实签名填参（1.0.34 实机日志确认）**：`PBZPausePlayingID` / `PBZResumePlayingID` 的真实签名是 **`(int32 PlayingID, float FadeTime)`**——上一版只认单个 int 参数被全数拦下（截获 50 执行 0），冻结只走了音量兜底（变小声）。现在按名填参：PlayingID=截获 ID、FadeTime=0（立即暂停/立即恢复）。这次调用必然能发出去
- **修复：对话语音真暂停（1.0.32 实机 bug）**：1.0.32 的组件音量归零实测只做到「变小声」（音量语义没吃透），且句子在冻结期间无声播完后，退出时只能从下一句出声。1.0.33 钩住 `PBZSoundFXSubsystem` 的 11 个 Post 函数截获 **PlayingID**，冻结时调用游戏自有的 **`PBZPausePlayingID`**（Wwise 原生暂停，真静音），解冻 **`PBZResumePlayingID`**——**当前句从停住的位置原样续播**。没截获到 ID 时回落 1.0.32 的音量兜底
- **修复：字幕退出后永久消失（1.0.30 实机 bug）**：`PBZSettingsShared` 上只有 `SetSubtitlesEnabled` 函数、没有 `bSubtitlesEnabled` 属性（属性在引擎 GlobalUserParam 上），旧代码只在属性读成功时记录原值 → 开关被翻转但退出还原分支整个被跳过。现在原值走 `AreSubtitlesEnabled` getter（属性读不到也能记），还原条件改为「只要动过开关」，还原失败会打 Warn 不再静默；字幕控件收集扩到游戏自制控件 `GAA_Subtitles_C`（场上没有 USubtitleDisplay，诊断日志列出的）。**曾被 1.0.30 卡掉字幕的存档需要在游戏设置里手动把字幕开回来一次**
- **对话语音压住的第三轮（游戏自有接口）**：真机普查在 `PBZSoundFXSubsystem` 上发现 `PBZStopAkComponent`/`PBZStopAllSoundsOnActor`/`PBZStopPlayingID`/`PBZPausePlayingID`/`PBZResumePlayingID`/`FadeOutAllSound`/`FadeInAllSound` 等游戏自己的音频控制函数（1.0.32 起静音走组件音量归零，可恢复；这些接口为后续按播放 ID 暂停留门）
- **隐藏对话字幕**：进入摄影模式自动隐藏对话字幕（`hide_subtitles` 默认 `true`）。双路径：游戏字幕设置 `bSubtitlesEnabled` 关掉管住新字幕，字幕显示控件（GameSubtitles 插件 `USubtitleDisplay`）Collapsed 管住已显示的，退出全部还原；失败每秒重试
- **音频普查二轮**：1.0.28 真机普查确认游戏有完整对话系统（`DialogueGraphNew_C`、`STTask_PlayDialogue_C`、函数库 `PlayAudio_C`/`StopAudio_C`）与多个音频子系统（`PBZMRQWwiseAudioSubsystem`/`PBZPersistentAudioSubsystem`/`MapAudioSubsystem` 等），但函数转储被上限挡住没轮到它们。本轮改为按优先级转储（子系统 > 函数库 > 任务/对话图 > 组件），并新增 `AkGameplayStatics`（Wwise BP 入口）全量函数表
- **隐藏敌人血条**：进入摄影模式自动隐藏敌人（怪物）头顶血条（`hide_enemy_health_bars` 默认 `true`）。游戏侧总开关是设置项 `MonsterHealthBarMode`（仅锁定目标/总是显示/满血隐藏）——摄影模式没有锁定目标，切到"仅锁定目标"即全部隐藏，退出还原原模式；失败每秒重试
- **剧情播放器暂停真正生效**：1.0.24 真机日志发现「命中 1,执行 0」——Sequencer 的 `Pause`/`Play` 是无参函数，参数块按 0 字节处理导致调用直接放弃。已修复（缓冲区至少留 1 字节），剧情/过场与场景里的后台播放器现在能真正暂停与恢复
- **Wwise 音频（对话语音）处理**：真机确认游戏用 Wwise（UE 混音管不到）。`stop_wwise_on_freeze`（1.0.30 起默认 `true`）：冻结时停止所有 Wwise 组件的声音——优先调用游戏自己的 `PBZSoundFXSubsystem.PBZStopAkComponent`，失败回落组件 `Stop`；停止不可恢复，解冻后由游戏自行重播（当前对话句被停掉，解冻后 Flow 图会继续下一句）；不想静音就在 `config.ini` 关掉
- **剧情中冻结修复（1.0.24）**：剧情（过场）时开启摄影模式，世界暂停拦不住 Sequencer 剧情播放器——画面像冻住了，动作和声音还在继续。现在冻结时会一并暂停剧情播放器（解冻/退出恢复，`freeze_sequences` 默认 `true`）；游戏暂停被拒绝时自动回退时间缩放；冻结状态每秒回读校验，被游戏改写就重新冻结（连续 3 次压不住则放弃并告警）
- **FOV 按住连发**：手柄十字键上/下、键盘 FOV 键（默认 `+` / `-`）单击仍是一格；按住约 0.35 秒后按 0.08 秒间隔连发，持续变焦不用连点
- **手柄滚转**：`RB` / `LB` 滚转相机（RB 顺时针 / LB 逆时针，与键盘 Q/E 同向）；原 LB/RB 变速移到摇杆按下（L3 加速 / R3 减速；两杆同按仍是进出模式组合键，不触发变速）
- **相机滚转**：`Q` / `E` 改为滚转（E 顺时针 / Q 逆时针，`roll_speed` 默认 60 度/秒，Shift/Ctrl 变速同样生效）；鼠标左键上移 / 右键下移保持不变，手柄 RT/LT 升降不变，`R` 重置含滚转角
- **图形菜单代码彻底移除**：游戏内无任何菜单界面，纯快捷键操作（F2 进入/退出、F 冻结、R 重置、滚轮 FOV、WASD/鼠标左右键/Shift/Ctrl 移动，键位均可配）；仅保留 UE4SS 控制台里的诊断面板
- 进入摄影模式自动隐藏**交互提示**（场景互物体旁的“E”键图标，`hide_interact_prompt` 默认 `true`，可在 `config.ini` 关闭），退出还原
- 超宽屏（21:9 / 32:9）支持：进入摄影模式自动解除 16:9 画幅约束，消除两侧黑边（`ultrawide_unlock` 默认 `true`，可在 `config.ini` 关闭），退出时还原
- DualSense / DualShock4 原生手柄支持（DInput8 轮询后端，L3+R3 进摄影模式）
- 进入摄影模式自动暂停时间（`auto_pause_time` 默认 `true`）、自动隐藏游戏 HUD
- 核心层 686 项单元测试全部通过（含方向键/十字键折算与 Ctrl/RS 视角精调用例）
- 产物：`PBZPhotoModeCpp_1.0.37-cpp.zip`

## 安装

需要《影之刃零》专用 UE4SS（[KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 的 `phantombladezero` 发行包，开发验证版本 `v3.0.1-1183-gcdbc0b66`）。

从 [Releases](https://github.com/lin-414/PBZPhotoMode/releases) 下载 `PBZPhotoModeCpp_1.0.36-cpp.zip`，解压到游戏根目录，形成：

```
<游戏目录>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\PBZPhotoModeCpp.dll
├─ config.ini
└─ enabled.txt
```

不需要改 `mods.txt`（`enabled.txt` 即启用）。详细说明见 [PBZPhotoModeCpp/docs/README.md](PBZPhotoModeCpp/docs/README.md)。

> **图形菜单已移除（v1.8.0）**：《影之刃零》Shipping 定制引擎对本模组是彻底黑盒——引擎内图形菜单路线（IoStore 资产 pak / puerts JS 层）全部实证判死（完整取证链见 [PROGRESS](PBZPhotoModeCpp/PROGRESS.md)）。模组为**纯快捷键操作**：F2 进入/退出、F 冻结、R 重置、滚轮调 FOV、WASD/鼠标左右键/Shift/Ctrl 移动、Q/E 滚转，全部在 config.ini 可改。

## 从源码构建

核心层单元测试不需要游戏与 UE4SS：

```bat
cd PBZPhotoModeCpp
tools\build_tests.bat          :: 521 项断言全绿
```

构建 DLL 需要一份 [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) 源码（含 Epic 授权的 `UEPseudo` 子模块）：

```bat
cd PBZPhotoModeCpp
tools\build_dll.bat D:\path\to\RE-UE4SS
```

完整依赖、构建步骤与踩坑记录见 [PBZPhotoModeCpp/docs/BUILD.md](PBZPhotoModeCpp/docs/BUILD.md)。

## 仓库结构

```
.
├─ PBZPhotoModeCpp/         # UE4SS C++ Mod
│  ├─ src/core/             # 纯逻辑层（配置/输入/状态机，含单元测试）
│  ├─ src/engine/           # UE4SS 引擎适配层
│  ├─ src/mod_app.cpp       # CppUserModBase 入口
│  ├─ tests/                # 521 项核心层断言
│  ├─ tools/                # 构建 / 打包脚本
│  └─ docs/                 # 构建、安装、核对清单
├─ docs/
│  └─ NEXUS_DESCRIPTION.md  # Nexus Mods 主页文案
├─ README.md
└─ .gitignore
```

## 版本记录

- **1.0.37**（2026-11-01）：输入手感减速——方向键/十字键平移乘 `arrow_pan_mult`（默认 0.5）、俯仰 `pitch_speed` 60→30；按住 Ctrl（手柄 RS 按下）时鼠标/右摇杆视角乘 `slow_look_mult`（默认 0.1）精调；`pad_look_speed` 180→60；核心层 668 → 686 项全绿
- **1.0.36**（2026-11-01）：静音双保险——加挂 Wwise 集成层 Post 钩子（AkGameplayStatics/AkComponent），ID 名单上限 1024，冻结时 ID 真暂停 + 组件音量归零两层同开，解冻双恢复。
- **1.0.35**（2026-11-01）：实机签名确认——`PBZPausePlayingID`/`PBZResumePlayingID` = `(int32 PlayingID, float FadeTime)`；按真实签名填参（FadeTime=0 立即生效），真暂停调用必然发出。
- **1.0.34**（2026-11-01）：实机修复——真暂停调用全被签名校验拦下（截获 37 执行 0）；改为自适应填参（数值=PlayingID、对象=WorldContext），执行 0 自动回落音量兜底，挂钩时打印两个暂停接口的真实参数签名。
- **1.0.33**（2026-11-01）：实机修复——音量归零只「变小声」且句子冻结期间无声播完，退出只能从下一句出声；钩住 Post 系列函数截获 PlayingID，冻结/解冻改走游戏自有 `PBZPausePlayingID`/`PBZResumePlayingID` 真暂停（原位置续播），无 ID 回落音量兜底。
- **1.0.32**（2026-10-31）：实机修复——`Stop` 会把当前句杀掉且解冻后静音不续；改为组件 `SetOutputBusVolume` 归零/恢复（可恢复静音，句子回调照常，解冻当前句从原处继续）。核心层测试 666 → 668 项。
- **1.0.30**（2026-10-31）：真机发现对话推进已被冻结（一句话说完不触发下一句），只剩当前句在响——`PBZSoundFXSubsystem` 上找到游戏自有接口 `PBZStopAkComponent` 等；冻结静音改走它（签名校验，失败回落 Stop），`stop_wwise_on_freeze` 默认改 true。核心层测试 649 → 666 项。
- **1.0.29**（2026-10-31）：普查二轮——按优先级转储子系统/函数库/任务/对话图的控制函数，新增 `AkGameplayStatics` 全量函数表；真机已确认对话系统形态（DialogueGraph/STTask/PlayAudio/StopAudio 函数库）。
- **1.0.28**（2026-10-31）：音频对象普查——真机确认 AkComponent=20、可用函数=Stop（组件级停止够不着 Wwise 直接 post 的对话语音）；首次冻结时全对象扫描 Ak/Audio/Sound/Voice/Dialog 相关类并列出疑似管理器的控制函数，为下一版可恢复音频暂停定目标。核心层测试 597 → 649 项。
- **1.0.27**（2026-10-31）：左右视角平移——键盘方向键左/右 + 手柄十字键左/右并入横移轴；键盘俯仰改方向键上/下；手柄 FOV 改 A/B（pad_fov_up=A / pad_fov_down=B）。
- **1.0.26**（2026-10-31）：俯仰调节——十字键上/下按住持续转（`pitch_speed` 默认 60 度/秒，±`pitch_limit` 夹紧，`invert_y` 不作用于按键）。
- **1.0.25**（2026-10-31）：真机日志修复——Sequencer 无参函数 `Pause`/`Play` 因空参数块调不出去（「命中 1,执行 0」），修复后剧情/后台播放器真正暂停恢复；新增 `stop_wwise_on_freeze`（默认关）一次性静音 Wwise 组件（对话语音不可恢复式停止），并加 Wwise 组件诊断。核心层测试 565 → 580 项。
- **1.0.24**（2026-10-31）：剧情冻结修复——冻结时同时暂停 Sequencer 剧情播放器（`freeze_sequences`，解冻/退出恢复）；暂停失败自动回退时间缩放；冻结期每秒回读校验，被改写自动重压（3 次失守放弃并告警）。核心层测试 521 → 565 项。
- **1.0.23**（2026-10-31）：FOV 按住连发——十字键上/下与键盘 FOV 键单击一格不变，按住约 0.35 秒后按 0.08 秒间隔连发，松开即停；手柄帮助文案同步修正为 L3/R3 变速。
- **1.0.22**（2026-10-31）：手柄滚转——`RB` / `LB` 滚转相机（与 Q/E 同向），原 LB/RB 变速移到摇杆按下（L3 加速 / R3 减速，两杆同按仍是进出组合）。
- **1.0.21**（2026-10-31）：相机滚转——`Q` / `E` 从升降改为滚转（E 顺时针 / Q 逆时针），新增 `roll_speed` 配置；鼠标左/右键与手柄 RT/LT 升降保持不变。
- **1.0.20**（2026-10-31，版本号回退）：图形菜单代码彻底移除——删除 DX12 ImGui 覆盖层、UMG 交互菜单、WBP 资产菜单、puerts 探针与全部菜单配置/键位，纯快捷键定格。1.1.0–1.8.1 为开发期图形菜单迭代，随路线判死整体作废，版本号对外回退到 1.0.x 线（见 PROGRESS）。
- **v1.6.0**（2026-10-31）：进入摄影模式隐藏交互提示（场景互物体旁的“E”键图标；游戏设置开关 + 控件组件双路径，退出还原）。
- **v1.5.0**（2026-10-31）：超宽屏支持——进入摄影模式自动解除 16:9 画幅锁（`ultrawide_unlock`），21:9/32:9 两侧黑边消除，退出还原。
- **v1.4.0**（2026-10-31）：DualSense/DualShock4 原生手柄支持（DInput8 后端，L3+R3 进摄影模式）。
- **v1.0.14**（2026-10-30）：进入摄影模式自动暂停时间（`auto_pause_time`，默认开启，可在 `config.ini` 关闭）。
- **v1.0.13**（2026-10-30）：移除实验性的截图功能，截图改用 NVIDIA App / Steam 等系统工具。
- **v1.0.12**（2026-10-30）：进入摄影模式自动隐藏游戏 HUD（UMG 根控件），退出恢复；`hide_game_hud` 默认改为 `true`。
- **v1.0.11**（2026-10-30）：F 冻结改为游戏暂停（`pause`），真机验证：暂停期间相机仍可移动，解冻正常恢复。
- 逐版本修复记录（含每次真机日志的根因分析）见 [PBZPhotoModeCpp/PROGRESS.md](PBZPhotoModeCpp/PROGRESS.md)。

## 致谢

- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) — Unreal Engine 脚本系统与 C++ Mod 加载器
- [KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) — 影之刃零专用分支

## 说明

截图请使用 NVIDIA App / GeForce Experience（默认 `Alt + F1`）或 Steam（`F12`）等系统工具。

本项目仅供单机游戏摄影与学习交流使用；使用 Mod 的风险由使用者自行承担。
