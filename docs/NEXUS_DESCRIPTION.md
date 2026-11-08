# Nexus Mods 主页文案（C++ 单版本，可直接粘贴）

## 1. Short description（短简介，≤255 字符）

```text
Free-camera photo mode for Phantom Blade Zero. F2 (or L3+R3) freezes time and detaches the camera: fly anywhere, tune FOV, add look effects, colour grading and engine depth of field, or hold the frame with F7. Optional menu. Native UE4SS C++ mod.
```

## 2. Full description（完整简介，Nexus BBCode 格式；标题只用 [b] 加粗、不用 [size]——Nexus 新版编辑器粘贴时会把 size 加粗标题自动转成 [h2] 标题）

```bbcode
[center][b]PBZ Photo Mode (C++)[/b]
[i]A native free-camera photo mode for Phantom Blade Zero[/i]

[i]Frame the shot. Freeze the world. Take the picture.[/i][/center]

[b]PBZ Photo Mode (C++)[/b] adds a proper photo mode to Phantom Blade Zero: a camera that detaches from the player, flies freely through the scene, and lets you compose clean shots without HUD clutter or the character blocking the view. It does not modify game files and does not touch your save data.

[b]Features[/b]
[list]
[*][b]True free camera[/b] — the camera detaches from the character and flies anywhere in the level. By default it takes over the game's own camera, so the view stays exactly as you framed it; if the takeover fails, it falls back to a separate cinematic camera automatically.
[*][b]Full camera control[/b] — look with the mouse, pan with WASD, rise and descend with the mouse buttons, roll with Q/E, level the horizon with Z, tilt and turn with the arrow keys, and change FOV with the wheel or the numpad. Arrow-key tilting, turning and rolling keep going while held, Shift boosts and Ctrl fine-tunes the movement, and every binding — including mouse buttons — can be changed.
[*][b]Gamepad support[/b] — enter and exit with L3 + R3, fly with the left stick, look with the right stick, rise and descend with the triggers, roll with the bumpers, level the horizon with both bumpers together, and reach FOV, tilt, turn, freeze and reset from the rest of the pad. Every button is rebindable, button combos are supported, and both Xbox and PlayStation names are accepted.
[*][b]Time freeze[/b] — time pauses automatically when you enter photo mode and F toggles it at any time. Choose a true game pause (enemies, NPCs and effects stop dead) or a slow-motion fallback, and cinematics are paused together with the world.
[*][b]Dialogue that resumes where it stopped[/b] — dialogue and cutscene audio are paused voice by voice rather than cut off, so the current line continues from the same position when you resume; an engine-wide mute remains as a fallback for unusual setups. During conversations the dialogue interface can be halted as well, while the camera stays live.
[*][b]Rock-still frames[/b] — freezing also cancels camera shake and camera modifiers, zeroes wind, pins material parameters, suspends cloth simulation and pauses skeletal animation — the drivers behind drifting cloth and foliage and crawling face shadows. Each of these can be switched off individually.
[*][b]Frame hold (F7)[/b] — hold the rendered frame on screen for a calm, perfectly sharp screenshot, then press F7 again to resume. Time is compensated on resume so animations do not jump, and a built-in safety timeout releases the hold automatically.
[*][b]Clean shots[/b] — the HUD, enemy health bars (including the locked target's), the lock-on marker, subtitles, the interaction prompt and optionally the player character are hidden on entry and restored when you leave.
[*][b]Look controls[/b] — five colour and light controls on top of the raw image: vignette, saturation, contrast, bloom and warm/cool tint, each with its own on/off switch and a per-slider reset button. Neutral values leave the game untouched, and everything is restored on exit.
[*][b]Colour grading[/b] — a separate 调色·进阶 (advanced colour) group with lift-gamma-gain style controls: brightness gain, gamma and black offset, driving the game's own colour-grading pipeline. Neutral values are no-ops.
[*][b]Chromatic aberration[/b] — real RGB fringing driven through the game's cinematic render gate (0 = untouched; enabling it switches the photo camera to the separate-camera backend automatically and restores your backend choice on exit).
[*][b]Engine depth of field[/b] — real DOF through the game's own cinematic render profile, switched off by default and toggled with [b]G[/b]: focus distance, F-stop and sensor width, with [b],[/b] / [b].[/b] pulling focus closer / farther (proportional steps, hold to repeat) and [b][[/b] / [b]][/b] opening / closing the aperture. Turning it on switches to the separate-camera backend automatically; the gate and all values are restored on exit.
[*][b]Look presets[/b] — save the current look (all nine look values plus the five switches) to numbered preset files from the menu and re-apply any preset from a dropdown; delete with a two-click confirmation. Presets never overwrite each other, so you can keep a library of grades.
[*][b]Seamless entry[/b] — the photo camera inherits the game camera's exact position, aim and FOV, so there is no jump when you enter.
[*][b]Ultrawide support[/b] — the game's 16:9 framing restriction is lifted on entry, so 21:9 and 32:9 displays are filled, and restored on exit.
[*][b]Input isolation[/b] — the game receives no movement or attack input while photo mode is active, so you can hold a mouse button to move the camera without swinging a sword. Everything is restored on exit.
[*][b]Optional in-game menu[/b] — with the free [url=https://www.nexusmods.com/phantombladezero/mods/314]PBZ-Framework-Menu[/url] installed, its menu key (default F8) opens a PBZ Photo Mode section: press-to-capture key rebinding, behaviour / hide / freeze toggles, camera, movement, gamepad, look, colour-grading and depth-of-field parameters, plus look presets. Changes apply live and are written back to config.ini, and a Status page shows the current state, pose readouts and quick buttons. Without it the mod is pure hotkeys with zero dependencies.
[*][b]Menu languages[/b] — English / 简体中文 / 日本語 / 한국어 out of the box. Texts live in plain JSON files inside the mod folder, so you can edit a translation or drop in a new language and the menu reloads it within seconds.
[*][b]Fully configurable[/b] — every keybind, speed, sensitivity, FOV range, freeze option, visibility toggle, and every look, colour-grading, chromatic-aberration and depth-of-field value can be changed in config.ini, and invalid values are clamped or ignored with a warning in the log.
[/list]

[b]Controls (default)[/b]
[list]
[*][b]F2[/b] — enter / exit photo mode
[*][b]Alt + mouse[/b] — free look
[*][b]W A S D[/b] — move forward / left / back / right
[*][b]Arrow Up / Down[/b] — tilt the camera up / down (hold to keep turning)
[*][b]Arrow Left / Right[/b] — turn the view left / right (hold to keep turning)
[*][b]Q / E[/b] — roll the camera (E clockwise / Q counter-clockwise)
[*][b]Z[/b] — level the camera (reset roll to zero; position, aim and FOV untouched)
[*][b]Shift / Ctrl[/b] — move faster / slower (holding Ctrl also slows the look speed for fine aiming)
[*][b]Mouse wheel[/b] — adjust FOV
[*][b]Numpad + / -[/b] — adjust FOV in steps (hold to repeat)
[*][b]Left / Right mouse button[/b] — move the camera up / down
[*][b]R[/b] — reset camera to the position, rotation and FOV from when you entered photo mode
[*][b]F[/b] — freeze / unfreeze time
[*][b]F7[/b] — hold / release the current frame
[*][b]G[/b] — depth of field on / off (off by default; see config.ini [code][dof][/code])
[*][b], / .[/b] — pull focus nearer / farther (hold to repeat; Shift coarse / Ctrl fine)
[*][b][ / ][/b] — open / close the aperture (smaller / larger F-stop)
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
[*][b]LB + BACK / LB + START[/b] — pull focus nearer / farther
[*][b]RB + BACK / RB + START[/b] — open / close the aperture
[/list]
(The focus and aperture combos share the bumpers with roll, so the camera rolls a little while a combo is held — rebind in config.ini if that bothers you. A gamepad key for the DOF master switch exists but is unbound by default.)
Gamepad bindings live in the gamepad section of [code]config.ini[/code]. Button names in both Xbox (LS, RS, START…) and PlayStation (L3, R3, OPTIONS…) styles are accepted; several buttons joined with + form a combo that must be held together, and [code]NONE[/code] unbinds an action.
[b]Important — turn off Steam Input:[/b] in Steam, right-click the game → Properties → Controller and disable [b]Enable Steam Input[/b]. With Steam Input on, the game may not see your controller at all, or the mod's gamepad hotkeys (L3 + R3 to enter / exit, A/B FOV, D-pad tilt / turn, stick-click speed shift, X freeze, Y reset) may not trigger — see Troubleshooting below.

[b]In-game menu (optional)[/b]
Optionally install [url=https://www.nexusmods.com/phantombladezero/mods/314][b]PBZ-Framework-Menu[/b][/url] (extract it into ue4ss/Mods next to this mod). Its menu key (default [b]F8[/b]) then opens a PBZ Photo Mode section in-game:
[list]
[*][b]Settings[/b] — rebind every key with press-capture, toggle behaviour / hide / freeze-enhancement options, and tune camera, movement, gamepad, look, colour-grading and depth-of-field parameters. Float sliders preview live while you drag them and are written to [code]config.ini[/code] when you release (the file stays the single source of truth — edit it by hand and reload it from the menu); every slider has a reset button, and the five look effects have individual on/off switches.
[*][b]Look presets[/b] — in the look group: save the current look to a numbered preset file, apply any preset from a dropdown, delete with a two-click confirmation. Preset files ([code]look_preset_<n>.ini[/code]) live next to config.ini and never overwrite each other.
[*][b]Status[/b] — photo-mode state (OFF / LIVE / FROZEN), position readouts, keybind cheat sheet, FOV slider and quick buttons (enter / exit, freeze, reset view, reload config).
[*][b]Language[/b] — English / 简体中文 / 日本語 / 한국어 out of the box. Texts live in [code]languages/*.json[/code] inside the mod folder: edit a translation or add a new language file and the menu hot-reloads it within seconds.
[/list]
The menu uses the styled widgets (pill switches, in-track sliders, styled dropdowns) of PBZ-Framework-Menu 0.7.0 or newer; older framework versions still work but fall back to plain widgets.
When upgrading, overwrite the whole PBZPhotoModeCpp mod folder — copying only the DLL leaves the menu text showing raw keys, because the dictionaries are loaded from [code]languages/[/code].

[b]Requirements[/b]
[list]
[*][b]Phantom Blade Zero[/b] (Steam version, UE 5.7)
[*][url=https://www.nexusmods.com/phantombladezero/mods/13][b]UE4SS — Phantom Blade Zero build[/b][/url]. This mod was built and tested against [b]v3.0.1-1183-gcdbc0b66[/b]; use the matching UE4SS build, as other versions may not be ABI-compatible.
[*][url=https://www.nexusmods.com/phantombladezero/mods/314][b]PBZ-Framework-Menu[/b][/url] [i](optional)[/i] — free in-game menu framework; adds the settings menu described above. 0.7.0+ recommended for the styled controls (older versions work with plain fallback widgets).
[/list]

[b]Installation[/b]
[b]Option A — with the mod manager (recommended)[/b]
[list=1]
[*]Install [url=https://www.nexusmods.com/phantombladezero/mods/2][b]Phantom Blade Mod Manager[/b][/url] from Nexus Mods and set it up following its own instructions.
[*]Use it to install this mod from [b]PBZPhotoModeCpp_2.1.1-cpp.zip[/b] (Files tab) — the manager also installs the required UE4SS Phantom Blade Zero build for you.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]
[b]Option B — manual[/b]
[list=1]
[*]Install the Phantom Blade Zero build of UE4SS into your game root folder (you should see dwmapi.dll and an ue4ss folder next to the game executable).
[*]Download [b]PBZPhotoModeCpp_2.1.1-cpp.zip[/b] from the Files tab and extract it into the game root folder.
[*]Check that the resulting structure is: [code]ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll[/code] plus config.ini, enabled.txt and the languages/ folder in the same mod folder.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]

[b]Configuration[/b]
All settings live in [code]ue4ss/Mods/PBZPhotoModeCpp/config.ini[/code]; the file is generated with an inline comment above every option, so it doubles as the reference. Edit it in any text editor, then re-enter photo mode or reload it from the in-game menu. Invalid values are clamped or ignored with a warning in UE4SS.log. You can tune movement speed and the boost / slow multipliers; look sensitivity, pitch limit and invert; FOV default, range, step and wheel behaviour; roll, pitch, yaw and vertical speeds; the vertical-key behaviour; the free-look modifier and whether it must be held; input blocking; freeze mode, auto-pause and cinematic handling; dialogue-interface freezing; audio pause and the engine-wide mute fallback; each freeze enhancement (camera shake, wind, material parameters, cloth, skeletal animation, motion blur, frame generation); the frame-hold safety timeout; HUD, health bar, lock-on marker, subtitle, interaction prompt and character visibility; ultrawide unlock; the camera backend; all keyboard, mouse and gamepad bindings including button combos; the five look values plus their individual on/off switches; the colour-grading trio (gain / gamma / offset); chromatic aberration; and the whole [code][dof][/code] section — master switch, focal distance (cm), F-stop, sensor width, focus step, aperture step, and a forensic bokeh-gate switch left off by default.
Upgrades are handled automatically: 2.1.0 entries appear with defaults on first launch, and keys retired by older versions are reported once in the log and cleaned from [code]config.ini[/code].

[b]Troubleshooting[/b]
[list]
[*][b]Nothing happens on F2:[/b] open [code]ue4ss/UE4SS.log[/code] and search for [code][PBZPhotoMode][/code]. Check that the mod folder name matches and that enabled.txt is in the mod root.
[*][b]Camera does not rotate:[/b] hold Alt while moving the mouse, or disable the free-look modifier requirement in config.ini. The mod writes input diagnostics to UE4SS.log once per second — check the raw/engine mouse lines there.
[*][b]The mod does not load:[/b] make sure you are using the Phantom Blade Zero UE4SS build listed above. The DLL imports UE4SS symbols and is not compatible with arbitrary UE4SS versions.
[*][b]Game input stuck after exiting:[/b] press F2 again to toggle out, or restart the game. Please report it with the relevant UE4SS.log lines.
[*][b]Where is the menu?[/b] There is none by default — the mod is pure hotkeys unless you install the optional PBZ-Framework-Menu (menu key F8); a status/keybind panel is also available in the UE4SS console.
[*][b]Menu shows raw label IDs instead of text:[/b] the [code]languages/[/code] folder is missing — reinstall the whole mod folder (see Installation).
[*][b]Subtitles or the interaction prompt stay hidden:[/b] this can happen after using an older build that wrote the setting off — install 2.0.0 or newer, which runs a one-time repair that turns both back on. If you intentionally play with subtitles off, disable that repair in config.ini and delete [code]ui_settings_repair_done.flag[/code].
[*][b]Gamepad not detected, or gamepad hotkeys don't trigger:[/b] disable [b]Steam Input[/b] for the game (Steam library → right-click → Properties → Controller → untick Enable Steam Input) — with Steam Input on, the game's XInput usually sees no controller at all, and even when it does, the mod's gamepad hotkeys (L3 + R3, A/B FOV, D-pad tilt / turn, stick-click speed shift…) may fail to trigger; this is especially common with DS4Windows / GameSir and other virtual-gamepad remappers. UE4SS.log shows whether XInput loaded and logs a line whenever a controller connects or disconnects.
[*][b]F7 frame hold does not come back:[/b] press F7 again, or wait for the safety timeout (60 seconds by default). While the frame is held the camera stops responding — that is expected; compose first, hold, screenshot, release.
[/list]

[b]Known limitations[/b]
[list]
[*]Photo mode only — this is not a replay editor or a cinematic sequence tool.
[*]Film grain is not available: the game skips that pass at its pinned quality setting. (Chromatic aberration and depth of field went the same way in older versions — 2.1.0 brings both back through the game's own cinematic render profile.)
[*]Depth of field ships switched off. It runs through the game's cinematic render profile, so while it is enabled the photo camera always uses the separate-camera backend (your camera-backend setting is not changed), and focus is manual only — there is no auto-focus on a clicked point.
[*]F7 holds the whole rendered frame, so the camera cannot be moved while a frame is held.
[*]The mod is tied to a specific UE4SS build; a game or UE4SS update may require a recompile.
[/list]

[b]Credits[/b]
[list]
[*][b]UE4SS[/b] — Unreal Engine scripting system and C++ mod loader, by the UE4SS-RE team.
[*][b]KintaroModding[/b] — the Phantom Blade Zero UE4SS package and the Phantom Blade Mod Manager.
[*]Phantom Blade Zero — S-Game.
[/list]

[center][i]If you enjoy the mod, an endorsement is appreciated. Bug reports are welcome — please include the relevant UE4SS.log lines.[/i][/center]
```

## 3. 中文版简介

**PBZ Photo Mode（C++）— 影之刃零摄影模式**

给《影之刃零》加一个真正的自由摄影相机：按 F2（或手柄 L3+R3）进入摄影模式，时间自动冻结，相机脱离角色，可以自由飞到场景任意位置构图。按住 Alt + 鼠标转视角，WASD 平移，鼠标左/右键升降，Q/E 滚转，Z 一键摆平地平线，方向键俯仰/转向，滚轮与小键盘调 FOV，F 冻结时间，F7 把画面定格成绝对静止的一帧；G 开关引擎景深、`,`/`.` 对焦、`[`/`]` 光圈；暗角/饱和/对比/泛光/冷暖五项画面效果、调色·进阶三件、色差与风格预设随手调。可选安装 PBZ-Framework-Menu 后按 F8 在游戏内菜单直接改设置。原生 UE4SS C++ Mod，不改游戏文件、不动存档。

**主要功能**

- **真正的自由相机**：默认接管游戏当前相机，构图不跳变；接管失败自动回退独立电影相机
- **完整镜头控制**：鼠标转视角、WASD 平移、鼠标键升降、Q/E 滚转、Z 摆平地平线、方向键俯仰/转向、滚轮与小键盘调 FOV；俯仰/转向/滚转按住连发，Shift 粗调、Ctrl 微调，所有键位（含鼠标键）都能改
- **手柄支持**：L3+R3 进出摄影模式，左摇杆移动、右摇杆视角、RT/LT 升降、LB/RB 滚转、LB+RB 同按摆平、A/B 调 FOV、十字键俯仰/转向、X 冻结、Y 重置；支持组合键，Xbox / PS 按钮叫法都认
- **时间冻结**：进入自动暂停，F 随时冻结/恢复；可选真暂停或慢动作，剧情/过场动画一并暂停
- **对白续播**：语音逐句真暂停而不是被切断，解冻后从原位置继续播；异常环境才用整引擎静音兜底。对话界面可一并冻结，相机保持可用
- **冻结增强**：停相机抖动与相机修饰器、风源归零、钉住材质参数、挂起布料模拟、暂停骨骼动画——专治冻结后布料/植被漂移和脸部阴影爬动，每项都能单独关闭
- **F7 画面定格**：屏幕停在最后一帧，随便截；恢复时补偿时间不跳帧，自带安全阀自动恢复
- **干净画面**：HUD、敌人血条（含锁定目标）、锁定白点、字幕、交互提示与角色本体（可选）进入即隐藏，退出全部还原
- **画面效果**：暗角、饱和度、对比度、泛光、冷暖五项，每项带独立开关与行内重置按钮；每次进入摄影模式恢复默认参数，中性值即不干预画面，退出还原
- **调色·进阶**：亮度增益 / 伽马 / 黑位三滑条，走游戏自己的颜色分级管线，中性值即不干预
- **色差**：真实 RGB 色边（0=不覆盖），走游戏过场渲染档 + 电影相机组件通道；启用时自动切独立相机后端，退出还原
- **引擎景深（默认关）**：走游戏自带的过场渲染档（与引擎原生 DOF 管线同源），G 键开关；对焦距离 / 光圈 F 值 / 传感器宽度可调，`,`/`.` 对焦拉近拉远（比例步进、按住连发），`[`/`]` 开大收小光圈；开启时自动切独立相机后端，退出全部还原
- **风格预设**：菜单里把当前风格（9 项参数 + 5 个开关）存成编号预设文件，下拉即套用；删除带两击确认，编号只增不回收、绝不互相覆盖
- **超宽屏**：进入解锁 16:9 画幅限制，21:9 / 32:9 铺满，退出还原
- **输入隔离**：摄影模式下游戏收不到移动/攻击输入，按住鼠标挪相机不会挥刀
- **可选游戏内菜单**：装 PBZ-Framework-Menu 后按 F8 打开「PBZ Photo Mode」——捕获式改键、行为/隐藏/冻结增强开关、相机/移动/手柄/画面/调色/景深参数；滑条拖动实时预览、松手落盘，风格预设保存/套用/删除全在菜单里完成（框架 0.7.0+ 为药丸开关/自绘滑条/风格化下拉的新皮肤，旧版框架自动回退朴素控件）；另有运行状态页。不装则纯快捷键、零依赖
- **菜单四语言**：简体中文 / English / 日本語 / 한국어，词典是模组目录里的 JSON，改译文或加新语言几秒热生效
- **全部可配**：键位、速度、灵敏度、FOV 范围、冻结方式、隐藏项、画面效果五项及独立开关、调色三件、色差、整段 [dof]（总开关/对焦距离/光圈/传感器/步进/取证开关）都在 config.ini 里改，非法值会被夹紧或忽略并在日志里提示

**操作（默认）**

| 键盘 / 鼠标 | 功能 |
| --- | --- |
| F2 | 进入 / 退出摄影模式 |
| Alt + 鼠标 | 自由转视角 |
| W A S D | 前 / 左 / 后 / 右平移 |
| 方向键 ↑ / ↓ | 俯仰（按住持续转） |
| 方向键 ← / → | 左右转视角（按住持续转） |
| Q / E | 滚转（E 顺时针 / Q 逆时针） |
| Z | 摆平地平线（只归零滚转） |
| Shift / Ctrl | 加速 / 减速（Ctrl 同时降低视角速度） |
| 滚轮 / 小键盘 + − | 调 FOV |
| 鼠标左 / 右键 | 相机升 / 降 |
| R | 回到进入时的位置、朝向与 FOV |
| F | 冻结 / 恢复时间 |
| F7 | 画面定格 / 恢复 |
| G | 景深开关（默认关，见 config.ini [dof] 段） |
| , / . | 对焦拉近 / 拉远（按住连发；Shift 粗调 / Ctrl 微调） |
| [ / ] | 开大 / 收小光圈（F 值减小 / 增大） |

**手柄（默认）**：L3+R3 进出；左摇杆移动、右摇杆视角；RT/LT 升降；LB/RB 滚转，LB+RB 同按摆平；L3/R3 加速/减速（R3 同时降低视角速度）；A/B 调 FOV；十字键上/下俯仰、左/右转向；X 冻结；Y 重置；LB+BACK / LB+START 对焦拉近/拉远，RB+BACK / RB+START 开大/收小光圈（与滚转共用肩键，组合期间会连带滚转，介意可在 config.ini 改键）；景深总开关手柄键存在但默认解绑。键位都在 config.ini 的手柄段里改，Xbox / PS 叫法都认，`NONE` 解绑。

**注意**：Steam 里要把本作的「使用 Steam 输入」关掉（库 → 右键游戏 → 属性 → 控制器），否则游戏可能看不到手柄，手柄快捷键也可能不触发。

**安装**

- 方法一（推荐）：安装 [Phantom Blade Mod Manager](https://www.nexusmods.com/phantombladezero/mods/2)（N 网），按其说明配置后，用它加载 `PBZPhotoModeCpp_2.1.1-cpp.zip` 完成安装（所需 UE4SS 也会由它一并装好）。
- 方法二（手动）：解压 `PBZPhotoModeCpp_2.1.1-cpp.zip` 到游戏根目录，形成 `ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll` 结构（同级还有 config.ini、enabled.txt 与 languages/），进游戏按 F2（或手柄 L3+R3）。
- 升级安装请**整个模组目录覆盖**：只拷 DLL 会缺 `languages/` 词典，菜单文案会显示成键名。升级全自动兼容：2.1.0 的新配置项首启按默认值出现，旧版本废弃的键会打一次日志并从 config.ini 清理。

**前置要求**：需要[《影之刃零》专用 UE4SS](https://www.nexusmods.com/phantombladezero/mods/13)（Kintaro 的 OFFICIAL UE4SS for PBZ 包；开发验证版本为 v3.0.1-1183-gcdbc0b66），不同 UE4SS 版本的 ABI 可能不兼容。可选：[PBZ-Framework-Menu](https://www.nexusmods.com/phantombladezero/mods/314)（游戏内设置菜单框架；0.7.0+ 为新皮肤控件，旧版自动回退朴素控件）。

**已知限制**

- 只做摄影模式，不是回放编辑器或过场动画工具。
- 胶片颗粒仍不可用：该 pass 被引擎画质档位整段跳过，不做假实现（色差与景深在旧版同样被判死，2.1.0 已经由游戏自带过场渲染档带回归来）。
- 景深默认关闭；启用期间摄影相机固定走独立相机后端（不改你的后端配置），对焦为纯手动步进，没有点击自动对焦。
- F7 定格期间相机不响应，先构图再定格。
- 模组绑定特定 UE4SS 构建，游戏或 UE4SS 大版本更新后可能需要重编译。

**截图**：本作 F9 无 UI 截图不可用（Shipping 导出表无对应符号），摄影模式内 UI 已由模组隐藏，直接用 NVIDIA App（Alt+F1）/ Steam（F12）/ Xbox Game Bar 截图即可。
