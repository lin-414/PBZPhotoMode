# 进度存档（2026-10-30 更新）

接着做的入口：读这份文件 → 看「下一步」→ 直接干。

## 一句话状态

**C++ 版 v1.0.13 已编译、已打包**：
`build-dll/PBZPhotoModeCpp.dll` → `dist/PBZPhotoModeCpp_1.0.13-cpp.zip`。
核心层 **257 项测试全绿**；v1.0.11 F 键冻结已实机验证；v1.0.12 新增 HUD 自动隐藏；v1.0.13 移除实验性的 F9 截图功能。

## 已完成

| 项 | 状态 |
| --- | --- |
| GitHub ↔ Epic 绑定 | 你已绑定，`UEPseudo` 拉取成功 |
| UE4SS 子模块 | `deps/first/Unreal`(UEPseudo) + `deps/first/patternsleuth` 均已检出 |
| Rust 工具链 | `rustc/cargo 1.99.1`，在 `%USERPROFILE%\.cargo\bin`（不在默认 PATH，`build_dll.bat` 会自动加） |
| C++ 核心层 | `src/core/` 全部完成，`tools\build_tests.bat` → **257 项断言全绿** |
| C++ 引擎层 | 已按 UEPseudo 真实 API 重写并在 MSVC 19.44 + Ninja 下**编译通过** |
| C++ 输入层 | `src/win_input.cpp`：低级鼠标钩子 + `GetAsyncKeyState` 轮询（UE4SS C++ 没有 keyup） |
| 发布包 | `dist/PBZPhotoModeCpp/`（dlls/config.ini/enabled.txt/README.md）→ `dist/PBZPhotoModeCpp_v1.0.0-cpp.zip` |
| 文档 | `docs/README.md`、`docs/BUILD.md`（含 6 条编译踩坑表）、`docs/P1_CHECKLIST.md` |

## v1.0.1 修了什么（2026-10-30 第二次提交）

实机反馈三条：按住 Alt 转不了视角 / 角色消失 / 左右键仍触发游戏动作。

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| Alt+鼠标 无反应 | `SetWindowsHookEx(WH_MOUSE_LL)` 的回调只派发给安装钩子的线程，且要求该线程 pump message；UE4SS 的 init 线程不 pump → 钩子"装成功"但永不回调（所以日志没报错，键盘却能用的原因：键盘走 `GetAsyncKeyState` 轮询） | 改成 Raw Input：专用线程 + 隐藏窗口 + `RIDEV_INPUTSINK` + 自己的消息循环；`GetCursorPos` 差分兜底（raw 连续 5 帧无事件才接管，避免重复计数） |
| 进模式后角色消失 | `hide_player` 默认 `true` | 默认改 `false`（摄影模式要能拍自己）；需要时配置里打开 |
| 左右键仍攻击/交互 | 只调了 `SetIgnoreMoveInput`，它只管移动轴 | 新增 `EnginePort::set_game_input_blocked`：PC/Pawn 的 `InputComponent.bBlockInput` + `DisableInput`/`EnableInput` + `SetIgnoreMoveInput`/`SetIgnoreLookInput`；失败只 warn 不阻断 |

顺带：滚轮调 FOV 不再要求按住 Alt；进模式时多一行 `鼠标输入源 raw=1 事件计数=N` 诊断日志；
`attach_view` 成功后回读 `ViewTarget` 校验（不一致就 warn，专治"对着没人看的相机使劲"）。

新增测试 `test_photo_mode_input_blocking`（4 组场景），核心层 217 → **236 项全绿**。

## v1.0.2 修了什么（2026-10-30 第三次提交）

反馈：按住 Alt 仍不能转视角；F2 进入后没有任何界面。

| 现象 | 根因 | 修法 |
| --- | --- | --- |
| 进模式后无任何界面 | `KismetSystemLibrary::PrintString` 调用成功、无报错，但本作不把引擎 on-screen debug message 画到屏幕 | 改 hook 游戏 HUD 的 `DrawHUD`（用实例类 `GetFunctionByNameInChain` 查，覆盖蓝图子类），在 post 回调里用 `AHUD::DrawText` + `Canvas` 画字；按 `Canvas.SizeX/SizeY` 定位右下角；`GetTextSize` 量尺寸；PrintString 降级为兜底 |
| Alt+鼠标无反应 | `RegisterRawInputDevices` 返回成功（raw=1）但事件计数=0 → 装上了收不到 | ① 每秒一行输入诊断（前台/raw/raw失效/鼠标事件/alt/dx/dy）；② Raw Input 自动判死：光标连续 30+ 帧在动而 raw 无事件 → 永久切 `GetCursorPos` 差分；③ 参数名自适应（`ParamBlock::prop`：候选名→按声明顺序） |
| （附带）ViewTarget 可能没真接管 | 之前只回读 `ViewTarget` 对象属性（它是 FViewTarget 结构体，读不到） | 用 `read_struct_object(pc, "ViewTarget", "Actor")` 正确回读，成功打 `相机接管确认`，失败打 `ViewTarget 校验失败` |

## 下一步（按顺序）

1. **实机验证**：把 zip 里的 `PBZPhotoModeCpp` 文件夹丢进
   `<游戏>\platform\steam\ue4ss\Mods\`，启动游戏，搜 `UE4SS.log` 里的 `[PBZPhotoMode]`。
   预期日志：`加载完成` → 按 F2 后 `进入摄影模式`。
2. 如果 `SetViewTargetWithBlend` / 相机创建报错，日志会写 `进入摄影模式失败：<原因>`，
   把那段贴回来（C++ 版的报错点更精确）。
3. 截图请交给 NVIDIA App / Steam 等系统工具，Mod 不内置截图。

## 关键环境事实（明天不用重查）

```
UE4SS 源码      D:/Documents/DSH/PBZ/_ref/ue4ss-src   （已 unshallow，tag: v3.0.1-1183-gcdbc0b66）
UEPseudo        .../deps/first/Unreal                  （有 include/ 与 generated_include/）
C++ 项目        D:/Documents/DSH/PBZ/PBZPhotoModeCpp
网络            GitHub 走代理 http://127.0.0.1:7897（时好时坏；直连不通）
子模块地址是 SSH  git@github.com:...，本机无 SSH key，
                用 `git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive`
```

## 今天最值钱的产出：已核实的 UE4SS C++ API

明天改 `ue4ss_port.cpp` / `mod_app.cpp` 时按这个写，别再猜了。

### 命名空间与头文件

```cpp
#include <Mod/CppUserModBase.hpp>          // 注意是 Mod/ 子目录，不是平铺
#include <UE4SSProgram.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

using namespace RC;
using namespace RC::Unreal;
```

mod 类继承 `RC::CppUserModBase`（**带 RC:: 前缀**），`ModName = STR("...")`（`STR` 展开成 `L"..."` 宽字面量）。

### 找对象 / 读属性 / 调函数

```cpp
// 按路径找（含 CDO）
UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));

// 场景里第一个实例（按类名 + 继承）
UObject* pc = UObjectGlobals::FindFirstOf(STR("PlayerController"));

// 读属性：模板化，按名字
FVector loc = *pc->GetValuePtrByPropertyNameInChain<FVector>(FromCharTypePtr<TCHAR>(STR("RelativeLocation")));
FProperty* prop = pc->GetPropertyByNameInChain(FromCharTypePtr<TCHAR>(STR("RelativeLocation")));
void* raw = prop->ContainerPtrToValuePtr<void>(pc);

// 调 UFUNCTION
UFunction* fn = obj->GetFunctionByNameInChain(FromCharTypePtr<TCHAR>(STR("SetViewTargetWithBlend")));
obj->ProcessEvent(fn, &params);          // UObject::ProcessEvent(UFunction*, void* Parms)
```

**参数布局不要硬编码**：UE5 的 `FInputKeyParams`、`FActorSpawnParameters` 等结构布局会变。
正确做法是反射遍历参数属性，按名字 + 类型写值（`FProperty::IsA<FFloatProperty>()` 之类），
嵌套结构用 `FStructProperty::GetStruct()` 拿到 `UScriptStruct` 再 `GetPropertyByNameInChain`。
`FStructProperty::GetStruct()` 实测返回 `TObjectPtr<UScriptStruct>&`（声明在
`generated_include/MemberVariableLayout_HeaderWrapper_FStructProperty.hpp`）。

### Hook

```cpp
// 全局引擎 tick（游戏线程，带 dt）——就用它驱动 PhotoMode::tick
RC::Unreal::Hook::RegisterEngineTickPreCallback(
    [](UEngine* Context, float DeltaSeconds) { /* ... */ });

// 挂 UFUNCTION（等价于脚本层的 RegisterHook）
auto ids = UObjectGlobals::RegisterHook(
    STR("/Script/Engine.PlayerController:InputKey"),   // 字符串重载
    preCallback, postCallback, customData);            // UnrealScriptFunctionCallable
```

回调类型（`Unreal/UFunctionStructs.hpp`）：

```cpp
using UnrealScriptFunctionCallable = std::function<void(UnrealScriptFunctionCallableContext& Context, void* CustomData)>;

// Context.TheStack.Locals() 就是参数内存块
template<typename ParamStruct> auto GetParams() -> ParamStruct&;   // 或
Context.SetReturnValue<T>(v);
```

### 输入 & ImGui

```cpp
// 只有 keydown，没有 keyup —— 所以"按住"必须靠 hook InputKey/InputAxis，不能靠这个
register_keydown_event(Input::Key::F2, []{ /* 开关 */ });

// ImGui tab：用 register_tab，mod 析构时自动清理
register_tab(STR("摄影模式"), [](CppUserModBase* instance) { ImGui::Text("..."); });

// on_ui_init 里必须调用，否则一画就崩
UE4SS_ENABLE_IMGUI()
```

### 日志

```cpp
Output::send<LogLevel::Verbose>(STR("中文 {}\n"), value);   // std::format 风格，不是 printf
```

### 构建配置名（容易踩）

UE4SS 用自定义构建类型：**`Game__Shipping__Win64`**，不是 `Release`。

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64
cmake --build build
```

## 下一步（按顺序）

1. `tools/build_dll.bat` —— 现在默认参数是 `-DCMAKE_BUILD_TYPE=Release`，**先改成 `Game__Shipping__Win64`**，
   否则 ABI/符号对不上。跑起来后先让 UE4SS 本体编译通过（第一次会很慢，十几分钟量级）。
2. 按上面「已核实的 API」重写这两个文件（其余不用动）：
   - `src/engine/ue4ss_port.cpp` + `src/engine/ue4ss_port.h`
   - `src/mod_app.cpp` + `src/mod_app.h`
   - `dllmain.cpp`（include 与 `RC::CppUserModBase` 前缀）
3. 输入方案（沿用已验证的思路）：
   - 开关类（F2/T/F/FOV）→ `register_keydown_event` 足够
   - 按住类（Alt/WASD/QE/左右键/鼠标位移/滚轮）→ hook `PlayerController:InputKey` + `InputAxis`，
     反射读 `FInputKeyParams` 里的 Key/Event/Delta，`FKey` 是含 `KeyName`(FName) 的结构体
   - 鼠标轴名：`MouseX` / `MouseY`，滚轮看 `MouseWheelAxis` 或 `MouseScrollUp/Down`（两个都要兜）
4. 编译通过后按 `docs/P1_CHECKLIST.md` 逐条核对 12 个辅助函数，再进游戏验收。

## 明天开工第一句

> 「继续做 C++ 版，先跑 tools/build_dll.bat 把构建配置改成 Game__Shipping__Win64」

## 备忘

- 核心层测试不要动坏：`tools\build_tests.bat` 必须保持 217 项全绿。
- 相机/时间/输入的所有顺序与回滚语义都已经锁在测试里，改引擎层不需要碰 `src/core/`。
- 进游戏验证前先确认 `mods.txt` 里没有其他摄影 Mod 占用 F2。

## 1.0.3-cpp（诊断版）

- 第三次真机日志结论：Raw Input 装了但收不到事件（「鼠标事件=595」后不再增长），所以「按住 Alt 不转」「滚轮不调 FOV」是同一个输入源问题；「HUD 上找不到 DrawHUD」说明 Canvas 画字路线在 PBZ 走不通。
- 改动：`src/engine/ue4ss_port.cpp` 新增 `hook_input_axis()`（hook `PlayerController::InputAxis`，读 `Key.KeyName` + `Delta`/`Value`）、`on_axis_event()`、`dump_engine_state()`、`camera_state_summary()`、`view_verified()`；`src/mod_app.cpp` 新增 `ensure_axis_hook()`、轴增量合并、`retry_view_attach()`；`src/win_input.cpp` 支持静音系统层鼠标（避免与引擎增量重复计数）。
- 版本 `src/core/version.h` → `1.0.3-cpp`；产物 `build-dll/PBZPhotoModeCpp.dll`（570,880 B）；核心层测试全绿；包 `dist/PBZPhotoModeCpp_v1.0.3-cpp.zip`。
- 下一步等用户日志：若「PC 输入相关函数」里没有 InputAxis/InputKey，就得换输入方案（hook InputComponent 或 PlayerTick）；若 MyHUD 为空，右下角菜单要改用 UMG widget 而不是 Canvas。

## 1.0.4-cpp（按第四次真机日志修）

日志证据：`PC 输入相关函数` 无 InputAxis/InputKey（Enhanced Input）；`raw=1 事件计数=0 raw失效=1`；`HUD 上找不到 DrawHUD` 但 `HUD=BP_PBZHud_C` 有 `ReceiveDrawHUD`；`读不到 PlayerController.ViewTarget.Actor`；相机生成在角色原点、朝向 (0,0)。

改动：
- `src/engine/ue4ss_port.cpp`：新增 `read_mouse_delta()`（调 `PlayerController.GetInputMouseDelta`）；`read_pov()` 优先用 `PlayerCameraManager.GetCameraLocation/GetCameraRotation`；`attach_view()` 校验改读 `PlayerCameraManager.ViewTarget.Target`；`ensure_overlay_hook()` 候选函数改 `ReceiveDrawHUD`/`DrawHUD`/`PostRender`；`dump_engine_state()` 增打印管理器 ViewTarget。
- `src/win_input.cpp/.h`：新增 `WH_MOUSE_LL` 低级鼠标钩子（滚轮来源），`ll_ready()` / `ll_wheel_events()`。
- `src/mod_app.cpp`：每帧读引擎鼠标增量并喂给 `InputLogic`，成功后静音系统层鼠标；诊断行加「滚轮钩子/滚轮事件」；ViewTarget 未确认时 0.5s 重试一次。
- `src/core/photo_mode.cpp`：屏蔽输入成功时也打日志。
- `config.ini.example`：`hide_player = false`。
- 版本 `1.0.4-cpp`；产物 577,536 B；核心层测试全绿；包 `dist/PBZPhotoModeCpp_v1.0.4-cpp.zip`。

下一步（等日志）：若 `管理器 ViewTarget=` 显示的不是 CameraActor，说明游戏每帧自己抢回视角，需要在 PC 的 tick 之后（post hook）再夺一次；若 `引擎鼠标增量生效` 没出现，改用 hook `BP_PlayerController_C` 的输入或 UMG 层。

补充（同一版内）：`src/engine/ue4ss_port.cpp` 增加 `reassert_view_target()`，`src/mod_app.cpp` 用 `Hook::RegisterEngineTickPostCallback` 在**所有 actor/PC Tick 之后**每帧重新夺一次 ViewTarget —— 有些游戏的 BP 每帧自己 SetViewTarget，只在 pre-tick 抢是抢不过的。产物 578,048 B，测试全绿，zip 已覆盖。

## 1.0.5-cpp（修 1.0.4 的启动崩溃）

崩溃分析（`crash_2026_10_30_09_49_58.8582240.dmp`，用 `_ref/dumpinfo.py` 解析）：
- 异常 `0xC0000005 ACCESS_VIOLATION`，故障地址 `UE4SS.dll+0x40c1de`，线程 33912。
- 时间线：09:49:39 启动 → 09:49:49 我们的 mod `加载完成 v1.0.4-cpp` → 09:49:57.8 UE4SS 哈希自检完成（首个 EngineTick）→ 09:49:58.0 崩。日志里**没有任何** `输入诊断`/`引擎对象`/`进入摄影模式`，也没有 LoadMap 事件 → 崩在游戏启动、地图还没加载的时候。
- 根因（1.0.4 引入的我的 bug）：`read_mouse_delta()` 被放在每帧 tick 里**无条件**调用，它内部会 `FindAllOf("PlayerController")`。启动阶段 UWorld 还没建，这个查询把 UE4SS 打崩了。1.0.3 没这问题，因为不进模式时完全不碰引擎。
- 修法：`src/mod_app.cpp` 的引擎鼠标增量读取加 `mode_->active()` 守卫；1.0.5 起「没进摄影模式就绝不调引擎接口」。
- 另加排障开关：mod 目录放 `no_wheel_hook.txt` 就不装 `WH_MOUSE_LL`（万一后续怀疑滚轮钩子）。

产物 584,704 B；核心层测试全绿；包 `dist/PBZPhotoModeCpp_v1.0.5-cpp.zip`。
新增工具：`_ref/dumpinfo.py`（读 dmp 的异常码/故障模块）、`_ref/dumpstack.py`（扫异常线程栈）。

## 1.0.6-cpp（视角不转的真正原因：相机变换被引擎拒绝 + 引擎增量没放大）

1.0.5 日志证据：
- `相机接管确认：ViewTarget 已是摄影相机`、`管理器 ViewTarget=CameraActor ...` → **视角确实被我们的相机接管了**，之前怀疑的「游戏抢视角」不成立。
- `屏幕菜单：钩住 HUD.ReceiveDrawHUD` → 菜单钩子挂上了。
- `已屏蔽游戏键鼠输入（角色不会跟着 WASD 走）` → 输入拦截生效。
- `引擎鼠标增量生效：dx=0.1 dy=0.2` → 引擎增量通道打通了，但**量级只有 0.1~0.2**（引擎给的是「像素÷游戏鼠标灵敏度」），乘 core 的 0.1 度/单位 ≈ 0.01°/帧，肉眼完全看不出来。
- 每秒诊断里 `alt=1 dx=-4…10` 忽大忽小 → 系统层 Raw 和引擎增量两个源交替喂进来，抖动。

修法：
- `src/mod_app.cpp`：引擎增量乘标定系数 `kEngineMouseScale = 40`（0.1 → 4 单位 ≈ 0.4°/帧）；引擎源可用后系统层鼠标一律静音；诊断行恢复相机状态并加 `引擎增量峰值`。
- `src/engine/ue4ss_port.cpp`：`apply_pose()` 拆成 `K2_SetActorLocation` + `K2_SetActorRotation` 两次调用，失败会打 `相机位姿应用失败：移动=… 旋转=…`（以前静默）；`spawn_camera()` 之后强制把根组件与相机组件的 `Mobility` 写成 `Movable(2)` —— 生成的 CameraActor 若是 Static，位置/旋转会被引擎静默拒绝，现象正是「WASD 能动但视角不转、方向和画面对不上」。
- `camera_state_summary()` 增加 `管理器朝向=(pitch,yaw)`，用来验证相机的变换是否真的生效（管理器视角跟着我们相机走才算成功）。

产物 588,288 B；核心层测试全绿；包 `dist/PBZPhotoModeCpp_v1.0.6-cpp.zip`。

## 1.0.8-cpp（按 2026-10-30 12:25 真机日志修：朝向永远 0 + 菜单不可见）

日志证据（UE4SS.log 815-860 行）：
- 进入摄影模式后 `管理器视角` 会跟着我们的相机位置变化，但 `管理器朝向=(0.0,0.0)`、`我们的相机朝向=(0,0)` 始终不变；
  同时 `输入诊断 alt=1 dx=420.6 dy=-11.4` 说明鼠标增量已经进入 core，core 也改了 pose 的 yaw/pitch —— 问题不在输入，在“把旋转写到相机 Actor”这一步。
- `HUD绘制=0`：ProcessEvent 过滤 `ReceiveDrawHUD` 从来没有命中，说明 `BP_PBZHud` 的绘制不走 AHUD::DrawHUD；
  之前只把 PrintString 当“钩子失败才用”的兜底，所以一直没有真正推送菜单。

根因：
1. **UEPseudo 的 `AActor::K2_SetActorRotation` 包装有 UE5 LWC 兼容 bug**：它用 `UE_COPY_STRUCT_INNER_PROPERTY(NewRotation, Pitch, float, ...)`
   往 `FRotator` 里写 **float**，但 UE5 的 `FRotator` 是 3 个 **double**（24 字节）。
   实际只写进 double 的低 4 字节，数值变成 ~1e-315，等价于 0 → 相机永远朝向 (0,0)。
   位置用的 `K2_SetActorLocation` 走的是分版本的 `UE_COPY_VECTOR`（>=5.0 用 double），所以位置正常、旋转失效。
2. **`KismetSystemLibrary::PrintString` 的参数名是 `duration`（小写）**，旧代码写的是 `"Duration"`；
   `find_property` 按 FProperty::GetName() 大小写敏感匹配失败，ParamBlock 静默返回，duration 保持 0，屏幕消息立刻消失 —— 看起来像“PrintString 调用成功但屏幕没有”。

修法：
- `src/engine/ue4ss_port.cpp`：新增 `set_actor_rotation()`，按反射属性写 `NewRotation.Pitch/Yaw/Roll`（自动识别 FDoubleProperty），`apply_pose` 改用它；
  失败日志会打印目标 loc/rot，便于下次直接定位。
- `show_overlay()` 改为：HUD Canvas 只有在 `overlay_hook_hits_ > 0` 时才作为主路径；否则每 0.35 秒用 `PrintString` 重推菜单（`duration=1.0`，同一个 FName Key），并一次性把 `UEngine.bEnableOnScreenDebugMessages/Display` 置 true（Shipping 配置可能关掉屏幕消息）。
  PrintString 文本按视口尺寸补空行/空格推到右下角（`build_print_menu_text`），保持原有观感。
- `hide_overlay()` 用同一个 Key 推空消息，T 隐藏/退出时立即擦掉。
- 版本 `1.0.8-cpp`；核心层测试保持 236 项全绿。产物 595,456 B；包 `dist/PBZPhotoModeCpp_v1.0.8-cpp.zip`；已覆盖游戏目录 `ue4ss\Mods\PBZPhotoModeCpp\dlls\PBZPhotoModeCpp.dll`。

## 1.0.9-cpp（2026-10-30 13:02 真机日志：鼠标 Y 反向 + 滚轮 FOV 反向 + R 重置）

日志证据（UE4SS.log 840-843）：`引擎鼠标增量生效：dx=-0.21 dy=0.21`；随后 `输入诊断 alt=1 dx=30.8 dy=30.8`，
但 `管理器朝向` 从 `(0.3,-149.3)` 变成 `(-53.7,-178.4)`：鼠标上移时 pitch 反而大幅下降。
引擎的 `MouseY` 与 `GetInputMouseDelta` 都是「向上为正」，而 core/Win32 的 `Intent.look_dy` 是「向下为正」，
旧代码没有翻转 Y，导致 Alt + 鼠标上下相反。

同段日志 884-905 的 FOV 变化也验证了滚轮方向：`Intent.wheel` 正值应为滚轮向上，旧代码 `fov + wheel*step` 会把“向上”变成放大 FOV。

改动：
- `src/mod_app.cpp`：引擎 `GetInputMouseDelta` 的 `dy` 取反；`InputAxis` 的 `axis_dy_` 取反；去掉 `handle_intent` 里重复的 `toggle_freeze`（`PhotoMode::tick` 已处理，之前会双翻转）。
- `src/core/photo_mode.cpp`：滚轮改为 `fov - wheel * fov_step`；新增 R 重置块，恢复进入时保存的 `reset_pose_` / `reset_fov_`。
- `src/core/photo_mode.h`：保存进入时快照；`src/core/input_logic.*`、`config.*`：新增 `key_reset`（默认 `R`）与 `Intent.reset_view`；菜单提示加 R。
- `config.ini.example` / 已安装 config.ini / dist config.ini：新增 `key_reset = R`。
- 核心层测试 236 → **252 项全绿**；版本 `1.0.9-cpp`；产物 595,968 B；包 `dist/PBZPhotoModeCpp_v1.0.9-cpp.zip`；已覆盖游戏目录 DLL。


## 1.0.10-cpp（2026-10-30 13:25 真机日志：F 键冻结时间不生效）

日志证据：
- 13:16:59 按 F 后有一行 `[PhotoMode] 时间已冻结`，13:17:07 又有一行 `时间已恢复` → F 键轮询、
  `Intent.toggle_freeze`、`PhotoMode::toggle_freeze` 整条链路是通的，不是按键没响应。
- 但冻结期间（13:17:01~13:17:03）相机/世界仍在动，说明 `UGameplayStatics::SetGlobalTimeDilation` 只是“调用成功”，
  实际生效倍率没有变。

根因：UE5 的 `UGameplayStatics::SetGlobalTimeDilation` 最终走到 `AWorldSettings::SetTimeDilation`：
1. `TimeDilation` 会被 `MinGlobalTimeDilation` / `MaxGlobalTimeDilation` 夹紧；
2. `GetEffectiveTimeDilation()` 在 `bAllowTimeDilation=false` 时直接返回 1.0，完全无视 `TimeDilation`。
PBZ 的 WorldSettings 很可能把 `bAllowTimeDilation` 关了（或把 Min 夹到 1.0），所以旧版日志显示“时间已冻结”但世界照常跑。

改动：
- `src/engine/ue4ss_port.cpp/.h`：新增 `world_settings()`（先从 `UWorld.PersistentLevel.WorldSettings` 读，失败再走 `UWorld::K2_GetWorldSettings`）。
- `set_time_dilation()` 改为直接写 `AWorldSettings.bAllowTimeDilation / TimeDilation / MinGlobalTimeDilation / MaxGlobalTimeDilation`，
  再调用一次 `SetGlobalTimeDilation` 同步引擎侧状态，并回读 `GetGlobalTimeDilation` 校验；第一次改时间前保存原始四个字段，解冻/退出时原样还原。
- `get_time_dilation()` 增加直接读 WorldSettings 的兜底；`dump_engine_state()` 进入摄影模式时会多打一行 `时间设置：WorldSettings bAllowTimeDilation=... TimeDilation=... Min=... Max=...`。
- 版本 `1.0.10-cpp`；核心层测试保持 252 项全绿；产物已重新打包为 `dist/PBZPhotoModeCpp_1.0.10-cpp.zip`，通过模组管理器安装（未覆盖游戏目录）。

## 1.0.11-cpp（2026-10-30 15:50 日志：TimeDilation=0.0001 生效，但 SGame 动作仍不冻结）

日志证据：15:50:15 的 `时间设置目标=0.0001 生效=0.0001 字段写入=ok/ok` 说明 `AWorldSettings` 已经改成功；
但用户确认角色/敌人/NPC 的动作与特效仍在播放 → PBZ 的 SGame 玩法 tick 不吃全局 `TimeDilation`。

改法：
- 默认 `freeze_mode` 改为 **pause**（`src/core/config.h`、`config.ini.example`、dist config）。
- `src/engine/ue4ss_port.cpp` 的 `set_paused()` 重写：
  - 暂停前把 `APlayerController.bShouldPerformFullTickWhenPaused` 设为 true（相机管理器继续 Tick，摄影相机可移动），
    解冻后恢复原值；
  - 临时把 `AGameModeBase.bPauseable` 设为 true，避免 `PlayerController::SetPause` 被 GameMode 拒绝；
  - `SetPause` 返回值通过 `invoke_bool` 读取，失败再走 `UGameplayStatics::SetGamePaused`，最后用 `IsGamePaused` 回读确认。
- 保留 `dilation` 模式（config 改回 `freeze_mode = dilation` 即可）。
- 版本 `1.0.11-cpp`；核心层测试 252 项全绿；包 `dist/PBZPhotoModeCpp_1.0.11-cpp.zip`。

### 1.0.11 实机验证（2026-10-30 16:04）

用户日志（UE4SS.log 843/845/855）：`游戏暂停成功 | IsGamePaused=true` → `时间已冻结`；暂停期间仍可用 Alt+鼠标转视角、WASD 移动相机；解冻 `IsGamePaused=false` → `时间已恢复`。**F 键冻结时间已确认可用。**

## 1.0.12-cpp（2026-10-30：进入相机模式时隐藏游戏内 HUD）

需求：进入摄影模式时隐藏游戏内 HUD。

问题：旧版 `hide_game_hud` 只是 `PlayerController.MyHUD.SetActorHiddenInGame`。本作 HUD 是纯 UMG，
AHUD Actor 上没有可绘制组件，所以这个开关看起来没有效果。CXXHeaderDump 证据：
`W_FrontEndHUDLayout_C : UPBZHUDLayout`，HP/快捷栏/武器/任务/Boss 血条都是它的子控件。

改动：
- `src/engine/ue4ss_port.cpp/.h`：
  - **主路径**：调用游戏自带的 `UPBZUIManagerSubsystem::SetHideUIForPV(true/false)`。它会广播
    `OnHideUIForPVChanged`，Boss 血条、教程提示、世界空间 HUD 等订阅者一起隐藏/恢复。
  - **补充路径**：枚举 `W_FrontEndHUDLayout_C`（找不到时退 `PBZHUDLayout`，再退 `WB_OverLayerOut_C`），
    记录每个控件的原始 `Visibility` 后设为 `Collapsed`；退出时按原值恢复。
  - 新增 `set_widget_visibility()` / `get_widget_visibility()`，走 `UWidget::SetVisibility(ESlateVisibility)`；
    反射层补了 `FEnumProperty` 的读写（`ESlateVisibility` 参数在 UE5.7 里是枚举属性）。
  - AHUD 的 `SetActorHiddenInGame` 保留为最后的兜底。
  - `set_hud_visible()` 的返回值改为“游戏 UIManager 或 UMG 根控件是否真的隐藏成功”；HUD 还没建好时返回 false，交给 core 重试。
  - `dump_engine_state()` 增加 `UIManager=... SetHideUIForPV=有/无` 和 `HUD控件=... Visibility=...` 诊断；找不到根控件时列出运行时带 HUD 的 Widget 类。
- `src/core/config.h`、`config.cpp`、`config.ini.example`、dist/已安装 config：`hide_game_hud` 默认改为 `true`。
- `src/core/photo_mode.*`：`hide_game_hud=true` 且进入时隐藏失败，每秒重试一次；成功后记入 `hud_hidden_`，退出时按原可见性恢复。
- 版本 `1.0.12-cpp`；核心层测试 252 → **258 项全绿**。
- 待真机验证：进入 F2 后看日志是否有 `UMG HUD 已隐藏：控件数=...`，以及画面 HUD 是否消失；退出 F2 后是否恢复。

## 1.0.13-cpp（2026-10-30：移除实验性的 F9 截图）

- 曾尝试给 C++ 版加入 F9 截图：`PlayerController.ConsoleCommand` 在 PBZ 的 `BP_PlayerController_C` 上不存在；改走 `UGameViewportClient::ConsoleCommand` 的 vtable 偏移属于未验证的 ABI 调用，存在崩溃风险。
- 已移除 `key_shutter`、`screenshot_command`、`run_command()`、`poll_screenshot()` 及菜单/ImGui 中的截图入口。
- 截图请使用 NVIDIA App / GeForce Experience（默认 `Alt + F1`）或 Steam（`F12`）等系统工具。
- 版本 `1.0.13-cpp`；核心层测试 258 → **257 项全绿**（移除截图测试）。
