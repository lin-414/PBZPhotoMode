# PBZ Photo Mode — 影之刃零摄影模式 Mod（C++）

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS C++ Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV；进入时自动暂停时间，`F` 可随时冻结/恢复。纯快捷键操作，游戏内无菜单界面。

## 功能

- **自由相机**：`F2` 进入 / 退出
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；左键上移 / 右键下移；`Q` / `E` 滚转（E 顺时针 / Q 逆时针）；手柄 `RB` / `LB` 同样滚转
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **时间冻结**：进入摄影模式自动暂停时间（默认开启，可在 `config.ini` 关闭）；`F` 随时冻结 / 恢复（默认使用游戏暂停，真正冻结角色、敌人与特效）
- **超宽屏支持**：进入摄影模式自动解除 16:9 画幅约束（游戏官方支持 21:9 / 32:9 超宽屏），两侧黑边消除，退出时还原
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **隐藏 HUD**：进入摄影模式自动隐藏游戏 HUD（血条/快捷栏/任务提示等），退出时恢复原可见性
- **隐藏交互提示**：进入摄影模式自动隐藏场景互物体旁的“E”键图标（世界空间 UMG），退出时恢复
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `config.ini` 里

## 当前版本

**1.0.23-cpp** —— 已编译：

- **FOV 按住连发**：手柄十字键上/下、键盘 FOV 键（默认 `+` / `-`）单击仍是一格；按住约 0.35 秒后按 0.08 秒间隔连发，持续变焦不用连点
- **手柄滚转**：`RB` / `LB` 滚转相机（RB 顺时针 / LB 逆时针，与键盘 Q/E 同向）；原 LB/RB 变速移到摇杆按下（L3 加速 / R3 减速；两杆同按仍是进出模式组合键，不触发变速）
- **相机滚转**：`Q` / `E` 改为滚转（E 顺时针 / Q 逆时针，`roll_speed` 默认 60 度/秒，Shift/Ctrl 变速同样生效）；鼠标左键上移 / 右键下移保持不变，手柄 RT/LT 升降不变，`R` 重置含滚转角
- **图形菜单代码彻底移除**：游戏内无任何菜单界面，纯快捷键操作（F2 进入/退出、F 冻结、R 重置、滚轮 FOV、WASD/鼠标左右键/Shift/Ctrl 移动，键位均可配）；仅保留 UE4SS 控制台里的诊断面板
- 进入摄影模式自动隐藏**交互提示**（场景互物体旁的“E”键图标，`hide_interact_prompt` 默认 `true`，可在 `config.ini` 关闭），退出还原
- 超宽屏（21:9 / 32:9）支持：进入摄影模式自动解除 16:9 画幅约束，消除两侧黑边（`ultrawide_unlock` 默认 `true`，可在 `config.ini` 关闭），退出时还原
- DualSense / DualShock4 原生手柄支持（DInput8 轮询后端，L3+R3 进摄影模式）
- 进入摄影模式自动暂停时间（`auto_pause_time` 默认 `true`）、自动隐藏游戏 HUD
- 核心层 521 项单元测试全部通过
- 产物：`PBZPhotoModeCpp_1.0.23-cpp.zip`

## 安装

需要《影之刃零》专用 UE4SS（[KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 的 `phantombladezero` 发行包，开发验证版本 `v3.0.1-1183-gcdbc0b66`）。

从 [Releases](https://github.com/lin-414/PBZPhotoMode/releases) 下载 `PBZPhotoModeCpp_1.0.23-cpp.zip`，解压到游戏根目录，形成：

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
