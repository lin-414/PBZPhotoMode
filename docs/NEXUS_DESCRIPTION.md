# Nexus Mods 主页文案（C++ 单版本，可直接粘贴）

## 1. Short description（短简介，≤255 字符）

```text
Free-camera photo mode for Phantom Blade Zero. Press F2 (or L3+R3 on a controller) to enter — time pauses automatically; fly with WASD or the sticks, look with mouse or right stick, wheel for FOV. Optional in-game menu. Native UE4SS C++ mod.
```

## 2. Full description（完整简介，Nexus BBCode 格式；标题只用 [b] 加粗、不用 [size]——Nexus 新版编辑器粘贴时会把 size 加粗标题自动转成 [h2] 标题）

```bbcode
[center][b]PBZ Photo Mode (C++)[/b]
[i]A native free-camera photo mode for Phantom Blade Zero[/i]

[i]Frame the shot. Freeze the world. Take the picture.[/i][/center]

[b]PBZ Photo Mode (C++)[/b] adds a proper photo mode to Phantom Blade Zero: a camera that detaches from the player, flies freely through the scene, and lets you compose clean shots without HUD clutter or the character blocking the view. It does not modify game files and does not touch your save data.

[b]Features[/b]
[list]
[*][b]True free camera[/b] — detaches from the character and lets you fly anywhere in the level.
[*][b]Full camera control[/b] — look with the mouse, move with WASD, rise/descend with the mouse buttons (rebindable in config.ini), roll the camera with Q/E (rebind with [code]key_roll_left[/code] / [code]key_roll_right[/code]), level the horizon with Z, tilt with Arrow Up/Down and turn the view with Arrow Left/Right (hold to keep turning), boost or slow with Shift/Ctrl (holding Ctrl also slows the look speed for fine aiming).
[*][b]Gamepad support[/b] — enter / exit photo mode with L3 + R3 (both sticks pressed together), fly with the left stick, look with the right stick, rise / descend with RT / LT, roll the camera with LB / RB (press both together to level the horizon), FOV with A / B, tilt and turn the view with the D-pad. Every binding is configurable in the [gamepad] section of config.ini (Xbox and PlayStation button names are both accepted).
[*][b]Time freeze[/b] — time is paused automatically when you enter photo mode (default on, can be disabled in config.ini); press F to freeze / unfreeze at any time. Game-pause mode stops enemies, NPCs and effects while the photo camera keeps moving. Cutscene (Sequencer) players are paused together with the world and resumed on unfreeze / exit, so freezing also works during story scenes; if the game refuses the pause, the mod falls back to time dilation automatically. Wwise audio (dialogue, character voices, weapons, ambience, music) is muted too while frozen ([code]stop_wwise_on_freeze[/code] and [code]wwise_engine_suspend[/code], both on by default): the mod first truly pauses each captured PlayingID with the game's [code]PBZPausePlayingID[/code] / [code]PBZResumePlayingID[/code] (so the current line resumes where it stopped); only when no PlayingID was captured does it fall back to muting the whole Wwise engine with [code]AK::SoundEngine::Suspend(true,false)[/code] — because a render-anyway mute keeps the audio timeline running, it is skipped whenever per-voice IDs are available (resolved from PBZ.exe's static Wwise exports). During dialogue and cutscenes the dialogue Flow UI can also be frozen ([code]freeze_dialogue_flow[/code], default on, only with [code]hide_subtitles[/code]): its widgets switch to Collapsed to stop the Slate tick, while the photo camera stays live.
[*][b]Rock-still frames[/b] — on freeze the mod also stops camera shakes and disables camera modifiers, zeroes the wind sources and pins material parameter collections (the drivers behind cloth/foliage flutter and crawling shadows), each toggleable in config.ini ([code]stop_camera_shakes_on_freeze[/code] / [code]freeze_wind[/code] / [code]freeze_mpc[/code]); [code]freeze_diag[/code] prints a per-second diagnostic if something still moves.
[*][b]Seamless entry[/b] — the photo camera inherits the game camera's exact position, aim and current FOV (no FOV jump on entering), and class-cached scans keep entering / exiting fast even in large levels.
[*][b]Clean shots[/b] — the game HUD (health, quickslot, quest text, boss bars), enemy (monster) health bars (including the locked target's), dialogue subtitles, the E-key interaction prompts and the lock-on marker dot on enemies are hidden automatically on entering photo mode and restored on exit; optionally hide the player character from the config.
[*][b]Input isolation[/b] — while photo mode is active the game does not receive movement/attack inputs, so you can hold a mouse button to move the camera without swinging a sword. Everything is restored on exit.
[*][b]Optional in-game menu[/b] — with the free [url=https://github.com/lin-414/PBZ-Framework-Menu]PBZ-Framework-Menu[/url] installed, its menu key (default F8) opens a PBZ Photo Mode section in-game: press-capture key rebinding, behavior / hide / freeze toggles, camera, movement and gamepad parameters — changes apply live and are written back to config.ini, plus a Status page with state, pose readouts and quick buttons. Without it the mod stays pure hotkeys, zero dependencies.
[*][b]Menu languages[/b] — the menu ships with English / 简体中文 / 日本語 / 한국어; texts live in languages/*.json inside the mod folder — edit a translation or drop in a new language file and the menu hot-reloads it within seconds.
[*][b]Fully configurable[/b] — every keybind, move speed, sensitivity, FOV range, auto-pause on entry, freeze mode and UI option can be changed in config.ini.
[/list]

[b]Controls (default)[/b]
[list]
[*][b]F2[/b] — enter / exit photo mode
[*][b]Alt + mouse[/b] — free look
[*][b]W A S D[/b] — move forward / left / back / right
[*][b]Arrow Up / Down[/b] — tilt the camera up / down (hold to keep turning)
[*][b]Arrow Left / Right[/b] — turn the view left / right (hold to keep turning)
[*][b]Q / E[/b] — roll the camera (E clockwise / Q counter-clockwise; rebind with [code]key_roll_left[/code] / [code]key_roll_right[/code])
[*][b]Z[/b] — level the camera (reset roll to zero; position, aim and FOV untouched)
[*][b]Shift / Ctrl[/b] — move faster / slower (holding Ctrl also slows the look speed for fine aiming)
[*][b]Mouse wheel[/b] — adjust FOV
[*][b]Numpad + / -[/b] — adjust FOV in steps (hold to repeat)
[*][b]Left / Right mouse button[/b] — move camera up / down (default; rebind with [code]key_vertical_up[/code] / [code]key_vertical_down[/code])
[*][b]R[/b] — reset camera to the position, rotation and FOV from when you entered photo mode
[*][b]F[/b] — freeze / unfreeze time
[/list]
All keys can be rebound in [code]config.ini[/code]. Without the optional menu framework the mod is pure hotkeys; an in-game menu is available (see below).

[b]Gamepad controls (default)[/b]
[list]
[*][b]LS + RS[/b] — enter / exit photo mode (press both stick clicks together)
[*][b]Left stick[/b] — move (analog speed)
[*][b]Right stick[/b] — look
[*][b]RT / LT[/b] — ascend / descend
[*][b]LB / RB[/b] — roll the camera (RB clockwise / LB counter-clockwise)
[*][b]LB + RB (together)[/b] — level the camera (reset roll to zero)
[*][b]L3 / R3 (stick clicks)[/b] — move faster / slower; R3 also slows the look speed for fine aiming (clicking both sticks together enters / exits photo mode)
[*][b]A / B[/b] — adjust FOV (click for one step, hold to repeat)
[*][b]D-pad Up / Down[/b] — tilt the camera up / down (hold to keep turning)
[*][b]D-pad Left / Right[/b] — turn the view left / right (hold to keep turning)
[*][b]X[/b] — freeze / unfreeze time
[*][b]Y[/b] — reset camera
[/list]
Gamepad bindings can be rebound in the [gamepad] section of [code]config.ini[/code]. Button names in both Xbox (LS, RS, START…) and PlayStation (L3, R3, OPTIONS…) styles are accepted; several buttons joined with + form a combo that must be held together, and [code]NONE[/code] unbinds an action.
[b]Important — turn off Steam Input:[/b] in Steam, right-click the game → Properties → Controller and disable [b]Enable Steam Input[/b]. With Steam Input on, the game may not see your controller at all, or the mod's gamepad hotkeys (L3 + R3 to enter / exit, A/B FOV, D-pad tilt / turn, stick-click speed shift, X freeze, Y reset) may not trigger — see Troubleshooting below.

[b]In-game menu (optional)[/b]
Optionally install [url=https://github.com/lin-414/PBZ-Framework-Menu][b]PBZ-Framework-Menu[/b][/url] (extract it into ue4ss/Mods next to this mod). Its menu key (default [b]F8[/b]) then opens a PBZ Photo Mode section in-game:
[list]
[*][b]Settings[/b] — rebind every key with press-capture, toggle behavior / hide / freeze-enhancement options, tune camera, movement and gamepad parameters. Changes apply live and are written back to [code]config.ini[/code] (the file stays the single source of truth — edit it by hand and reload it from the menu).
[*][b]Status[/b] — photo-mode state (OFF / LIVE / FROZEN), position readouts, keybind cheat sheet, FOV slider and quick buttons (enter / exit, freeze, reset view, reload config).
[*][b]Language[/b] — English / 简体中文 / 日本語 / 한국어 out of the box. Texts live in [code]languages/*.json[/code] inside the mod folder: edit a translation or add a new language file and the menu hot-reloads it within seconds.
[/list]
When upgrading, overwrite the whole PBZPhotoModeCpp mod folder — copying only the DLL leaves the menu text showing raw keys, because the dictionaries are loaded from [code]languages/[/code].

[b]Requirements[/b]
[list]
[*][b]Phantom Blade Zero[/b] (Steam version, UE 5.7)
[*][url=https://github.com/KintaroModding/ue4ss][b]UE4SS — Phantom Blade Zero build[/b][/url]. This mod was built and tested against [b]v3.0.1-1183-gcdbc0b66[/b]; use the matching UE4SS build, as other versions may not be ABI-compatible.
[*][url=https://github.com/lin-414/PBZ-Framework-Menu][b]PBZ-Framework-Menu[/b][/url] [i](optional)[/i] — free in-game menu framework; adds the settings menu described above.
[/list]

[b]Installation[/b]
[b]Option A — with the mod manager (recommended)[/b]
[list=1]
[*]Install [url=https://www.nexusmods.com/phantombladezero/mods/2][b]Phantom Blade Mod Manager[/b][/url] from Nexus Mods and set it up following its own instructions.
[*]Use it to install this mod from [b]PBZPhotoModeCpp_1.0.75-cpp.zip[/b] (Files tab) — the manager also installs the required UE4SS Phantom Blade Zero build for you.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]
[b]Option B — manual[/b]
[list=1]
[*]Install the Phantom Blade Zero build of UE4SS into your game root folder (you should see dwmapi.dll and an ue4ss folder next to the game executable).
[*]Download [b]PBZPhotoModeCpp_1.0.75-cpp.zip[/b] from the Files tab and extract it into the game root folder.
[*]Check that the resulting structure is: [code]ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll[/code] plus config.ini, enabled.txt and the languages/ folder in the same mod folder.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]

[b]Configuration[/b]
Edit [code]ue4ss/Mods/PBZPhotoModeCpp/config.ini[/code]. Invalid values fall back to defaults with a warning in UE4SS.log. Useful options include move_speed, look_sensitivity, fov_min/fov_max, roll_speed, pitch_speed, yaw_speed, slow_look_mult, freeze_mode (pause or dilation), freeze_sequences, stop_wwise_on_freeze, wwise_engine_suspend, stop_camera_shakes_on_freeze, freeze_wind, freeze_mpc, freeze_diag, freeze_dialogue_flow, auto_pause_time, hide_player, hide_game_hud, hide_interact_prompt, hide_enemy_health_bars, hide_lockon_marker, hide_subtitles, repair_ui_settings_once (one-time repair that re-enables subtitles / interaction prompts if an older build left them off), ultrawide_unlock, menu_language, all keybinds (including the arrow-key look keys, the roll keys (key_roll_left / key_roll_right), the roll-reset key, and key_vertical_up / key_vertical_down for camera rise/descend — mouse middle/side buttons or keyboard keys accepted), and the [gamepad] section (pad_toggle / pad_freeze / pad_reset / pad_fov_up / pad_fov_down, pad_pitch_up / pad_pitch_down, pad_yaw_left / pad_yaw_right, pad_roll_reset, pad_look_speed, pad_move_scale, pad_deadzone, pad_enabled).

[b]Troubleshooting[/b]
[list]
[*][b]Nothing happens on F2:[/b] open [code]ue4ss/UE4SS.log[/code] and search for [code][PBZPhotoMode][/code]. Check that the mod folder name matches and that enabled.txt is in the mod root.
[*][b]Camera does not rotate:[/b] hold Alt while moving the mouse. The mod writes input diagnostics to UE4SS.log once per second — check the raw/engine mouse lines there.
[*][b]The mod does not load:[/b] make sure you are using the Phantom Blade Zero UE4SS build listed above. The DLL imports UE4SS symbols and is not compatible with arbitrary UE4SS versions.
[*][b]Game input stuck after exiting:[/b] press F2 again to toggle out, or restart the game. Please report it with the relevant UE4SS.log lines.
[*][b]Where is the menu?[/b] There is none by default — the mod is pure hotkeys unless you install the optional PBZ-Framework-Menu (menu key F8); a status/keybind panel is also available in the UE4SS console.
[*][b]Menu shows raw keys like #pad_enabled:[/b] the [code]languages/[/code] folder is missing — reinstall the whole mod folder (see Installation).
[*][b]Subtitles or the E-key interaction prompt stay hidden:[/b] this can happen after using an older build that wrote the setting off — install 1.0.69 or newer, which runs a one-time repair that turns both back on ([code]repair_ui_settings_once[/code], on by default). If you intentionally play with subtitles off, set it to false in config.ini and delete [code]ui_settings_repair_done.flag[/code].
[*][b]Gamepad not detected, or gamepad hotkeys don't trigger:[/b] disable [b]Steam Input[/b] for the game (Steam library → right-click → Properties → Controller → untick Enable Steam Input) — with Steam Input on, the game's XInput usually sees no controller at all, and even when it does, the mod's gamepad hotkeys (L3 + R3, A/B FOV, D-pad tilt / turn, stick-click speed shift…) may fail to trigger; this is especially common with DS4Windows / GameSir and other virtual-gamepad remappers. UE4SS.log shows whether XInput loaded (1.0.18+ loads it from the system directory) and logs a line whenever a controller connects or disconnects.
[/list]

[b]Known limitations[/b]
[list]
[*]Photo mode only — this is not a replay editor or a cinematic sequence tool.
[*]The mod is tied to a specific UE4SS build; a game or UE4SS update may require a recompile.
[/list]

[b]Credits[/b]
[list]
[*][url=https://github.com/UE4SS-RE/RE-UE4SS]UE4SS[/url] — Unreal Engine scripting system and C++ mod loader.
[*][url=https://github.com/KintaroModding/ue4ss]KintaroModding/ue4ss[/url] — Phantom Blade Zero UE4SS branch.
[*]Phantom Blade Zero — S-Game.
[/list]

Source code, issue tracker and development notes: [url=https://github.com/lin-414/PBZPhotoMode]github.com/lin-414/PBZPhotoMode[/url]

[center][i]If you enjoy the mod, an endorsement is appreciated. Bug reports are welcome — please include the relevant UE4SS.log lines.[/i][/center]
```

## 3. 中文版简介

**PBZ Photo Mode（C++）— 影之刃零摄影模式**

给《影之刃零》加一个真正的自由摄影相机：按 F2（或手柄左右摇杆同时按下 L3+R3）进入摄影模式，相机脱离角色，可以自由飞到场景任意位置构图；按住 Alt + 鼠标转视角，WASD 平移，鼠标左/右键升降（可在 config.ini 改键），Q/E 滚转画面（Z 一键摆平地平线），滚轮调 FOV，F 冻结时间。可选安装 PBZ-Framework-Menu 后按 F8 在游戏内菜单直接改设置。

**主要功能**

- 真正的自由相机，不受角色位置限制
- 方向键视角控制：↑/↓ 按住持续俯仰（30 度/秒），←/→ 按住持续左右转视角（90 度/秒）；按住 Ctrl（手柄按下右摇杆 RS）时鼠标/右摇杆视角降到十分之一速，精调构图
- 升降键可自定义：`[keys] key_vertical_up` / `key_vertical_down` 默认鼠标左/右键，可改用鼠标中键/侧键（`MOUSE_MIDDLE` / `MOUSE_X1` / `MOUSE_X2`）或键盘键（如 `PAGE_UP` / `PAGE_DOWN`）；PBZFM 菜单改键同样支持捕获鼠标键
- 旧版遗留修复：首次运行会把被旧版本写坏的字幕/交互提示开关一次性恢复（`repair_ui_settings_once` 默认开，成功后写 `ui_settings_repair_done.flag`；本就习惯关字幕可设为 false）
- 进入体验顺滑：进入摄影模式继承游戏当前镜头的真实 FOV（不再跳变到 90），进出扫描按类缓存提速，大场景也流畅；新增滚转归零快捷键——键盘 Z / 手柄 LB+RB 同按，只摆平地平线、不动构图
- 时间冻结：进入摄影模式自动暂停时间（`auto_pause_time` 默认开启，可在 `config.ini` 关闭）；角色、敌人、NPC 与特效会真正静止，相机仍可移动；剧情/过场播放器一并暂停（解冻或退出时恢复，`freeze_sequences` 可关），游戏拒绝暂停时自动回退时间缩放；冻结时同步处理 Wwise 音频（`stop_wwise_on_freeze` 默认开启）：有截获到逐 PlayingID 时逐语音真暂停（`PBZPausePlayingID` / `PBZResumePlayingID`，保住当前句位置）并跳过整引擎 Suspend；只有没有截获到 ID 时才用整引擎 `AK::SoundEngine::Suspend(true,false)` render-anyway 作静音兜底（`wwise_engine_suspend` 默认开启；正常有 ID 时不会调用，避免继续推进语音时间轴），解冻后从原处续播；对话/过场中可选择冻结对话 Flow（`freeze_dialogue_flow` 默认开启，仅 `hide_subtitles=true` 时生效），停止对话 UI 的 Tick，但保留摄影相机
- 冻结增强：冻结时同时停用相机修饰器与相机 shake、风源归零、钉住材质参数集合——专治冻结后布料/植被边缘闪烁与脸部阴影爬动（`stop_camera_shakes_on_freeze` / `freeze_wind` / `freeze_mpc` 均可单独关闭，`freeze_diag` 每秒输出冻结诊断）
- 手柄支持：**左右摇杆同时按下（L3+R3）** 进入 / 退出摄影模式；左摇杆移动、右摇杆转视角、RT/LT 升降、**RB/LB 滚转画面**（RB 顺时针 / LB 逆时针，与键盘 Q/E 同向）、**LB+RB 同按滚转归零**、L3/R3 变速（LS 加速 / RS 减速，按下 RS 同时把视角速度降到十分之一精调；两杆同按为进出组合，不触发变速）、A/B 调 FOV（单击一格、按住连发）、十字键上/下俯仰、十字键左/右转视角、X 冻结、Y 重置；键位全部可在 `config.ini` 的 `[gamepad]` 段修改（Xbox / PS 按钮叫法都认，`NONE` 解绑）
- 手柄注意：Steam 需把本作的「使用 Steam 输入」关掉（Steam 库 → 右键游戏 → 属性 → 控制器），否则游戏进程可能看不到手柄，或手柄快捷键（L3+R3 进出摄影模式、A/B 调 FOV、十字键俯仰/转向、摇杆按下变速、X 冻结、Y 重置）无法触发
- 可隐藏角色、HUD、交互提示、敌人血条（含锁定目标血条）、锁定白点标记与对话字幕，拍出干净的画面
- 可选游戏内菜单：安装 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu) 后按其呼出键（默认 F8）打开「PBZ Photo Mode」——捕获式改键、行为/隐藏/冻结增强开关、相机/移动/手柄参数，改动**实时生效并写回 config.ini**；另有运行状态页（状态/位姿/快捷按钮）；不装则纯快捷键、零依赖
- 菜单界面四语言：自带 简体中文 / English / 日本語 / 한국어 词典（`languages/*.json`），改译文或加新语言文件约 2 秒热生效；UE4SS 控制台另有 ImGui 诊断面板
- 所有键位、速度、灵敏度、FOV 范围、自动暂停开关都可在 `config.ini` 中修改
- 截图请使用 NVIDIA App / GeForce Experience 或 Steam 等系统工具
- 原生 UE4SS C++ Mod，不改游戏文件、不动存档

**前置要求**

需要《影之刃零》专用 UE4SS（KintaroModding 的 phantombladezero 版本；开发验证版本为 v3.0.1-1183-gcdbc0b66）。不同 UE4SS 版本的 ABI 可能不兼容。可选：[PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu)（游戏内设置菜单框架）。

**安装**

- 方法一（推荐）：安装 [Phantom Blade Mod Manager](https://www.nexusmods.com/phantombladezero/mods/2)（N 网），按其说明配置后，用它加载 `PBZPhotoModeCpp_1.0.75-cpp.zip` 完成安装（所需 UE4SS 也会由它一并装好）。
- 方法二（手动）：解压 `PBZPhotoModeCpp_1.0.75-cpp.zip` 到游戏根目录，形成 `ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll` 结构（同级还有 config.ini、enabled.txt 与 languages/），进游戏按 F2（或手柄 L3+R3）。
- 升级安装请**整个模组目录覆盖**：只拷 DLL 会缺 `languages/` 词典，菜单文案会显示成键名。

详细说明与源码见：https://github.com/lin-414/PBZPhotoMode
