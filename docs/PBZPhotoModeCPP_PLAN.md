# PBZ Photo Mode 转 C++ (UE4SS C++ Mod) 迁移方案 v0.1

> 目标：把现有 Lua 版（v1.0.4，已上机可用）重写为 UE4SS C++ (DLL) Mod，
> 保留全部现有功能，并拿到只有 C++ 才能给的三件事：真正的输入吞噬、真正的界面绘制、后处理（景深/曝光/调色）。

---

## 0. 一页纸结论

| 项 | 结论 |
| --- | --- |
| 技术上可不可行 | 可行。这个分叉的 `UE4SS.dll` 里带着 C++ mod 加载器（`start_mod` / `CppUserModBase` / `main.dll` 字符串都在），`CppUserModBase.hpp:226` 有 `register_tab` |
| 有没有硬门槛 | **有一个，且必须你来做**：构建 C++ mod 要链 `UE4SS` target，而它的 `Unreal/*.hpp` 全在私有子模块 `Re-UE4SS/UEPseudo` 里（`git ls-remote` 返回 Repository not found）→ 需要把 GitHub 账号绑定 Epic 并接受邀请 |
| 工期 | 6-9 人日（不含 Epic 审核等待）；真正的新工作只有"输入 hook"和"界面"两块，相机/时间/配置是机械移植 |
| 现成资产怎么用 | 55KB 的 Lua 版就是**规格书**：进/出流程、5 参 `SetViewTargetWithBlend`、三层输入降级、各种坑都已在日志里验证过，C++ 版照着搬即可 |
| 回退方案 | Lua 版 v1.0.4 保持可发行；C++ 版以 beta 并行，`mods.txt` 二选一 |

---

## 1. 目标与范围

**要做（对齐 Lua 版 1.0.4）**

1. `F2` 进/出摄影模式；右下角（或可拖拽）菜单；`T` 隐藏菜单；`F` 冻结时间。
2. 自由相机：脱离角色、按住 `Alt` 转视角、`WASD` 平移、`Shift/Ctrl` 加减速、`QE` 升降、左右键上下、滚轮 FOV。
3. 角色与 HUD：`SetIgnoreMoveInput`、隐藏主角模型、可选隐藏 HUD、退出全量还原。
4. `F9` 快门。

**C++ 才做得到、本次要拿下（Lua 做不到的部分）**

5. **吞输入**：摄影模式下左键/右键/滚轮/移动键在处理完后**直接丢弃**，游戏完全收不到（不再靠"摘输入组件 + 试探回滚"）。
6. **真界面**：可点击的滑条/按钮/中文标签，而不是 ASCII 拼图。两条实现路线见 §5.5。
7. **后处理**：景深、曝光补偿、饱和度/对比度/暗角，可实时调；退出还原。

**不做（明确砍掉）**

- 不做 UMG 蓝图资产（要打包 .uasset，超出范围）；
- 不做画廊 / PNG 合成（收益低）；
- 不改游戏文件、不动存档。

---

## 2. 为什么是 UE4SS C++ Mod，而不是别的形态

| 形态 | 评价 |
| --- | --- |
| **UE4SS C++ Mod（本方案）** | 白拿：注入器、反射（不用每游戏生成 SDK）、日志、ImGui、崩溃转储、mods.txt 加载顺序。代价是 ABI 绑定 UE4SS 版本 |
| 自写 DLL + 自己注入 | 要自己做注入、自己做反射（读 FUObjectArray/GNatives 偏移）、自己做日志与配置。工程量翻倍，没有一项是本项目需要的 |
| 继续 Lua | 已证明能跑，但"吞输入"和"真界面"两件事在 Lua 层没有 API 可用 |
| 外部 overlay（DXGI + ImGui） | 能画漂亮 UI，但碰不到引擎（改不了 FOV/后处理/时间），只能当辅助，不能当主体 |

关键认知：**UE4SS 的 C++ mod 并不是"有 SDK 的强类型编程"**。这个分叉不给你 `SDK/ACameraActor.hpp` 这种生成代码，反射层跟 Lua 版是同一套（`StaticFindObject` / `ProcessEvent` / 属性偏移），
所以真实差异在：**更快的调用、能 inline hook、能直接读写内存、能用 ImGui**。移植时不要指望"类型安全"，要靠一套自己的反射小工具（§5.1）。

---

## 3. 现实约束（都已核实，不是猜的）

### 3.1 硬门槛：UEPseudo 是私有的

```
.gitmodules:  [submodule "deps/first/Unreal"]  url = git@github.com:Re-UE4SS/UEPseudo.git
实测:         git ls-remote https://github.com/Re-UE4SS/UEPseudo  ->  Repository not found
```

`UE4SS/include` 里只有 `Mod/`、`GUI/`、`Input/`、`LuaType/` 等，**没有 `Unreal/` 目录** ——
`Unreal/UObject.hpp`、`Unreal/Hooks.hpp`、`Unreal/UObjectGlobals.hpp` 全部来自 UEPseudo。
没有它：编不过、也看不到 hook API。

**对策**（按优先级）：

1. 你的 GitHub 绑定 Epic（`UE4SS` 文档 Part 1 的标准流程），然后 `git submodule update --init --recursive`。
2. 向分叉作者 KintaroModding 索取与 `v3.0.1-1183-gcdbc0b66` 对应的 dev 包（头文件 + `UE4SS.lib`）。
3. 都没有 → 退回 Lua 版，或改用"外部 overlay + Lua"的混合方案。

> 在这条打通之前，我可以先把**除编译之外的一切**写好（CMake、全部模块、配置解析、文档、打包脚本），一旦拿到权限当天就能出第一个可加载的 DLL。

### 3.2 本机工具链现状

| 依赖 | 要求 | 本机 | 结论 |
| --- | --- | --- | --- |
| MSVC | 工具集 ≥ 14.43 / VS ≥ 17.13 | `14.44.35207` / BuildTools `17.14.37710` | OK |
| CMake | ≥ 3.22 | 已装 | OK |
| git | — | 已装 | OK |
| Rust | ≥ 1.73 | **未安装** | 需装（`winget install Rustlang.Rustup`） |
| Ninja | 可选 | 无 | 用 MSVC 生成器代替 |
| UE4SS 源码 | 必须与出货 DLL 同 commit | 已浅克隆于 `_ref/ue4ss-src`，需 `git fetch --unshallow` | 待办 |

### 3.3 ABI 必须对齐（这条最容易翻车）

`CppUserModBase.hpp` 顶部原文：

> "When making C++ mods, keep in mind that they will break if UE4SS and the mod don't use the same C Runtime library version. This includes them being compiled in different configurations (Debug/Release)."

所以顺序必须是：

1. `git fetch --unshallow` 后 checkout **`v3.0.1-1183-gcdbc0b66`**（tag 指向 `aedfc5bc`），而不是 `main` HEAD（`97adba0`，比出货版本新）。
2. 用本机 MSVC 14.44 以 `Game__Shipping__Win64` 配置编一遍 UE4SS，产出 `UE4SS.lib` 给 mod 链接。
3. 运行时**继续用包里那个 `UE4SS.dll`**（分叉修过 UE 5.7 兼容，不能替换成我们自己编的）。两者同源码同配置，ABI 才对得上。
4. 若加载即崩（大概率是 CRT 不匹配）：改用 `UE4SSCPPTemplate` 的 `new_mod_setup.bat` 走官方模板流程，或向作者确认他们的构建工具链版本。

### 3.4 UI 形态的真相（必须先说清，免得起期待）

`register_tab` 注册的 ImGui 界面，**渲染在 UE4SS 自己的 GUI 窗口里**（就是 `Ctrl+O` 那个开发者控制台），不是自由浮层。
要看到它，用户得把 `UE4SS-settings.ini` 的 `GuiConsoleEnabled` 从 `0` 改成 `1`（分叉默认是 0），进游戏后按 `Ctrl+O` 打开窗口。

所以两条路（详见 §5.5）：

- **路线 A（快）**：ImGui tab。控件齐全（滑条/按钮/中文），但长得像开发工具，还得开着控制台窗口。
- **路线 B（像样）**：hook 引擎渲染，用 `UCanvas` 自绘 HUD（真·游戏内叠加、可用游戏自带中文字体）。工作量大，且要处理"每帧何时画"的时机问题。

---

## 4. 架构设计

### 4.1 目录结构

```
PBZPhotoModeCpp/                    <- 独立 mod 名，可与 Lua 版并存
  CMakeLists.txt
  dllmain.cpp                       <- start_mod / uninstall_mod
  src/
    mod_app.h / .cpp                <- CppUserModBase 子类：生命周期、hook 注册、tick 分发
    config.h / .cpp                 <- config.ini 解析 + 默认值 + 热重载
    log.h                           <- Output::send 封装（error/warn/info/trace）
    refl.h / .cpp                   <- 反射工具：FindObject / 属性读写 / CallUFunction
    photo_state.h                   <- 唯一的快照状态表（退出时按它还原）
    camera.h / .cpp                 <- 相机接管、位姿应用、FOV、pitch clamp
    input.h / .cpp                  <- 按键状态表、InputKey/InputAxis hook、输入吞噬
    time_ctl.h / .cpp               <- 时间缩放快照/还原
    ui_tab.h / .cpp                 <- 路线 A：ImGui tab
    ui_canvas.h / .cpp              <- 路线 B：UCanvas 叠加（可后置）
    postfx.h / .cpp                 <- FPostProcessSettings 读写与还原
    shutter.h / .cpp                <- 快门
  tools/
    build.ps1                       <- 一键 configure + build
    pack.ps1                        <- 产出 Mods/PBZPhotoModeCpp/dlls/main.dll
  docs/
    API_NOTES_CPP.md                <- P1 侦察结论（对应 Lua 版的 API_NOTES.md）
    README.md / 测试清单.md
```

### 4.2 生命周期时序

```
DLL 被 LoadLibrary
  -> start_mod() 返回 ModApp            // 构造函数里只填元数据 + register_* 回调
  -> on_program_start()                 // 读 config.ini（此时还没有 Unreal 模块）
  -> on_unreal_init()                   // 找对象 / 挂 hook 的唯一安全时机
       GameplayStatics CDO、KismetSystemLibrary CDO、CameraActor class、换场景 hook
  -> on_ui_init() -> UE4SS_ENABLE_IMGUI()   // 不调用则任何 ImGui 绘制都会崩（官方原文）
       加载中文字体、register_tab(...)
  -> 每帧 on_update() 或 engine tick 回调   // 状态机推进 / 位姿应用 / 菜单重绘判定
  -> Ctrl+R (on_ue4ss_update)               // 重读 config.ini
  -> 卸载：先完整还原（leave），再删 tab
```

### 4.3 状态表（唯一真相源）

Lua 版的核心资产是那张 `S` 表 + `leave_photo_mode()` 逐项还原。C++ 版照抄这个纪律：

```cpp
struct PhotoState {
    bool  active{};
    UObject* camera_actor{};
    UObject* prev_view_target{};
    float prev_fov{}, cur_fov{};
    bool  prev_ignore_move{}, player_hidden{};
    bool  prev_show_hud{}; bool hud_touched{};
    float prev_time_dilation{1.0f}; bool frozen{};
    std::vector<uint8_t> prev_pp_settings{};   // 整个 FPostProcessSettings 快照
    Vec3 loc{}; Rot rot{};                     // 相机位姿
    bool  menu_visible{};
};
```

规则：**任何"改游戏状态"的操作，都必须先写进这张表再改**；`leave()` 只认这张表，绝不反推。

---

## 5. 关键实现要点

### 5.1 反射层（不用游戏 SDK）

Lua 版用的 `StaticFindObject` / `ecall` / 属性读写，在 C++ 对应：

| Lua | C++（UEPseudo 具体名字以 P1 侦察为准） |
| --- | --- |
| `StaticFindObject("/Script/Engine.CameraActor")` | `UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.CameraActor"))` |
| `pc:GetWorld()` / `obj.Prop` | `GetValuePtrByPropertyNameInChain<T>(STR("PlayerCameraManager"))` 一类 |
| `ecall(obj, "SetViewTargetWithBlend", a,b,c,d,e)` | 参数缓冲 + `ProcessEvent(ufunc, parms)` |
| `ExecuteInGameThread` | 不需要：C++ 的 hook 回调本来就在游戏线程 |

**P1 的第一件事**就是把这层包成 3-5 个模板函数（`call_ufunc(name, args...)`、`get_prop<T>`、`set_prop`），
后面所有模块只用这层，避免每个模块各自踩反射的坑。

### 5.2 相机

跟 Lua 版完全一致的三步（已在实机验证成功，照搬）：

1. 取当前 POV（`PlayerCameraManager.GetCameraCachePOV`）→ 相机 Actor 就位（画面不跳）。
2. `World:SpawnActor(CameraActor class, loc, rot)`。
3. `PlayerController:SetViewTargetWithBlend(actor, 0, 0, false, false)`，**5 个参数**（Lua 版踩过 4 参的坑）。

C++ 额外优势：可以每帧直接写相机组件的 `FMinimalViewInfo`（若 UEPseudo 有该结构），或继续走 `K2_SetActorLocationAndRotation`，两者都能做，P2 里对比确定。

### 5.3 输入（本次最大的技术增量）

**第一步（P3a）：拿到完整状态。**
Lua 版没有"按住"API，只能 hook `InputKey/InputAxis` 自己记表。C++ 可以直接：

- hook `PlayerController:InputKey` / `InputAxis`（UFunction 级，post 回调），**或者**
- 在 tick 里 `GetAsyncKeyState(VK_LBUTTON / VK_RBUTTON / VK_MENU / VK_SHIFT ...)`（C++ 才能这么干）。

推荐：**鼠标增量走引擎**（`InputAxis` 的 MouseX/MouseY，灵敏度与游戏一致），**修饰键/左右键走 Win32**（简单可靠）。

**第二步（P3b，spike）：吞掉事件。** 三个候选，按顺序试：

1. UE4SS 的 hook API 若支持"跳过原函数"（`CallbackData` 带 skip / detour 能力），return 掉即可；
2. 用 UE4SS 暴露的 inline hook（PolyHook2）在 `UPlayerInput::ProcessInput` 上做 detour，摄影模式下把我们在用的键从事件里摘掉再放行；
3. 都不行 → 保留 Lua 版已验证的组合拳：`SetIgnoreMoveInput(true)` + `PopInputComponent`（C++ 里比 Lua 更安全：能精确判断组件来源）。

> 这一项是唯一"可能拿不到"的目标，所以设计上不让它成为依赖：**功能不依赖吞噬成功**，吞噬只是消灭"左键顺手挥刀"的残余风险。

### 5.4 时间冻结

同 Lua：`SetGlobalTimeDilation(0.0001)`，进入时快照、退出还原。
C++ 版可加一条 Lua 做不到的：真正的 `SetGamePaused(true)` + 相机 Actor 的 `PrimaryActorTick.bTickEvenWhenPaused = true`，
这样世界完全静止（连物理都停），而相机照动 —— 作为 `freeze_mode = dilation | pause` 选项给玩家选。

### 5.5 界面（两条路，先 A 后 B）

**路线 A：ImGui tab（P4 主线，1-2 天）**

```cpp
auto on_ui_init() -> void override {
    UE4SS_ENABLE_IMGUI();                       // 必须，否则崩
    auto& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f, nullptr,
                                 io.Fonts->GetGlyphRangesChineseFull());
    register_tab(STR("PBZ Photo Mode"), [](CppUserModBase* inst){ /* ImGui::SliderFloat ... */ });
}
```

要点：

- 中文靠 `msyh.ttc`（微软雅黑）加载字形；不加载就只有方块。
- 菜单在 UE4SS GUI 窗口里 → README 里要教用户开 `GuiConsoleEnabled=1` 并按 `Ctrl+O`。
- 同时保留"游戏内右下角纯文字状态条"（Lua 版那套 PrintString 思路），这样不开控制台也能看到 FOV/冻结状态。

**路线 B：UCanvas 自绘（P4b，可选 1-2 天）**

hook 视口绘制（`UGameViewportClient::Draw` 或 `AHUD` 相关 UFunction），从回调里拿 Canvas，
用 `K2_DrawText` / `K2_DrawLine` 画：标题、FOV 数值、滑条、三分线/黄金比辅助线。
这条才是"像真摄影模式"的形态；风险是时机（一帧内多次绘制）和字体对象获取，放 P4b 试，失败不影响主线。

### 5.6 后处理（C++ 的核心卖点之一）

相机组件上的 `PostProcessSettings`（`FPostProcessSettings`）整块快照 → 改 → 退出整块写回。
可调字段（都要同时置对应的 `bOverride_` 位）：

| 参数 | 字段 |
| --- | --- |
| 景深 | `DepthOfFieldFstop`、`ManualFocusDistance`、`DepthOfFieldDepthBlurAmount`、`DepthOfFieldNearBlurSize` |
| 曝光 | `AutoExposureMinBrightness`、`AutoExposureMaxBrightness`、`AutoExposureBias` |
| 调色 | `ColorSaturation`、`ColorContrast`、`ColorGain`、`SceneColorTint` |
| 氛围 | `VignetteIntensity`、`BloomIntensity`、`MotionBlurAmount` |

难点：`FPostProcessSettings` 是个很大的结构体，**字段偏移必须靠 UEPseudo 的类型定义**。
P5 里先打印结构体大小与关键字段偏移做校验，再写。

### 5.7 快门

沿用已验证的 `HighResShot`（`PlayerController:ConsoleCommand(FString, bool)`），C++ 里零风险。
进阶（可选）：自抓 backbuffer 存 PNG（要 D3D11 hook，工作量大，不进主线）。

### 5.8 配置与热重载

- `Mods/PBZPhotoModeCpp/config.ini`，自写 ~60 行 INI 解析（键值 + 注释 + 默认值兜底），不引入新依赖。
- `register_on_ue4ss_update` → `Ctrl+R` 就地重读，不用重启游戏。
- 配置项与 Lua 版 `config.lua` 一一对应，新增 `freeze_mode`、`ui_mode`(tab/canvas/both)、`dry_run`。

### 5.9 稳定性（C++ 版的命门）

| 措施 | 说明 |
| --- | --- |
| 每个 hook 回调 try/catch | Lua 靠 `pcall`，C++ 必须自己兜；异常绝不能穿过引擎栈 |
| 指针有效性校验 | 每帧校验相机/PC/Pawn，失效立刻 `leave()`（对应 Lua 版 check_state） |
| 换场景自动退出 | `Hook::register_load_map_...` 等价物 |
| safe mode | `config.ini` 里 `dry_run=1`：只打日志、不碰引擎 |
| 崩溃定位 | UE4SS 自带 CrashDumper + 我们的分级日志 |

---

## 6. 迁移映射表（Lua 1.0.4 -> C++）

| Lua 函数/函数块 | 规模 | C++ 目标 |
| --- | --- | --- |
| `enter_photo_mode()` | ~180 行 | `camera.cpp: enter()` + 填 `PhotoState` |
| `leave_photo_mode()` | ~80 行 | `camera.cpp: leave()` 逐项还原 |
| `tick()` | ~60 行 | `mod_app.cpp: on_update()` |
| `apply_pose` / `apply_fov` | ~30 行 | `camera.cpp` |
| `freeze_character_movement` | ~40 行 | 不再需要（改用 `SetIgnoreMoveInput` + 吞输入） |
| `on_input_key` / `on_input_axis` | ~120 行 | `input.cpp`（UFunction hook 或 Win32 状态） |
| `menu_text` / `refresh_menu` | ~90 行 | `ui_tab.cpp`（ImGui）或 `ui_canvas.cpp` |
| `apply_time_freeze` | ~20 行 | `time_ctl.cpp` |
| `parse_config` | ~80 行 | `config.cpp`（INI） |
| `log/ecall/pcall` 包装 | ~60 行 | `log.h` + `refl.h` |

> 现有 Lua 版 55KB 代码的价值不是被复用，而是**把"该怎么调用"全部验证过了**——
> 参数个数、失败分支、日志措辞、还原顺序，直接搬，不需要重新试错。

---

## 7. 里程碑与验收

| 阶段 | 内容 | 完成定义（DoD） | 预计 |
| --- | --- | --- | --- |
| **P0** 环境 | 装 Rust、`git fetch --unshallow`、checkout 出货 tag、`git submodule update --init`、编 UE4SS 出 `UE4SS.lib` | 一个打印 hello 的 DLL 被游戏加载，日志出现 mod 名 | 半天（+账号等待） |
| **P1** 侦察 | 探测 DLL：反射、属性读写、`ProcessEvent`、各类 hook 注册、ImGui tab 是否出现 | 产出 `docs/API_NOTES_CPP.md`，每条结论带可复现日志 | 半天 |
| **P2** 相机闭环 | F2 进/出、SpawnActor、5 参 SetViewTargetWithBlend、每帧位姿 | 画面切到自由相机、退出回到角色，与 Lua 版一致 | 1 天 |
| **P3** 输入 | 状态表（Alt/Shift/Ctrl/左右键/滚轮/WASD 按住）、视角增量 | 与 Lua 版手感一致；吞输入 spike 出结论 | 1-2 天 |
| **P4** 界面 | ImGui tab（中文、滑条、按钮、T 隐藏）+ 右下角状态条 | 鼠标可点、中文字形正常、不崩 | 1-2 天 |
| **P4b**（可选） | UCanvas 游戏内叠加 + 三分线 | overlay 与游戏 HUD 共存、无闪烁 | 1-2 天 |
| **P5** 时间/后处理 | dilation 与 pause 两模式；景深/曝光/调色 | 参数实时生效、退出字节级还原 | 1 天 |
| **P6** 快门/稳定性 | F9、换场景退出、异常保护、dry_run | 连续进出 20 次不崩、读档不残留 | 1 天 |
| **P7** 打包发布 | build/pack 脚本、README、测试清单、并行说明 | 用户按文档 3 步装好即可用 | 1 天 |

**总计 6-9 人日**（不含 Epic 审核等待）。P0 的账号门槛是唯一不可压缩的外部依赖。

---

## 8. 测试策略（本机没有游戏，怎么保证质量）

Lua 版的经验可以完整复用：

1. **纯逻辑抽出来单测**：配置解析、输入到动作的映射、位姿数学、状态机进出顺序 —— 放在不 include UEPseudo 的 TU 里，
   用自写断言（或 Catch2 单头）在本机跑，`build.ps1 test` 一键。
2. **干跑模式**：`dry_run=1` 时所有引擎调用改成只打日志，可以验证进出流程顺序而不碰游戏。
3. **上机清单**：把 Lua 版 `测试清单.md` 改写成 C++ 版（同样验收项 + 新增吞输入/界面/后处理三项）。
4. **回归对照**：同一操作，Lua 版与 C++ 版的日志应能一一对应（保留 `[PBZPM]` 前缀与相同措辞）。

---

## 9. 风险与对策

| 风险 | 概率 | 影响 | 对策 |
| --- | --- | --- | --- |
| Epic 账号绑定迟迟不通 | 中 | 全线阻塞 | 先做所有非编译工作；同时找分叉作者要 dev 包 |
| ABI/CRT 不匹配 → 加载即崩 | 中 | 高 | 严格按出货 tag 构建、对齐 `Game__Shipping__Win64`；失败走官方模板仓库 |
| 输入 hook 导致崩溃/卡死 | 中 | 高 | 先只用 `SetIgnoreMoveInput`（已验证）；inline detour 放独立开关后面 |
| 吞输入做不到 | 中 | 低 | 功能不依赖它 |
| ImGui 中文字体加载失败 | 低 | 低 | 退回英文界面并日志提示 |
| UE4SS 版本更新导致失效 | 高（长期） | 中 | README 写清绑定版本；重编脚本化 |
| 玩家安装复杂度上升 | 中 | 中 | 提供开箱即用的 ini 片段 + 一键脚本 |

---

## 10. 交付物

1. `PBZPhotoModeCpp/` 完整源码（CMakeLists、build.ps1、pack.ps1）。
2. 发布包 `PBZPhotoModeCpp_v0.x.zip` → `Mods/PBZPhotoModeCpp/{dlls/main.dll, config.ini, enabled.txt}`。
3. `docs/API_NOTES_CPP.md`：P1 侦察结论（hook/反射/ImGui 逐条实测）。
4. `docs/README.md` + `docs/测试清单.md`（含 `GuiConsoleEnabled=1` 与 `Ctrl+O`）。
5. 与 Lua 版并存说明（mod 名不同，默认只开一个，避免抢 F2）。

---

## 11. 需要你现在定的 5 件事

1. **Epic 绑定**：你能去做 GitHub↔Epic 绑定（拿 UEPseudo）吗？还是走"向分叉作者要 dev 包"？这决定 P0 能否开始。
2. **Rust**：允许我在这台机器上装 Rust 工具链吗（约 300MB）？
3. **界面形态**：先做 ImGui tab（快、丑、要开控制台）还是直接冲 UCanvas 游戏内叠加（慢、像真 HUD）？
4. **Lua 版**：保留 v1.0.4 作为默认发行、C++ 版做 beta 并行（推荐），还是直接替换？
5. **发行范围**：自用 / 小范围 / 公开分发？（影响 ABI 兼容矩阵与测试强度）

---

## 附录 A：本方案已核实的证据

| 结论 | 证据 |
| --- | --- |
| 出货 DLL 带 C++ mod 加载器 | `ue4ss/UE4SS.dll` 内字符串含 `start_mod`、`CppUserModBase`、`main.dll` |
| mod 可注册 ImGui tab | `UE4SS/include/Mod/CppUserModBase.hpp:226` `register_tab(...)`；`GUI/GUITab.hpp:20` `RenderFunctionType = void(*)(CppUserModBase*)` |
| ImGui 必须先启用 | 官方文档：`on_ui_init` 里必须 `UE4SS_ENABLE_IMGUI()`，否则渲染即崩 |
| UEPseudo 私有 | `.gitmodules` 指向 `git@github.com:Re-UE4SS/UEPseudo.git`；`git ls-remote` → Repository not found |
| 无 `Unreal/` 头文件 | `ls UE4SS/include` 无 `Unreal` 目录（只有 Mod/GUI/Input/LuaType/...） |
| ABI 与 CRT 强绑定 | `CppUserModBase.hpp` 类注释原文（见 §3.3） |
| 出货 tag | `git rev-parse v3.0.1-1183-gcdbc0b66` → `aedfc5bc`；本机克隆是 shallow（需 `--unshallow`） |
| 安装形态 | 官方文档：`Mods/<ModName>/dlls/main.dll` |
