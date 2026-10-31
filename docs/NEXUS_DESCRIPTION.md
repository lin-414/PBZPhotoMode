# Nexus Mods 主页文案（C++ 单版本，可直接粘贴）

## 1. Short description（短简介，≤255 字符）

```text
Free-camera photo mode for Phantom Blade Zero. Press F2 (or L3+R3 on a controller) to enter — time pauses automatically; fly with WASD or the sticks, look with mouse or right stick, wheel for FOV. Native UE4SS C++ mod.
```

## 2. Full description（完整简介，Nexus BBCode 格式）

```bbcode
[center][size=6][b]PBZ Photo Mode (C++)[/b][/size]
[size=4]A native free-camera photo mode for Phantom Blade Zero[/size]

[i]Frame the shot. Freeze the world. Take the picture.[/i][/center]

[b]PBZ Photo Mode (C++)[/b] adds a proper photo mode to Phantom Blade Zero: a camera that detaches from the player, flies freely through the scene, and lets you compose clean shots without HUD clutter or the character blocking the view. It does not modify game files and does not touch your save data.

[b][size=5]Features[/size][/b]
[list]
[*][b]True free camera[/b] — detaches from the character and lets you fly anywhere in the level.
[*][b]Full camera control[/b] — look with the mouse, move with WASD, rise/descend with the mouse buttons, roll the camera with Q/E, boost or slow with Shift/Ctrl.
[*][b]Gamepad support[/b] — enter / exit photo mode with L3 + R3 (both sticks pressed together), fly with the left stick, look with the right stick, rise / descend with RT / LT, roll the camera with LB / RB, FOV with the D-pad. Every binding is configurable in the [gamepad] section of config.ini (Xbox and PlayStation button names are both accepted).
[*][b]Time freeze[/b] — time is paused automatically when you enter photo mode (default on, can be disabled in config.ini); press F to freeze / unfreeze at any time. Game-pause mode stops enemies, NPCs and effects while the photo camera keeps moving. Cutscene (Sequencer) players are paused together with the world and resumed on unfreeze / exit, so freezing also works during story scenes; if the game refuses the pause, the mod falls back to time dilation automatically. Optional: `stop_wwise_on_freeze` silences Wwise audio components (dialogue, ambience, music) while frozen — the stop is not resumed automatically.
[*][b]Clean shots[/b] — the game HUD (health, quickslot, quest text, boss bars) is hidden automatically on entering photo mode and restored on exit; optionally hide the player character from the config.
[*][b]Input isolation[/b] — while photo mode is active the game does not receive movement/attack inputs, so you can hold a mouse button to move the camera without swinging a sword. Everything is restored on exit.
[*][b]Fully configurable[/b] — every keybind, move speed, sensitivity, FOV range, auto-pause on entry, freeze mode and UI option can be changed in config.ini.
[/list]

[b][size=5]Controls (default)[/size][/b]
[list]
[*][b]F2[/b] — enter / exit photo mode
[*][b]Alt + mouse[/b] — free look
[*][b]W A S D[/b] — move forward / left / back / right
[*][b]Q / E[/b] — roll the camera (E clockwise / Q counter-clockwise)
[*][b]Shift / Ctrl[/b] — move faster / slower
[*][b]Mouse wheel[/b] — adjust FOV
[*][b]Numpad + / -[/b] — adjust FOV in steps (hold to repeat)
[*][b]Left / Right mouse button[/b] — move camera up / down
[*][b]R[/b] — reset camera to the position, rotation and FOV from when you entered photo mode
[*][b]F[/b] — freeze / unfreeze time
[/list]
All keys can be rebound in [code]config.ini[/code]. The mod is pure hotkeys — there is no in-game menu UI.

[b][size=5]Gamepad controls (default)[/size][/b]
[list]
[*][b]LS + RS[/b] — enter / exit photo mode (press both stick clicks together)
[*][b]Left stick[/b] — move (analog speed)
[*][b]Right stick[/b] — look
[*][b]RT / LT[/b] — ascend / descend
[*][b]LB / RB[/b] — roll the camera (RB clockwise / LB counter-clockwise)
[*][b]L3 / R3 (stick clicks)[/b] — move faster / slower (click one stick; clicking both together enters / exits photo mode)
[*][b]D-pad Up / Down[/b] — adjust FOV (hold to repeat)
[*][b]X[/b] — freeze / unfreeze time
[*][b]Y[/b] — reset camera
[/list]
Gamepad bindings can be rebound in the [gamepad] section of [code]config.ini[/code]. Button names in both Xbox (LS, RS, START…) and PlayStation (L3, R3, OPTIONS…) styles are accepted; several buttons joined with + form a combo that must be held together, and [code]NONE[/code] unbinds an action.
[b]Important — turn off Steam Input:[/b] in Steam, right-click the game → Properties → Controller and disable [b]Enable Steam Input[/b]. With Steam Input on, the game may not see your controller at all, or the mod's gamepad hotkeys (L3 + R3 to enter / exit, D-pad FOV, stick-click speed shift, X freeze, Y reset) may not trigger — see Troubleshooting below.

[b][size=5]Requirements[/size][/b]
[list]
[*][b]Phantom Blade Zero[/b] (Steam version, UE 5.7)
[*][url=https://github.com/KintaroModding/ue4ss][b]UE4SS — Phantom Blade Zero build[/b][/url]. This mod was built and tested against [b]v3.0.1-1183-gcdbc0b66[/b]; use the matching UE4SS build, as other versions may not be ABI-compatible.
[/list]

[b][size=5]Installation[/size][/b]
[b]Option A — with the mod manager (recommended)[/b]
[list=1]
[*]Install [url=https://www.nexusmods.com/phantombladezero/mods/2][b]Phantom Blade Mod Manager[/b][/url] from Nexus Mods and set it up following its own instructions.
[*]Use it to install this mod from [b]PBZPhotoModeCpp_1.0.23-cpp.zip[/b] (Files tab) — the manager also installs the required UE4SS Phantom Blade Zero build for you.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]
[b]Option B — manual[/b]
[list=1]
[*]Install the Phantom Blade Zero build of UE4SS into your game root folder (you should see dwmapi.dll and an ue4ss folder next to the game executable).
[*]Download [b]PBZPhotoModeCpp_1.0.23-cpp.zip[/b] from the Files tab and extract it into the game root folder.
[*]Check that the resulting structure is: [code]ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll[/code] plus config.ini and enabled.txt in the same mod folder.
[*]Launch the game and press [b]F2[/b] (or press both stick clicks — L3 + R3 — on a controller).
[/list]

[b][size=5]Configuration[/size][/b]
Edit [code]ue4ss/Mods/PBZPhotoModeCpp/config.ini[/code]. Invalid values fall back to defaults with a warning in UE4SS.log. Useful options include move_speed, look_sensitivity, fov_min/fov_max, roll_speed, freeze_mode (pause or dilation), auto_pause_time, hide_player, hide_game_hud, all keybinds, and the [gamepad] section (pad_toggle / pad_freeze / pad_reset / pad_fov_up / pad_fov_down, pad_look_speed, pad_move_scale, pad_deadzone, pad_enabled).

[b][size=5]Troubleshooting[/size][/b]
[list]
[*][b]Nothing happens on F2:[/b] open [code]ue4ss/UE4SS.log[/code] and search for [code][PBZPhotoMode][/code]. Check that the mod folder name matches and that enabled.txt is in the mod root.
[*][b]Camera does not rotate:[/b] hold Alt while moving the mouse. The mod writes input diagnostics to UE4SS.log once per second — check the raw/engine mouse lines there.
[*][b]The mod does not load:[/b] make sure you are using the Phantom Blade Zero UE4SS build listed above. The DLL imports UE4SS symbols and is not compatible with arbitrary UE4SS versions.
[*][b]Game input stuck after exiting:[/b] press F2 again to toggle out, or restart the game. Please report it with the relevant UE4SS.log lines.
[*][b]Where is the menu?[/b] There is none — this mod is pure hotkeys (an in-game graphical menu is not feasible in this game's shipping build; see the project readme). A status/keybind panel is available in the UE4SS console if you need one.
[*][b]Gamepad not detected, or gamepad hotkeys don't trigger:[/b] disable [b]Steam Input[/b] for the game (Steam library → right-click → Properties → Controller → untick Enable Steam Input) — with Steam Input on, the game's XInput usually sees no controller at all, and even when it does, the mod's gamepad hotkeys (L3 + R3, D-pad FOV, stick-click speed shift…) may fail to trigger; this is especially common with DS4Windows / GameSir and other virtual-gamepad remappers. UE4SS.log shows whether XInput loaded (1.0.18+ loads it from the system directory) and logs a line whenever a controller connects or disconnects.
[/list]

[b][size=5]Known limitations[/size][/b]
[list]
[*]Photo mode only — this is not a replay editor or a cinematic sequence tool.
[*]The mod is tied to a specific UE4SS build; a game or UE4SS update may require a recompile.
[/list]

[b][size=5]Credits[/size][/b]
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

给《影之刃零》加一个真正的自由摄影相机：按 F2（或手柄左右摇杆同时按下 L3+R3）进入摄影模式，相机脱离角色，可以自由飞到场景任意位置构图；按住 Alt + 鼠标转视角，WASD 平移，鼠标左/右键升降，Q/E 滚转画面，滚轮调 FOV，F 冻结时间。

**主要功能**

- 真正的自由相机，不受角色位置限制
- 摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- 时间冻结：进入摄影模式自动暂停时间（`auto_pause_time` 默认开启，可在 `config.ini` 关闭）；角色、敌人、NPC 与特效会真正静止，相机仍可移动；剧情/过场播放器一并暂停（解冻或退出时恢复，`freeze_sequences` 可关），游戏拒绝暂停时自动回退时间缩放；对话语音可选一次性静音（`stop_wwise_on_freeze` 默认关闭，解冻后由游戏自行重播）
- 手柄支持：**左右摇杆同时按下（L3+R3）** 进入 / 退出摄影模式；左摇杆移动、右摇杆转视角、RT/LT 升降、**RB/LB 滚转画面**（RB 顺时针 / LB 逆时针，与键盘 Q/E 同向）、L3/R3 变速（LS 加速 / RS 减速；两杆同按为进出组合，不触发变速）、十字键调 FOV、X 冻结、Y 重置；键位全部可在 `config.ini` 的 `[gamepad]` 段修改（Xbox / PS 按钮叫法都认，`NONE` 解绑）
- 手柄注意：Steam 需把本作的「使用 Steam 输入」关掉（Steam 库 → 右键游戏 → 属性 → 控制器），否则游戏进程可能看不到手柄，或手柄快捷键（L3+R3 进出摄影模式、十字键调 FOV、摇杆按下变速、X 冻结、Y 重置）无法触发
- 可隐藏角色、隐藏 HUD，拍出干净的画面
- 所有键位、速度、灵敏度、FOV 范围、自动暂停开关都可在 `config.ini` 中修改
- 截图请使用 NVIDIA App / GeForce Experience 或 Steam 等系统工具
- 原生 UE4SS C++ Mod，支持 UE4SS 控制台中的 ImGui 面板

**前置要求**

需要《影之刃零》专用 UE4SS（KintaroModding 的 phantombladezero 版本；开发验证版本为 v3.0.1-1183-gcdbc0b66）。不同 UE4SS 版本的 ABI 可能不兼容。

**安装**

- 方法一（推荐）：安装 [Phantom Blade Mod Manager](https://www.nexusmods.com/phantombladezero/mods/2)（N 网），按其说明配置后，用它加载 `PBZPhotoModeCpp_1.0.23-cpp.zip` 完成安装（所需 UE4SS 也会由它一并装好）。
- 方法二（手动）：解压 `PBZPhotoModeCpp_1.0.23-cpp.zip` 到游戏根目录，形成 `ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll` 结构，进游戏按 F2（或手柄 L3+R3）。

详细说明与源码见：https://github.com/lin-414/PBZPhotoMode
