# Nexus Mods 主页文案（可直接粘贴）

## 1. Short description（短简介，≤255 字符）

```text
Free-camera photo mode for Phantom Blade Zero. Press F2 to enter, Alt + mouse to look, WASD to fly, wheel for FOV, F to freeze time, F9 for screenshots. Lua and UE4SS C++ builds included.
```

## 2. Full description（完整简介，Nexus BBCode 格式）

```bbcode
[center][size=6][b]PBZ Photo Mode[/b][/size]
[size=4]Free-camera photo mode for Phantom Blade Zero[/size]

[i]Frame the shot. Freeze the world. Take the picture.[/i][/center]

[b]PBZ Photo Mode[/b] adds a proper photo mode to Phantom Blade Zero: a camera that detaches from the player, flies freely through the scene, and lets you compose and capture clean screenshots without HUD clutter or the character blocking the view. It does not modify game files and does not touch your save data.

[b][size=5]What you get[/size][/b]
[list]
[*][b]C++ build (recommended)[/b] — a native UE4SS C++ mod with true game-input blocking, game-pause time freeze, configurable via INI, and an optional ImGui control tab inside the UE4SS console.
[*][b]Lua build (no compiler needed)[/b] — a pure Lua UE4SS mod for players who want a simple drop-in install. Same core camera features, text-menu interface.
[/list]

[b][size=5]Features[/size][/b]
[list]
[*][b]True free camera[/b] — detaches from the character and lets you fly anywhere in the level.
[*][b]Full camera control[/b] — look with the mouse, move with WASD, rise/descend with Q/E, boost or slow with Shift/Ctrl.
[*][b]Time freeze[/b] — freeze the action with F. The C++ build uses game pause by default so enemies, NPCs and effects actually stop, while the photo camera keeps moving.
[*][b]Clean shots[/b] — hide the menu with T; optionally hide the player character and the game HUD from the config.
[*][b]Input isolation[/b] — while photo mode is active the game does not receive movement/attack inputs, so you can hold a mouse button to move the camera without swinging a sword. Everything is restored on exit.
[*][b]Fully configurable[/b] — every keybind, move speed, sensitivity, FOV range, freeze mode and UI option can be changed in config.ini (C++) or config.lua (Lua).
[*][b]Screenshot shortcut[/b] — F9 triggers the game's HighResShot command; the command and resolution are configurable.
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
[*][b]F9[/b] — take a screenshot
[/list]
All keys can be rebound in the config file.

[b][size=5]Requirements[/size][/b]
[list]
[*][b]Phantom Blade Zero[/b] (Steam version, UE 5.7)
[*][url=https://github.com/KintaroModding/ue4ss][b]UE4SS — Phantom Blade Zero build[/b][/url]. The C++ build was built and tested against [b]v3.0.1-1183-gcdbc0b66[/b]; use the matching UE4SS build, as other versions may not be ABI-compatible.
[/list]
The Lua build also requires UE4SS, but no compiler or build tools.

[b][size=5]Installation — C++ build[/size][/b]
[list=1]
[*]Install the Phantom Blade Zero build of UE4SS into your game root folder (you should see dwmapi.dll and an ue4ss folder next to the game executable).
[*]Download [b]PBZPhotoModeCpp_1.0.11-cpp.zip[/b] from the Files tab and extract it into the game root folder.
[*]Check that the resulting structure is: [code]ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll[/code] plus config.ini and enabled.txt in the same mod folder.
[*]Launch the game and press [b]F2[/b]. The menu appears in the bottom-right corner.
[/list]

[b][size=5]Installation — Lua build[/size][/b]
[list=1]
[*]Install the same Phantom Blade Zero build of UE4SS.
[*]Copy the [b]PBZPhotoMode[/b] folder into [code]ue4ss/Mods/PBZPhotoMode/[/code]. The folder must contain Scripts/main.lua and Scripts/config.lua.
[*]Make sure enabled.txt exists in the mod root folder.
[*]Launch the game and press [b]F2[/b].
[/list]

[b]Do not enable the Lua and C++ builds at the same time[/b] — both listen for F2 and will fight over the same keys. Pick one.

[b][size=5]Configuration[/size][/b]
[list]
[*][b]C++:[/b] edit [code]ue4ss/Mods/PBZPhotoModeCpp/config.ini[/code]. Invalid values fall back to defaults with a warning in UE4SS.log.
[*][b]Lua:[/b] edit [code]ue4ss/Mods/PBZPhotoMode/Scripts/config.lua[/code]. Restart the game or reload the Lua mod after saving.
[/list]
Useful options include move_speed, look_sensitivity, fov_min/fov_max, freeze_mode (pause or dilation), hide_player, hide_game_hud and all keybinds.

[b][size=5]Troubleshooting[/size][/b]
[list]
[*][b]Nothing happens on F2:[/b] open [code]ue4ss/UE4SS.log[/code] and search for [code][PBZPhotoMode][/code]. Check that the mod folder name matches, that enabled.txt is in the mod root, and that only one version is enabled.
[*][b]Camera does not rotate:[/b] hold Alt while moving the mouse. The C++ build writes input diagnostics to UE4SS.log once per second — check the raw/engine mouse lines there.
[*][b]C++ build does not load:[/b] make sure you are using the Phantom Blade Zero UE4SS build listed above. The DLL imports UE4SS symbols and is not compatible with arbitrary UE4SS versions.
[*][b]Game input stuck after exiting:[/b] press F2 again to toggle out, or restart the game. Please report it with the relevant UE4SS.log lines.
[*][b]No menu but camera works:[/b] press T to show the menu, or enable the UE4SS console and use the ImGui tab.
[/list]

[b][size=5]Known limitations[/size][/b]
[list]
[*]Photo mode only — this is not a replay editor or a cinematic sequence tool.
[*]The C++ build is tied to a specific UE4SS build; a game or UE4SS update may require a recompile.
[*]Screenshot location and resolution depend on the game's HighResShot command and your game settings.
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
[*][b]1.0.11-cpp[/b] — 2026-10-30 — Time freeze now uses game pause by default; verified in-game that enemies, NPCs and effects stop while the camera stays movable.
[*][b]1.0.4-lua[/b] — Final stable Lua release.
[/list]

[center][i]If you enjoy the mod, an endorsement is appreciated. Bug reports are welcome — please include the relevant UE4SS.log lines.[/i][/center]
```

## 3. 中文版简介（用于中文社区 / 备用）

**PBZ Photo Mode — 影之刃零摄影模式**

给《影之刃零》加一个真正的自由摄影相机：按 F2 进入摄影模式，相机脱离角色，可以自由飞到场景任意位置构图；按住 Alt + 鼠标转视角，WASD 平移，Q/E 升降，滚轮调 FOV，F 冻结时间，F9 截图。

**主要功能**

- 真正的自由相机，不受角色位置限制
- 摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- 时间冻结：C++ 版默认使用游戏暂停，角色、敌人、NPC 与特效会真正静止，相机仍可移动
- 可隐藏角色、隐藏 HUD，拍出干净的画面
- 所有键位、速度、灵敏度、FOV 范围都可在 config 中修改
- 提供 C++（UE4SS DLL）与 Lua（免编译）两个版本

**前置要求**

需要《影之刃零》专用 UE4SS（KintaroModding 的 phantombladezero 版本；C++ 版开发验证版本为 v3.0.1-1183-gcdbc0b66）。Lua 版同样需要 UE4SS，但不需要编译。

**安装**

- C++ 版：解压 `PBZPhotoModeCpp_1.0.11-cpp.zip` 到游戏根目录，形成 `ue4ss/Mods/PBZPhotoModeCpp/dlls/PBZPhotoModeCpp.dll` 结构，进游戏按 F2。
- Lua 版：把 `PBZPhotoMode` 文件夹复制到 `ue4ss/Mods/PBZPhotoMode/`，确认 `enabled.txt` 存在，进游戏按 F2。

两个版本不要同时启用。详细说明与源码见：https://github.com/lin-414/PBZPhotoMode
