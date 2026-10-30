# PBZ Photo Mode — 影之刃零摄影模式 Mod

给《影之刃零》（Phantom Blade Zero，UE 5.7）增加**自由摄影相机**的 UE4SS Mod。
不改游戏文件、不动存档，装上即用。

> 按 `F2` 进入摄影模式：右下角弹出菜单，`Alt + 鼠标` 自由转视角，`WASD` 平移，
> 滚轮调 FOV，`F` 冻结时间，`F9` 截图。

## 功能

- **自由相机**：`F2` 进入 / 退出，右下角菜单，`T` 隐藏 / 显示
- **视角控制**：按住 `Alt` + 移动鼠标；`WASD` 平移；`Q` / `E` 升降
- **镜头参数**：滚轮调 FOV，小键盘 `+` / `-` 微调，`R` 一键重置到进入时的视角
- **时间冻结**：`F` 冻结 / 恢复（C++ 版默认用游戏暂停，真正冻结角色、敌人与特效）
- **输入隔离**：摄影模式下屏蔽游戏键鼠输入，退出时完整还原
- **可配置**：键位、移动速度、灵敏度、FOV 范围、冻结方式等全部写在 `config` 里
- **截图**：`F9`（Lua 版支持 `HighResShot`）

## 两个版本

| 版本 | 位置 | 状态 | 说明 |
| --- | --- | --- | --- |
| **Lua 版 v1.0.4** | [`PBZPhotoMode/`](PBZPhotoMode/) | 稳定可用 | 纯 Lua，无需编译，装进 UE4SS 的 Mods 目录即用 |
| **C++ 版 v1.0.11** | [`PBZPhotoModeCpp/`](PBZPhotoModeCpp/) | 主力版本，已编译并在真机验证核心功能 | UE4SS C++ DLL，支持 ImGui 面板、真正的输入吞噬，便于扩展后处理 |

C++ 版是当前主维护版本；Lua 版保留为免编译的备用方案。两版不要同时启用（会抢 `F2`）。

## 安装

### Lua 版

1. 安装影之刃零专用 UE4SS（[KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 的 `phantombladezero` 发行包），整个放进游戏根目录。
2. 把 [`PBZPhotoMode`](PBZPhotoMode/) 文件夹复制到 `游戏目录\ue4ss\Mods\PBZPhotoMode\`。
3. 确认 mod 根目录存在 `enabled.txt`，进游戏按 `F2`。

详细步骤与排错见 [PBZPhotoMode/README.md](PBZPhotoMode/README.md)。

### C++ 版

从 [Releases](../../releases) 下载 `PBZPhotoModeCpp_1.0.11-cpp.zip`，解压到游戏根目录，形成：

```
<游戏目录>\platform\steam\ue4ss\Mods\PBZPhotoModeCpp\
├─ dlls\PBZPhotoModeCpp.dll
├─ config.ini
└─ enabled.txt
```

不需要改 `mods.txt`（`enabled.txt` 即启用）。详细说明见 [PBZPhotoModeCpp/docs/README.md](PBZPhotoModeCpp/docs/README.md)。

## 从源码构建（C++ 版）

核心层单元测试不需要游戏与 UE4SS：

```bat
cd PBZPhotoModeCpp
tools\build_tests.bat          :: 252 项断言全绿
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
├─ PBZPhotoMode/            # Lua 版（v1.0.4）
│  ├─ Scripts/
│  │  ├─ main.lua
│  │  └─ config.lua
│  ├─ enabled.txt
│  ├─ README.md
│  └─ 测试清单.md
├─ PBZPhotoModeCpp/         # C++ 版（v1.0.11）
│  ├─ src/core/             # 纯逻辑层（配置/输入/状态机/菜单模型，含单元测试）
│  ├─ src/engine/           # UE4SS 引擎适配层
│  ├─ src/mod_app.cpp       # CppUserModBase 入口
│  ├─ tests/                # 252 项核心层断言
│  ├─ tools/                # 构建 / 打包脚本
│  └─ docs/                 # 构建、安装、核对清单
├─ docs/
│  └─ PBZPhotoModeCPP_PLAN.md  # C++ 迁移方案与架构记录
├─ README.md
└─ .gitignore
```

## 版本记录

- **C++ v1.0.11**（2026-10-30）：`F` 冻结改为游戏暂停（`pause`），真机验证：暂停期间相机仍可移动，解冻正常恢复。
- **Lua v1.0.4**：最终稳定版，已按多轮真机日志修复输入、菜单、视角问题。

C++ 版逐版本修复记录（含每次真机日志的根因分析）见 [PBZPhotoModeCpp/PROGRESS.md](PBZPhotoModeCpp/PROGRESS.md)。

## 致谢

- [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) — Unreal Engine 脚本系统与 C++ Mod 加载器
- [KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) — 影之刃零专用分支

## 说明

本项目仅供单机游戏摄影与学习交流使用；使用 Mod 的风险由使用者自行承担。
