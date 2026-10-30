--[[========================================================================
  PBZ Photo Mode —— 配置文件 (config.lua)
  影之刃零 (Phantom Blade Zero) 摄影 / 自由相机 Mod / UE4SS Lua

  改完保存后，重启游戏（或者按 Ctrl+R 重载 Lua mod）生效。
  如果这个文件被删掉或写错，mod 会自动退回 main.lua 里的默认值。

  两种键名不要混：
    以 "Key." 指的是 UE4SS 的 Key 枚举名，写在这里不带前缀 —— 例如
        F2 / F / T / NUM_ONE / ADD（小键盘 +）/ SUBTRACT（小键盘 -）
    以 "FKey" 指的是 Unreal 的按键名 —— 例如
        LeftAlt / LeftShift / LeftControl / LeftMouseButton / MouseScrollUp
========================================================================]]

return {
    ---------------------------------------------------------------- 开关键
    -- 打开 / 关闭摄影模式（弹出 / 收起右下角菜单，接管相机）
    key_toggle          = "F2",

    -- 菜单显示 / 隐藏（只在摄影模式内有效）
    key_menu            = "T",

    -- 冻结 / 恢复时间（只在摄影模式内有效）
    key_freeze          = "F",

    ------------------------------------------------------------ 自由视角
    -- 是否必须按住 Alt 才能用鼠标转视角。
    -- true  = 按住 Alt 才转，松开 Alt 鼠标不动相机（常规"自由视角"手感）
    -- false = 摄影模式下鼠标永远控制相机
    free_look_requires_alt = true,
    -- 备用输入源（日志 -> 使用 look_deg）下没有 InputKey 事件，检测不到 Alt 是否按住。
    -- true = 这条路下自由视角常开（移动鼠标就转视角）；false = 完全不转（不建议）
    look_deg_free_look_always = true,

    hold_key_free_look  = "LeftAlt",       -- 自由视角的按住键
    hold_key_fast       = "LeftShift",     -- 加速移动
    hold_key_slow       = "LeftControl",   -- 减速移动（精调构图）

    -- 鼠标灵敏度：度 / 每个鼠标计数。调小更细腻
    look_sensitivity    = 0.10,
    -- 垂直方向反转。如果发现"鼠标上推镜头往下走"，把它改成 true
    invert_y            = false,
    -- 俯仰角限制（度），89 = 可以几乎垂直看天/看地
    pitch_limit         = 89.0,

    ------------------------------------------------------------ 相机移动
    -- UE 的单位是厘米：400 = 4 米/秒
    move_speed          = 400.0,
    fast_multiplier     = 4.0,     -- 按住 Shift 的倍率
    slow_multiplier     = 0.15,    -- 按住 Ctrl 的倍率
    -- 鼠标左键上升 / 右键下降 / E 上升 / Q 下降 的速度
    vertical_speed      = 250.0,
    -- 拿不到引擎原始输入时（日志显示 -> 使用 look_deg），没有"按住"状态，
    -- 移动键和鼠标键变成"按一下走一步"，下面是每次的步长（厘米）
    tap_step            = 120.0,
    tap_step_vertical   = 60.0,

    ---------------------------------------------------------------- FOV
    fov_default         = 60.0,    -- 读不到游戏当前 FOV 时用的基准值
    fov_min             = 15.0,
    fov_max             = 150.0,
    fov_step            = 2.0,     -- 滚轮一格改变的度数
    fov_key_step        = 5.0,     -- 键盘微调的步长
    -- 键名必须是 UE4SS 的 Key 枚举名（全大写）：ADD / SUBTRACT 是小键盘 + -，
    -- OEM_PLUS / OEM_MINUS 是主键盘的 = 和 -（没有小键盘时用这两个）
    key_fov_up          = "ADD",        -- 放大视野
    key_fov_down        = "SUBTRACT",   -- 收窄视野
    key_fov_up_alt      = "OEM_PLUS",   -- 备用键（主键盘 =）
    key_fov_down_alt    = "OEM_MINUS",  -- 备用键（主键盘 -）

    ------------------------------------------------------------ 快门
    -- 按下后让引擎自己截一张图，存到 PBZ/Saved/Screenshots
    -- 拍之前会自动把我们的菜单藏起来，拍完再显示出来。
    key_shutter         = "F9",
    -- 截图命令；留空 = 禁用快门键。
    -- "HighResShot 1"        = 以当前分辨率截一张（最稳）
    -- "HighResShot 2"        = 2 倍分辨率（4K 屏出 8K 图，慢一点）
    -- "HighResShot 3840x2160" = 指定分辨率
    screenshot_command  = "HighResShot 1",

    ------------------------------------------------------------ 时间冻结
    -- 0.0001 是 UE 允许的最小时间缩放，等价于"完全静止"，
    -- 但引擎仍在 tick，所以相机 / 菜单不会跟着卡住。
    freeze_time_dilation = 0.0001,

    ------------------------------------------------------------ 输入隔离
    -- 进入摄影模式时，把 Pawn 的输入组件从输入栈里临时摘掉：
    -- true  = 游戏自己的按键（攻击 / 切换武器 / 移动）完全不响应，
    --         这样按住左键升降相机时角色不会挥刀、滚轮不会换武器
    -- false = 不管游戏输入（万一遇到"退出后角色不能动"之类的怪问题，改这个试）
    -- 注意：只有在能拿到引擎原始输入（InputKey/InputAxis）时才生效，
    --       走备用输入源时自动不生效（日志会说明）。
    block_game_input    = true,

    -- 备用输入源模式下（日志 -> 使用 look_deg）是否也摘输入组件。
    -- 摘了：左键/右键改为升降时不会同时挥刀，游戏动作键全部失效；
    --       但如果这个游戏的视角输入也是走 Pawn 输入组件注册的，摘掉后
    --       "按住 Alt 转视角"就会失效 —— 所以默认 false，需要才打开。
    pop_input_in_look_mode = false,

    -- 首选方案：让 PlayerController 忽略移动输入（UE 自带的锁移动开关）。
    -- 角色不走路、不播走路动画，也不影响鼠标转视角 —— 比改 MaxWalkSpeed 干净。
    -- 引擎里没有这个函数时，会自动退化到下面的 freeze_character。
    ignore_move_input   = true,

    -- 兜底方案：把 CharacterMovement 的 MaxWalkSpeed / 加速度清零、并停住惯性。
    -- 只在 ignore_move_input 不可用时才会执行。退出时自动还原（含原 MovementMode）。
    freeze_character = true,

    -- 备用输入源（-> 使用 look_deg）下，mod 会先摘掉角色的输入组件、再验证 1.2 秒
    -- 视角输入是否还活着；活着就保留（游戏按键全哑），断了就立刻还原。
    -- 太短容易误判，太长会让鼠标短暂失灵。
    auto_block_verify_ms = 1200,

    -- 进摄影模式时隐藏主角模型（摄影模式一般不想让角色挡构图）。退出时恢复。
    hide_player         = true,

    -- 进摄影模式时顺带把游戏 HUD 也藏起来（血条/小地图等），退出时恢复。
    -- 注意：跨过场动画后 HUD 可能被引擎重新打开，按 T 开关一次菜单即可重新同步。
    hide_game_hud       = false,

    ---------------------------------------------------------------- 菜单
    menu_visible_on_enter = true,

    -- "bottom_right" = 右下角（默认：用空行 + 空格把引擎的屏幕调试文字推过去）
    -- "top_left"     = 左上角原生位置（调试用）
    menu_anchor         = "bottom_right",

    -- 位置微调：-1 = 按分辨率自动换算。
    -- 分辨率 / 字体缩放导致菜单位置不理想时，直接填具体数字，每 +1 = 下移 / 右移一格。
    menu_pad_top        = -1,
    menu_pad_left       = -1,

    menu_refresh_ms     = 100,     -- 菜单刷新间隔（毫秒）
    menu_use_color      = true,    -- 菜单颜色（关掉就是纯白）

    ------------------------------------------------------------ 高级 / 调试
    loop_interval_ms    = 8,       -- 相机更新循环间隔（毫秒），卡顿就调到 16
    -- 打开后会把每个按键事件写进 ue4ss/UE4SS.log，用来确认键名是否正确
    debug_log_input     = false,
    -- 打开后记录每次引擎调用失败的具体原因
    debug_log_engine    = false,
}
