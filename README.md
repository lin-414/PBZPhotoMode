# PBZ Photo Mode — 影之刃零摄影模式 Mod（C++）

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS C++ Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：右下角弹出菜单，`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV；进入时自动暂停时间，`F` 可随时冻结/恢复。

## 功能

- **自由相机**：`F2` 进入 / 退出，右下角菜单，`T` 隐藏 / 显示
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；`Q` / `E` 升降
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **时间冻结**：进入摄影模式自动暂停时间（默认开启，可在 `config.ini` 关闭）；`F` 随时冻结 / 恢复（默认使用游戏暂停，真正冻结角色、敌人与特效）
- **超宽屏支持**：进入摄影模式自动解除游戏的 16:9 画幅锁定，21:9 / 32:9 显示器不再有左右黑边，退出时还原
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **隐藏 HUD**：进入摄影模式自动隐藏游戏 HUD（血条/快捷栏/任务提示等），退出时恢复原可见性
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `config.ini` 里

## 当前版本

**v1.5.0-cpp** —— 已编译：

- 超宽屏（21:9 / 32:9）支持：进入摄影模式自动解除游戏 16:9 画幅锁定，消除两侧黑边（`ultrawide_unlock` 默认 `true`，可在 `config.ini` 关闭），退出时还原
- DualSense / DualShock4 原生手柄支持（DInput8 轮询后端，L3+R3 进摄影模式）
- 进入摄影模式自动暂停时间（`auto_pause_time` 默认 `true`）、自动隐藏游戏 HUD
- 核心层 527 项单元测试全部通过
- 产物：`PBZPhotoModeCpp_1.5.0-cpp.zip`

## 安装

需要《影之刃零》专用 UE4SS（[KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 的 `phantombladezero` 发行包，开发验证版本 `v3.0.1-1183-gcdbc0b66`）。

从 [Releases](https://github.com/lin-414/PBZPhotoMode/releases) 下载 `PBZPhotoModeCpp_1.5.0-cpp.zip`，解压到游戏根目录，形成：

```
<游戏目录>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\PBZPhotoModeCpp.dll
├─ config.ini
└─ enabled.txt
```

不需要改 `mods.txt`（`enabled.txt` 即启用）。详细说明见 [PBZPhotoModeCpp/docs/README.md](PBZPhotoModeCpp/docs/README.md)。

## 从源码构建

核心层单元测试不需要游戏与 UE4SS：

```bat
cd PBZPhotoModeCpp
tools\build_tests.bat          :: 298 项断言全绿
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
├─ PBZPhotoModeCpp/         # UE4SS C++ Mod（v1.5.0）
│  ├─ src/core/             # 纯逻辑层（配置/输入/状态机/菜单模型，含单元测试）
│  ├─ src/engine/           # UE4SS 引擎适配层
│  ├─ src/mod_app.cpp       # CppUserModBase 入口
│  ├─ tests/                # 298 项核心层断言
│  ├─ tools/                # 构建 / 打包脚本
│  └─ docs/                 # 构建、安装、核对清单
├─ docs/
│  └─ NEXUS_DESCRIPTION.md  # Nexus Mods 主页文案
├─ README.md
└─ .gitignore
```

## 版本记录

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
