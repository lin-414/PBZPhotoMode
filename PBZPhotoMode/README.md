# PBZ Photo Mode —— 影之刃零 摄影相机 Mod

给《影之刃零》(Phantom Blade Zero, UE 5.7) 加一个**自由摄影相机**：
按 `F2` 右下角弹出菜单、按住 `Alt` 用鼠标转视角、`WASD` 平移、滚轮调 FOV、`F` 冻结时间、`T` 隐藏菜单。

纯 Lua 实现，基于 [KintaroModding/ue4ss](https://github.com/KintaroModding/ue4ss) 这个针对影之刃零的 UE4SS 分支，
**不需要** 改游戏文件、不用 C++ 编译、不动存档。

```
PBZPhotoMode/
├── enabled.txt       ← 存在即启用（等价于在 mods.txt 里写 PBZPhotoMode : 1）
├── README.md
└── Scripts/
    ├── main.lua      ← mod 本体（必须叫这个名字、放在 Scripts 小写目录）
    └── config.lua    ← 所有可调参数，改这个文件就行
```

---

## 一、前置条件

1. 先装好 **UE4SS**（影之刃零专用分支的发行版）：

   - 下载页：<https://github.com/KintaroModding/ue4ss/releases>
   - 取带 `phantombladezero` 字样的压缩包（例如 `phantombladezero_ue4ss_v3.0.1-1183-gcdbc0b66.zip`）
   - 解压后应该看到 `dwmapi.dll` 和 `ue4ss\` 文件夹，**整个丢进游戏根目录**
     （和 `PBZ.exe`、`PBZ-Win64-Shipping.exe` 同级，也就是 `...\Steam\steamapps\common\Phantom Blade Zero\`）
   - 游戏是 `platform\steam\` 这种结构的话，就放在外层那个含 `PBZ.exe` 的目录里

2. 启动一次游戏，确认 `游戏目录\ue4ss\UE4SS.log` 有内容、末尾能看到
   `Starting Lua mod 'shared'` 之类字样 —— 说明 UE4SS 工作正常。

   > 这个包里 **看不到控制台窗口是正常的**：`UE4SS-settings.ini` 里
   > `ConsoleEnabled = 0`、`GuiConsoleEnabled = 0`。所有输出都写进 `ue4ss\UE4SS.log`。
   > 想临时开控制台，把这两项改成 `1`（或者按 `Ctrl+O` 切换 GUI 控制台）。

---

## 二、安装这个 mod

1. 把整个 `PBZPhotoMode` 文件夹复制到：

   ```
   游戏目录\ue4ss\Mods\PBZPhotoMode\
   游戏目录\ue4ss\Mods\PBZPhotoMode\Scripts\main.lua
   游戏目录\ue4ss\Mods\PBZPhotoMode\Scripts\config.lua
   ```

   ⚠️ 目录名大小写要一致：`Scripts` 首字母大写、`main.lua` 全小写。放错层级是最常见的“没反应”原因。

2. 启用它，二选一：

   - **方式 A（推荐）**：确认 `ue4ss\Mods\PBZPhotoMode\enabled.txt` 存在（内容随意，空文件也行）。
     注意是 **mod 根目录**，不是 `Scripts\` 里面。
   - **方式 B**：编辑 `游戏目录\ue4ss\Mods\mods.txt`，在 `Keybinds : 1` 之前加一行：

     ```
     PBZPhotoMode : 1
     ```

     > 这个分支里 **`mods.txt` 的优先级高于 `mods.json`**。文件里 `;` 开头会整行注释掉，
     > 行内空格会被去掉，短于 4 个字符的行会被跳过 —— 所以别写成 `PBZ : 1`。

3. 进游戏（走到能自由行动的场景），按 `F2`。右下角出现菜单即成功。
   失败就看 `ue4ss\UE4SS.log`，搜 `PBZPhotoMode`。

---

## 三、按键

| 按键 | 作用 |
| --- | --- |
| `F2` | 打开 / 关闭摄影模式（进出自由相机，菜单自动弹出 / 收起） |
| `Alt` + 鼠标 | 按住 Alt 自由转视角（松开 Alt 鼠标不动相机） |
| `W` `A` `S` `D` | 前后左右平移 |
| `Shift` / `Ctrl` | 加速 4 倍 / 减速（精调构图） |
| `Q` / `E` | 下降 / 上升 |
| `↑` `↓` `←` `→` | 备用输入源下的安全平移键（游戏基本不会占用这四个键） |
| `PageUp` / `PageDown` | 备用输入源下的升降（同样是为了避开游戏绑定） |
| **鼠标左键 / 右键** | 上升 / 下降（长按持续移动） |
| **鼠标滚轮** | 调 FOV（视野），每格 2°，范围 15°–150° |
| `T` | 显示 / 隐藏右下角菜单（拍照前先按 T 清屏） |
| 小键盘 `+` / `-`，主键盘 `=` / `-` | 键盘调 FOV（滚轮不可用时的备用） |
| `F` | 冻结 / 恢复时间（角色、特效、粒子一起静止，相机仍可自由移动） |
| `F9` | 快门：自动藏掉菜单，让引擎自己截一张图到 `PBZ/Saved/Screenshots`，拍完菜单回来 |

菜单里标题右侧的 `live` / `FROZEN` 就是冻结状态；`F2=exit` 是退出提示。

**进入摄影模式后，角色和 UI 会被这样处理（都会在退出时还原）：**

1. `PlayerController:SetIgnoreMoveInput(true)` —— 用引擎自带的"忽略移动输入"开关。
   `AddInputVector` 这条路上来的移动直接被丢弃，所以按 `WASD` 既不走人、也不播走路动画，
   而**鼠标视角完全不受影响**（这个开关只管移动，不管 look）。
2. 万一引擎里没有这个函数，才退化到把 `CharacterMovement` 的 `MaxWalkSpeed` / 加速度清零并停惯性。
   日志里会写清楚用了哪一种。
3. 主角模型会被隐藏（`SetActorHiddenInGame`）—— 镜头已经和角色脱开了，角色留在原地只会挡构图。
   不想要就把 `hide_player` 改成 `false`。
4. 想连血条 / 小地图一起藏掉，把 `hide_game_hud` 改成 `true`（默认关，免得玩家不适应）。

> 键位都可以在 `config.lua` 里改。滚轮在某些输入配置下拿不到时，
> 默认还能用小键盘 `+` / `-`（`ADD` / `SUBTRACT`）和主键盘 `=` / `-`（`OEM_PLUS` / `OEM_MINUS`）调 FOV。
>
> **Alt 只在能拿到引擎原始输入时才生效**（日志 `-> 使用 raw`）。如果日志显示 `-> 使用 look_deg`，
> 说明这个游戏里拿不到原始按键事件，检测不到 Alt 是否按住 —— 这时自由视角是**常开**的
> （移动鼠标就转视角，不用按 Alt），并且请改用方向键 / PageUp / PageDown 移动相机，
> 因为这条路上游戏自己的 WASD 和鼠标左右键仍会响应。

---

## 四、配置

所有参数在 `Scripts\config.lua`，改完重启游戏生效。（如果 `ue4ss\UE4SS-settings.ini` 里把 `EnableHotReloadSystem` 打开，还可以按 `Ctrl+R` 就地重载所有 Lua mod —— 这项默认是关的。）
文件里每一项都有中文注释，这里只讲最常调的几项：

| 配置 | 默认 | 说明 |
| --- | --- | --- |
| `key_toggle` | `"F2"` | 开关摄影模式 |
| `key_menu` | `"T"` | 菜单显隐 |
| `key_freeze` | `"F"` | 时间冻结 |
| `look_sensitivity` | `0.10` | 鼠标灵敏度，转太快就调小 |
| `invert_y` | `false` | 上推鼠标镜头却往下走时改 `true` |
| `move_speed` | `400.0` | 平移速度（UE 单位 = 厘米/秒） |
| `fov_step` | `2.0` | 滚轮一格多少度 |
| `key_fov_up` / `key_fov_down` | `ADD` / `SUBTRACT` | 键盘调 FOV |
| `tap_step` / `tap_step_vertical` | `120` / `60` | 备用输入源下"点一下走一步"的步长（厘米） |
| `menu_anchor` | `"bottom_right"` | 改 `"top_left"` 回到左上角原生位置 |
| `menu_pad_top` / `menu_pad_left` | `-1` | 菜单位置微调；`-1` = 按分辨率自动算 |
| `block_game_input` | `true` | 摄影时屏蔽游戏自身按键（见下方说明） |
| `ignore_move_input` | `true` | 首选：让 PlayerController 忽略移动输入（不影响鼠标转视角） |
| `freeze_character` | `true` | 兜底：上一个不可用时才清零 `MaxWalkSpeed`（退出时还原） |
| `hide_player` | `true` | 进入摄影模式时隐藏主角模型（退出时恢复） |
| `hide_game_hud` | `false` | 进入时连游戏 HUD 一起隐藏 |
| `key_shutter` | `"F9"` | 快门键 |
| `screenshot_command` | `"HighResShot 1"` | 快门执行的控制台命令；留空 = 禁用（`HighResShot 2` 可出 2 倍分辨率） |
| `auto_block_verify_ms` | `1200` | 备用源下"摘了输入组件后验证视角还活着"的窗口（毫秒） |
| `look_deg_free_look_always` | `true` | 备用输入源下自由视角常开（检测不到 Alt 时的兜底） |
| `pop_input_in_look_mode` | `false` | 备用输入源下也摘掉角色输入组件（想彻底不误触攻击时打开） |
| `freeze_time_dilation` | `0.0001` | 冻结时的时间缩放（UE 最小值，等效完全静止） |

**键名两套写法，别混：**

- `key_toggle` / `key_menu` / `key_freeze` / `key_fov_up` 用的是 UE4SS 的 **Key 枚举名**，
  不带 `Key.` 前缀：`F1`…`F12`、`T`、`NUM_ONE`、`ADD`、`SUBTRACT`、`PAGE_UP`、`OEM_PLUS`…
- `hold_key_free_look` / `hold_key_fast` / `hold_key_slow` 用的是 Unreal 的 **FKey 名**：
  `LeftAlt`、`LeftShift`、`LeftControl`、`RightMouseButton`、`MiddleMouseButton`…

**`block_game_input` 三种取值：**

- `true`（默认）：进入摄影模式时把角色的输入组件从输入栈里临时摘掉，
  按左键不会挥刀、滚轮不会换武器、WASD 不会让角色走（因为 WASD 被用来移相机了）。
- `false`：完全不碰游戏输入。适合“退出后角色不能动”之类的异常情况。
- `"ignore"`：额外调用 `SetIgnoreMoveInput/SetIgnoreLookInput`。**仅在“摄影模式下游戏视角
  还被鼠标带着转”时作为补救**——注意它有可能连带屏蔽送进 mod 的鼠标轴，导致转不了视角。

---

## 五、常见问题

**1. 按 F2 没反应**
- `enabled.txt` 是否在 `Mods\PBZPhotoMode\` 里（不是 `Scripts\`）？`mods.txt` 是否写了 `PBZPhotoMode : 1` 且在 `Keybinds : 1` 之前？
- 日志里有没有 `PBZPhotoMode` 字样：`游戏目录\ue4ss\UE4SS.log`。
  - 有 `[PBZPhotoMode] 就绪：游戏内按 F2 打开摄影模式` → mod 起来了，问题在键位。
  - 完全没有 → 路径/开关不对（`Scripts` 大小写、层级多套了一层）。
- 可以先把 `config.lua` 的 `debug_log_input = true` 打开，看按键事件有没有进日志。

**2. 菜单出现了，但按住 Alt 转不了视角**
- 这个分支的 UE4SS 里 `ProcessConsoleExec` 相关的控制台 hook 是失效的，但按键 hook 正常。
  mod 会依次尝试几种输入源（见“技术说明”），日志里有一行会写清楚用的是哪一种：

  ```
  [PBZPhotoMode] 输入来源: InputKey=true InputAxis=true 备用(Pawn yaw=false pitch=false / PC yaw=false pitch=false) -> 使用 raw
  ```

  - `-> 使用 raw`：拿到了引擎原始输入（最理想：鼠标轴、滚轮、按键按住状态全都有）
  - `-> 使用 look_deg`：走游戏自己的视角输入。`Pawn:AddControllerYaw/PitchInput` 和
    `PlayerController:AddYawInput/AddPitchInput` **四个钩子会全部挂上**，
    每帧按“Pawn 层优先、Pawn 层没值才吃 PC 层”仲裁一次，所以既不会转双倍，
    也不会因为游戏只走了其中一层而完全不动。
    此时鼠标转视角正常、滚轮走键盘 `+`/`-`，**移动改成"点一下走一步"**
    （`tap_step` / `tap_step_vertical`），因为这种输入源没有"按住"状态。
  - `-> 使用 none`：全都没拿到，日志会再打两行警告，此时只能平移（点按）+ 键盘调 FOV
- **先确认视口真的切过去了**：日志里搜 `视口切换成功`。
  - 有 `视口切换成功：PlayerController:SetViewTargetWithBlend(5)` → 视口已接管，视角还不动就是输入源问题，往下看。
  - 一条都没有、只有 `切视口失败` → 把失败那几行（`切视口失败 ... -> xxx`）发出来。
    mod 会依次试 `SetViewTargetWithBlend`(5 参实参)/(4 参实参)、`ClientSetViewTarget`、
    `PlayerCameraManager:SetViewTarget`，四条全败才会这样。
  - `-> 使用 look_deg` 时若转视角还是不动，把 `config.lua` 的 `debug_log_input` 设成 `true`，
    日志里会打出每一帧收到的角度增量（`Pawn:AddControllerYawInput: 0.23` 这种），能直接看出是哪层没送到。
- 若日志说用原生输入、但视角还是不动：把 `block_game_input` 从 `"ignore"` 改回 `true`（或相反试一次）。

**2.5 按 WASD 是人物在动，不是相机在动**
- 1.0.3 起进入摄影模式会自动冻结角色移动，正常情况下已经不会发生。看日志确认：
  - `已冻结角色移动：WASD 不会再把人推走` → 这一步生效了，人物不可能再走。
  - `已屏蔽游戏按键：摘掉角色输入组件后视角输入照常可用（视角层=xxx）` → 最理想，
    游戏连攻击键都不响应了。
  - `摘掉角色输入组件后视角输入断了，已还原（视角层=xxx）` → 你这个游戏把视角输入
    也挂在角色输入组件上，所以不能摘；此时角色移动仍是冻结的，只是鼠标左右键可能
    还会触发攻击（用方向键 / PageUp / PageDown 操作相机就不会误触）。
- 若确实想让游戏彻底不响应任何按键（代价是鼠标也不能转视角），把
  `pop_input_in_look_mode` 改成 `true`。

**3. 时间冻结了但菜单/相机也卡住**
- 不会发生：mod 用的是 `SetGlobalTimeDilation(0.0001)` 而不是 `SetPause`，
  引擎仍在 tick。如果你自己装了别的暂停类 mod，可能互相打架，先关掉它试。

**4. 菜单挡住了画面 / 位置偏了**
- 菜单是用引擎的屏幕调试文字画的（引擎调试字体**没有中文字形**，所以菜单是英文 + ASCII 边框）。
- 位置靠“空行 + 空格”把文字推到右下角，是基于 16:9、1080p 左右的字宽/行高估算的。
- 分辨率或字体缩放不同导致偏移时，直接设 `menu_pad_top` / `menu_pad_left` 为具体数字微调
  （每 +1 约等于挪一个字符格子）。也可以 `menu_anchor = "top_left"` 回到原生位置。

**5. 退出后角色不能动 / 视角没还回去**
- mod 退出时会：销毁相机、把视口目标还给 Pawn、把摘掉的输入组件塞回去、恢复时间缩放。
- 万一卡住（比如切换场景时异常），mod 注册了 `RegisterLoadMapPostHook`，**读图/换场景会自动退出摄影模式**，
  不会把坏状态带过去。
- 仍然异常时，把 `block_game_input` 设为 `false` 再试，这是最保守的配置。

**6. 会不会封号 / 影响存档**
- 纯客户端内存层的 Lua mod，不改存档、不改游戏文件、不联网。单人游戏使用。

---

## 六、技术说明（为什么这么实现）

引擎版本 UE 5.7，这个分支的 UE4SS 只给了 Lua 绑定，**没有任何绘制 / ImGui API**（源码里 `grep DrawText|UCanvas` 全空），
所以所有效果都得用引擎自带的功能拼出来：

1. **自由相机**：用 `World:SpawnActor(StaticFindObject("/Script/Engine.CameraActor"), loc, rot)` 生成一个
   `ACameraActor`，再 `PlayerController:SetViewTargetWithBlend(actor, 0)`（失败则退回
   `PlayerCameraManager:SetViewTarget(actor, {BlendTime=0})`）把视口切过去。
   之后每帧只改这个 Actor 的 `Location` / `Rotation` / `FieldOfView`，角色本体一动不动。
   退出时 `SetViewTargetWithBlend(Pawn, 0)` 还回去，再 `K2_DestroyActor`。

2. **时间冻结**：`GameplayStatics:SetGlobalTimeDilation(World, 0.0001)`。
   不用 `SetPause` 的原因：暂停会连带停掉 PlayerCameraManager 的更新，画面和相机一起僵死，
   而摄影模式恰恰要求“世界静止、相机能动”。

3. **右下角菜单**：`KismetSystemLibrary:PrintString(World, text, true, false, color, duration, FName("PBZPhotoModeMenu"))`
   —— 屏幕调试文字从左上角开始渲染，所以用 ASCII 边框画一个块，前面补空行和空格把它顶到右下角。
   同一帧重复推送会叠加，因此用固定的 `FName` 键 + 只在文本变化时重推来避免重影。

4. **输入三层降级**（这个 UE4SS 分支的 UFunction 暴露情况不确定，所以做了兜底）：
   - ① `RegisterHook("/Script/Engine.PlayerController:InputKey")` + `:InputAxis` —— 拿引擎原始鼠标/键盘事件，
     能拿到 KEYDOWN/KEYUP 上升沿、鼠标轴、滚轮。这是最理想的源。
   - ② 拿不到 ① 时，同时 hook 四支：`Pawn:AddControllerYawInput` / `AddControllerPitchInput` +
     `PlayerController:AddYawInput` / `AddPitchInput` —— 直接借用游戏自己的视角输入
     （Enhanced Input 的鼠标视角最终都会走到其中一层，但**哪一层因游戏而异**，所以四支全挂）。
     同一帧里 PC 层的函数会继续调用 Pawn 层，两层都会有值；因此每帧只按
     “原始输入 > Pawn 层 > PC 层”的顺序取一个，绝不叠加，也就不会出现"视角转双倍"。
     走这条路时不摘输入组件（否则游戏根本不会把视角输入送过来），
     代价是移动键只能"点按"（`RegisterKeyBind` 没有按住状态）。
   - ③ 滚轮拿不到时，`config.lua` 里的键盘 `ADD` / `SUBTRACT` 顶上。
   - ④ 备用输入源下"屏蔽游戏按键"分两步走（因为摘输入组件有可能把视角输入一起摘掉）：
     (a) 先 `PlayerController:SetIgnoreMoveInput(true)` —— 引擎自带的锁移动开关，
         不碰 look，所以鼠标视角照常；引擎没有这个函数时才退化到清零 `MaxWalkSpeed`；
     (b) 再试摘 Pawn 的输入组件，然后**验证 1.2 秒**：还能收到视角输入就保留（游戏按键全哑），
         收不到就立刻 `PushInputComponent` 还原。结论会写进日志。
   日志会写明用的是哪一层。

5. **按住状态（Alt / Shift / Ctrl）**：UE4SS 的 `RegisterKeyBind` 只有“按下”回调、没有松开回调，
   所以自由视角用的是 ① 里 `InputKey` 的 KEYUP 事件维护的按住状态表；`RegisterKeyBind` 只用来做
   `F2` / `T` / `F` 这种“按一下切一下”的开关。

6. **稳定性**：所有引擎调用都走 `pcall` 包装（失败只记日志不影响游戏），
   每次 tick 校验相机 / PlayerController 是否还有效，`RegisterLoadMapPostHook` 负责换场景时自动退出。

---

## 七、参考实现：上古卷轴 PhotoMode 给了什么启发

本版（1.0.4）照着 <https://github.com/powerof3/PhotoMode>（SKSE C++ 插件）重排了"输入隔离 / 状态还原"这一层。
它和我们的平台完全不同，但有四件事可以直接搬，也解释了我们之前踩的坑：

1. **它不抢输入事件，只翻引擎状态位。** 它的输入监听器永远返回"继续传递"，
   真正的屏蔽来自 `ControlMap::ToggleControls(掩码, false, true)`（只关"菜单/交互/跳跃"这几个标志位，
   **移动和视角这两个标志位一个都没关**）+ 五个 handler 的 `SetInputEventHandlingEnabled(false)`。
   翻译到 UE 就是：`SetIgnoreMoveInput`（锁移动）比"掐输入源"稳，
   因为**掐掉输入源会把我们自己的视角来源一起掐掉** —— 这正是 1.0.3 里"摘了输入组件→视角也不能转"的同一个陷阱。
2. **它借引擎自己的自由相机，不自己写相机数学。** 平移/俯仰/灵敏度全是引擎 `FreeCameraState` 的，
   插件只负责"切过去、调速度、退出切回来"。我们也一样：相机做成 `ACameraActor` + `SetViewTarget`，
   一行位移积分都不自己写（只把位置/朝向/FOV 写到引擎对象上）。
3. **快照 / 还原纪律 + 每帧自检自动退出。** 它每个 tab 有一份 `OriginalState{Get,Revert}`，
   退出逐字段写回；并且每帧检查"菜单还能显示吗"，一变身 / 进过场 / 开画廊就立刻完整退出。
   我们对应的是：一张 `S` 状态表 + `leave_photo_mode()` 逐项还原 + 每帧校验相机/控制器是否还有效 +
   换场景钩子强制退出。
4. **时间冻结用"停时"而不是"改倍速"。** 它用的是 `Main::freezeTime`（和 `tfc 1` 同一个开关），
   相机和 UI 都还能动。我们的 `SetGlobalTimeDilation(0.0001)` 是同一个思路：
   世界静止、但引擎仍在 tick，所以相机能动、菜单能画。

搬不过来的部分（平台差异，列出来免得后来人白找）：

| 上古卷轴的东西 | 为什么搬不过来 | UE 这边的做法 |
| --- | --- | --- |
| `PlayerCamera::currentState` / `ToggleFreeCameraMode` | UE 没有相机状态栈，只有 ViewTarget | `SpawnActor(CameraActor)` + `SetViewTargetWithBlend` |
| `FreeCameraState` 的平移/俯仰数学 | 引擎内置，插件里根本没有这段代码 | 自己按 dt 写位移（本 mod 就是这么做的） |
| 直接改 `DOF::blur*` 内存偏移 | 引擎私有全局量 | 只能走 `PostProcessSettings`（本版没做景深） |
| `ImageSpaceManager` 调色 | 没有对应体系 | `PostProcessSettings` 的 Color* 参数 |
| ImGui 菜单（可鼠标点、可拖） | 这个 UE4SS 分支没有绘制 API | 屏幕调试文字 + 快捷键（本 mod 的形态） |
| 截图后 PNG 滤镜 / 分层景深 | ReShade 生态专属 | 只能交给 `HighResShot` |

它值得抄但本版还没做的：构图辅助线（三分/黄金比）、相机位存档、
"按住某键才把鼠标交给相机"的显式模式、以及"被屏蔽掉的原生功能要自己补回来"的思路。

## 八、已知限制

- 菜单文字只能 ASCII（引擎调试字体无中文），UI 不能拖动、不能用鼠标点，全靠快捷键。
- 菜单位置是按 1080p/16:9 估的字宽行高，分辨率差异大时需要手动 `menu_pad_*` 微调。
- 没有景深 / 曝光 / 调色之类的进阶摄影参数（要改后处理体积，风险更大，本版没做）；
  隐藏 HUD 和主角模型已经支持（`hide_game_hud` / `hide_player`），快门见下条。
- 快门走的是引擎的控制台命令（`HighResShot`）：能出图，但不能像专业摄影 mod 那样
  在截图前额外套滤镜 / 分层景深合成。
- 只在单人模式使用；联机 / 反作弊环境下不要开。

---

版本 1.0.4 · 目标：影之刃零 (UE 5.7.4) + KintaroModding/ue4ss 分支

**改动记录**

- 1.0.4（参考上古卷轴的 PhotoMode 项目重构"输入隔离"这一层）
  - 改：屏蔽角色移动改用引擎自带的 `SetIgnoreMoveInput`，不再上来就清零 `MaxWalkSpeed` ——
    人不会走、也不播走路动画，鼠标视角不受影响，退出还原更干净（`freeze_character` 降级为兜底）。
  - 新增：隐藏主角模型（`hide_player`，默认开）与隐藏游戏 HUD（`hide_game_hud`，默认关），退出都还原。
  - 新增：`F9` 快门（`HighResShot`，拍照前自动藏菜单）。
  - 新增：参考 Skyrim PhotoMode 的"每个模块 快照/还原 + 每帧自检自动退出"纪律，
    所有新状态（忽略移动、隐藏模型、隐藏 HUD）都登记在同一张状态表里，退出时逐项还原。
- 1.0.2（按实机 `UE4SS.log` 修）
  - 修：`SetViewTargetWithBlend` 参数个数不对导致相机接管失败——现在按 5 参 → 4 参 → `ClientSetViewTarget`
    → `PlayerCameraManager:SetViewTarget` 依次尝试，并把成功的是哪条写进日志。
  - 修：视角钩子从“逐轴二选一降级”改成“四支全挂 + 帧内仲裁”，避免游戏只走某一层时视角不动。
  - 新增：Pawn / PC 两层钩子的挂载结果都打进启动日志，方便定位。
  - 新增：菜单按实际屏幕比例自适应右下角位置；FOV 微调键除小键盘 `+`/`-` 外，主键盘 `=`/`-` 也可用。
  - 新增：`pop_input_in_look_mode` 开关（备用输入源下也能摘掉角色输入组件，避免左键误触攻击）。
  - 修：备用输入源下检测不到 Alt（没有 InputKey 事件），原来会导致视角完全不动——现在常开。
  - 新增：方向键 / PageUp / PageDown 作为备用输入源下的平移与升降键。
  - 修：`tick` 里对输入来源判定的前向引用（真机上会每帧报 `attempt to call a nil value`）。
- 1.0.3（实机反馈：按 WASD 动的是人物）
  - 新增：进入摄影模式自动冻结角色移动（`MaxWalkSpeed`/加速度清零 + 停惯性），退出还原；
    带安全阀：读到原值已经是 0 时不重复记录，避免把人永久钉住。
  - 新增：备用输入源下自动探测视角输入来自哪一层，并"先摘输入组件 → 验证 1.2 秒"，
    视角断了就自动回滚（不会出现"按键屏蔽了但视角也不能转"）。
  - 新增：`freeze_character` 开关；探测/回滚结论全部写进 `UE4SS.log`。
- 1.0.2：按实机日志修视口接管与视角钩子。
- 1.0.0：首个可用版本。
