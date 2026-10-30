# Nexus Mods 主页文案（C++ 单版本，可直接粘贴）

## 1. Short description（短简介，≤255 字符）

```text
Free-camera photo mode for Phantom Blade Zero. Press F2 to enter — time pauses automatically; Alt + mouse to look, WASD to fly, wheel for FOV, F to toggle freeze. Native UE4SS C++ mod.
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
[*][b]Full camera control[/b] — look with the mouse, move with WASD, rise/descend with Q/E, boost or slow with Shift/Ctrl.
[*][b]Time freeze[/b] — time is paused automatically when you enter photo mode (default on, can be disabled in config.ini); press F to freeze / unfreeze at any time. Game-pause mode stops enemies, NPCs and effects while the photo camera keeps moving.
[*][b]Clean shots[/b] — the game HUD (health, quickslot, quest text, boss bars) is hidden automatically on entering photo mode and restored on exit; hide the menu with T; optionally hide the player character from the config.
[*][b]Input isolation[/b] — while photo mode is active the game does not receive movement/attack inputs, so you can hold a mouse button to move the camera without swinging a sword. Everything is restored on exit.
[*][b]Fully configurable[/b] — every keybind, move speed, sensitivity, FOV range, auto-pause on entry, freeze mode and UI option can be changed in config.ini.
[/list]

[b][size=5]Controls (default)[/size][/b]
[list]
[*][b]F2[/b] — enter / exit photo mode
[*][b]Alt + mouse[/b] — free look
[*][b]W A S D[/b] — move forward / left / back / right
[*][b]Q / E[/b] — descend / ascend
[*][b]Shift / Ctrl[/b] — move faster / slower
[*][b]Mouse wheel[/b] — adjust FOV
[*][b]Numpad + / -[/b] — adjust FOV in steps
[*][b]Left / Right mouse button[/b] — move camera up / down
[*][b]R[/b] — reset camera to the position, rotation and FOV from when you entered photo mode
[*][b]F[/b] — freeze / unfreeze time
[*][b]T[/b] — show / hide menu
[/list]
All keys can be rebound in [code]config.ini[/code].

[b][size=5]Requirements[/size][/b]
[list]
[*][b]Phantom Blade Zero[/b] (Steam version, UE 5.7)
[*][url=https://github.com/KintaroModding/ue4ss][b]UE4SS — Phantom Blade Zero build[/b][/url]. This mod was built and tested against [b]v3.0.1-1183-gcdbc0b66[/b]; use the matching UE4SS build, as other versions may not be ABI-compatible.
[/list]

[b][size=5]Installation[/size][/b]
[list=1]
[*]Install the Phantom Blade Zero build of UE4SS into your game root folder (you should see dwmapi.dll and an ue4ss folder next to the game executable).
[*]Download [b]PBZPhotoModeCpp_1.0.14-cpp.zip[/b] from the Files tab and extract it into the game root folder.
[*]Check that the resulting structure is: [code]ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll[/code] plus config.ini and enabled.txt in the same mod folder.
[*]Launch the game and press [b]F2[/b]. The menu appears in the bottom-right corner.
[/list]

[b][size=5]Configuration[/size][/b]
Edit [code]ue4ss/Mods/PBZPhotoModeCpp/config.ini[/code]. Invalid values fall back to defaults with a warning in UE4SS.log. Useful options include move_speed, look_sensitivity, fov_min/fov_max, freeze_mode (pause or dilation), auto_pause_time, hide_player, hide_game_hud and all keybinds.

[b][size=5]Troubleshooting[/size][/b]
[list]
[*][b]Nothing happens on F2:[/b] open [code]ue4ss/UE4SS.log[/code] and search for [code][PBZPhotoMode][/code]. Check that the mod folder name matches and that enabled.txt is in the mod root.
[*][b]Camera does not rotate:[/b] hold Alt while moving the mouse. The mod writes input diagnostics to UE4SS.log once per second — check the raw/engine mouse lines there.
[*][b]The mod does not load:[/b] make sure you are using the Phantom Blade Zero UE4SS build listed above. The DLL imports UE4SS symbols and is not compatible with arbitrary UE4SS versions.
[*][b]Game input stuck after exiting:[/b] press F2 again to toggle out, or restart the game. Please report it with the relevant UE4SS.log lines.
[*][b]No menu but camera works:[/b] press T to show the menu, or enable the UE4SS console and use the ImGui tab.
[/list]

[b][size=5]Known limitations[/size][/b]
[list]
[*]Photo mode only — this is not a replay editor or a cinematic sequence tool.
[*]The mod is tied to a specific UE4SS build; a game or UE4SS update may require a recompile.
[*]Single-player use only. Do not use it in online or co-op sessions.
[/list]

[b][size=5]Credits[/size][/b]
[list]
[*][url=https://github.com/UE4SS-RE/RE-UE4SS]UE4SS[/url] — Unreal Engine scripting system and C++ mod loader.
[*][url=https://github.com/KintaroModding/ue4ss]KintaroModding/ue4ss[/url] — Phantom Blade Zero UE4SS branch.
[*]Phantom Blade Zero — S-Game.
[/list]

Source code, issue tracker and development notes: [url=https://github.com/lin-414/PBZPhotoMode]github.com/lin-414/PBZPhotoMode[/url]

[b][size=5]Version history[/size][/b]
[list]
[*][b]1.0.14[/b] — 2026-10-30 — Time is paused automatically when entering photo mode (auto_pause_time, default on; can be disabled in config.ini). Core tests: 298 passed.
[*][b]1.0.13[/b] — 2026-10-30 — Removed the experimental screenshot feature; use NVIDIA App / GeForce Experience or Steam for screenshots. Core tests: 257 passed.
[*][b]1.0.12[/b] — 2026-10-30 — Game HUD (health, quickslot, quest text, boss bars) is hidden automatically in photo mode and restored on exit.
[*][b]1.0.11[/b] — 2026-10-30 — Time freeze uses game pause by default; verified in-game that enemies, NPCs and effects stop while the camera stays movable. Core tests: 252 passed.
[/list]

[center][i]If you enjoy the mod, an endorsement is appreciated. Bug reports are welcome — please include the relevant UE4SS.log lines.[/i][/center]
```

## 3. 中文版简介

**PBZ Photo Mode（C++）— 影之刃零摄影模式**

给《影之刃零》加一个真正的自由摄影相机：按 F2 进入摄影模式，相机脱离角色，可以自由飞到场景任意位置构图；按住 Alt + 鼠标转视角，WASD 平移，Q/E 升降，滚轮调 FOV，F 冻结时间。

**主要功能**

- 真正的自由相机，不受角色位置限制
- 摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- 时间冻结：进入摄影模式自动暂停时间（`auto_pause_time` 默认开启，可在 `config.ini` 关闭）；角色、敌人、NPC 与特效会真正静止，相机仍可移动
- 可隐藏角色、隐藏 HUD，拍出干净的画面
- 所有键位、速度、灵敏度、FOV 范围、自动暂停开关都可在 `config.ini` 中修改
- 截图请使用 NVIDIA App / GeForce Experience 或 Steam 等系统工具
- 原生 UE4SS C++ Mod，支持 UE4SS 控制台中的 ImGui 面板

**前置要求**

需要《影之刃零》专用 UE4SS（KintaroModding 的 phantombladezero 版本；开发验证版本为 v3.0.1-1183-gcdbc0b66）。不同 UE4SS 版本的 ABI 可能不兼容。

**安装**

解压 `PBZPhotoModeCpp_1.0.14-cpp.zip` 到游戏根目录，形成 `ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll` 结构，进游戏按 F2。

详细说明与源码见：https://github.com/lin-414/PBZPhotoMode
