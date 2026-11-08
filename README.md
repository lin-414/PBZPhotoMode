# PBZ Photo Mode — 影之刃零摄影模式 Mod（C++）

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS C++ Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV；`F9` 无 UI 截图**在本作不可用**（Shipping 导出表无 `FScreenshotRequest::RequestScreenshot`），摄影模式内 UI 已隐藏，请用 NVIDIA `Alt+F1` / Steam `F12` / Xbox Game Bar；
> 进入时自动暂停时间，`F` 可随时冻结/恢复。纯快捷键操作，游戏内无菜单界面。

> **实现边界**：当前注入方式是 `UObjectGlobals::RegisterHook` 的 UFunction 级钩子 + Engine Tick 回调 + `GetProcAddress` 导出解析；摄影模式的引擎暂停不冻结 QPC/GetTickCount（1.0.66 起移除该路径，避免移动视角卡顿）；仅 **F7 画面定格**（挂起 PBZ.exe/MRTCEngine 等游戏引擎线程）使用 PBZ Pause 式的四时间 API IAT 补偿，见 [docs/README.md](PBZPhotoModeCpp/docs/README.md) hotfix29。

## 功能

- **自由相机**：`F2` 进入 / 退出
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；方向键 左/右转视角（`yaw_speed`）、上/下俯仰（`pitch_speed`）；升降默认左键上移 / 右键下移（`key_vertical_up` / `key_vertical_down` 可改）；`Q` / `E` 滚转（E 顺时针 / Q 逆时针，`key_roll_left` / `key_roll_right` 可改）；手柄十字键同方向键、`RB` / `LB` 滚转；`Z` 滚转归零（只回正滚转、构图不动，手柄 `LB+RB` 同按）
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **景深：本作不支持，功能已整个移除（hotfix32）**。引擎景深的四条通道（相机 POV / 自建体积 / 关卡体积 / `ViewTarget.POV`）全部实测「写入成功、回读正确、画面零变化」，89px 参数也不糊；`ShowFlags.DepthOfField` 在反射层拿不到；径向模糊替代方案也卡在挂载。所以菜单页、`[dof]` 配置段、对焦/光圈按键（`PAGE_UP`/`PAGE_DOWN`/`[`/`]`）已全部删除，旧配置里的残留键会合并成一条提示后忽略。想给截图加虚化请用 NVIDIA Ansel / ReShade 等外部后期。
- **接管当前相机（UUU 式）**：`[camera] takeover_current_camera` 默认 `true`——不 spawn、不切 ViewTarget，每帧在 `GameViewportClient::Tick` 之后覆写 `CameraCache.POV`；进入失败或连续 15 帧回读不一致会自动回退独立相机；设 `false` 可固定走独立相机
- **无 UI 截图（本作不可用）**：F9 机制已实现（`GetProcAddress` 解析 `FScreenshotRequest::RequestScreenshot(bShowUI=false)`，带前台窗口焦点门控与 2 秒落盘观测），但**本作实测 Shipping 导出表没有该符号**，按 F9 只会输出「截图能力不可用」。摄影模式内 UI 已由本 Mod 隐藏，请直接用 NVIDIA `Alt+F1` / Steam `F12` / Xbox Game Bar。若有其它 UE 游戏导出该符号，F9 机制可用
- **CineCamera 焦距联动**：包内有 `CineCameraActor` / `CineCameraComponent` / `CurrentFocalLength` / `Filmback`，改 FOV 同步真实焦距；`UCineCaptureComponent2D`（CineCameraSceneCapture 插件）未打进 Shipping，因此没有高品质无 UI 出图通道
- **F7 画面定格（内置冻结渲染帧）**：正常画质预览构图 → `F7` 挂起游戏引擎线程（渲染/音频/逻辑；PBZFM/本模组输入钩子线程保留），屏幕停在最后一帧 → 从容截图 → 再按 `F7` 恢复。参考 PBZ Pause：`NtGetNextThread` 枚举 + `HeapLock` 防死锁、Wwise `Suspend/Wakeup` 协调、四个时间 API 的 IAT 补偿（恢复时扣掉定格时长，动画/物理/计时器不跳变）；安全阀默认 60 秒。定格期间相机不响应输入（预期取舍）。**定格期间 NVIDIA `Alt+F1`、Steam `F12` 这类依赖游戏出帧的截屏快捷键会静默失效**：N 卡捕获跑在游戏进程内、必须等到下一次 `Present` 才能抓帧，而渲染线程已被挂起、永远没有新帧（快捷键本身没失效，是在等一帧不会来的画面）。定格截图请用系统级捕获 `Win+PrintScreen`（存 `图片\屏幕截图`）或 Xbox Game Bar `Win+Alt+PrtScr`——走 DWM 合成器、不依赖游戏 Present，定格期间可用。根因存档与已评估的备选设计见 [docs/CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。
- **时间冻结**：进入摄影模式自动暂停时间（默认开启，可在 `config.ini` 关闭）；`F` 随时冻结 / 恢复（默认使用游戏暂停，真正冻结角色、敌人与特效）；剧情/过场播放器一并暂停（解冻恢复），冻结状态每秒自检、被游戏改写时自动重压；Wwise 音频在有逐 PlayingID 时逐语音真暂停（`PBZPausePlayingID` / `PBZResumePlayingID`，保住当前句位置）并跳过整引擎 Suspend，只有没截获到 ID 时才用整引擎 `Suspend(true,false)` 作静音兜底（当前 PBZ 从主程序静态导出解析，`wwise_engine_suspend` 默认开启）；对话/过场中的对话 Flow 可选择在冻结时停 Tick（`freeze_dialogue_flow` 默认开启，仅 `hide_subtitles=true` 时生效，保留相机）
- **超宽屏支持**：进入摄影模式自动解除 16:9 画幅约束（游戏官方支持 21:9 / 32:9 超宽屏），两侧黑边消除；暂停/处决演出把画幅重新锁成 16:9 时会在渲染前每帧压回，退出时按进入前状态还原
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **隐藏 HUD**：进入摄影模式自动隐藏游戏 HUD（血条/快捷栏/任务提示等），退出时恢复原可见性。粗粒度走 `SetHideUIForPV` + `AHUD.bShowHUD` + UMG 根控件折叠；字幕/世界空间交互提示/锁定标记/对话 Flow 仍细粒度处理（有明确理由），存活判定用 `is_ok()`，避免扫描漏项导致控件永远停在 `Collapsed`
- **隐藏交互提示**：进入摄影模式自动隐藏场景互物体旁的“E”键图标（世界空间 UMG），退出时恢复
- **隐藏敌人血条**：进入摄影模式自动隐藏敌人（怪物）头顶血条（切到游戏自带"仅锁定目标"血条显示模式，退出还原原模式）
- **隐藏锁定标记**：进入摄影模式自动隐藏锁定敌人时其身上出现的白点标记——1.0.68 起接游戏自己的锁定系统（`TargetingComponent`：藏 `LockWidget`/副目标指示器、关指示器开关、清锁定目标），锁定目标的血条也一并消失，退出成对恢复（`hide_lockon_marker` 可关）
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `config.ini` 里
- **画面效果菜单（PBZFM）**：每个数值滑块都有独立「重置」按钮；画面效果五项（暗角 / 饱和度 / 对比度 / 泛光 / 冷暖）各有独立开关（关掉不清值，便于 A/B），且**这五项在每次打开摄影模式时恢复默认参数**；「调色·进阶」分组页另有 3 个调色滑条（亮度增益 / 伽马 / 黑位，与饱和/对比同一颜色分级通道，实机验证有效）——**中性值即不覆盖该项**（不动滑条绝不毁游戏原调色），同样每次打开恢复默认
- **风格预设保存/套用/删除**：「画面效果」组页「保存预设」把当前风格快照（9 风格字段 + 开关）存为 `<mod>/look_preset_<编号>.ini`——编号自动递增、**绝不覆盖已有预设**（删除后编号也不回收）；下拉框列出全部预设、**选中即整表套用**；「删除预设」按钮删当前选中项（两击确认防误删，5 秒不点自动复位）
- **游戏内设置菜单（可选）**：安装 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu) 后，游戏内按其呼出键（默认 `F8`）即可在菜单里改键位/开关/参数并**实时生效**（改动写回 `config.ini`）；不装则纯快捷键，零依赖

## 当前版本

> 各版本条目中的断言数字为当时快照；当前断言总数以 [docs/test_count.txt](PBZPhotoModeCpp/docs/test_count.txt) 为准（由脚本生成）。

**2.0.4-cpp** —— 已编译（2026-11-06）：

- **修复退出摄影模式后无法移动视角（输入锁残留）**：UE 的 `SetIgnoreLookInput` / `SetIgnoreMoveInput` 是**堆叠计数**（每次 +1/-1），且 `IgnoreLookInput` / `IgnoreMoveInput` 是非 UPROPERTY 的 `uint8`，反射读不到计数。进入摄影模式前若游戏自己已持有一把视角锁（暂停/过场/菜单），旧版“进入 +1、退出 -1”会把计数留在 1，退出后 `IsLookInputIgnored()` 仍为 true → 角色能走但鼠标完全转不动视角。现在进入前记录 `IsLookInputIgnored()` / `IsMoveInputIgnored()` 的布尔原状态，退出时按 `plan_ignore_lock_restore()` 选择 `Reset*`（清残留）或补回一把锁；读不到状态则 `Reset*` 兜底。核心层 **5,329 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.3-cpp** —— 已编译（2026-11-06）：

- **画面效果操控细化 + 进摄影模式恢复默认**：PBZFM 菜单所有数值滑块行尾新增独立「重置」按钮（恢复编译期默认值并写回 `config.ini`）；「画面效果」五项（暗角 / 饱和度 / 对比度 / 泛光 / 冷暖）行首新增独立开关按钮，关掉只停用该项、保留滑块值，重置同时恢复默认值与开关；`[look]` 新增 `vignette_enabled` / `saturation_enabled` / `contrast_enabled` / `bloom_enabled` / `temperature_enabled` 五个配置键。**每次进入摄影模式时五项都会恢复默认参数**（数值归中性、开关归默认开），`mod_app` 同步把默认值写回 `config.ini` 并刷新 PBZFM 菜单显示。四语言词典同步新增「重置 / 开 / 关」等文案。核心层 **5,325 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.2-cpp** —— 已编译（2026-11-06）：

- **处决/过场残留 + 超宽屏暂停修复**：① 处决动画中进入摄影模式再退出后角色模型定格、只能重启：接近零的演出时间倍率不再被当成进入前原值保存，退出/解冻时清理玩家 Pawn 的 `CustomTimeDilation`、`bPauseAnims`、`bNoSkeletonUpdate` 与 `CinematicTimeDilation` 残留；② 21:9 超宽屏暂停时被游戏重新锁成 16:9：进入时即使未锁也保存画幅原值，每帧在渲染前重申解锁，退出按进入前状态还原（过场/处决的瞬时 16:9 锁不写回）。核心层 **5,155 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.1-cpp** —— 已编译（2026-11-06）：

- **PBZFM 菜单打开期间暂停摄影模式输入**：PBZFM 覆盖菜单与游戏同进程，`foreground_is_ours()` 判不出它，菜单里的左键会被 `GetAsyncKeyState` 轮询命中 `key_vertical_up`（默认 `MOUSE_LEFT`）导致相机上升。本版每帧查询 `PBZFM_IsMenuOpen`，菜单可见时键鼠/手柄全部暂停，关闭首帧只同步物理键状态、不产生按下边沿。

**2.0.0-cpp 正式版** —— 已编译（2026-11-05）：

- **2.0 线正式发布**：hotfix1~35 的成果收进不带后缀的版本号（`PBZPM_HOTFIX 35 → 0`）。摄影模式本体（世界冻结 / 帧冻结、键位全可配、接管相机与独立相机双后端、画幅解锁、出图通道）、**画面效果五项全部实机生效**（暗角 / 饱和度 / 对比度 / 泛光 / 冷暖）、PBZFM 可选图形菜单、四语言词典。**明确不支持并已从界面移除**：引擎景深 / 手动对焦、色差、胶片颗粒（取证结论见 `core/tuning.h` 顶部）。首次进游戏会按需原子重写 `config.ini`，把旧版残留的 `[dof]` 与色差/胶片颗粒键清掉。核心层 **5,133 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix35** —— 已编译：

- **旧配置真清理**。hotfix34 删了色差/胶片颗粒，但旧 `config.ini` 里的残留键只是「提示一次然后忽略」，文件里那几行还在。本版给 `ConfigLoadResult` 加 `legacy_keys`，`reload_config()` 拿到非空时用规范格式**原子重写一次 config.ini**（`.tmp` → rename），把 `[dof]` 段、`key_focus_*`/`key_aperture_*`/`pad_focus_*`/`pad_aperture_*`、`chromatic_aberration`、`film_grain` 从文件里物理删掉，并打一行 `配置清理：已从 config.ini 删除 N 个已废弃键并重写文件（…）`。**只在真命中废弃键时才写盘**（干净配置不会每次启动白重写，有单测盯着），重写走 `config_ini_text()` 规范序列化（顺带清掉未知/拼错键）。核心层 5,124 → **5,133 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix34** —— 已编译：

- **色差与胶片颗粒整条删除**（按景深那套做法）。PBZFM「画面效果」组里两条滑条、`[look]` 段 `chromatic_aberration` / `film_grain` 两键、四语言两个标签键、`set_look()` 里的 `SceneFringeIntensity` / `FilmGrainIntensity` / `ChromaticAberrationStartOffset` 写入与下发回读字段、`log_look_gates()` 取证函数、`LookOptions` 两个字段一并移除；旧 `config.ini` 残留这两键时合并成**一条**提示后静默忽略（与 `[dof]` 一致）。取证结论改由 `core/tuning.h` 顶部注释承担。`[look]` 现在只剩 **暗角 / 饱和 / 对比 / 泛光 / 冷暖** 五项，全部实测生效。核心层 5,291 → **5,124 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix33** —— 已编译：

- **胶片颗粒判定「本作不支持」**。hotfix32 的补噪声纹理方案实测无效：纹理确实挂上了（`Good64x64TilingNoiseHighFreq`）、`FilmGrainTexelSize=1` 写了、`r.FilmGrain=1`、强度拉到 6.85，画面里一粒颗粒都没有 → 颗粒 pass 与色差 pass 同属被 `r.Tonemapper.Quality=2` 钉死的那一档。本版删掉 `attach_film_grain_texture()` 与相关状态、`FilmGrainTexture`/`FilmGrainTexelSize` 写入（证伪的东西不留），强度仍写入作参数记录，四语言菜单标签标注「本作不支持」，`config.ini.example` 注释同步。核心层 **5,291 项**测试全绿。**至此 `[look]` 七项收口：暗角 / 泛光 / 冷暖 / 饱和度 / 对比度 生效，色差 / 胶片颗粒 本作不支持。**

**2.0.0-cpp-hotfix32** —— 已编译：

- **景深功能整体移除**。菜单「景深（手动对焦）」整页、`[dof]` 配置段、对焦/光圈键位、`Intent.*_nudge` 与连发状态、`PhotoMode::apply_depth_of_field` 与全部 `dof_*` 状态、`EnginePort` 三个景深接口、引擎侧 `write_post_process_dof`（含 SEH 包装）与 `takeover_pp_*` 快照、`core/dof_math.h`、四语言 11 个标签键、1,106 项单测断言——端到端删干净，不再保留「参数记录」这种没用的入口。自建后处理体积保留（改名 `ensure_post_volume` / `post_volume_`），画面效果七项不受影响；旧 `config.ini` 的 `[dof]` 与键位残留合并成一条提示后静默忽略。DLL **-43,008 字节**，核心层 6,397 → **5,291 项**测试全绿。同版**修掉画面效果两处真 bug**：① `ColorSaturation`/`ColorContrast` 在 UE5 是 `FVector4`（成员名 **X/Y/Z/W**，不是 R/G/B/A），hotfix29~31 一直按 R/G/B/A 找 → 三个通道全读不到，饱和度/对比度**一个字节都没写进去**，现在按 X/Y/Z/W 写入并回读；② 胶片颗粒当时以为缺 `FilmGrainTexture`，于是挑噪声贴图挂上去（日志 `画面效果：胶片颗粒纹理 = …`）——**实测无效，hotfix33 已删除该机制**。色差**判定为本作不支持**（`r.SceneColorFringeQuality` 钉死在 0、优先级高于任何游戏设置级写入；console 命令在本作不执行、UE4SS 不暴露 `IConsoleManager`），菜单标签照此标注、抬档代码删除。详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix31** —— 已编译：

- **输入手感 + 景深死代码清零**。① 对焦/光圈也能**按住连发**（与 FOV 同一套状态机：按下 1 格，按住 0.35 秒后每 0.08 秒 1 格，掉帧一帧补多格）；② `Shift` 粗调 / `Ctrl` 微调（×`boost_mult` / ×`slow_mult`，与移动同一套倍率，且乘在比例步进之后）；③ 对焦**比例步进** `max(dof_focus_step, 当前距离×10%)`——1.2m 处 25cm、10m 处 1m、100m 处 10m，远距离不再按到天荒地老；④ **删除全部景深实验设施**（通道③④⑤、ShowFlags 采样、console 闸门、探针、材质普查、MPC 参数 dump，共 20 个函数/整块）：`ue4ss_port_camera_view.cpp` 3,046 → 1,926 行、头文件 825 → 679 行、DLL 小 23,040 字节；⑤ 景深反射写入套上 **SEH 兜底**，写坏内存只告警一次、不带崩游戏；⑥ **世界换代时作废接管相机与后处理快照**并回退独立相机（以前会把旧世界的值写进新地图的相机）；⑦ **画面效果四项失效修复**——色差/颗粒/饱和/对比在 hotfix30 里拖到位却没画面变化，真因是**引擎后处理档位**：本作 `r.Tonemapper.Quality=2`、`r.SceneColorFringeQuality=0`（`BaseScalability.ini` 的 `PostProcessQuality@0/@1`），这些 pass 被整段跳过，而暗角/泛光/冷暖不受档位管辖所以一直正常；本版加「写后回读」日志（`画面效果：回读 …`）、`ColorGradingIntensity` / `ChromaticAberrationStartOffset` 兜底，并用游戏自己的 `SetPostProcessingQuality(3)` + `ApplyNonResolutionSettings()` 临时抬档（**以 cvar 回读判定成败**，抬不动立刻写回，退出摄影模式还原）。核心层 6,365 → **6,397 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix30** —— 已编译：

- **景深收口，删除径向模糊实验**。hotfix28 的日志给出最后一格：40 个后处理体积的 `WeightedBlendables` 一个都不带条目，自建槽位时连 `FWeightedBlendables.Array` 的地址都取不到 → 材质根本挂不上，这条替代路线在挂载那一步就断了（不是「挂上了不够糊」）。本版把 `attach_radial_blur_material` / `detach_radial_blur_material` / `scan_weighted_blendables` 及全部状态与日志整体删除（engine 层 -233 行、头文件 -21 行），不留死代码；`config.ini.example` 的 `[dof]` 段注释改写为「本作不渲染引擎景深、以下仅为记录」，不再宣称能看出虚化。`[look]` 画面效果不受影响。核心层 **6,365 项**测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix29** —— 已编译：

- **画面效果全套**：暗角 / 色差 / 胶片颗粒 / 饱和度 / 对比度 / 泛光 / 冷暖 7 项全部接进 `[look]` 段与 PBZFM「画面效果」菜单，走同一套已被证明有效的自建后处理体积通道（每项一对 `bOverride_*` + 值）。中性值即关闭（暗角/色差/颗粒/泛光/冷暖=0，饱和/对比=1），关闭时把 `bOverride_` 置回 false 把该项交还游戏。`ColorSaturation`/`ColorContrast` 写 FVector4，冷暖写 `SceneColorTint`；字段缺失只告警一次并列名。核心层测试 5,816 → **6,365 项**全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix28** —— 已编译：

- **自建 blendable 槽位没走到头 → 每一步加日志**。hotfix27 材质找到了，但自建槽位那段静默返回；本版把「取 `WeightedBlendables` → 取 `Array` → 取 `Inner` → 取 `Object/Weight` → 分配 → 写入」每一步都拆开打日志，失败时输出具体是哪一环，据此确定 5.7 里这个结构的真实字段名/类型。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix28** —— 已编译：

- **新增屏幕暗角（Vignette）**。景深走不通，但同一轮探针第 2 段证明「写进自建 `PostProcessVolume` 的 `VignetteIntensity`」是本作 gameplay 视图里**唯一实测能出画面**的后处理通道。本版做成正式功能：新增 `[look]` 段 `vignette = 0`（0~4，0=关闭），PBZFM 菜单新增「画面效果（暗角）」滑条，拖完即时生效；走自建无界高优先级体积（与景深同一个），关闭时把 `bOverride_VignetteIntensity` 置回 false，退出摄影模式恢复。核心层测试 5,728 → **5,816 项**全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix27** —— 已编译：

- **借不到槽位 → 用引擎分配器造一个**。hotfix26 找到材质 `M_FX_PP_RadialBlur`，但游戏里 40 个后处理体积的 `WeightedBlendables` 全是空的。本版改用 UE4SS 暴露的 `FMemory::Malloc/Free`（游戏自己的 GMalloc，Binned2 只认它；CRT malloc 交给它 free 会崩）分配 1 个 `FWeightedBlendable`（16 字节），写入材质与权重 1.0，并把 `FScriptArray` 头设成 `{Data,1,1}`；退出时先清空数组头、再自己释放，避免二次释放。风险：效果强弱未定、极端情况下可能崩——把 `[dof] enabled` 改 false 即可完全绕开。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix26** —— 已编译：

- **径向模糊伪虚化**。引擎景深已确认不渲染，但后处理体积的 `WeightedBlendables` 是走过引擎 PP 混合链的（暗角证明过）。本版把游戏自己的后处理材质 `M_FX_PP_RadialBlur` **借槽位**挂到某个已有一条 blendable 的体积上（记原值、退出还原；不自己分配 TArray），实现「中心清晰、四周发糊」。注意：这不是景深，前景背景会一起糊。日志打印 `伪虚化诊断：N 个体积里带 WeightedBlendables 的有 M 个` 与挂载结果。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix25** —— 已编译：

- **收口：景深标注为「本作不支持」，关闭实验通道**。hotfix24 日志给出最后一块拼图：`ViewTarget.POV 的 fstop 在两次 tick 之间从 1.84 变回 40.00`——即便每帧重申，游戏仍会把 `fstop` 写回 `F40`（全合焦）。证据链完整：相机 POV 无效；自建体积只有暗角生效、光圈 88px 无效；关卡体积无效；ViewTarget.POV 每帧被写回；`r.DOF.*` 全健康但 console 命令在本作完全不执行；`ShowFlags@0xB8` 整局不变（不是真的 flags）；6864 个材质里无 DOF 后处理材质 → **本作 gameplay 视图不渲染引擎景深**。本版：① 四个景深标签在四种语言里追加「本作不支持」；② `tuning::kDofExperimentalChannels = false` 关闭四条实验通道/探针/采样/每帧重申；③ 首次下发景深时打一次性说明。配置项与滑条保留为参数记录。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix24** —— 已编译：

- **通道⑤ 改为每帧重申**。hotfix23 结果：`ShowFlags@0xB8` 整局一次没变（说明那块内存不是真的 `FEngineShowFlags`，`bit20=1` 作废）；材质普查 6864 个里没有任何 DOF/bokeh 后处理材质（只有 `M_FX_PP_RadialBlur` 等径向模糊）。但审计发现 `ViewTarget.POV` 的 focal 两次进入分别为 221/362 → 这份 POV 被游戏反复重填，而通道⑤ 之前只在开启/拖滑条时写，写一次就被下一帧的 F40 抹掉。本版每帧重申（探针第 3 段期间用强力参数），并打印「fstop 在两次 tick 之间变回 X」的诊断。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix23** —— 已编译：

- **88px 打进四条通道仍不糊 → 收束核查**。hotfix22 把 fstop=0.1 / 对焦 300cm / 传感器 200mm（≈88px）同时写进相机 POV、自建体积、关卡自带体积、`ViewTarget.POV`（后两者写入成功），画面依旧完全不糊，而同一个体积的暗角有效 → 「值不对 / 通道不对 / 被覆盖」全部排除，只剩「这个视图里没有 DOF pass」。本版：① 每帧采样 `UGameViewportClient+0xB8` 的 16 字节并记录变化，验证那块内存到底是不是真的 `FEngineShowFlags`；② 材质普查，列出所有名字含 blur/dof/bokeh/coc 的材质——找到就能用游戏自己的模糊材质做背景虚化，找不到就把景深标注为本作不支持。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix22** —— 已编译：

- **找到按着光圈的那个值：`ViewTarget.POV` 里写着 `fstop=F40`**。hotfix21 打印出 `景深诊断：ViewTarget.POV 的景深原值 focal=221(override=1) fstop=F40.00(override=1) sensor=23.8(override=1)`——游戏在相机管理器的 `ViewTarget.POV` 里显式把光圈写成 F40（≈0.04px，等于全部合焦），而这份 POV 在**体积之后以 weight=1** 生效，于是写体积/相机 POV 的光圈全被按回去，只有暗角这类没被覆盖的字段能穿过。本版把**通道⑤ 加进探针第 3 段**：fstop=0.1 / 对焦 300cm / 传感器 200mm（≈88px）现在同时写进四条通道。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix21** —— 已编译：

- **通道⑤：相机管理器的 `ViewTarget.POV`**。hotfix20 的通道④没成（玩家 Pawn/PC 身上没有原生 `CameraComponent`）。改走 UE 里视图 PP 的另一条来源 `APlayerCameraManager::ViewTarget.POV`：它由 `UpdateCamera` 从相机组件填好，而摄影模式会暂停世界导致它不再重建、一直停在进入前的状态；它很可能带着 `bOverride_DepthOfFieldFocalDistance=1` + `focal=0`（关 gameplay DOF 的标准写法），且在体积之后以 weight=1 生效——正好解释「体积的暗角有效、体积的光圈无效」。本版快照并写入这份 POV 的景深三项，退出还原，并打印 `景深诊断：ViewTarget.POV 的景深原值 …` 直接指认元凶。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix20** —— 已编译：

- **新增通道④：玩家相机组件**。hotfix19 证实**console 命令在本作里完全不生效**（Kismet 与直连 `PlayerController::ConsoleCommand` 都改不动 `r.Tonemapper.Quality`），过去十几轮的 cvar/ShowFlag 操作全是空转。探针显示「相机 POV 的 PP 无效、体积的暗角有效、体积的光圈（90px）无效」——只能是体积之后还有一步单独覆盖 DOF 字段：`CalcSceneView` 时从 ViewTarget 相机组件重填的 `ViewInfo.PostProcessSettings`（weight=1），那里极可能写着 `bOverride_DepthOfFieldFocalDistance=1` + `focal=0`（关 gameplay DOF 的标准写法）。本版遍历玩家 Pawn/PC 名下的 `CameraComponent`，快照并写入焦点/光圈/传感器（退出还原），并打印 `景深诊断：玩家相机组件 … 的景深原值 …` 直接指认元凶。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix19** —— 已编译：

- **90px 强力景深照样不糊 → 转头查 console 命令到底执行了没有**。hotfix18 把 fstop=0.1 / 对焦 300cm / 传感器 200mm（约 90px）同时写进相机 POV、自建体积、关卡自带体积，画面依旧完全不糊，于是「通道不对」「参数太弱」都排除。但日志里 `r.DepthOfFieldQuality 写 3 回读=2` 与 `ShowFlag.DepthOfField 翻转后内存一位没变` 同时出现，最像的解释是**这些 console 命令根本没执行**（`ExecuteConsoleCommand` 返回成功 ≠ 执行）。本版用可回读的 `r.Tonemapper.Quality` 验证 exec 通道，不过就切到直连 `APlayerController::ConsoleCommand`，再在可用通道上重做 ShowFlag 翻转与 `sg.DepthOfFieldQuality 4`。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix18** —— 已编译：

- **用「不可能看不见」的强力景深测通道**。hotfix16 探针经确认：相机 POV 写 `ColorSaturation` 毫无变化、自建体积写 `VignetteIntensity` 明确「四周变暗」→ **相机 `CameraCache.POV` 的 `PostProcessSettings` 在本作 gameplay 视图里不被采纳**。回看历史：过去所有极端光圈（F0.08 / 421cm / 164mm ≈ 200px）都写在死通道上，而现用参数 F1.8 / 678cm / 286mm 只有约 3px。本版把探针第 3 段换成强力景深（fstop=0.1、对焦 300cm、传感器 200mm ≈ 90px），同时写进相机 POV / 自建体积 / 关卡自带体积，2.5 秒后还原。第 3 段糊了 = DOF 能跑（只是通道/参数问题）；没糊 = 该视图里 DOF 被引擎关掉。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix17** —— 已编译：

- **写错通道了：改用关卡自带体积 + 实证 ShowFlag 位索引**。hotfix16 日志：游戏画质「景深质量」=4、`r.DOF.*` 全健康、`r.DepthOfFieldQuality` 被钉在 2（质量闸门全部排除）；探针第 1 段（相机 POV 的 `ColorSaturation`）毫无反应、第 2 段（自建体积的 `VignetteIntensity`）有明显反应 → **之前十几轮把景深写在相机 `CameraCache.POV` 的 `PostProcessSettings` 上，而这条通道在本作 gameplay 视图里很可能没被采纳**；深度模糊（不看光圈的分支）也没糊。本版：① 新增通道③——把光圈/焦距/传感器同时写进关卡自带的 `PostProcessVolume`（一定在 `UWorld::PostProcessVolumes` 里），退出按快照还原；② 用引擎自己的 `ShowFlag.DepthOfField 0/1` 翻一次并对比内存差异，**实测**真正的位索引与当前值，翻不动就明确判定这条路走不通。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix16** —— 已编译：

- **撤回 ShowFlags 内存写入 + 加一根「深度模糊」探针**。打包后核对了 UE 源码：① `FEngineShowFlags` 在 Shipping 下`SHOWFLAG_FIXED_IN_SHIPPING` 类开关**没有存储**，「第一个 32 位字 bit20 = DepthOfField」的映射不成立——hotfix14/15 往那块内存写 bit20 有改错字段的风险，本版**只读不写**（hex 留档），hotfix15 的包作废；② 后处理混合顺序是「体积 → 相机 POV（weight=1，最后生效）→ 相机动画 PP → `UpdatePhotographyPostProcessing`」，所以之前摘体积覆盖并不必要，能顶掉 DOF 的只可能是 POV 之后那几步；③ `DepthOfFieldScale` 在 `DiaphragmDOF.cpp` 里出现 0 次（CircleDOF 专用），关不掉 DiaphragmDOF。新增探针第三段：`IsEnabled` 的第二条放行分支 `DepthBlurRadius > 0`（完全不看光圈），`DepthOfFieldDepthBlurRadius=400 / Amount=1.0` 同时写相机与自建体积——背景 3 米外糊掉 = 引擎 DOF 活着（问题在焦距/光圈被顶掉），三段都没反应 = 本作没跑 DiaphragmDOF。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix15** —— 已编译：

- **闸门每 2 秒复核 + 两条后处理通道分开测**。hotfix14 日志里 `EngineShowFlags@0xB8 = 07FFFFFF FFFFFBFF FFFFFFFF FFFFFFFF`（bit20 本来就是 1，上一版按 5.3 的声明顺序判「低 12 位全 1 / 12~16 为 0」，实机不满足 → 每次误报「形状不符」而不敢写，把真结论埋了）、`r.DepthOfFieldQuality 2 → 3 | 回读=2`（这条 cvar 被钉住，写了等于没写）、`sg.DepthOfFieldQuality=0` 且包里存在 `gq.DepthOfFieldQualityLevel` 一族（本作把「景深质量」做成了独立画质档）三条同时成立。本版：① `tick_dof_gates()` 每 2 秒复核——一次性打印 11 个相关 cvar 的普查行、`gq.DepthOfFieldQualityLevel` 低于 3 抬到 3 并在摄影模式期间补位（退出还原）、`r.DepthOfFieldQuality` 写不动只告警「被钉住」、`ShowFlags.DepthOfField` 若在进入之后被清掉立刻补位并告警；② 探针重做——A 段只去色（整屏黑白）、停顿 1 秒、B 段只加暗角（四周变暗），肉眼即可分辨到底是「相机 POV」还是「自建体积」在起作用；③ ShowFlags 形状判定改成「位很密、四个字都不为 0」，不再误报。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix14** —— 已编译：

- **最后一道闸门：`ShowFlags.DepthOfField` 位**。hotfix13 的日志证实关卡体积确实覆盖了光圈（原值 `fstop=F7.1`）且已被摘掉，但 `focal=421cm / F0.08 / sensor=164mm`（公式算 203px 背景虚化）依然毫无虚化。后处理通道是活的（暗角生效）、结构是对的（2208 字节 / 588 字段）、覆盖也摘了，只剩「引擎不建 DOF pass」。UE5 里这道条件就是 `EngineShowFlags.DepthOfField`：查 `ShowFlagsValues.inl`，它是 `ALWAYS_ACCESSIBLE` 的真实位（按声明序为**第 20 位**），默认全开、游戏可在运行时关掉。本版从 `UGameViewportClient + 0xB8` 读 `EngineShowFlags` 前 8 个字打印 hex、用「低 12 位全 1 / 12~16 为 0」的形状确认偏移正确，若 bit20 是 0 就置位并回读；形状不符则只在日志留 hex、不写内存。另加 DOF 采样参数（`r.DOF.Gather.RingCount` 等）读数与抬档。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix13** —— 已编译：

- **暗角生效 → 后处理通道是活的，真凶是关卡体积盖掉了光圈**。hotfix12 探针已证明写的是真正的 `FPostProcessSettings`（2208 字节 / 588 字段），属性名只缺 `ExposureCompensation`，并且 `VignetteIntensity=3` 让你看到了「四周变暗」——相机后处理确实进了渲染。唯一能同时解释「暗角生效、去色无效、景深无效」的机制是后处理字段被更高优先级的覆盖顶掉：本关有两个**有界** `PostProcessVolume` 显式覆盖了 `DepthOfFieldFstop`（priority 5 / 1）。本版在开启景深时快照并摘掉这些体积的 DOF 覆盖（退出/关景深原样写回），让本 Mod 的焦点/光圈/传感器成为唯一来源。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix12** —— 已编译：

- 探针 A（相机通道去色）和 B（自建体积染红）都执行了，但画面完全没有变化。结合 hotfix8 里同一条通道算到 33px 虚化也没反应，本版先排除更基础的可能：**我们写的结构/属性名在这个 build 里到底起不起作用**。探针改为「自证 + 多重旋钮」——打印被写结构的字节数与字段数（真正的 `FPostProcessSettings` 有两百多个字段，数字小得离谱就说明写错了结构），同时改 `ExposureCompensation +6`、`VignetteIntensity 3`、`ColorSaturation 0` 三个互不相关的旋钮，并在写之前核对每个 `bOverride_*` / 值属性是否存在（缺哪个打印哪个，并列出该结构里所有 `color/exposure/vignette/bloom/depthoffield` 真实字段名）。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix11** —— 已编译：

- hotfix8 的日志把所有闸门都排除了：`r.DepthOfFieldQuality=2`、CoC 半径 0.025、`override` 全 1、`blendWeight=1.00`、自建体积 priority=10000 且独立相机确实是 ViewTarget，按薄透镜公式算到 **16.6~33.2px@1920** 的画面虚化，依然毫无变化。所以只剩两种可能：引擎 DOF 根本没跑，或我们的后处理写入通道没进渲染。本版用两段探针分开它们：A（1.2s）只把**相机通道**饱和度压 0 → 黑白；B（1.2s）只把**自建体积** `ColorGain` 改强红 → 全红。另外加了一次性 `MPC 景深候选参数` 日志（本作后处理大量走 MPC，若景深是自家材质做的会在这里露出参数名）。核心层 5,728 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix8** —— 已编译：

- **「调数值没反应」的真因是物理量**：hotfix7 的自检日志证明引擎侧闸门全开（`r.DepthOfFieldQuality=2`、CoC 半径上限 0.025、`override` 全 1、`blendWeight=1.00`，`DepthOfFieldEnabled` 在本作不存在）。按薄透镜模型换算，玩家的落点只有 0.4~2.5px 虚化、旧默认（F4/35mm/5m）0.42px、旧配置（对焦 513 米 + F18.4）0.00px —— 2px 以下肉眼等于没有。改法：光圈下限 0.5→0.05、传感器宽度上限 100→400mm（光圈改对数滑条），默认值 300cm/F0.5/60mm（≈9.7px@1920），日志直接打 `预计背景虚化≈X.Xpx`，旧配置进模式时一次性纠正并打印原值，另加一次性黑白探针用来区分「虚化太弱」与「后处理通道没进渲染」。核心层 4,524 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix7** —— 已编译：

- **「景深调数值没反应」：把引擎侧三道闸门一次调到可用**。既有证据说明字段写对了、后处理通道也通（AA/动态模糊/GI 覆盖每帧生效，自建 priority=10000 的无界体积建得起来，关卡 6 个体积都不抢 DOF），剩余可能全在引擎闸门：`r.DepthOfFieldQuality<=0`（不建 DOF pass）、`ShowFlag.DepthOfField` 关、`r.DOF.Kernel.Max*Radius` 把 CoC 夹没；另外补写 UE5.4+ 的 per-view 总开关 `DepthOfFieldEnabled`。现在开景深时一次处理这三道闸门（各记基线，退出/关景深/世界换代还原，失败交给重试环），并加一轮「景深自检」日志（CVar 读数 + 两个通道的写后回读，读不到印 `<na>`）。核心层 4,505 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix6** —— 已编译：

- **对焦距离滑条窗口直接收到 300~1500cm**（按你的建议）：菜单只铺 3~15m，1 像素≈12cm、线性更好拖；`[dof]` 配置与 PAGE_UP 按键步进仍是 1..100000cm（微距/远景照旧），窗口外的旧值打开菜单会贴右端、拖动即夹到 1500。`对焦步进` 保持对数。核心层 4,503 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix5** —— 已编译：

- **修复「对焦距离滑条拖不到可用值，画面自然没景深」**：hotfix4 的诊断证明体积通道正常（关卡 6 个体积里只有两个**有界**体积覆盖 `fstop`，其余不碰 DOF），问题在数值——1..100000cm 的线性滑条 1 像素≈500cm，可用的 300~1500cm 只占最左 0.3%，每次落点都落在几万 cm + F16~20 这类「物理上全清晰」的区间。`对焦距离` / `对焦步进` 改为**对数滑条**（500cm 位于 54% 处），其余 Float 保持线性；补断言锁死「对数字段下界>0」与这两个字段必须对数。hotfix4 的 Info 级诊断全部保留。核心层 4,502 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix4** —— 已编译：

- **修复「菜单数值改得动、画面没有景深」**：UE 的最终后处理按优先级把场上 `PostProcessVolume` 的 `bOverride_*` 盖在相机/POV 设置之上（相机只是基座、没有优先级），关卡里只要有体积覆盖 DOF，写进去的值就进不了画面。新增**景深专用后处理体积**通道（`bUnbound + Priority=10000`，DOF 三项与 override 写进它的 `Settings`），原相机/POV 通道保留、任一成功即可用；退出/关景深/切后端/世界换代时销毁。另加 Info 级诊断：体积建立日志、场上体积优先级与 DOF 覆盖清单、每次下发的 `focal/fstop/sensor` 实际值。核心层 4,498 项测试仍全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix3** —— 已编译：

- **修复 PBZFM 菜单滑条拖不动 / 松手回弹**：自绘页每帧从 `mirror_` 重建草案，而 Float 滑条是「拖动中只落草案、松手才提交」——中间值每帧被丢弃、松手提交的还是旧值（ImGui 拖拽是逐帧增量，绑定变量被每帧重置就只能动一帧）。改为草案跨帧写回 `mirror_`，并加代次校验防止旧草案覆盖同帧的配置重载。**相机/移动/手柄/冻结各组的所有 Float 滑条**一并修好。核心层 4,498 项测试仍全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp-hotfix2** —— 已编译：

- **景深/手动对焦缺陷修复**：手柄调焦/调光圈不再连带滚转（四个组合键按住时肩上键只当修饰键）；景深写不进改按秒重探、环境恢复即自动可用（不再「一次失败整局对焦键失效」）；接管快照与运行值拆成两套变量，退出还原真正写回进入前的原值；后端热切换保留本次会话调好的对焦值，切换失败回滚也重新下发景深；独立相机后端下关景深立即撤销 `bOverride_*`；景深写失败不再被误报成「应用相机位姿失败」；`[dof]` 合法范围集中到 `core/tuning.h`。核心层 4,498 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**2.0.0-cpp** —— 已编译：

- **冗余清理 + 缺陷修复 + 相机后端热切换**：删 3,129 行冗余（含与 UE4SS 逐字节相同的 ImGui backends 2,763 行、变更史从 README 迁出 829→195 行、合并重复构建脚本），释放 1,833 MB 过期构建目录；修 7 处实际缺陷（解冻级联分叉致布料/植被可能永久静止、手柄回退键位错 3 处、发行包配置与代码默认值差 10 项、菜单坏键名显示 `?`、4 个零消费者配置项、`.bat` 中文注释导致脚本半坏、PBZFM 菜单事件只注册不反注册）；相机后端（接管当前相机 ↔ 独立相机）改为**菜单内即时切换**，失败自动回滚；`GameViewportClientTickPost` 缺失时自动降级为独立相机（原先会让镜头卡死）。核心层 4,444 项测试全绿，详见 [CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md)。

**1.0.76-cpp** —— 已编译：

- **修复「进入相机模式自动冻结后，布料件整体换位」**：不再写 `pbz.Sway.SkipSkeletalKinematicBoneUpdate=1`（会让 Sway 布料一次性回到动画姿态，用户实测表现为整件布料换位、褶皱形状不变）；`r.MorphTarget.Enable/ForceUpdate` 从“关速度输出”批次移除（全局关 morph 会改布料/皮肤形态，属 A/B 开关副作用）；新增 `Sway 全对象扫描` 日志（真实类名+实例名）与渲染 CVar 逐项耗时日志；核心层 4398 项测试全绿，详见 [docs/README.md](PBZPhotoModeCpp/docs/README.md)。

**1.0.75-cpp** —— 已编译：

- **景深/手动对焦 + 接管当前相机 + F9 截图机制 + HUD 恢复重构（本批未升版本号）**：新增 `[dof]` 组与对焦/光圈键（景深写 `PostProcessSettings` 时同步置 `bOverride_*`）；`[camera] takeover_current_camera` 默认 `true`，每帧在 `GameViewportClient::Tick` 之后覆写 `CameraCache.POV`，失败/连续 15 帧回读不一致自动回退独立相机；F9 机制用 `GetProcAddress` 解析 `FScreenshotRequest::RequestScreenshot(bShowUI=false)`，但本作导出表无该符号（实测不可用，降级外部工具）；HUD 恢复改为 `SetHideUIForPV` + `bShowHUD` + UMG 根控件，字幕/提示/锁定/Flow 保留细粒度，存活判定改 `is_ok()`。核心层断言总数见 [docs/test_count.txt](PBZPhotoModeCpp/docs/test_count.txt)
- **Q/E 滚转键可配置**：新增 `[keys] key_roll_left` / `key_roll_right`（默认 `Q` / `E`，左=逆时针、右=顺时针）——此前 Q/E 硬编码在 `input_logic.cpp`，`config.ini` 只能改 `roll_speed`，改不了滚转键；现在 `config.ini` 与 PBZFM 菜单「快捷键（键盘）」页都能改，旧配置不写新键名时行为不变
- **高危/中度问题收口（本批未升版本号）**：H-1～H-5 与 M-4/M-5/M-10 落地——退出恢复失败改为有限重试且不再吞错、时间/暂停原始值按世界令牌失效、反射遍历与 `ProcessEvent` 加 SEH 隔离、跨世界缓存统一失效、PBZFM 线程状态归类；配置越界值改为 clamp 到边界（M-5）。核心层断言总数见 [docs/test_count.txt](PBZPhotoModeCpp/docs/test_count.txt)
- 核心层测试全绿（当前断言总数见 [docs/test_count.txt](PBZPhotoModeCpp/docs/test_count.txt)，由 `tools/update_test_count.bat` 自动写入）

**1.0.74-cpp** —— 已编译：

- **新增「过场对话 Flow 冻结/诊断（保留相机，不挂线程）」**：新增配置 `freeze_dialogue_flow`（默认 true，仅 `hide_subtitles=true` 时生效）——冻结时把 `WBP_FlowDialog_C` 从 `Hidden` 改为 `Collapsed` 停止 Slate Tick，避免对话 Flow 在冻结期继续推进/提前结束当前语音，解冻恢复 `Hidden`（仍隐藏但继续 Tick）；退出时按原可见性恢复。新增 `PlayAudio_C.ForceFinishAudio` / `StopAudio_C.ExecuteUbergraph_StopAudio` 只读诊断钩子，冻结期被调用打日志（前 8 次）。不挂线程、不碰 QPC/IAT、不改变 Wwise/相机逻辑
- 核心层测试 **2400 项全绿**

**1.0.73-cpp** —— 已编译：

- **修复「1.0.72 过场/对话中进入摄影模式后语句仍不续播」**：1.0.72 的 Wwise 整引擎 `AK::SoundEngine::Suspend(true,false)` 是 render-anyway——音频继续处理、语音时间轴照常推进，即使逐 PlayingID `PBZPausePlayingID` 执行成功，当前句也会在冻结期间无声播完，解冻后无法从原位置续播。本版改为：有逐语音 PlayingID 时**跳过整引擎 Suspend**，逐 PlayingID 真暂停（`PBZPausePlayingID`/`PBZResumePlayingID`）为唯一常规路径；整引擎 `Suspend(true,false)` 仅在没截获到任何 PlayingID 时作静音兜底（`wwise_engine_suspend` 仍默认 true）。另新增 `GetPlayingIDPositionMs` 语音位置校验日志（暂停/恢复前后比对播放头位置），并补对话相关类函数诊断
- 核心层测试 **2306 项全绿**

**1.0.72-cpp** —— 已编译：

- **修复「过场动画进入相机模式再退出后被截断的语句不能正常续播」**：① 1.0.66 起的 Wwise 整引擎 `AK::SoundEngine::Suspend(false,false)` 是硬停音频线程（音频完全不处理），会丢掉事件队列/语音流状态；现在改为 `Suspend(true,false)` render-anyway 静音（音频继续处理但不输出，保住语音状态），并恢复 1.0.36 实机验证过的逐 PlayingID `PBZPausePlayingID`/`PBZResumePlayingID` 为第一层，整引擎只作第二层；② 进入摄影模式时保存相机管理器的原 `ViewTarget`（过场相机），退出优先还原它，不再固定回玩家 Pawn；③ `WBP_FlowDialog_C` 等对话控件改用 `Hidden`（保留 Tick）而不是 `Collapsed`，且检测到对话 UI 时不再关闭 `bEnableSubtitles`，避免当前语句/字幕被截断
- 核心层测试 **2301 项全绿**

**1.0.71-cpp** —— 已编译：

- **修复「字幕/交互提示反复进出后仍消失」**：1.0.70 的 UI 一次性修复从未执行（`find_live_player_pawn()` 要求 Player+Pawn+bBegunPlay 全命中，实机 Player/世界属性读不到）；且设置原值 getter 优先读到未同步旧副本（`GetSubtitlesEnabled()`=关但字幕实际在显示），退出把原值还原成关。现在玩家就绪判定只要求 Pawn 可读、`bBegunPlay` 只在明确 false 时拦截；`read_bool_setting()` 改为属性优先（`bEnableSubtitles` / `bSceneInteractPromptVisible`）、getter 兜底，写仍走 setter + Pending + 属性三路
- 核心层测试 **2294 项全绿**

**1.0.70-cpp** —— 已编译：

- **修复「进入游戏时崩溃」**：1.0.69 的 UI 一次性修复在启动 3 秒调用 `player_pawn()`，`controller()` 缓存的 PlayerController 在换图/读档时已销毁，`GetClassPrivate()` 读出 `0xFFFFFFFFFFFFFFFF`，旧 `is_ok` 只判非空 → `find_property` 把 -1 当 `UStruct*` 构造 `TFieldIterator` 读地址 -1 崩溃（dump：`crash_2026_11_02_15_36_45`，RIP=`PBZPhotoModeCpp.dll+0x198A3`）。现在 `is_ok` 增加指针范围/对齐 + SEH 保护的类指针读取 + `FUObjectItem` 对象数组反查；UI 修复改用 `find_live_player_pawn()` 实时枚举（Player+Pawn 都在且 `UWorld.bBegunPlay=true`）
- 核心层测试 **2294 项全绿**

**1.0.69-cpp** —— 已编译：

- **修复「退出摄影模式后字幕与交互按钮一直消失」**：一次性修复（`repair_ui_settings_once` 默认开）把字幕/场景交互提示两个游戏设置强制打开、回读校验并立即存盘（写 `ui_settings_repair_done.flag`，只做一次）；同时给这两个设置加 getter 优先读取 + setter/pending/属性三路写 + 回读校验，不再静默写坏玩家设置
- 核心层测试 **2294 项全绿**

**1.0.68-cpp** —— 已编译：

- **锁定白点与锁定血条一起隐藏**：实机日志确认血条残留根因是「OnlyLockedTarget 只为锁定目标显示血条」；exe 取证找到游戏锁定系统 `TargetingComponent`（`LockWidget`/`SecondaryLockIndicators`/`SetShowLockIndicator`/`HasLockTarget`/`GetLockTarget`/`SetLockTarget`/`StopLockTarget`）。进入摄影模式时藏指示器控件、关指示器开关、保存并清掉锁定目标（血条随之消失），退出成对恢复；关键词补 `focus`/`indicator`/`hpbar` 等，怪物世界控件（WidgetComponent）兜底；诊断④输出锁定系统详情
- 核心层测试 **2294 项全绿**

**1.0.67-cpp** —— 已编译：

- **清理两处旧日志提示**：`PostProcess 快照失败` 不再误报（本版不修改后期处理，标记为已处理）；相机修饰器列表改为同时兼容 UE5 的 `ModifierList` 与旧名 `CameraModifiers`，冻结时能正常停用/恢复，不再打印“无数组”警告
- 核心层测试 **2294 项全绿**

**1.0.66-cpp** —— 已编译：

- **修复移动视角频繁卡顿**：彻底移除 1.0.65 的时间偏移补偿 IAT Hook。它把 `PBZ.exe` / `MRTCEngine.dll` 的 QPC 一起冻结，而摄影模式不能像 PBZPause 那样挂起渲染线程，渲染帧计时停摆就会被感知为卡顿；旧配置项 `time_offset_compensation` 自动忽略并告警。
- **Wwise 整引擎 Suspend 真正生效**：当前 PBZ 把 Wwise 静态链接进 `PBZ.exe`，1.0.65 只查 `AkSoundEngineDLL.dll` 导致静默回落；现在优先从主程序导出解析 `Suspend` / `WakeupFromSuspend` / `IsInitialized`，日志会写明接口来源，失败才回落逐语音。
- 核心层测试 **2294 项全绿**。（1.0.65 的 2372 项降到 2294 项，是移除了时间偏移补偿（IAT Hook）的专用用例，不是测试回退）

**1.0.65-cpp** —— 已编译：

- **修复 1.0.64 冻结崩溃**：`PatchModuleImports` 计算导入函数名时漏加模块基址，`strcmp` 读到 `AddressOfData` 的 RVA 裸值（PBZ.exe 为 0xD9CEABE）导致 0xC0000005；已按 PBZPause 原实现改为 `module_base + AddressOfData + 2`，并补上 IAT/名称 RVA 的边界校验
- **Wwise 整引擎 Suspend**：冻结时优先调用 `AK::SoundEngine::Suspend(false,false)`，解冻 `WakeupFromSuspend(0)`，一次覆盖环境/音乐/语音/UI 全部声音；失败自动回落原逐 PlayingID + 组件音量路径（新配置 `wwise_engine_suspend` 默认 true）
- **时间偏移补偿（1.0.66 已移除）**：借鉴 PBZPause 的 IAT Hook 方案，冻结前 hook 主程序与游戏侧 DLL 的 `QueryPerformanceCounter` / `GetTickCount` / `GetTickCount64` / `timeGetTime`，解冻时把暂停时长累加进偏移——动画/物理/粒子/计时器不跳时间（新配置 `time_offset_compensation` 默认 true，Hook 失败自动降级；不碰本模组自身 CRT）

**1.0.63-cpp** —— 已编译：

- **升降键可在 config.ini 配置**：新增 `[keys] key_vertical_up` / `key_vertical_down`（默认 `MOUSE_LEFT` / `MOUSE_RIGHT`）。此前鼠标左右键硬编码在输入层，config.ini 只能改 `mouse_vertical_mode`（translate/look）和 `vertical_speed`，不能换键。现在可写 `MOUSE_MIDDLE` / `MOUSE_X1` / `MOUSE_X2` 等鼠标键，也可改成键盘键（如 `PAGE_UP` / `PAGE_DOWN`）；PBZFM 菜单同步支持捕获鼠标键

**1.0.46-cpp** —— 已编译：

- **方向键/十字键左右改转向**：方向键左/右与手柄十字键左/右从横向平移改为**左右转视角（偏航）**，与上/下俯仰组成完整方向键视角簇（横移交给 `WASD` / 左摇杆）；新配置 `yaw_speed`（默认 90 度/秒，Shift/Ctrl 变速生效），`arrow_pan_mult` 废弃；配置键 `key_pan_*`/`pad_pan_*` 改名 `key_yaw_*`/`pad_yaw_*`，**旧键名自动映射继续生效**，自定义键位不丢

**1.0.44-cpp** —— 已编译：

- **隐藏锁定标记（锁定敌人身上的白点）**：新配置项 `hide_lockon_marker`（默认开，PBZFM 菜单「隐藏」页同名开关）。进入摄影模式时按类名/资产名关键词（锁定/目标/标记家族）扫描并隐藏 UMG 控件与可视组件（粒子/贴花/小面片），退出按原值还原；白点的真实类名离线证不了，零命中时打一次性诊断清单（可见控件/可视组件/玩家锁定组件）供实机日志收敛。隐藏失败才按秒重试（上限 10 次）

**1.0.43-cpp** —— 已编译：

- **PBZFM 菜单页改客户端自绘 + 自治多语言**：语言切换做进「PBZ Photo Mode」设置页（跟随 PBZFM / zh-CN / en-US / ja-JP / ko-KR / 外部词典），设置项与状态页文案全部由本模组翻译，不再依赖框架语言文件；**外部词典** = `PBZPhotoModeCpp\languages\*.json`（UTF-8 扁平 JSON，key=中文原文，`__display`=显示名；可覆盖内置、可新增语言，游戏内热重载）；语言选择持久化到 `config.ini` 的 `[ui] menu_language`（默认 `auto`=跟随 PBZFM 菜单）

**1.0.42-cpp** —— 已编译：

- **接入游戏内菜单框架 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu)（可选组件）**：装上 PBZFM 后按其呼出键（默认 `F8`）打开菜单，侧栏出现「PBZ Photo Mode」分区——**「设置」页**：键盘键位（捕获式改键）、行为开关（自动暂停/屏蔽输入/超宽屏/手柄）、隐藏开关、冻结增强、相机与移动参数、手柄参数，改动**实时生效并写回 config.ini**（config.ini 仍是唯一真源，菜单相当于遥控器，重启/卸载 PBZFM 后设置保留）；**「运行状态」页**：OFF/LIVE/FROZEN 状态、位置朝向、键位速查 + FOV 滑条 + 进入/退出/冻结/恢复/重置视角/重载配置按钮。不装 PBZFM 时与旧版完全一致（纯快捷键）。线程模型：菜单回调只在框架线程提交数据，全部引擎交互回到引擎 Tick 线程执行

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

从 [Releases](https://github.com/lin-414/PBZPhotoMode/releases) 下载 `PBZPhotoModeCpp_1.0.75-cpp.zip`，解压到游戏根目录，形成：

```
<游戏目录>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\PBZPhotoModeCpp.dll
├─ config.ini
└─ enabled.txt
```

不需要改 `mods.txt`（`enabled.txt` 即启用）。详细说明见 [PBZPhotoModeCpp/docs/README.md](PBZPhotoModeCpp/docs/README.md)。

> **图形菜单已移除（v1.8.0）**：《影之刃零》Shipping 定制引擎对本模组是彻底黑盒——引擎内图形菜单路线（IoStore 资产 pak / puerts JS 层）全部实证判死（完整取证链见 [PROGRESS](PBZPhotoModeCpp/PROGRESS.md)）。模组为**纯快捷键操作**：F2 进入/退出、F 冻结、R 重置、滚轮调 FOV、WASD/鼠标左右键/Shift/Ctrl 移动、Q/E 滚转，全部在 config.ini 可改（升降键 `key_vertical_up` / `key_vertical_down` 也支持改成鼠标中键、侧键或键盘键）。

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
└─ .gitignore
```

## 版本记录

> 说明：以下历史条目中的断言数字是当时的快照；当前断言总数以 [docs/test_count.txt](PBZPhotoModeCpp/docs/test_count.txt) 为准（由脚本生成）。

- **2.0.0**（2026-11-04）：冗余清理（−3,129 行 / −1,833 MB）+ 7 处缺陷修复 + 相机后端菜单热切换 + 版本号支持 hotfix 序号；交付纪律不变（只出包、不部署）

- **1.0.75 收口批次 · 摄影功能**（2026-11-03，未升版本号）：景深/手动对焦（新增 `[dof]` 组与对焦/光圈键，写 `PostProcessSettings` 时同步置 `bOverride_*`，否则引擎忽略）；接管当前相机（`[camera] takeover_current_camera` 默认 `true`，不 spawn/不切 ViewTarget，每帧在 `GameViewportClient::Tick` 之后覆写 `CameraCache.POV`，进入失败或连续 15 帧回读不一致自动回退独立相机）；CineCamera 焦距联动（`CineCaptureComponent2D` 未打进 Shipping）；F9 无 UI 截图机制（导出表探测 + 焦点门控 + 2 秒落盘观测；本作实测导出表无符号，不可用，降级外部工具）；HUD 恢复重构（`SetHideUIForPV` + `bShowHUD` + UMG 根控件，字幕/提示/锁定/Flow 保留细粒度，存活判定改 `is_ok()`）；断言总数见 `docs/test_count.txt`
- **1.0.75 第二阶段**（2026-11-03）：补结构性测试缺口——mod_app 的引擎侧编排抽到 `core/ModRuntime`（依赖注入，可用 FakeEngine 驱动完整进退路径），win_input 的 XInput/摇杆映射抽到 `core/gamepad`，PBZFM 字段校验补失败路径；遗留常量并入 `core/tuning.h`（数值不变）；断言总数改由 `tools/update_test_count.bat` 写入 `docs/test_count.txt`（单一来源）
- **1.0.75 收口批次**（2026-11-03）：H-1～H-5 与 M-4/M-5/M-10 落地——退出恢复失败改为有限重试且不再吞错、时间/暂停原始值按世界令牌失效、反射遍历与 `ProcessEvent` 加 SEH 隔离、跨世界缓存统一失效、PBZFM 线程状态归类；配置越界值改为 clamp 到边界（M-5）；PostFX 快照死接口整体移除（该批仍不改通用后期处理，`save_postfx`/`restore_postfx` 从进入/退出流程与 `FakeEngine` 一并删除；1.0.75 摄影功能批次起仅景深会写 `PostProcessSettings`）；核心层测试全绿（断言总数见 `docs/test_count.txt`）
- **1.0.75**（2026-11-02）：Q/E 滚转键可配置——新增 `[keys] key_roll_left` / `key_roll_right`（默认 Q/E），修复此前 Q/E 硬编码、`config.ini` 只能改 `roll_speed` 不能改键的问题；PBZFM 菜单与四语言词典同步；核心层测试全绿（断言总数见 `docs/test_count.txt`）
- **1.0.74**（2026-11-02）：新增「过场对话 Flow 冻结/诊断（保留相机，不挂线程）」——`freeze_dialogue_flow`（默认 true，仅 `hide_subtitles=true` 生效）冻结时把 `WBP_FlowDialog_C` 改为 `Collapsed` 停止 Slate Tick，解冻恢复 `Hidden`（仍隐藏但继续 Tick）；新增 `PlayAudio_C.ForceFinishAudio` / `StopAudio_C.ExecuteUbergraph_StopAudio` 只读诊断钩子；核心层测试全绿（断言总数见 `docs/test_count.txt`）
- **1.0.73**（2026-11-02）：修复「1.0.72 过场/对话中进入摄影模式后语句仍不续播」——整引擎 `Suspend(true,false)` 的 render-anyway 会让音频时间轴继续推进，逐 PlayingID 真暂停也保不住当前句；现在有逐语音 ID 时跳过整引擎 Suspend（只走 `PBZPausePlayingID`/`PBZResumePlayingID` 真暂停），整引擎仅在无 ID 时作静音兜底；新增 `GetPlayingIDPositionMs` 位置校验日志；核心测试 2301 → 2306 项全绿
- **1.0.72**（2026-11-02）：修复「过场动画进入/退出摄影模式后语句断句不续播」——Wwise 整引擎 `AK::SoundEngine::Suspend(false,false)` 硬停音频线程会丢语音流状态，改为 `Suspend(true,false)` render-anyway 静音（音频继续处理但不输出），并恢复逐 PlayingID `PBZPausePlayingID`/`PBZResumePlayingID` 为第一层；进入时保存并还原原 `ViewTarget`（过场相机），不再固定回玩家 Pawn；对话控件 `WBP_FlowDialog_C` 等改用 `Hidden` 保留 Tick，对话中不关闭 `bEnableSubtitles`；核心测试 2294 → 2301 项全绿
- **1.0.67**（2026-11-02）：清理旧日志提示——PostProcess 快照不再误报失败（本版不改后期处理，标记为已处理；后续版本已把该死接口整体移除）；相机修饰器列表兼容 UE5 的 `ModifierList`（保留 `CameraModifiers` 旧名回退），冻结/恢复修饰器正常，不再打印“无数组”警告；核心测试 2294 项全绿
- **1.0.66**（2026-11-02）：修复 1.0.65 移动视角卡顿——移除会冻结 `PBZ.exe`/`MRTCEngine.dll` QPC 的时间偏移补偿 IAT Hook；Wwise 整引擎 Suspend 改为优先从 PBZ.exe 静态导出解析（1.0.65 只查 AkSoundEngineDLL.dll 导致静默回落）；核心测试 2294 项全绿（1.0.65 的 2372 → 2294 是移除时间偏移补偿用例，不是测试回退）
- **1.0.65**（2026-11-02）：修复 1.0.64 冻结崩溃——IAT 导入名解析需 `module_base + AddressOfData`，上一版直接拿 RVA 当指针导致 `strcmp` 访问 0xD9CEABE 附近崩溃（dump 0xC0000005）；补模块边界校验后重编，核心测试 2372 项全绿
- **1.0.64**（2026-11-02）：借鉴 PBZPause——Wwise 整引擎 `AK::SoundEngine::Suspend(false,false)` / `WakeupFromSuspend(0)` 优先、逐语音兜底；新增 `time_offset_compensation` 用 IAT Hook 冻结 QPC/GetTickCount/GetTickCount64/timeGetTime 并在解冻时累加暂停时长偏移，避免时间跳变；核心层测试 2216 → 2372 项全绿
- **1.0.63**（2026-11-02）：升降键可配置——新增 `key_vertical_up` / `key_vertical_down`（默认鼠标左/右键），键名表支持 `MOUSE_LEFT/RIGHT/MIDDLE/X1/X2`；输入层 5 个鼠标键统一并入 `GetAsyncKeyState` 轮询，translate/look 两模式与 PBZFM 改键同步；核心层测试 2084 → 2216 项全绿
- **1.0.46**（2026-11-01）：方向键/十字键左右从平移改为转向——`Intent` 新增 `yaw_axis`，tick 按 `yaw_speed`（新配置，默认 90 度/秒）积分；`key_pan_*`/`pad_pan_*` 改名 `key_yaw_*`/`pad_yaw_*`（旧键名兼容映射），`arrow_pan_mult` 废弃；核心层测试 2069 → 2080 项全绿（新增旧键名兼容、方向键/十字键偏航 intent、tick 偏航积分用例）
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
- **v1.0.14**（2026-10-30）：进入摄影模式自动暂停时间（`auto_pause_time`，默认开启，可在 `config.ini` 关闭）。
- **v1.0.13**（2026-10-30）：当版移除了实验性的 HighResShot 截图（本作 `BP_PlayerController_C` 无 `ConsoleCommand`，且 HighResShot 仅 Editor/standalone 可用）；1.0.75 收口批次恢复了 F9 截图机制，但**本作实测导出表无 `FScreenshotRequest::RequestScreenshot`，F9 不可用**，请用 NVIDIA App / Steam 等系统工具；该机制对导出此符号的其它 UE 游戏有效。
- **v1.0.12**（2026-10-30）：进入摄影模式自动隐藏游戏 HUD（UMG 根控件），退出恢复；`hide_game_hud` 默认改为 `true`。
- **v1.0.11**（2026-10-30）：F 冻结改为游戏暂停（`pause`），真机验证：暂停期间相机仍可移动，解冻正常恢复。
- 逐版本修复记录（含每次真机日志的根因分析）见 [PBZPhotoModeCpp/PROGRESS.md](PBZPhotoModeCpp/PROGRESS.md)。
- 逐版本条目（1.0.3 → 1.0.76，62 节）见 [PBZPhotoModeCpp/docs/CHANGELOG.md](PBZPhotoModeCpp/docs/CHANGELOG.md) —— 完整变更史只在那里维护一份。

## 致谢

- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) — Unreal Engine 脚本系统与 C++ Mod 加载器
- [KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) — 影之刃零专用分支

## 说明

摄影模式内按 `F9` 不会出图：**本作实测 Shipping 导出表没有 `FScreenshotRequest::RequestScreenshot`**，日志会输出「截图能力不可用」。请使用 NVIDIA App / GeForce Experience（默认 `Alt + F1`）、Steam（`F12`）或 Xbox Game Bar（`Win + Alt + Pr`）等系统工具；默认配置下摄影模式内 UI 已由本 Mod 隐藏，外部截图即等效无 UI。F9 机制本身保留，对导出该符号的其它 UE 游戏可用。

本项目仅供单机游戏摄影与学习交流使用；使用 Mod 的风险由使用者自行承担。
