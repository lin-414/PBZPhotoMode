--[[========================================================================
  PBZ Photo Mode v1.0.4
  影之刃零 (Phantom Blade Zero) 摄影 / 自由相机 Mod，基于 UE4SS Lua

  功能
    F2            打开 / 关闭摄影模式（右下角弹出菜单，接管相机）
    T             显示 / 隐藏菜单
    F             冻结 / 恢复时间（时间缩放 0.0001）
    按住 Alt      自由视角：鼠标转动相机
    A / D / W / S 相机前后左右平移（Shift 加速 / Ctrl 精调）
    Q / E         相机下降 / 上升
    鼠标左键      上升
    鼠标右键      下降
    鼠标滚轮      调整 FOV

  安装位置（注意大写 Scripts、小写 main.lua）
    <游戏>\platform\steam\ue4ss\Mods\PBZPhotoMode\Scripts\main.lua
    并在 <游戏>\platform\steam\ue4ss\Mods\mods.txt 里加一行：
        PBZPhotoMode : 1

  技术要点（为什么这么写）
    1. 接管相机：在玩家当前位置/朝向生成一个引擎自带的 ACameraActor，再用
       APlayerController:SetViewTargetWithBlend 把"视口目标"切到它身上，
       之后每帧直接写它的 Location/Rotation/FOV。这样完全不碰游戏自己的
       相机相机栈（弹簧臂 / 锁定镜头 / 过肩镜头），退出时把视口目标还回去即可。
    2. 输入：UE4SS 的 RegisterKeyBind 只有"按下"，没有"按住/松开"，
       所以按住 Alt、鼠标移动、滚轮这些必须 hook
       APlayerController:InputKey / InputAxis 才能拿到按下-松开状态与鼠标增量。
    3. 时间冻结：用 UGameplayStatics:SetGlobalTimeDilation(0.0001) 而不是
       SetGamePaused。真正 pause 会连相机管理器一起停掉，画面会卡死；
       0.0001 时引擎仍在 tick，角色动作/特效/物理全部静止，但相机与菜单照常刷新。
    4. 屏幕菜单：UE4SS 的 Lua 端没有任何 2D 绘制 API（ImGui 只给 C++ mod），
       所以用 UKismetSystemLibrary:PrintString 写引擎自带的屏幕调试文字，
       再用换行符/空格把它顶到右下角。
========================================================================]]

local MOD_NAME    = "PBZPhotoMode"
local MOD_VERSION = "1.0.4"

--==========================================================================
-- 0. 配置
--==========================================================================
local DEFAULTS = {
    key_toggle = "F2",
    key_menu = "T",
    key_freeze = "F",

    free_look_requires_alt = true,
    -- 备用输入源（日志 -> 使用 look_deg）下拿不到 Alt 的按住状态，
    -- true = 自由视角常开（移动鼠标即转视角），false = 完全不转
    look_deg_free_look_always = true,
    hold_key_free_look = "LeftAlt",
    hold_key_fast = "LeftShift",
    hold_key_slow = "LeftControl",

    look_sensitivity = 0.10,
    invert_y = false,
    pitch_limit = 89.0,

    move_speed = 400.0,
    fast_multiplier = 4.0,
    slow_multiplier = 0.15,
    vertical_speed = 250.0,
    -- 备用输入源下移动键"点一下走一步"的步长（厘米）
    tap_step = 120.0,
    tap_step_vertical = 60.0,

    fov_default = 60.0,
    fov_min = 15.0,
    fov_max = 150.0,
    fov_step = 2.0,
    fov_key_step = 5.0,
    key_fov_up = "ADD",
    key_fov_down = "SUBTRACT",
    -- 快门键：发一条控制台命令让引擎自己截图（HighResShot 1 = 下一帧存一张高分辨率图）
    key_shutter = "F9",
    -- 截图命令；留空 = 禁用快门键。默认存到 PBZ/Saved/Screenshots
    screenshot_command = "HighResShot 1",
    key_fov_up_alt = "OEM_PLUS",
    key_fov_down_alt = "OEM_MINUS",

    freeze_time_dilation = 0.0001,

    block_game_input = true,
    pop_input_in_look_mode = false,
    -- 进入摄影模式时冻结角色移动（MaxWalkSpeed/加速清零 + 停住惯性）
    -- 这一步与输入路由无关，保证 WASD 无论如何都推不动人
    -- 让 PlayerController 忽略移动输入（UE 自带开关，最干净；不行才退回 freeze_character）
    ignore_move_input = true,
    -- 进摄影模式时把游戏 HUD 一起隐藏（false = 保留游戏 UI）
    hide_game_hud = false,
    -- 进摄影模式时把主角模型隐藏（摄影模式基本都想让角色退出画面）
    hide_player = true,
    freeze_character = true,
    -- 备用输入源下"摘输入组件后验证视角是否还活着"的窗口（毫秒）
    -- 太短容易误判（你没动鼠标就被还原），太长会让鼠标短暂失灵，1.2 秒比较稳
    auto_block_verify_ms = 1200,

    menu_visible_on_enter = true,
    menu_anchor = "bottom_right",
    menu_pad_top = -1,
    menu_pad_left = -1,
    menu_refresh_ms = 100,
    menu_use_color = true,

    loop_interval_ms = 8,
    debug_log_input = false,
    debug_log_engine = false,
}

local CFG = {}
local CONFIG_UNKNOWN = {}      -- config.lua 里拼错的键名，启动时报警
local CONFIG_ERROR = nil       -- config.lua 语法错误 / 加载失败的原因
do
    for k, v in pairs(DEFAULTS) do CFG[k] = v end
    local ok, user = pcall(require, "config")
    if ok then
        if type(user) == "table" then
            for k, v in pairs(user) do
                if DEFAULTS[k] == nil then
                    CONFIG_UNKNOWN[#CONFIG_UNKNOWN + 1] = tostring(k)
                end
                CFG[k] = v
            end
        else
            CONFIG_ERROR = "config.lua 没有 return 一个 table"
        end
    else
        -- 常见情况：文件不存在（那就纯用默认值），或里面语法写错了
        CONFIG_ERROR = tostring(user)
    end
end

--==========================================================================
-- 1. 日志
--==========================================================================
local function log(fmt, ...)
    local msg = fmt
    if select("#", ...) > 0 then
        local ok, s = pcall(string.format, fmt, ...)
        msg = ok and s or tostring(fmt)
    end
    print(string.format("[%s] %s\n", MOD_NAME, msg))
end

local ENGINE_LOG_BUDGET = 40
local function log_engine(fmt, ...)
    if not CFG.debug_log_engine then return end
    if ENGINE_LOG_BUDGET <= 0 then return end
    ENGINE_LOG_BUDGET = ENGINE_LOG_BUDGET - 1
    log(fmt, ...)
end

--==========================================================================
-- 2. 小工具
--==========================================================================
local function is_valid(obj)
    if obj == nil then return false end
    if type(obj) ~= "userdata" then return false end
    local ok, res = pcall(function() return obj:IsValid() end)
    return ok and res == true
end

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- 安全调用 UE 函数：返回 ok, 返回值（失败不抛错）
local function ecall(obj, method, ...)
    if obj == nil then return false end
    local f
    local indexed = pcall(function() f = obj[method] end)
    if not indexed or f == nil then return false end
    local ok, ret = pcall(f, obj, ...)
    if not ok then
        log_engine("调用 %s 失败: %s", tostring(method), tostring(ret))
        return false
    end
    return true, ret
end

-- 同 ecall，但失败时把 UE4SS 的原始错误文本带出来（排查"函数存在但参数个数不对"必需）
local function ecall_x(obj, method, ...)
    if obj == nil then return false, "对象为 nil" end
    local f
    local indexed = pcall(function() f = obj[method] end)
    if not indexed then return false, "取 " .. tostring(method) .. " 时出错" end
    if f == nil then return false, "对象上没有 " .. tostring(method) end
    local ok, ret = pcall(f, obj, ...)
    if not ok then return false, tostring(ret) end
    return true, ret
end

-- 把 FName / FKey / 字符串统一成小写字符串，方便比较
local function to_key_name(v)
    if v == nil then return nil end
    local t = type(v)
    if t == "string" then
        return v:lower()
    elseif t == "table" then
        if v.KeyName ~= nil then return to_key_name(v.KeyName) end
        if v.Key ~= nil then return to_key_name(v.Key) end
        return nil
    elseif t == "userdata" then
        local ok, s = pcall(function() return v:ToString() end)
        if ok and type(s) == "string" then return s:lower() end
        local ok2, s2 = pcall(tostring, v)
        if ok2 and type(s2) == "string" then return s2:lower() end
    end
    return nil
end

-- 从 RemoteUnrealParam 列表里取出 "按键名 + 数字参数 + 结构体参数"
-- 这样写是因为 InputKey / InputAxis / FInputKeyParams 三种签名在引擎版本间不一样
local function read_params(...)
    local out = { key = nil, numbers = {}, tables = {}, event = nil }
    local count = select("#", ...)
    for i = 1, count do
        local param = select(i, ...)
        local ok, value = pcall(function() return param:get() end)
        if ok and value ~= nil then
            local vt = type(value)
            if vt == "table" then
                out.tables[#out.tables + 1] = value
                if out.key == nil then
                    local k = to_key_name(value)
                    if k then out.key = k end
                end
            elseif vt == "number" then
                out.numbers[#out.numbers + 1] = value
            elseif vt == "string" then
                if out.key == nil then out.key = value:lower() end
            end
        end
    end
    -- FInputKeyParams 里事件类型和位移都藏在结构体字段中
    if type(out.numbers[1]) == "number" then
        out.event = out.numbers[1]
    else
        for _, tbl in ipairs(out.tables) do
            if type(tbl.Event) == "number" then
                out.event = tbl.Event
                break
            end
        end
    end
    for _, tbl in ipairs(out.tables) do
        if type(tbl.Delta) == "table" then
            out.delta = tbl.Delta
            break
        end
    end
    return out
end

--==========================================================================
-- 3. 引擎对象获取（优先用 UEHelpers，缺失时自带等价实现）
--==========================================================================
local UEHelpers
do
    local ok, mod = pcall(require, "UEHelpers")
    if ok and type(mod) == "table" then
        UEHelpers = mod
    end
end

local CACHE = {}

local function cached(full_name, key)
    local c = CACHE[key]
    if c ~= nil and is_valid(c) then return c end
    local obj = StaticFindObject(full_name)
    if is_valid(obj) then
        CACHE[key] = obj
        return obj
    end
    return nil
end

local function get_player_controller()
    if UEHelpers then
        local pc = UEHelpers.GetPlayerController()
        if is_valid(pc) then return pc end
        return nil
    end
    local controllers = FindAllOf("PlayerController")
    if controllers then
        for _, pc in ipairs(controllers) do
            if is_valid(pc) then return pc end
        end
    end
    return nil
end

local function get_kismet_system_library()
    if UEHelpers then
        local lib = UEHelpers.GetKismetSystemLibrary()
        if is_valid(lib) then return lib end
    end
    return cached("/Script/Engine.Default__KismetSystemLibrary", "ksl")
end

local function get_gameplay_statics()
    if UEHelpers then
        local lib = UEHelpers.GetGameplayStatics()
        if is_valid(lib) then return lib end
    end
    return cached("/Script/Engine.Default__GameplayStatics", "gps")
end

local function get_engine()
    if UEHelpers then
        local engine = UEHelpers.GetEngine()
        if is_valid(engine) then return engine end
    end
    local engines = FindAllOf("Engine")
    if engines then
        for _, engine in ipairs(engines) do
            if is_valid(engine) then return engine end
        end
    end
    return nil
end

local function get_viewport_size()
    -- UPlayerController:GetViewportSize 是引擎反射函数，最可靠
    local pc = get_player_controller()
    if is_valid(pc) then
        local size = {}
        local ok = ecall(pc, "GetViewportSize", size)
        if ok and type(size) == "table" and type(size.X) == "number" and size.X > 0 then
            return size.X, size.Y
        end
    end
    local engine = get_engine()
    if is_valid(engine) then
        local gvc = engine.GameViewport
        if is_valid(gvc) then
            local size = {}
            local ok = ecall(gvc, "GetViewportSize", size)
            if ok and type(size) == "table" and type(size.X) == "number" and size.X > 0 then
                return size.X, size.Y
            end
        end
    end
    return 1920, 1080
end

--==========================================================================
-- 4. 状态
--==========================================================================
-- 输入来源是否可用（在第 9 节注册 hook 时填好）
local SRC = { input_key = false, input_axis = false, look_deg = false }

-- 输入来源判定（enter / tick / keybind 都要用，所以紧跟 SRC 定义）
local function active_input_source()
    if SRC.input_key or SRC.input_axis then return "raw" end
    if SRC.look_deg then return "look_deg" end
    return "none"
end

local S = {
    active = false,
    frozen = false,
    menu_visible = false,

    pc = nil,
    world = nil,
    cam_mgr = nil,
    cam_actor = nil,
    cam_comp = nil,

    -- 相机数据
    loc = { X = 0, Y = 0, Z = 0 },
    yaw = 0,
    pitch = 0,
    fov = 60,

    -- 进入前的现场
    orig_view_target = nil,
    orig_time_dilation = 1.0,
    popped_input = nil,          -- 被临时摘下的 Pawn 输入组件
    movement_frozen = nil,       -- 冻结角色移动前记录的原值
    block_state = "off",         -- off / probing / verify / blocked / kept
    block_timer = 0,
    seen_pawn = 0,               -- 探测：来自 Pawn 层的视角事件计数
    seen_pc = 0,                 -- 探测：来自 PlayerController 层的视角事件计数
    look_layer = "?",            -- 探测结论：视角输入实际走哪一层

    -- 每帧累积的输入
    keys = {},
    mouse_dx = 0.0,
    mouse_dy = 0.0,
    look_deg_x = 0.0,      -- 备用输入源：Pawn 层给的角度增量
    look_deg_y = 0.0,
    pc_deg_x = 0.0,        -- 备用输入源：PlayerController 层给的角度增量
    pc_deg_y = 0.0,
    wheel = 0,
    wheel_from_key = false,

    -- 内部
    loop_handle = nil,
    last_clock = nil,
    last_menu_clock = 0,
    last_menu_text = nil,
    game_ignore_set = false,
    ignore_move_set = false,     -- 已让 PlayerController 忽略移动输入
    ignore_look_set = false,     -- 已让 PlayerController 忽略视角输入（仅 full 模式）
    orig_show_hud = nil,         -- 隐藏 HUD 前的原值
    player_hidden = false,       -- 已隐藏主角模型
    last_menu_error = nil,
    apply_pose = nil,
    apply_fov = nil,
    target_check_clock = 0,
}

--==========================================================================
-- 5. 屏幕菜单（引擎自带 On-Screen Debug Message）
--==========================================================================
local function menu_frame(rows, title, subtitle)
    local width = 0
    for _, row in ipairs(rows) do
        if #row > width then width = #row end
    end
    if #title + #subtitle + 1 > width then width = #title + #subtitle + 1 end

    local out = {}
    out[#out + 1] = "+" .. string.rep("=", width + 2) .. "+"
    local gap = width - #title - #subtitle
    if gap < 1 then gap = 1 end
    out[#out + 1] = "| " .. title .. string.rep(" ", gap) .. subtitle .. " |"
    out[#out + 1] = "+" .. string.rep("-", width + 2) .. "+"
    for _, row in ipairs(rows) do
        out[#out + 1] = "| " .. string.format("%-" .. width .. "s", row) .. " |"
    end
    out[#out + 1] = "+" .. string.rep("=", width + 2) .. "+"
    return out
end

-- 只有拿到引擎原始输入时滚轮才可用；备用输入源下 FOV 走键盘
local function wheel_available()
    return SRC.input_key or SRC.input_axis
end

local function build_menu_lines()
    -- 引擎的屏幕调试字体没有中文字形，菜单一律用 ASCII
    local rows = {
        "F  : freeze / restore time",
        "T  : show / hide this menu",
        "ALT + mouse : rotate camera",
        "W A S D     : move   SHIFT : fast",
        "Q / E       : down / up",
        wheel_available() and "LMB / RMB   : up / down"
                           or "LMB / RMB   : up / down (tap)",
        wheel_available() and string.format("WHEEL       : FOV %.1f", S.fov)
                           or string.format("KEYS +/-    : FOV %.1f", S.fov),
    }
    local title = "PBZ PHOTO MODE v" .. MOD_VERSION
    local subtitle = S.frozen and "FROZEN F2=exit" or "live F2=exit"
    return menu_frame(rows, title, subtitle)
end

local function build_menu_string()
    local lines = build_menu_lines()

    local width = 0
    for _, line in ipairs(lines) do
        if #line > width then width = #line end
    end

    local pad_top, pad_left = 0, 0
    if CFG.menu_anchor == "bottom_right" then
        local screen_w, screen_h = get_viewport_size()
        -- 引擎屏幕调试文字：约 13px 行高 / 6.5px 字宽（1080p、100% 字体缩放）
        local line_h = math.max(6, screen_h / 78)
        local char_w = math.max(3, screen_w / 290)
        local rows = math.max(1, math.floor((screen_h - 24) / line_h))
        local cols = math.max(1, math.floor((screen_w - 24) / char_w))

        -- 右对齐 / 底部对齐，各留 2~4 个字符的边距（避免贴边被裁掉）
        pad_top = rows - #lines - 2
        pad_left = cols - width - 4
        pad_top = clamp(pad_top, 0, math.max(0, rows - #lines - 1))
        pad_left = clamp(pad_left, 0, math.max(0, cols - width - 1))
    end
    if type(CFG.menu_pad_top) == "number" and CFG.menu_pad_top >= 0 then
        pad_top = math.floor(CFG.menu_pad_top)
    end
    if type(CFG.menu_pad_left) == "number" and CFG.menu_pad_left >= 0 then
        pad_left = math.floor(CFG.menu_pad_left)
    end

    local prefix_space = string.rep(" ", pad_left)
    local out = {}
    for i = 1, pad_top do out[#out + 1] = " " end
    for _, line in ipairs(lines) do
        out[#out + 1] = prefix_space .. line
    end
    return table.concat(out, "\n")
end

local MENU_KEY_NAME = "PBZPhotoModeMenu"

local function push_menu(text, duration)
    if not S.active then return end
    local ksl = get_kismet_system_library()
    if not is_valid(ksl) then
        if S.last_menu_error ~= "no-ksl" then
            S.last_menu_error = "no-ksl"
            log("菜单不可用：找不到 KismetSystemLibrary（功能仍然正常，状态见日志）")
        end
        return
    end
    local world_ctx = S.world or S.pc
    local color = { R = 1.0, G = 0.85, B = 0.35, A = 1.0 }
    if not CFG.menu_use_color then color = { R = 1.0, G = 1.0, B = 1.0, A = 1.0 } end
    local ok = ecall(ksl, "PrintString", world_ctx, text, true, false, color,
        duration or 0.5, FName(MENU_KEY_NAME))
    if not ok and S.last_menu_error ~= "print-failed" then
        S.last_menu_error = "print-failed"
        log("菜单不可用：KismetSystemLibrary:PrintString 调用失败（功能仍然正常）")
    end
end

local function hide_menu()
    if not S.active then return end
    local ksl = get_kismet_system_library()
    if is_valid(ksl) then
        ecall(ksl, "PrintString", S.world or S.pc, "", true, false,
            { R = 0, G = 0, B = 0, A = 0 }, 0.01, FName(MENU_KEY_NAME))
    end
    S.last_menu_text = nil
end

local function refresh_menu(force)
    if not S.active or not S.menu_visible then return end
    local now = os.clock()
    if not force and (now - S.last_menu_clock) * 1000 < CFG.menu_refresh_ms then return end
    S.last_menu_clock = now
    local text = build_menu_string()
    if text == S.last_menu_text and not force then
        -- 同一内容也周期性重推，避免 Duration 到期后文字消失
        if (now - (S.last_menu_push or 0)) < 0.3 then return end
    end
    S.last_menu_text = text
    S.last_menu_push = now
    push_menu(text, 0.5)
end

--==========================================================================
-- 6. 相机
--==========================================================================
local function euler_forward(pitch_deg, yaw_deg)
    local p = math.rad(pitch_deg)
    local y = math.rad(yaw_deg)
    local cp = math.cos(p)
    return { X = cp * math.cos(y), Y = cp * math.sin(y), Z = math.sin(p) }
end

local function euler_right(yaw_deg)
    local y = math.rad(yaw_deg)
    return { X = -math.sin(y), Y = math.cos(y), Z = 0 }
end

-- 挑一个能用的"设置相机位姿"的方法，并缓存成闭包
local function build_pose_applier(actor)
    local probe_loc = { X = 0, Y = 0, Z = 0 }
    local probe_rot = { Pitch = 0, Yaw = 0, Roll = 0 }

    if is_valid(actor) and ecall(actor, "K2_SetActorLocationAndRotation",
            probe_loc, probe_rot, false, {}, true) then
        return function(loc, rot)
            ecall(actor, "K2_SetActorLocationAndRotation", loc, rot, false, {}, true)
        end, "K2_SetActorLocationAndRotation"
    end

    if is_valid(actor) and ecall(actor, "K2_SetActorLocation", probe_loc, false, {}, true)
            and ecall(actor, "K2_SetActorRotation", probe_rot, true) then
        return function(loc, rot)
            ecall(actor, "K2_SetActorLocation", loc, false, {}, true)
            ecall(actor, "K2_SetActorRotation", rot, true)
        end, "K2_SetActorLocation + K2_SetActorRotation"
    end

    -- 最后手段：直接写根组件的世界变换
    local root = nil
    if is_valid(actor) then
        local ok, value = ecall(actor, "GetPropertyValue", "RootComponent")
        if ok and is_valid(value) then root = value end
        if root == nil and is_valid(actor.RootComponent) then root = actor.RootComponent end
    end
    if is_valid(root) and ecall(root, "K2_SetWorldLocationAndRotation",
            probe_loc, probe_rot, false, {}, true) then
        return function(loc, rot)
            ecall(root, "K2_SetWorldLocationAndRotation", loc, rot, false, {}, true)
        end, "RootComponent:K2_SetWorldLocationAndRotation"
    end

    return nil, nil
end

local function build_fov_setter(comp, cam_mgr)
    if is_valid(comp) and ecall(comp, "SetFieldOfView", 60.0) then
        return function(fov)
            ecall(comp, "SetFieldOfView", fov)
            -- 有些游戏的相机管理器会覆盖组件 FOV，顺手同步一份
            if is_valid(cam_mgr) then ecall(cam_mgr, "SetFOV", fov) end
        end, "CameraComponent:SetFieldOfView"
    end
    if is_valid(cam_mgr) then
        return function(fov) ecall(cam_mgr, "SetFOV", fov) end, "PlayerCameraManager:SetFOV"
    end
    return nil, nil
end

-- FWeakObjectPtr / RemoteObject 之类的包装统一拆成真正的 UObject
local function unwrap_object(v)
    if v == nil then return nil end
    local ok_get, getter = pcall(function() return v.Get end)
    if ok_get and getter ~= nil then
        local ok2, inner = pcall(function() return v:Get() end)
        if ok2 and inner ~= nil then return inner end
    end
    return v
end

local function read_view_target_actor(pc, cam_mgr)
    local target = nil
    pcall(function()
        local vt = cam_mgr.ViewTarget
        if vt ~= nil then target = vt.Target end
    end)
    target = unwrap_object(target)
    if is_valid(target) then return target end
    if is_valid(pc.Pawn) then return pc.Pawn end
    return nil
end

--[[ 切换视口目标
     UE4SS 的 __call 是"严格参数个数"的：UFunction 声明了几个参数就必须传几个，
     少一个就 throw（不是用 C++ 默认值补齐）。SetViewTargetWithBlend 声明了 5 个，
     所以这里必须把 BlendTime / BlendFunc / BlendExp / bLockOutgoing 全传齐。 ]]
local function set_view_target(pc, cam_mgr, actor, errors)
    errors = errors or {}

    local function attempt(label, obj, method, ...)
        local ok, err = ecall_x(obj, method, ...)
        if ok then
            log_engine("视口切换成功：%s", label)
            return true
        end
        errors[#errors + 1] = label .. " -> " .. tostring(err)
        return false
    end

    local params = { BlendTime = 0.0, BlendFunction = 0, BlendExp = 0.0, bLockOutgoing = false }

    if is_valid(pc) then
        -- EViewTargetBlendFunction.VTBlend_Linear == 0
        if attempt("PlayerController:SetViewTargetWithBlend(5)", pc,
                "SetViewTargetWithBlend", actor, 0.0, 0, 0.0, false) then return true end
        if attempt("PlayerController:SetViewTargetWithBlend(4)", pc,
                "SetViewTargetWithBlend", actor, 0.0, 0, 0.0) then return true end
        if attempt("PlayerController:ClientSetViewTarget", pc,
                "ClientSetViewTarget", actor, params) then return true end
    end
    if is_valid(cam_mgr) then
        if attempt("PlayerCameraManager:SetViewTarget", cam_mgr, "SetViewTarget", actor, params) then
            return true
        end
    end
    return false
end

--==========================================================================
-- 7. 进入 / 离开摄影模式
--==========================================================================
local start_loop   -- 前向声明：真正的实现在第 8 节 tick 之后

local function stop_loop()
    if S.loop_handle ~= nil then
        pcall(CancelDelayedAction, S.loop_handle)
        S.loop_handle = nil
    end
end

-- 把 Pawn 的输入组件从 PlayerController 的输入栈里摘掉：
--   游戏自己的绑定（攻击 / 切换武器 / 移动）就不再响应摄影模式的按键，
--   但 PlayerController::InputKey 仍会被调用，所以我们的 InputKey hook 照常工作。
--   这是"屏蔽游戏输入"最干净的做法：纯反射 API，退出时 Push 回去即可恢复。
local function block_gameplay_input(pc)
    if not is_valid(pc) then return false end
    local pawn = pc.Pawn
    if not is_valid(pawn) then return false end

    local comp
    local ok_prop, value = pcall(function() return pawn.InputComponent end)
    if ok_prop then comp = value end
    if not is_valid(comp) then return false end

    local ok, popped = ecall(pc, "PopInputComponent", comp)
    if ok and popped == true then
        S.popped_input = comp
        return true
    end
    return false
end

local function restore_gameplay_input(pc)
    if S.popped_input == nil then return end
    if is_valid(pc) and is_valid(S.popped_input) then
        ecall(pc, "PushInputComponent", S.popped_input)
    end
    S.popped_input = nil
end

--==========================================================================
-- 冻结角色移动：与输入路由无关地让 WASD 推不动人。
-- 只清速度和加速度上限，不改 MovementMode，这样重力 / 动画状态都保持正常，
-- 不会出现 T-pose 或悬空静止之类的怪异画面。
--==========================================================================
local function freeze_character_movement(pc)
    if not is_valid(pc) then return nil end
    local pawn = pc.Pawn
    if not is_valid(pawn) then return nil end

    local record = { pawn = pawn }

    local cm = nil
    local ok, value = pcall(function() return pawn.CharacterMovement end)
    if ok and is_valid(value) then cm = value end
    if cm == nil then
        local ok2, value2 = ecall(pawn, "GetCharacterMovement")
        if ok2 and is_valid(value2) then cm = value2 end
    end

    if is_valid(cm) then
        record.comp = cm
        record.max_walk = cm.MaxWalkSpeed
        record.max_accel = cm.MaxAcceleration
        -- 安全阀：如果本来就是 0（上一次异常退出没还原），不要再把它当"原值"记下来，
        -- 否则会永久把人钉在原地。
        if type(record.max_walk) ~= "number" or record.max_walk < 1.0 then
            log("警告：角色 MaxWalkSpeed 已经是 %s（不是正常值），本次不冻结、也不记录为原值",
                tostring(record.max_walk))
            record.max_walk = nil
            record.max_accel = nil
            return record
        end
        pcall(function() cm.MaxWalkSpeed = 0.0 end)
        pcall(function() cm.MaxAcceleration = 0.0 end)
        if cm.MaxWalkSpeed ~= 0.0 and record.max_walk ~= nil then
            -- 属性写不进去时，退回"禁掉移动能力"这个更激进的做法
            if not ecall(cm, "DisableMovement") then
                ecall(cm, "SetMovementMode", 0)
            end
            record.mode_off = true
        end
    end

    -- 立刻停住惯性，避免进入瞬间还带着速度滑一段
    if is_valid(cm) then
        ecall(cm, "StopMovementImmediately")
    end

    return record
end

local function restore_character_movement()
    local r = S.movement_frozen
    S.movement_frozen = nil
    if r == nil then return end
    if is_valid(r.comp) then
        if r.max_walk ~= nil then pcall(function() r.comp.MaxWalkSpeed = r.max_walk end) end
        if r.max_accel ~= nil then pcall(function() r.comp.MaxAcceleration = r.max_accel end) end
        if r.mode_off then ecall(r.comp, "SetMovementMode", 1) end   -- MOVE_Walking
    end
end

local function enter_photo_mode()
    if S.active then return end

    local pc = get_player_controller()
    if not is_valid(pc) then
        log("进入失败：找不到 PlayerController（还在主菜单 / 加载中？）")
        return
    end

    local cam_mgr = pc.PlayerCameraManager
    if not is_valid(cam_mgr) then
        log("进入失败：PlayerCameraManager 无效")
        return
    end

    local world = pc:GetWorld()
    if not is_valid(world) then
        log("进入失败：UWorld 无效")
        return
    end

    -- 记下角色，退出时要把隐藏的模型恢复、把输入交还回去
    S.pawn = pc.Pawn

    local cam_class = StaticFindObject("/Script/Engine.CameraActor")
    if not is_valid(cam_class) then
        log("进入失败：找不到 /Script/Engine.CameraActor")
        return
    end

    -- 当前画面（位置 / 朝向 / FOV），保证进入时镜头不跳
    local loc, rot, cur_fov
    local ok_pov, pov = ecall(cam_mgr, "GetCameraCachePOV")
    if ok_pov and type(pov) == "table" then
        if type(pov.Location) == "table" then loc = pov.Location end
        if type(pov.Rotation) == "table" then rot = pov.Rotation end
        if type(pov.FOV) == "number" then cur_fov = pov.FOV end
    end
    if loc == nil then
        local ok_loc, value = ecall(cam_mgr, "GetCameraLocation")
        if ok_loc and type(value) == "table" then loc = value end
    end
    if rot == nil then
        local ok_rot, value = ecall(cam_mgr, "GetCameraRotation")
        if ok_rot and type(value) == "table" then rot = value end
    end
    if loc == nil then loc = { X = 0, Y = 0, Z = 0 } end
    if rot == nil then rot = { Pitch = 0, Yaw = 0, Roll = 0 } end
    if cur_fov == nil or cur_fov < 1 then cur_fov = CFG.fov_default end

    -- 生成相机
    local ok_spawn, actor = ecall(world, "SpawnActor", cam_class, loc, rot)
    if not ok_spawn or not is_valid(actor) then
        log("进入失败：SpawnActor(ACameraActor) 失败")
        return
    end

    local comp = actor.CameraComponent
    if not is_valid(comp) then
        log("警告：CameraActor 上没有 CameraComponent，FOV 可能无法调整")
    end

    local pose_applier, pose_desc = build_pose_applier(actor)
    if pose_applier == nil then
        log("进入失败：找不到任何可用的设置相机位姿的函数")
        ecall(actor, "K2_DestroyActor")
        return
    end
    local fov_setter, fov_desc = build_fov_setter(comp, cam_mgr)

    -- 记录现场
    S.pc = pc
    S.world = world
    S.cam_mgr = cam_mgr
    S.cam_actor = actor
    S.cam_comp = comp
    S.apply_pose = pose_applier
    S.apply_fov = fov_setter
    S.orig_view_target = read_view_target_actor(pc, cam_mgr)

    local gps = get_gameplay_statics()
    if is_valid(gps) then
        local ok_td, td = ecall(gps, "GetGlobalTimeDilation", world)
        if ok_td and type(td) == "number" and td > 0 then S.orig_time_dilation = td end
    end

    -- 相机数据
    S.loc = { X = loc.X or 0, Y = loc.Y or 0, Z = loc.Z or 0 }
    S.yaw = rot.Yaw or 0
    S.pitch = rot.Pitch or 0
    S.fov = cur_fov
    S.frozen = false
    S.keys = {}
    S.mouse_dx, S.mouse_dy = 0.0, 0.0
    S.look_deg_x, S.look_deg_y = 0.0, 0.0
    S.pc_deg_x, S.pc_deg_y = 0.0, 0.0
    S.wheel = 0
    S.wheel_from_key = false

    -- 应用
    if fov_setter then fov_setter(cur_fov) end
    pose_applier(S.loc, { Pitch = S.pitch, Yaw = S.yaw, Roll = 0 })
    if is_valid(comp) then
        pcall(function() comp.bConstrainAspectRatio = false end)
    end

    local vt_errors = {}
    if not set_view_target(pc, cam_mgr, actor, vt_errors) then
        log("进入失败：无法把视口目标切到相机。各种调用形式的结果：")
        for _, e in ipairs(vt_errors) do log("  - %s", e) end
        ecall(actor, "K2_DestroyActor")
        return
    end
    log("相机已接管：位姿方式=%s，FOV 方式=%s", tostring(pose_desc), tostring(fov_desc))

    -- 屏蔽角色操作（两层，按"先不影响视角、再尽量彻底"的顺序来）
    --
    -- ① 冻结角色移动：把 CharacterMovement 的 MaxWalkSpeed / MaxAcceleration 清零并
    --    立即停住惯性。这一步与输入路由无关，所以无论游戏把 WASD 送到哪一层，
    --    人都推不动 —— 这是解决"按 WASD 跑的是人、不是相机"最稳的一招。
    --    不改 MovementMode，重力与动画状态保持正常。
    -- ② 再尝试摘掉 Pawn 的输入组件：游戏的攻击/移动/切武器就全哑了（左键也不会挥刀）。
    --    但有的游戏连视角输入都走这个组件，摘掉后视角会一起没掉。
    --    所以备用输入源下不硬摘，而是"先摘 → 再验证 1.2 秒（auto_block_verify_ms）"：
    --    还能收到视角输入就保留，收不到就立刻还原（见 tick 里的探测状态机）。
    if CFG.block_game_input then
        -- ① 引擎自带的"忽略移动输入"：UPawnMovementComponent::AddInputVector 会直接丢弃，
        --    角色既不会走、也不会播走路动画，是最干净的一招（相当于过场动画里锁住移动）。
        --    它不影响 look 输入，所以我们的视角来源不会被掐断。
        if CFG.ignore_move_input ~= false and ecall(pc, "SetIgnoreMoveInput", true) then
            S.ignore_move_set = true
            log("已让控制器忽略移动输入（SetIgnoreMoveInput）：WASD 不会再推走角色")
        end

        -- ② 兜底：上面那个开关不存在 / 没生效时，直接把移动能力冻结
        if not S.ignore_move_set and CFG.freeze_character then
            S.movement_frozen = freeze_character_movement(pc)
            if S.movement_frozen then
                log("已冻结角色移动（MaxWalkSpeed=0）：WASD 不会再推走角色")
            else
                log("未能冻结角色移动（没找到 CharacterMovement），只能靠摘输入组件")
            end
        end

        -- ③ 隐藏主角模型：镜头已经和角色脱开了，角色留在原地只会挡住构图
        if CFG.hide_player and is_valid(S.pawn) and ecall(S.pawn, "SetActorHiddenInGame", true) then
            S.player_hidden = true
            log("已隐藏主角模型（退出时恢复）：镜头里不会再有主角")
        end

        -- ④ 可选：进摄影模式时隐藏游戏 HUD，方便取景
        if CFG.hide_game_hud then
            local ok_hud, hud = pcall(function() return pc.bShowHUD end)
            if ok_hud and hud ~= nil then
                S.orig_show_hud = hud
                pcall(function() pc.bShowHUD = false end)
                log("已隐藏游戏 HUD（退出时恢复）")
            end
        end

        local source = active_input_source()
        if source == "raw" or CFG.pop_input_in_look_mode == true then
            if block_gameplay_input(pc) then
                S.block_state = "blocked"
                log("已临时摘掉 Pawn 输入组件：攻击/移动等游戏动作键不会响应摄影操作")
            else
                S.block_state = "kept"
                log("未能摘掉 Pawn 输入组件（不影响摄影功能，只是游戏动作键可能同时响应）")
            end
        elseif source == "look_deg" then
            S.block_state = "probing"
            S.block_timer = os.clock()
            S.seen_pawn, S.seen_pc = 0, 0
            log("备用输入源：进入后先动一下鼠标，我会观察视角输入走哪一层，"
                .. "再决定要不要摘角色输入组件（不影响你正常用）")
        end

        -- 仅当用户显式要求"彻底屏蔽"时才连 look 一起忽略（会掐断我们自己的视角来源，
        -- 只有在你确定要靠键盘/脚本转视角时才用）
        if CFG.block_game_input == "full" or CFG.block_game_input == "ignore" then
            if ecall(pc, "SetIgnoreLookInput", true) then
                S.ignore_look_set = true
                log("已设置 IgnoreLookInput：游戏的视角输入也停了（鼠标将无法转视角）")
            end
        end
    end

    S.active = true
    S.menu_visible = CFG.menu_visible_on_enter
    S.last_clock = os.clock()
    S.last_menu_clock = 0
    S.last_menu_text = nil
    S.target_check_clock = os.clock()

    log("摄影模式开启 | 位姿写入: %s | FOV 写入: %s | 初始 FOV %.1f",
        tostring(pose_desc), tostring(fov_desc), cur_fov)
    start_loop()
    refresh_menu(true)
end

local function leave_photo_mode(reason)
    if not S.active then return end
    S.active = false
    stop_loop()

    -- 1. 时间恢复
    local gps = get_gameplay_statics()
    if is_valid(gps) and is_valid(S.world) then
        ecall(gps, "SetGlobalTimeDilation", S.world, S.orig_time_dilation)
    end

    -- 2. 视口还给游戏（要先于销毁相机）
    if is_valid(S.pc) then
        local back_errors = {}
        if is_valid(S.orig_view_target) then
            if not set_view_target(S.pc, S.cam_mgr, S.orig_view_target, back_errors) then
                log("警告：退出时视口没能切回原来的目标")
                for _, e in ipairs(back_errors) do log("  - %s", e) end
            end
        elseif is_valid(S.pc.Pawn) then
            set_view_target(S.pc, S.cam_mgr, S.pc.Pawn)
        end
    end

    -- 3. 相机销毁
    if is_valid(S.cam_actor) then
        ecall(S.cam_actor, "K2_DestroyActor")
    end

    -- 4. 输入恢复
    if S.player_hidden and is_valid(S.pawn) then
        ecall(S.pawn, "SetActorHiddenInGame", false)
    end
    S.player_hidden = false

    if is_valid(S.pc) then
        if S.ignore_move_set then ecall(S.pc, "SetIgnoreMoveInput", false) end
        if S.ignore_look_set then ecall(S.pc, "SetIgnoreLookInput", false) end
        if S.orig_show_hud ~= nil then
            pcall(function() S.pc.bShowHUD = S.orig_show_hud end)
            S.orig_show_hud = nil
        end
    end
    S.game_ignore_set = false
    S.ignore_move_set = false
    S.ignore_look_set = false
    S.pawn = nil
    restore_gameplay_input(S.pc)
    restore_character_movement(S.pc)
    S.block_state = "off"
    S.seen_pawn, S.seen_pc = 0, 0

    -- 5. 菜单清理
    S.menu_visible = false
    S.frozen = false
    S.keys = {}
    S.cam_actor = nil
    S.cam_comp = nil
    S.apply_pose = nil
    S.apply_fov = nil
    S.last_menu_text = nil

    log("摄影模式关闭（%s）", tostring(reason or "toggle"))
end

--==========================================================================
-- 8. 每帧更新
--==========================================================================
local function apply_time_freeze(on)
    local gps = get_gameplay_statics()
    if not is_valid(gps) or not is_valid(S.world) then return end
    local dilation = on and CFG.freeze_time_dilation or S.orig_time_dilation
    if dilation <= 0 then dilation = CFG.freeze_time_dilation end
    ecall(gps, "SetGlobalTimeDilation", S.world, dilation)
    S.frozen = on
    log(on and "时间已冻结" or "时间已恢复")
    refresh_menu(true)
end

local function held(name)
    if name == nil then return false end
    return S.keys[name:lower()] == true
end

local function tick()
    if not S.active then return end

    local now = os.clock()
    local dt = 1.0 / 60.0
    if S.last_clock ~= nil then
        dt = now - S.last_clock
    end
    S.last_clock = now
    dt = clamp(dt, 0.0, 0.1)

    -- 相机被销毁 / 玩家换人 / 关卡切换 -> 自动退出
    if not is_valid(S.cam_actor) then
        leave_photo_mode("相机已失效")
        return
    end
    if not is_valid(S.pc) then
        leave_photo_mode("PlayerController 已失效")
        return
    end

    -- 游戏偶尔会把视口目标抢回去（换人 / 剧情镜头 / 相机栈重置），1 秒检查一次并抢回来
    if now - S.target_check_clock > 1.0 then
        S.target_check_clock = now
        if is_valid(S.cam_mgr) then
            local cur = nil
            pcall(function()
                local vt = S.cam_mgr.ViewTarget
                if vt ~= nil then cur = vt.Target end
            end)
            cur = unwrap_object(cur)
            if is_valid(cur) and cur ~= S.cam_actor then
                set_view_target(S.pc, S.cam_mgr, S.cam_actor)
                log("视口目标被游戏抢回，已重新接管")
            end
        end
    end

    -- ---------- 输入屏蔽探测（只在备用输入源下启用） ----------
    if S.block_state == "probing" then
        if S.seen_pawn > 0 or S.seen_pc > 0 then
            S.look_layer = (S.seen_pawn > 0) and "pawn" or "pc"
            if block_gameplay_input(S.pc) then
                S.block_state = "verify"
                S.block_timer = now
                S.seen_pawn, S.seen_pc = 0, 0
            else
                S.block_state = "kept"
                log("没能摘掉角色输入组件（不影响摄影），已改用冻结移动的方式屏蔽")
            end
        elseif now - S.block_timer > 10.0 then
            S.block_state = "kept"
            log("10 秒内没有收到视角输入，保守起见没有摘角色输入组件（角色移动已经被冻结）")
        end
    elseif S.block_state == "verify" then
        if S.seen_pawn > 0 or S.seen_pc > 0 then
            S.block_state = "blocked"
            log("已屏蔽游戏按键：摘掉角色输入组件后视角输入照常可用（视角层=%s）", S.look_layer)
            refresh_menu(true)
        elseif now - S.block_timer > (CFG.auto_block_verify_ms or 1200) / 1000.0 then
            restore_gameplay_input(S.pc)
            S.block_state = "kept"
            log("摘掉角色输入组件后视角输入断了，已还原（视角层=%s）。"
                .. "角色移动仍然是冻结的，所以 WASD 不会推走人。", S.look_layer)
            log("彻底屏蔽游戏按键的代价是鼠标也不能转视角了（相机只能用方向键 / "
                .. "PageUp / PageDown 平移，朝向固定），所以默认没有强摘。")
        end
    end

    -- ---------- 视角 ----------
    -- 两个输入源任取其一：原始鼠标增量（需要乘灵敏度）或游戏自己的视角输入（已是度）
    -- 同一帧可能有多个来源被触发（玩家控制器层会继续调用 Pawn 层），
    -- 所以按优先级只取一个：原始输入 > Pawn 层 > PlayerController 层。
    -- 这样既不会转双倍，也能在游戏没走 Pawn 层时自动改吃 PC 层的值。
    local dyaw, dpitch = 0.0, 0.0
    if S.mouse_dx ~= 0.0 or S.mouse_dy ~= 0.0 then
        dyaw = S.mouse_dx * CFG.look_sensitivity
        dpitch = -S.mouse_dy * CFG.look_sensitivity   -- 鼠标下移 = 向下看
    elseif S.look_deg_x ~= 0.0 or S.look_deg_y ~= 0.0 then
        dyaw = S.look_deg_x
        dpitch = -S.look_deg_y          -- 游戏给的正值 = 向下看
        if CFG.debug_log_input then log("用 Pawn 层角度增量 %.4f / %.4f", S.look_deg_x, S.look_deg_y) end
    elseif S.pc_deg_x ~= 0.0 or S.pc_deg_y ~= 0.0 then
        dyaw = S.pc_deg_x
        dpitch = -S.pc_deg_y
        if CFG.debug_log_input then log("用 PC 层角度增量 %.4f / %.4f", S.pc_deg_x, S.pc_deg_y) end
    end
    S.look_deg_x, S.look_deg_y, S.pc_deg_x, S.pc_deg_y = 0.0, 0.0, 0.0, 0.0
    S.mouse_dx, S.mouse_dy = 0.0, 0.0

    local free_look = (not CFG.free_look_requires_alt)
                      or held(CFG.hold_key_free_look)
                      -- 备用输入源拿不到按住状态（没有 InputKey 事件），
                      -- 所以这条路下自由视角常开，否则视角永远不动
                      or (active_input_source() == "look_deg" and CFG.look_deg_free_look_always ~= false)
    if free_look and (dyaw ~= 0.0 or dpitch ~= 0.0) then
        if CFG.invert_y then dpitch = -dpitch end
        S.yaw = S.yaw + dyaw
        S.pitch = clamp(S.pitch + dpitch, -CFG.pitch_limit, CFG.pitch_limit)
        if S.yaw > 180.0 then S.yaw = S.yaw - 360.0 end
        if S.yaw < -180.0 then S.yaw = S.yaw + 360.0 end
    end

    -- ---------- 移动 ----------
    local speed = CFG.move_speed
    if held(CFG.hold_key_fast) then speed = speed * CFG.fast_multiplier end
    if held(CFG.hold_key_slow) then speed = speed * CFG.slow_multiplier end

    local forward = euler_forward(S.pitch, S.yaw)
    local right = euler_right(S.yaw)
    local mx, my, mz = 0, 0, 0

    if held("w") then mx = mx + forward.X; my = my + forward.Y; mz = mz + forward.Z end
    if held("s") then mx = mx - forward.X; my = my - forward.Y; mz = mz - forward.Z end
    if held("d") then mx = mx + right.X;   my = my + right.Y end
    if held("a") then mx = mx - right.X;   my = my - right.Y end

    local vstep = CFG.vertical_speed * dt
    if held("leftmousebutton") then S.loc.Z = S.loc.Z + vstep end
    if held("rightmousebutton") then S.loc.Z = S.loc.Z - vstep end
    if held("e") then S.loc.Z = S.loc.Z + vstep end
    if held("q") then S.loc.Z = S.loc.Z - vstep end

    if mx ~= 0 or my ~= 0 or mz ~= 0 then
        local k = speed * dt
        S.loc.X = S.loc.X + mx * k
        S.loc.Y = S.loc.Y + my * k
        S.loc.Z = S.loc.Z + mz * k
    end

    -- ---------- FOV ----------
    if S.wheel ~= 0 then
        S.fov = clamp(S.fov + S.wheel * CFG.fov_step, CFG.fov_min, CFG.fov_max)
        S.wheel = 0
        if S.apply_fov then S.apply_fov(S.fov) end
    end

    -- ---------- 写回相机 ----------
    if S.apply_pose then
        S.apply_pose(S.loc, { Pitch = S.pitch, Yaw = S.yaw, Roll = 0 })
    end

    -- ---------- 菜单 ----------
    refresh_menu(false)
end

function start_loop()
    stop_loop()
    if type(LoopInGameThreadWithDelay) == "function" then
        local ok_loop, handle = pcall(LoopInGameThreadWithDelay, CFG.loop_interval_ms, function()
            local ok, err = pcall(tick)
            if not ok then
                log("tick 出错: %s", tostring(err))
            end
        end)
        if ok_loop then
            S.loop_handle = handle
        else
            log("LoopInGameThreadWithDelay 调用失败: %s", tostring(handle))
            S.loop_handle = nil
        end
    end
    if S.loop_handle == nil and type(LoopAsync) == "function" then
        -- 老版本 UE4SS 回退（LoopAsync 已废弃但仍在）
        S.loop_handle = -1
        LoopAsync(CFG.loop_interval_ms, function()
            if not S.active then return true end
            pcall(tick)
            return false
        end)
    end
    if S.loop_handle == nil then
        log("警告：主循环启动失败，相机不会跟随输入更新")
    end
end

--==========================================================================
-- 9. 输入 hook
--==========================================================================
-- 9. 输入
--==========================================================================
--  UE4SS 的 RegisterKeyBind 只有"按下"，没有"按住 / 松开"，
--  所以"按住 Alt 转视角""鼠标滚轮调 FOV"必须 hook 引擎的输入函数。
--  但不同引擎版本里哪些函数是 UFUNCTION 并不稳定，所以输入分两层，谁可用用谁：
--    ① PlayerController:InputKey / :InputAxis  —— 原始输入（按键状态 + 鼠标增量 + 滚轮）
--    ② Pawn:AddControllerYawInput / :AddControllerPitchInput —— 游戏自己的视角输入（单位已是度）
--  ①可用 → 进摄影模式时把 Pawn 的输入组件摘掉，游戏完全不响应我们的按键；
--  ①不可用 → 退回②，此时游戏自己的按键仍会响应（日志会提示）。
--==========================================================================

local function handle_key(key, event, delta)
    if key == nil then return end

    -- IE_Axis(4)：某些引擎版本把鼠标位移也走 InputKey(FInputKeyParams) 送进来
    if event == 4 then
        if delta ~= nil and not SRC.input_axis then
            local dx = delta.X or 0.0
            local dy = delta.Y or 0.0
            if key == "mousex" or key == "mouse2d" then
                S.mouse_dx = S.mouse_dx + dx
            end
            if key == "mousey" or key == "mouse2d" then
                S.mouse_dy = S.mouse_dy + dy
            end
        end
        return
    end

    local pressed  = (event == 0) or (event == 2)   -- IE_Pressed / IE_Repeat
    local released = (event == 1)                   -- IE_Released

    if pressed then
        S.keys[key] = true
    elseif released then
        S.keys[key] = false
    end

    -- 滚轮（部分输入配置里滚轮走 InputKey）
    if pressed then
        if key == "mousescrollup" then
            S.wheel = S.wheel + 1
            S.wheel_from_key = true
        elseif key == "mousescrolldown" then
            S.wheel = S.wheel - 1
            S.wheel_from_key = true
        end
    end

    if CFG.debug_log_input then
        log("InputKey: %s event=%s", key, tostring(event))
    end
end

local function on_input_key(Context, ...)
    if not S.active then return end
    local params = read_params(...)
    if params.key == nil then return end
    handle_key(params.key, params.event or 0, params.delta)
end

local function on_input_axis(Context, ...)
    if not S.active then return end
    local params = read_params(...)
    local key = params.key
    if key == nil then return end
    local delta = params.numbers[1] or 0

    if key == "mousex" then
        S.mouse_dx = S.mouse_dx + delta
    elseif key == "mousey" then
        S.mouse_dy = S.mouse_dy + delta
    elseif key == "mousewheelaxis" then
        if not S.wheel_from_key then
            S.wheel = S.wheel + delta
        end
    elseif key == "leftmousebutton" or key == "rightmousebutton" then
        S.keys[key] = delta > 0.5
    elseif key == "lefttrigger" or key == "righttrigger" then
        -- 手柄扳机也能升降，聊胜于无
        S.keys[key] = delta > 0.5
    end

    if CFG.debug_log_input then
        log("InputAxis: %s delta=%s", key, tostring(delta))
    end
end

-- 备用源：游戏自己的视角输入（Val 已经是"度"，灵敏度由游戏自己算好了）
local function make_axis_handler(field, label, layer)
    return function(Context, ...)
        if not S.active then return end
        if layer == "pawn" then
            S.seen_pawn = S.seen_pawn + 1
        else
            S.seen_pc = S.seen_pc + 1
        end
        local params = read_params(...)
        local val = params.numbers[1]
        if type(val) ~= "number" then return end
        S[field] = S[field] + val
        if CFG.debug_log_input then log("%s: %s", label, tostring(val)) end
    end
end

local on_pawn_yaw   = make_axis_handler("look_deg_x", "Pawn:AddControllerYawInput", "pawn")
local on_pawn_pitch = make_axis_handler("look_deg_y", "Pawn:AddControllerPitchInput", "pawn")
local on_pc_yaw     = make_axis_handler("pc_deg_x",   "PC:AddYawInput", "pc")
local on_pc_pitch   = make_axis_handler("pc_deg_y",   "PC:AddPitchInput", "pc")

local function try_hook(path, callback)
    local ok, err = pcall(RegisterHook, path, callback)
    if not ok then
        log_engine("hook 失败 %s : %s", path, tostring(err))
    end
    return ok
end

local function register_engine_hooks()
    SRC.input_key  = try_hook("/Script/Engine.PlayerController:InputKey", on_input_key)
    SRC.input_axis = try_hook("/Script/Engine.PlayerController:InputAxis", on_input_axis)

    -- 只有在拿不到原始输入时才需要备用源
    if not (SRC.input_key or SRC.input_axis) then
        -- 优先 Pawn 层（最末端，不会被别的东西再消费一次），
        -- 某一轴失败时再退到 PlayerController 层的同轴函数 —— 逐轴判断，
        -- 这样绝不会同一轴挂两个 hook 导致视角转双倍。
        -- 四个都挂上，帧内再仲裁（同一帧只会用其中一个的值）。
        -- 游戏到底走哪一层不可知，全挂上最稳。
        SRC.look_pawn_yaw   = try_hook("/Script/Engine.Pawn:AddControllerYawInput", on_pawn_yaw)
        SRC.look_pawn_pitch = try_hook("/Script/Engine.Pawn:AddControllerPitchInput", on_pawn_pitch)
        SRC.look_pc_yaw     = try_hook("/Script/Engine.PlayerController:AddYawInput", on_pc_yaw)
        SRC.look_pc_pitch   = try_hook("/Script/Engine.PlayerController:AddPitchInput", on_pc_pitch)
        SRC.look_deg = SRC.look_pawn_yaw or SRC.look_pawn_pitch
                    or SRC.look_pc_yaw   or SRC.look_pc_pitch
    end

    log("输入来源: InputKey=%s InputAxis=%s 备用(Pawn yaw=%s pitch=%s / PC yaw=%s pitch=%s) -> 使用 %s",
        tostring(SRC.input_key), tostring(SRC.input_axis),
        tostring(SRC.look_pawn_yaw), tostring(SRC.look_pawn_pitch),
        tostring(SRC.look_pc_yaw), tostring(SRC.look_pc_pitch),
        active_input_source())
    if active_input_source() == "none" then
        log("警告：拿不到任何鼠标输入！自由视角无法使用（F2/T/F 键仍然可用）。")
        log("      请把 ue4ss/UE4SS.log 里 [PBZPhotoMode] 开头的几行发出来定位。")
    elseif active_input_source() == "look_deg" then
        log("提示：当前使用备用输入源，游戏自己的按键仍会响应（按左键可能触发攻击）。")
    end
end

--==========================================================================
local function bind_key(key_name, callback, label)
    local key_enum = Key and Key[key_name]
    if key_enum == nil then
        log("未知键位 '%s'（%s），已跳过", tostring(key_name), tostring(label))
        return
    end
    local ok = pcall(RegisterKeyBind, key_enum, function()
        local ok2, err = pcall(callback)
        if not ok2 then log("%s 回调出错: %s", tostring(label), tostring(err)) end
    end)
    if not ok then
        log("绑定键位 %s 失败", tostring(key_name))
    end
end

local function toggle_menu()
    if not S.active then return end
    S.menu_visible = not S.menu_visible
    if S.menu_visible then
        refresh_menu(true)
    else
        hide_menu()
    end
end

-- 快门：借引擎自己的截图命令。先藏掉我们的菜单，下一帧再拍，
-- 拍完把菜单还回去——这样成片里不会有覆盖层。
local function take_screenshot()
    if not S.active then return end
    local cmd = CFG.screenshot_command
    if cmd == nil or cmd == "" then
        log("没有配置 screenshot_command，快门键无动作")
        return
    end
    if not is_valid(S.pc) then return end

    local was_visible = S.menu_visible
    if was_visible then hide_menu() end

    local sent = ecall(S.pc, "ConsoleCommand", cmd, true)
    if sent then
        log("已触发截图：%s（成品在 PBZ/Saved/Screenshots 里）", tostring(cmd))
    else
        log("截图命令没发出去（当前引擎可能没有 ConsoleCommand），可改用 F12 手动截图")
    end

    -- 400ms 后把菜单收回来
    if was_visible and ExecuteInGameThreadWithDelay then
        ExecuteInGameThreadWithDelay(400, function()
            if S.active and not S.menu_visible then
                S.menu_visible = true
                refresh_menu(true)
            end
        end)
    end
end

-- 滚轮之外再给两个键做 FOV 微调（万一某个引擎版本拿不到滚轮事件）
local function nudge_fov(delta)
    if not S.active then return end
    S.fov = clamp(S.fov + delta, CFG.fov_min, CFG.fov_max)
    if S.apply_fov then S.apply_fov(S.fov) end
    refresh_menu(true)
end

--==========================================================================
-- 备用输入源下的"点按移动"
--   RegisterKeyBind 只有按下回调、没有按住状态，所以拿不到引擎原始输入时，
--   移动键改为"按一下走一步"。原生输入可用时这个函数直接返回，
--   避免和 tick 里的按住移动重复叠加。
--==========================================================================
local function tap_direction(key)
    local fwd = euler_forward(S.pitch, S.yaw)
    local right = euler_right(S.yaw)
    if key == "w" or key == "up_arrow" then return fwd end
    if key == "s" or key == "down_arrow" then return { X = -fwd.X, Y = -fwd.Y, Z = -fwd.Z } end
    if key == "d" or key == "right_arrow" then return right end
    if key == "a" or key == "left_arrow" then return { X = -right.X, Y = -right.Y, Z = 0 } end
    return nil
end

local function tap_move(key)
    if not S.active then return end
    if active_input_source() == "raw" then return end

    local dir = tap_direction(key)
    if dir then
        local step = CFG.tap_step
        S.loc.X = S.loc.X + dir.X * step
        S.loc.Y = S.loc.Y + dir.Y * step
        S.loc.Z = S.loc.Z + dir.Z * step
    elseif key == "e" or key == "page_up" then
        S.loc.Z = S.loc.Z + CFG.tap_step_vertical
    elseif key == "q" or key == "page_down" then
        S.loc.Z = S.loc.Z - CFG.tap_step_vertical
    elseif key == "leftmousebutton" then
        S.loc.Z = S.loc.Z + CFG.tap_step_vertical
    elseif key == "rightmousebutton" then
        S.loc.Z = S.loc.Z - CFG.tap_step_vertical
    else
        return
    end

    if S.apply_pose then
        S.apply_pose(S.loc, { Pitch = S.pitch, Yaw = S.yaw, Roll = 0 })
    end
end

local function init_keybinds()
    -- 键位回调解在 UE4SS 线程上，必须切到游戏线程再碰引擎对象
    bind_key(CFG.key_toggle, function()
        ExecuteInGameThread(function()
            if S.active then
                leave_photo_mode("F2")
            else
                enter_photo_mode()
            end
        end)
    end, "toggle")

    bind_key(CFG.key_menu, function()
        ExecuteInGameThread(toggle_menu)
    end, "menu")

    bind_key(CFG.key_freeze, function()
        ExecuteInGameThread(function()
            if not S.active then return end
            apply_time_freeze(not S.frozen)
        end)
    end, "freeze")

    bind_key(CFG.key_fov_up, function()
        ExecuteInGameThread(function() nudge_fov(CFG.fov_key_step) end)
    end, "fov_up")

    bind_key(CFG.key_shutter, function()
        ExecuteInGameThread(take_screenshot)
    end, "shutter")

    bind_key(CFG.key_fov_down, function()
        ExecuteInGameThread(function() nudge_fov(-CFG.fov_key_step) end)
    end, "fov_down")

    -- 备用 FOV 键（主键盘 = / -），空字符串表示不用
    if type(CFG.key_fov_up_alt) == "string" and CFG.key_fov_up_alt ~= ""
            and CFG.key_fov_up_alt ~= CFG.key_fov_up then
        bind_key(CFG.key_fov_up_alt, function()
            ExecuteInGameThread(function() nudge_fov(CFG.fov_key_step) end)
        end, "fov_up_alt")
    end
    if type(CFG.key_fov_down_alt) == "string" and CFG.key_fov_down_alt ~= ""
            and CFG.key_fov_down_alt ~= CFG.key_fov_down then
        bind_key(CFG.key_fov_down_alt, function()
            ExecuteInGameThread(function() nudge_fov(-CFG.fov_key_step) end)
        end, "fov_down_alt")
    end

    -- 点按移动（只在原生输入不可用时生效）
    local tap_keys = {
        { "w", "W" }, { "a", "A" }, { "s", "S" }, { "d", "D" },
        { "q", "Q" }, { "e", "E" },
        { "leftmousebutton", "LEFT_MOUSE_BUTTON" },
        { "rightmousebutton", "RIGHT_MOUSE_BUTTON" },
        -- 备用输入源下游戏仍会吃 WASD / 鼠标键（摘不掉输入组件），
        -- 所以再给一套大概率没被游戏占用的键：方向键平移、PageUp/Down 升降
        { "up_arrow", "UP_ARROW" },
        { "down_arrow", "DOWN_ARROW" },
        { "left_arrow", "LEFT_ARROW" },
        { "right_arrow", "RIGHT_ARROW" },
        { "page_up", "PAGE_UP" },
        { "page_down", "PAGE_DOWN" },
    }
    for _, entry in ipairs(tap_keys) do
        local logical, enum_name = entry[1], entry[2]
        bind_key(enum_name, function()
            ExecuteInGameThread(function() tap_move(logical) end)
        end, "tap_" .. logical)
    end
end

--==========================================================================
-- 11. 安全网：换关 / 读档时自动退出，避免相机和输入卡在 mod 手里
--==========================================================================
local function register_safety_hooks()
    local ok = pcall(function()
        RegisterLoadMapPostHook(function()
            if S.active then
                ExecuteInGameThread(function()
                    leave_photo_mode("关卡切换")
                end)
            end
        end)
    end)
    if not ok then log("RegisterLoadMapPostHook 不可用（不影响主功能）") end
end

--==========================================================================
-- 12. 初始化
--==========================================================================
local function main()
    if CONFIG_ERROR ~= nil then
        log("config.lua 未被使用（%s），当前跑的是 main.lua 里的默认配置", CONFIG_ERROR)
    end
    for _, k in ipairs(CONFIG_UNKNOWN) do
        log("警告：config.lua 里的键名 '%s' 不存在，已被忽略（检查拼写）", k)
    end

    log("v%s 载入中… (Lua %s)", MOD_VERSION, tostring(_VERSION))
    log("配置: toggle=%s menu=%s freeze=%s anchor=%s",
        tostring(CFG.key_toggle), tostring(CFG.key_menu),
        tostring(CFG.key_freeze), tostring(CFG.menu_anchor))

    local pc = get_player_controller()
    if is_valid(pc) then
        log("已找到 PlayerController: %s", pc:GetFullName())
    else
        log("暂时没有 PlayerController（正常，主菜单/读档时还没有），功能会在进入游戏后可用")
    end

    register_engine_hooks()
    register_safety_hooks()
    init_keybinds()

    log("就绪：游戏内按 %s 打开摄影模式", tostring(CFG.key_toggle))

    -- 对外暴露只读状态：方便排查问题 / 给别的 mod 或自动化脚本读取
    _G.PBZPhotoMode = {
        version = MOD_VERSION,
        state = S,
        config = CFG,
        is_active = function() return S.active end,
    }
end

main()
