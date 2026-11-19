# 接入 Mods Menu 149（SLEEP）— 实现与维护说明

> 目标菜单：[Mods Menu - In-Game Mod Settings](https://www.nexusmods.com/phantombladezero/mods/149)（SLEEP，v0.9.x）
> 可选桥接：[Mods Menu - UE4SS Bridge](https://www.nexusmods.com/phantombladezero/mods/149?tab=files)（v0.3.1+）
> 本模组：PBZPhotoModeCpp（UE4SS C++ DLL）

## 1. 为什么走 UE4SS Bridge

Mods Menu 149 给模组作者留了三条接入路线（见其《guide for mod authors》）：

| 路线 | 适用 | 本模组 |
| --- | --- | --- |
| JavaScript 模组（`PBZModsMenu\Mods\<Id>\main.js`） | 由 Mods Menu 自带加载器运行的 JS 模组 | 不适用：我们是 UE4SS C++ DLL |
| **UE4SS 模组 + Bridge** | Lua / C++ UE4SS 模组 | **采用** |
| shared config hub（protocol 1） | 已向共享配置中心发布设置的脚本模组 | 不适用（未实现该协议） |

结论：本模组必须在玩家安装 **Mods Menu + UE4SS Bridge** 时才能出现在菜单里。没有 Bridge 时，Mods Menu 对 UE4SS 模组的设置是只读，且仅靠 `enabled.txt` 启用的模组根本不会列出——所以 Bridge 实际上是本模组接入的硬前置（Mods Menu 本体仍可不装）。

## 2. 文件与数据流

所有路径位于 `PBZ\platform\steam\ue4ss\Mods\`：

```
Mods\
  PBZPhotoModeCpp\
    PBZPhotoModeCpp.modsmenu.json      ← 随包分发（设置行声明，含 en / zh-Hans 文案）
    PBZPhotoModeCpp.config.json        ← Mods Menu 的 68 项菜单子集；首次运行由模组创建，之后由 Bridge 写入
    PBZPhotoModeCpp.settings.json      ← 全量配置、唯一真源（所有原 config.ini 字段）
  PBZModsMenuBridge\
    events-PBZPhotoModeCpp.txt         ← Bridge 保存后写 Changed=<n> / End=1，本模组轮询
    pending-PBZPhotoModeCpp.json       ← Bridge 内部用（菜单 → Bridge → config.json）
    saved-PBZPhotoModeCpp.txt          ← Bridge 内部用（保存确认）
```

数据流（一次菜单改动）：

1. 玩家在游戏设置 → Mods Menu → PBZ Photo Mode 里改一项；
2. 菜单把完整 JSON 写到 `PBZModsMenuBridge\pending-PBZPhotoModeCpp.json`；
3. Bridge 校验后原子替换 `PBZPhotoModeCpp.config.json`，再写 `events-PBZPhotoModeCpp.txt`（`Changed=<毫秒>`、`End=1`）；
4. 本模组引擎 Tick 每 250ms 读 events，读到更大的 `Changed` 后解析菜单子集 `config.json`；
5. `core/mods_menu.cpp` 把菜单值覆盖到 `Config`（越界夹紧、未知键忽略），`mod_app` 应用并 `save_config_json_file()` 写回全量 `settings.json`；
6. 应用后本模组再次把 `settings.json` 的 68 项镜像回 `config.json`，保证两边一致。

反向（PBZFM 菜单 / 快捷键 / 控制台 / 手动改 `settings.json` 后重载）：`reload_config()`、`pbzfm_apply_pending()` 与 `apply_config_from_mods_menu()` 都会调用 `ModsMenuClient::write_config_json()`，把最新值同步进菜单子集 JSON；Mods Menu 改值也会回灌 PBZFM 的显示副本。

升级：3.0 首次启动时若 `settings.json` 不存在但旧 `config.ini` 存在，自动读旧 INI、写出 `settings.json`，并把旧文件改名为 `config.ini.migrated`。

## 3. Descriptor（`PBZPhotoModeCpp.modsmenu.json`）

格式取自 Mods Menu 作者指南：

```json
{
  "title": { "en": "PBZ Photo Mode", "zh-Hans": "影之刃零摄影模式" },
  "description": { "en": "...", "zh-Hans": "..." },
  "version": "2.2.0",
  "settings": [
    { "type": "heading", "label": { "en": "Behavior", "zh-Hans": "行为" } },
    { "key": "auto_pause_time", "type": "toggle", "label": { "en": "...", "zh-Hans": "..." }, "default": true, "help": { "en": "...", "zh-Hans": "..." } },
    { "key": "freeze_mode", "type": "choice", "label": { "en": "Freeze mode", "zh-Hans": "冻结方式" }, "default": "pause",
      "options": [ { "value": "dilation", "label": { "en": "Slow motion", "zh-Hans": "慢动作" } },
                   { "value": "pause", "label": { "en": "True pause", "zh-Hans": "真暂停" } } ] },
    { "key": "move_speed", "type": "number", "label": { "en": "Move speed", "zh-Hans": "移动速度" }, "default": 400, "min": 50, "max": 2000, "step": 10 }
  ]
}
```

约束：

- `title` 最长 80、`description` 300、`label` 80、`help` 400；当前只支持 `en` / `zh-Hans` / `zh-Hant`（缺省回退英文）；
- 每个非 heading 项必须有 `key`（`[A-Za-z0-9_]{1,64}`）；
- `number` 的 `default` 必须在 `[min, max]` 内，`step > 0`；
- `choice` 的 `default` 必须命中一个 option 的 `value`；
- 模组最多 100 个 settings（本模组 75 项：7 个 heading + 68 项设置）。

> **范围决定（2.2.0）**：只保留 41 项基础设置 + 27 个键位 choice。景深详细参数与画面效果/调色不进入 Mods Menu——实机确认摄影模式内打不开游戏设置菜单，这些参数只在取景时需要调，放进菜单反而无法边看边调；改由 PBZFM（F8）或 `settings.json` 承载。

## 4. 暴露字段（68 项）

| 分组 | 字段 |
| --- | --- |
| 行为 | `auto_pause_time`、`block_game_input`、`wheel_fov`、`wheel_fov_invert`、`ultrawide_unlock`、`takeover_current_camera`、`pad_enabled`、`freeze_mode`（choice） |
| 隐藏 | `hide_player`、`hide_game_hud`、`hide_interact_prompt`、`hide_enemy_health_bars`、`hide_lockon_marker`、`hide_subtitles` |
| 冻结 | `freeze_sequences`、`freeze_dialogue_flow`、`stop_wwise_on_freeze`、`wwise_engine_suspend`、`stop_camera_shakes_on_freeze`、`freeze_wind`、`freeze_mpc`、`freeze_cloth`、`freeze_skeletal_animation`、`freeze_motion_blur_off` |
| 相机与移动 | `fov_default`、`fov_step`、`move_speed`、`boost_mult`、`slow_mult`、`slow_look_mult`、`look_sensitivity`、`pitch_limit`、`vertical_speed`、`roll_speed`、`pitch_speed`、`yaw_speed`、`mouse_vertical_mode`（choice） |
| 景深 | `dof_enabled`（仅总开关；详细参数不进入 Mods Menu） |
| 手柄 | `pad_look_speed`、`pad_move_scale`、`pad_deadzone` |
| 快捷键（键盘，末尾） | `key_toggle`、`key_freeze`、`key_frame_freeze`、`key_reset`、`key_roll_reset`、`key_fov_up`、`key_fov_down`、`key_pitch_up`、`key_pitch_down`、`key_yaw_left`、`key_yaw_right`、`key_roll_left`、`key_roll_right`、`key_vertical_up`、`key_vertical_down`、`key_move_forward`、`key_move_back`、`key_move_left`、`key_move_right`、`key_boost`、`key_slow`、`key_free_look`、`key_dof_toggle`、`key_focus_near`、`key_focus_far`、`key_aperture_open`、`key_aperture_close`（全部 choice） |

**刻意不暴露**：

- 键位候选范围：Mods Menu 的键位行是**自定义 choice**（每行 64 个常用键名，含 `MOUSE_LEFT/RIGHT/MIDDLE/X1/X2`），不是任意键捕获；列表外的键请用可选的 PBZFM 菜单（F8，「快捷键（键盘）」页，捕获式改键）或直接手改 `PBZPhotoModeCpp.settings.json`；手柄组合键（`LS+RS`、`LB+RB` 等）仍需手改 `pad_*` 字段；
- 景深详细参数（`dof_focal_distance`、`dof_fstop`、`dof_sensor_width`、`dof_focus_step`、`dof_aperture_step`）不在 Mods Menu：摄影模式内打不开游戏设置菜单，菜单里调不了取景参数；请用 PBZFM（F8）的景深页或直接编辑 `settings.json`；
- 画面效果/调色（`vignette`、`saturation`、`contrast`、`bloom`、`temperature`、`chromatic_aberration`、`color_gain`、`color_gamma`、`color_offset` 与 5 个 `*_enabled` 开关）不在 Mods Menu，原因同上；它们是**会话级参数**，进入和退出摄影模式都会恢复默认（`reset_look_defaults`），需要长期保存请用 PBZFM 的「保存预设」（`look_preset_<编号>.ini`）；
- 部分高级/诊断字段（如 `fov_min`、`fov_max`、`invert_y`、`free_look_requires_alt`、`dof_bokeh_gate`、高级 `freeze_*` A/B 开关、`freeze_diag`、`imgui_tab`、`menu_language`、`log_level` 等）不在 Mods Menu：描述符 100 项上限（当前 75 项）装不下完整 138 个配置字段，PBZFM 也不显示这些项，请直接编辑 `settings.json`；
- 动作行（action）：会引入 `ModsMenuAction_*` 与 `done-<name>.txt` 的额外协议，本期不做。

## 5. 代码结构

| 文件 | 作用 |
| --- | --- |
| `assets/PBZPhotoModeCpp.modsmenu.json` | 描述符源文件；`tools/pack.py` 打包时复制到包根 |
| `src/core/config_json.h/.cpp` | 全量 JSON 配置读写（`PBZPhotoModeCpp.settings.json`），复用 INI 解析器的字段/范围/弃用键逻辑 |
| `src/core/mods_menu.h/.cpp` | 纯核心：扁平 JSON 解析/序列化、68 项菜单字段表（41 个基础设置 + 27 个键位 choice；与内部字段表 `pbzfm_fields()`（历史文件名）共用读写器）、Config ↔ 菜单 JSON 往返；随 `pbzpm_core` 单测 |
| `src/mods_menu_client.h/.cpp` | DLL 侧：Bridge 检测、菜单子集 config.json 创建/镜像、250ms events 轮询；只在 `PBZPM_WITH_UE4SS` 编译 |
| `src/mod_app.cpp` | `reload_config()` 读 `settings.json` / 迁移旧 `config.ini`；`on_engine_tick()` 轮询并 `apply_config_from_mods_menu()` 写回 `settings.json` |
| `tests/test_mods_menu.cpp` / `tests/test_config_json.cpp` | JSON、字段一致性、越界/坏值、Config 往返、未知键保留、全字段往返 |

启动检测：`ModsMenuClient::start()` 只检查两件事——descriptor 存在、`Mods\PBZModsMenuBridge` 目录存在。任一不满足就 `active_=false`，不创建任何文件、不影响原快捷键模式。

## 6. 维护清单

- **加字段**：在 `core/mods_menu.cpp::mods_menu_settings()` 加 key/kind（该 key 必须已存在于 `pbzfm_fields()`），在 `PBZPhotoModeCpp.modsmenu.json` 加对应行，跑 `tests\build_tests.bat`；`test_field_table_consistency` 会检查字段表存在与类型一致。
- **改范围/默认值**：范围以内部字段表 `pbzfm_fields()` 为准（与旧 INI 解析共用），descriptor 的 `min/max/step/default` 必须同步；改完运行 `Python\bin\python.exe tools\check_modsmenu_descriptor.py`（结构校验 + 与 `config.ini.example` 默认值比对）。
- **Bridge 协议变更**：以 SLEEP 的《guide for mod authors》为准；events 必须 `End=1` 才读，`Changed` 单调递增。
- **不打包**：`PBZPhotoModeCpp.config.json` 与 `PBZPhotoModeCpp.settings.json` 都绝不随包分发（会重置玩家设置）；`PBZPhotoModeCpp.modsmenu.json` 必须随包分发。
- **卸载兼容**：只删 Mods Menu/Bridge 时，`config.json` 留在模组目录不影响本模组运行（本模组仍读 `settings.json`）。

## 7. 验证清单

1. 未装 Mods Menu：启动游戏、F2 进摄影模式正常，日志无 Mods Menu 相关错误；
2. 装 Mods Menu + Bridge：设置 → Mods Menu → 出现「PBZ Photo Mode」分区、7 个分组、68 项（末尾「快捷键（键盘）」27 行；不含景深详细参数与画面效果/调色）；标题/说明/帮助中文正常；
3. 改 `move_speed` / `hide_game_hud` / `freeze_mode`：几秒内生效（进摄影模式看移动速度 / HUD / 冻结方式），`settings.json` 对应值已更新；
4. 重启游戏：菜单显示上次保存值；`settings.json` 未被重置；
5. 「Restore defaults」：恢复 descriptor 里的默认值，`settings.json` 同步；
6. 手改 `settings.json` 后重载：Mods Menu 重新打开后显示新值（JSON 镜像生效）；
7. 只装 Mods Menu 不装 Bridge：本模组不出现在列表（预期，Bridge 是硬前置）。
