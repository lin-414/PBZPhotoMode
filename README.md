# PBZ Photo Mode — 影之刃零摄影模式 Mod（C++）

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS C++ Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV；`F9` 无 UI 截图**在本作不可用**（Shipping 导出表无 `FScreenshotRequest::RequestScreenshot`），摄影模式内 UI 已隐藏，请用 NVIDIA `Alt+F1` / Steam `F12` / Xbox Game Bar；
> 进入时自动暂停时间，`F` 可随时冻结/恢复。纯快捷键操作；可选安装 PBZFM（F8 全量菜单，含键位捕获）或 SLEEP 的 Mods Menu 149 + UE4SS Bridge（游戏设置里 68 项：41 个基础设置 + 27 个键位 choice），见下方「双游戏内菜单」。

> **实现边界**：当前注入方式是 `UObjectGlobals::RegisterHook` 的 UFunction 级钩子 + Engine Tick 回调 + `GetProcAddress` 导出解析；摄影模式的引擎暂停不冻结 QPC/GetTickCount（1.0.66 起移除该路径，避免移动视角卡顿）；仅 **F7 画面定格**（挂起 PBZ.exe/MRTCEngine 等游戏引擎线程）使用 PBZ Pause 式的四时间 API IAT 补偿，见 [docs/README.md](PBZPhotoModeCpp/docs/README.md) hotfix29。

## 功能

- **自由相机**：`F2` 进入 / 退出
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；方向键 左/右转视角（`yaw_speed`）、上/下俯仰（`pitch_speed`）；升降默认左键上移 / 右键下移（`key_vertical_up` / `key_vertical_down` 可改）；`Q` / `E` 滚转（E 顺时针 / Q 逆时针，`key_roll_left` / `key_roll_right` 可改）；手柄十字键同方向键、`RB` / `LB` 滚转；`Z` 滚转归零（只回正滚转、构图不动，手柄 `LB+RB` 同按）
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **景深（默认关闭，取证开关保留）**：2.1 轮曾用游戏过场渲染档 + tagged cvar 门控复活，但 2026-11-07 实机三种门控变体全部崩溃——本作 shipping 管线里 DOF pass 是空壳路径，`dof_enabled=false` 为稳定态；对焦距离/光圈/传感器/步进参数与 `,` `.` `[` `]` 热键保留，供游戏更新后复查。开启可能导致崩溃，普通玩家请保持关闭。
- **接管当前相机（UUU 式）**：`[camera] takeover_current_camera` 默认 `true`——不 spawn、不切 ViewTarget，每帧在 `GameViewportClient::Tick` 之后覆写 `CameraCache.POV`；进入失败或连续 15 帧回读不一致会自动回退独立相机；设 `false` 可固定走独立相机
- **无 UI 截图（本作不可用）**：F9 机制已实现（`GetProcAddress` 解析 `FScreenshotRequest::RequestScreenshot(bShowUI=false)`，带前台窗口焦点门控与 2 秒落盘观测），但**本作实测 Shipping 导出表没有该符号**，按 F9 只会输出「截图能力不可用」。摄影模式内 UI 已由本 Mod 隐藏，请直接用 NVIDIA `Alt+F1` / Steam `F12` / Xbox Game Bar。若有其它 UE 游戏导出该符号，F9 机制可用
- **CineCamera 焦距联动**：包内有 `CineCameraActor` / `CineCameraComponent` / `CurrentFocalLength` / `Filmback`，改 FOV 同步真实焦距；`UCineCaptureComponent2D`（CineCameraSceneCapture 插件）未打进 Shipping，因此没有高品质无 UI 出图通道
- **F7 画面定格（内置冻结渲染帧）**：正常画质预览构图 → `F7` 挂起游戏引擎线程（渲染/音频/逻辑；其他覆盖层/本模组输入钩子线程保留），屏幕停在最后一帧 → 从容截图 → 再按 `F7` 恢复。参考 PBZ Pause：`NtGetNextThread` 枚举 + `HeapLock` 防死锁、Wwise `Suspend/Wakeup` 协调、四个时间 API 的 IAT 补偿（恢复时扣掉定格时长，动画/物理/计时器不跳变）；安全阀默认 60 秒。定格期间相机不响应输入（预期取舍）。**定格期间 NVIDIA `Alt+F1`、Steam `F12` 这类依赖游戏出帧的截屏快捷键会静默失效**：N 卡捕获跑在游戏进程内、必须等到下一次 `Present` 才能抓帧，而渲染线程已被挂起、永远没有新帧（快捷键本身没失效，是在等一帧不会来的画面）。定格截图请用系统级捕获 `Win+PrintScreen`（存 `图片\屏幕截图`）或 Xbox Game Bar `Win+Alt+PrtScr`——走 DWM 合成器、不依赖游戏 Present，定格期间可用。根因存档与已评估的备选设计见 [docs/CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。
- **时间冻结**：进入摄影模式自动暂停时间（默认开启，可在 `PBZPhotoModeCpp.settings.json` 关闭）；`F` 随时冻结 / 恢复（默认使用游戏暂停，真正冻结角色、敌人与特效）；剧情/过场播放器一并暂停（解冻恢复），冻结状态每秒自检、被游戏改写时自动重压；Wwise 音频在有逐 PlayingID 时逐语音真暂停（`PBZPausePlayingID` / `PBZResumePlayingID`，保住当前句位置）并跳过整引擎 Suspend，只有没截获到 ID 时才用整引擎 `Suspend(true,false)` 作静音兜底（当前 PBZ 从主程序静态导出解析，`wwise_engine_suspend` 默认开启）；对话/过场中的对话 Flow 可选择在冻结时停 Tick（`freeze_dialogue_flow` 默认开启，仅 `hide_subtitles=true` 时生效，保留相机）
- **超宽屏支持**：进入摄影模式自动解除 16:9 画幅约束（游戏官方支持 21:9 / 32:9 超宽屏），两侧黑边消除；暂停/处决演出把画幅重新锁成 16:9 时会在渲染前每帧压回，退出时按进入前状态还原
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **隐藏 HUD**：进入摄影模式自动隐藏游戏 HUD（血条/快捷栏/任务提示等），退出时恢复原可见性。粗粒度走 `SetHideUIForPV` + `AHUD.bShowHUD` + UMG 根控件折叠；字幕/世界空间交互提示/锁定标记/对话 Flow 仍细粒度处理（有明确理由），存活判定用 `is_ok()`，避免扫描漏项导致控件永远停在 `Collapsed`
- **隐藏交互提示**：进入摄影模式自动隐藏场景互物体旁的“E”键图标（世界空间 UMG），退出时恢复
- **隐藏敌人血条**：进入摄影模式自动隐藏敌人（怪物）头顶血条（切到游戏自带"仅锁定目标"血条显示模式，退出还原原模式）
- **隐藏锁定标记**：进入摄影模式自动隐藏锁定敌人时其身上出现的白点标记——1.0.68 起接游戏自己的锁定系统（`TargetingComponent`：藏 `LockWidget`/副目标指示器、关指示器开关、清锁定目标），锁定目标的血条也一并消失，退出成对恢复（`hide_lockon_marker` 可关）
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `PBZPhotoModeCpp.settings.json` 里
- **画面效果（JSON 配置）**：暗角 / 饱和度 / 对比度 / 泛光 / 冷暖 / 调色·进阶三件 / 景深参数都保存在 `settings.json`；每次进入和退出摄影模式都会恢复默认参数（会话级），中性值即不覆盖游戏原调色
- **双游戏内菜单（均可选）**：**PBZ-Framework-Menu（PBZFM，F8）**提供全量设置页（含键位捕获，支持鼠标键；画面效果/调色/风格预设/状态页/语言切换）；**SLEEP「Mods Menu 149」+ UE4SS Bridge** 提供游戏设置里的 68 项（41 个基础设置 + 末尾 27 个键位 choice，每行 64 个常用键名）。两者都读写同一份 `PBZPhotoModeCpp.settings.json`，互相同步；只装一个或都不装都不影响快捷键模式。Mods Menu 走官方 UE4SS Bridge 路线（descriptor + `config.json` + `events-*.txt`），不修改菜单本体；[look]/调色为会话级参数，进入/退出摄影模式都会恢复默认。景深详细参数与画面效果/调色不在 Mods Menu：摄影模式内打不开游戏设置菜单，这些取景参数请用 PBZFM（F8）或 `settings.json`

## 安装

需要《影之刃零》专用 UE4SS（[KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 的 `phantombladezero` 发行包，开发验证版本 `v3.0.1-1183-gcdbc0b66`）。

从 [Releases](https://github.com/lin-414/PBZPhotoMode/releases) 下载最新的 `PBZPhotoModeCpp_*.zip` 发行包，解压到游戏根目录，形成：

```
<游戏目录>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\PBZPhotoModeCpp.dll
├─ PBZPhotoModeCpp.modsmenu.json（随包，Mods Menu 描述符）
├─ languages\（随包，UE4SS 控制台词典）
├─ PBZPhotoModeCpp.settings.json（首次运行生成）
└─ enabled.txt
```

不需要改 `mods.txt`（`enabled.txt` 即启用）。详细说明见 [PBZPhotoModeCpp/docs/README.md](PBZPhotoModeCpp/docs/README.md)。

> 可选游戏内菜单：安装 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu) 后按 F8 打开全量设置（含键位捕获）；或安装 SLEEP 的 [Mods Menu 149](https://www.nexusmods.com/phantombladezero/mods/149) + UE4SS Bridge 后在游戏设置里调 68 项（41 基础设置 + 27 键位）。两个菜单都不随本包分发。

> **游戏内菜单（可选）**：PBZFM（PBZ-Framework-Menu，F8 覆盖窗）提供全量设置页与键位捕获；SLEEP「Mods Menu 149」+ UE4SS Bridge 提供游戏设置里的 68 项（41 个基础设置 + 末尾 27 个键位 choice）；两者都读写 `PBZPhotoModeCpp.settings.json`，都不装时仍是纯快捷键模式（F2 进入/退出、F 冻结、R 重置、滚轮调 FOV、WASD/鼠标左右键/Shift/Ctrl 移动、Q/E 滚转）。原生引擎内 UMG/pak 图形菜单路线仍判死，PBZFM 是进程外覆盖窗，不是引擎内菜单。

## 从源码构建

核心层单元测试不需要游戏与 UE4SS：

```bat
cd PBZPhotoModeCpp
tools\build_tests.bat          :: 核心层测试全绿（当前断言总数见 docs/test_count.txt）
```

> 这里的「项」是运行时断言次数：`tests/` 下的 `CHECK*` 宏经表驱动/循环放大后的实际次数写在 `docs/test_count.txt`（单一来源），不是独立测试用例数。

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
│  ├─ tests/                # 核心层运行时断言（项数见 docs/test_count.txt）
│  ├─ tools/                # 构建 / 打包脚本
│  └─ docs/                 # 构建、安装、核对清单
├─ docs/
│  └─ NEXUS_DESCRIPTION.md  # Nexus Mods 主页文案
├─ README.md
├─ LICENSE               # GPL-3.0 许可证
└─ .gitignore
```

## 致谢

- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) — Unreal Engine 脚本系统与 C++ Mod 加载器
- [KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) — 影之刃零专用分支

## 说明

摄影模式内按 `F9` 不会出图：**本作实测 Shipping 导出表没有 `FScreenshotRequest::RequestScreenshot`**，日志会输出「截图能力不可用」。请使用 NVIDIA App / GeForce Experience（默认 `Alt + F1`）、Steam（`F12`）或 Xbox Game Bar（`Win + Alt + Pr`）等系统工具；默认配置下摄影模式内 UI 已由本 Mod 隐藏，外部截图即等效无 UI。F9 机制本身保留，对导出该符号的其它 UE 游戏可用。

本项目仅供单机游戏摄影与学习交流使用；使用 Mod 的风险由使用者自行承担。

## 许可证

本项目以 [GNU General Public License v3.0](LICENSE)（GPL-3.0）发布；它是自由软件，你可以依据该许可证重新分发和修改它。本项目不作任何担保，详见 [LICENSE](LICENSE) 全文。
