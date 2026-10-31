# 进度存档（2026-10-31 更新）

接着做的入口：读这份文件 → 看「下一步」→ 直接干。

## 一句话状态

**C++ 版 v1.5.0 已编译**（新增：超宽屏支持——进入摄影模式解除 16:9 画幅锁消除两侧黑边，`ultrawide_unlock` 配置，退出还原；上一版 DualSense/DualShock4 手柄 DInput8 后端；WBP 资产菜单骨架就绪，**资产（Phase 3）未制作，当前装包会自动落回 1.2.x 文字面板**）。
核心层 **527 项测试全绿**；v1.2.3 UMG 文字面板（命中区偏移待实机验证，现为 WBP 失败兜底）；v1.1.x ImGui 直绘在帧生成环境下已判死；v1.0.19-1.0.20 UMG 文字菜单实机验证通过。

## 交付流程（2026-10-31 起固定）

- **只出包，不部署**：构建完成后把产物打进 `dist/PBZPhotoModeCpp_<版本>-cpp.zip`（结构：`PBZPhotoModeCpp/{dlls/PBZPhotoModeCpp.dll, config.ini, enabled.txt, README.md}`），**不要**把 DLL/README/config 覆盖到游戏目录——用户用模组管理器自行安装 zip。
- 游戏目录 `platform\steam\ue4ss\Mods\PBZPhotoModeCpp\` 当前留着 1.0.19 那份（供本次实机验证），之后构建/交付流程不再碰它。

## 已完成

| 项 | 状态 |
| --- | --- |
| GitHub ↔ Epic 绑定 | 你已绑定，`UEPseudo` 拉取成功 |
| UE4SS 子模块 | `deps/first/Unreal`(UEPseudo) + `deps/first/patternsleuth` 均已检出 |
| Rust 工具链 | `rustc/cargo 1.99.1`，在 `%USERPROFILE%\.cargo\bin`（不在默认 PATH，`build_dll.bat` 会自动加） |
| C++ 核心层 | `src/core/` 全部完成，`tools\build_tests.bat` → **298 项断言全绿** |
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

## 1.0.14-cpp（2026-10-30：进入摄影模式自动暂停时间）

需求：打开相机模式时自动暂停时间，用户可在 ini 配置文件中自行修改开关，默认开启。

实现：
- `src/core/config.h/.cpp`：新增 `auto_pause_time`（默认 `true`，位于 `[effects]` 段）；`default_config_text()` 同步输出。
- `src/core/photo_mode.cpp`：`PhotoMode::enter()` 成功进入后按配置调用 `toggle_freeze()`，复用现有 `freeze_mode`（pause/dilation）逻辑；退出/解冻按原逻辑还原。
- `src/core/menu_model.cpp`、`src/ui_tab.cpp`：游戏内菜单新增「进入时暂停」状态行，ImGui 面板新增对应开关。
- `config.ini.example` / dist config / 游戏目录 config：新增 `auto_pause_time = true` 与中文注释。
- 核心层测试 257 → **298 项全绿**（新增自动暂停、关闭自动暂停、dilation 自动冻结、失败不阻断等场景）。
- 版本 `1.0.14-cpp`；产物 `build-dll/PBZPhotoModeCpp.dll`（614,912 B）；包 `dist/PBZPhotoModeCpp_1.0.14-cpp.zip`（328,209 B）；已覆盖游戏目录 DLL、README 与 config。

## 1.0.15-cpp（2026-10-30：手柄支持，默认快捷键 = 左右摇杆同时按下）

需求：加上手柄支持，默认快捷键为左右摇杆（L3+R3）同时按下进入/退出摄影模式，键位可在配置文件里改。

实现：
- **新文件 `src/core/gamepad.h/.cpp`**：`PadButton` 枚举（Xbox 规范名，兼容 PS 叫法 CROSS/CIRCLE/SQUARE/TRIANGLE/L1/R1/L2/R2/L3/R3/OPTIONS/SHARE/PS/HOME）、`parse_pad_combo("LS+RS")`（`+` 连接组合键，`NONE` 解绑）、`apply_stick_deadzone` 径向死区（分量夹紧→幅度判零→线性缩放回 0..1）、扳机阈值 `kPadTriggerHeld = 0.3`。
- **`[gamepad]` 配置节**：`pad_enabled`（总开关）、`pad_toggle`（默认 `LS+RS`）、`pad_menu`（START）、`pad_freeze`（X）、`pad_reset`（Y）、`pad_fov_up/down`（十字键上/下）、`pad_look_speed`（右摇杆转速，度/秒，默认 180）、`pad_move_scale`（左摇杆移动比例，默认 1）、`pad_deadzone`（默认 0.15）；解析器做范围保护，非法值回退默认并记 warning。
- **`InputLogic`**：新增 `on_gamepad(PadState)`；`build()` 里手柄按键用「组合键按下沿」触发（本帧全按住且上一帧不是），摇杆折算成 Intent：左摇杆→move_forward/right（模拟行程）、右摇杆→新字段 `pad_look_dx/dy`（度/秒，XInput 上推=负 dy）、RT/LT→move_up、LB/RB→boost/slow。
- **`PhotoMode::tick`**：`pad_look_* × dt` 转角度（不受 free_look_requires_alt 限制，invert_y 同样生效）；新字段 `Intent.fov_nudge`（±1 格 `fov_step`）——顺带把**只解析从未生效的键盘 `key_fov_up/down` 一并接通**；移动归一化改为 `k = min(1, 1/len)`：键盘对角（len≈1.41）仍是全速，摇杆半程就是半速。
- **`reset()` 不清手柄快照**（关键坑）：handle_toggle 里会调 `input_.reset()`，若把 pad_prev_ 一起清掉，仍按住的 LS+RS 下一帧会被当成新的按下沿 → 进出模式死循环。键盘没这个问题是因为 win_input 的 key_state_ 自己记住边沿。已有测试锁住该行为。
- **`win_input.cpp`**：动态加载 XInput（xinput1_4 → 1_3 → 9_1_0，不链 xinput.lib；SDK 头没有 `XINPUT_GAMEPAD_GUIDE`，手动补宏 0x0400）。未锁定槽位时每帧只扫一个槽位（最坏 4 帧轮完，避免每帧 4 次 XInput 调用的卡顿），连上后锁定槽位独占轮询，连续 3 帧读失败判拔出；切后台强制喂空快照防组合键卡住。`poll()` 签名加 `gamepad_enabled`。
- **接线与 UI**：mod_app 传 `cfg_.pad_enabled`、手柄插拔各打一行日志（`手柄已连接（XInput 槽位 N）`）、诊断行加 `手柄槽位=%d`、加载日志变成「按 F2 或手柄 LS+RS 进入摄影模式」；游戏内文字菜单与 ImGui 面板都加了手柄提示/设置（开关、视角速度、死区滑条）。
- 测试 298 → **393 项全绿**（名字表、组合键解析、死区、边沿/防重复触发、enabled 门控、模拟量移动与视角、PhotoMode 集成）；`CMakeLists.txt` 与 `build_tests.bat` 已加 gamepad.cpp。
- 版本 `1.0.15-cpp`；产物 `build-dll/PBZPhotoModeCpp.dll`（640,000 B）；包 `dist/PBZPhotoModeCpp_1.0.15-cpp.zip`（340,864 B）；已覆盖游戏目录 DLL、README 与 config（config 已含 `[gamepad]` 节）。

### 1.0.15 待真机验证

1. 游戏内按 **LS+RS** 应进入/退出摄影模式，日志应有 `手柄已连接（XInput 槽位 N）`。
2. 进模式后：左摇杆移动、右摇杆转视角、RT/LT 升降、十字键调 FOV、X 冻结、Y 重置、START 隐藏菜单。
3. **注意**：`block_game_input` 对键鼠已实测有效，但它是否同样屏蔽**手柄**输入未经真机验证——若进摄影模式后角色仍跟着左摇杆动，需要给输入屏蔽再加手柄通道（Enhanced Input 的清理或 `SetIgnoreMoveInput` 之外的手段）。
4. X 键在游戏内若有其他功能（如互动），非摄影模式下按 LS+RS/X 仍会触发游戏自身动作，属预期行为。

## 1.0.15 实机日志结论（2026-10-30 23:51 UE4SS.log，942 行）

- v1.0.15 加载成功（`按 F2 或手柄 LS+RS 进入摄影模式`）；F2 进出、HUD 隐藏/恢复、F 冻结/恢复全部正常。
- **整场会话 `手柄槽位=-1`，无任何 `手柄已连接` 日志** —— XInput 从未发现手柄。两种可能：① 测试时手柄没插；② XInput 加载静默失败（旧代码无日志无法区分）；③ 手柄不是 XInput 设备（蓝牙直连的 PS/Switch 手柄 XInput 看不见，需要 Steam Input / DS4Windows 包装）。
- 23:51:49~58 的连续 暂停/恢复 是键盘 F 反复按（手柄槽位=-1 排除手柄触发）。

## 1.0.16-cpp（2026-10-30：XInput 状态不再静默）

- 加载日志区分：`XInput 已加载（插上手柄会打「手柄已连接」日志）` vs `XInput 加载失败，手柄功能不可用！`（`Win32InputSource::pad_api_ready()`）。
- 下次日志据此分流：已加载但槽位=-1 → 手柄没插或非 XInput 设备；加载失败 → 查游戏目录是否有代理 xinput dll。
- 版本 `1.0.16-cpp`；核心层测试保持 393 项全绿；产物 640,000 B；包 `dist/PBZPhotoModeCpp_1.0.16-cpp.zip`（341,039 B）；已覆盖游戏目录 DLL 与 README（config 无变化）。

## 1.0.16 实机日志结论（2026-10-31 00:06）

- `XInput 已加载` —— 加载环节没问题。但该局只跑了 70 秒、没进摄影模式，且无 `手柄已连接`。
- 用户确认手柄是 **Xbox 手柄 USB 直连**（XInput 一定能看到的类型）。槽位=-1 说明当时手柄对系统不可见：USB 插着但**手柄没开机**（Xbox 指示灯没亮）也会枚举不到。

## 1.0.17-cpp（2026-10-31：手柄扫描心跳 + 离线构建脚本）

- `mod_app` 启动 10 秒后打一行 `手柄扫描正常：暂未发现手柄，持续扫描中`（或 `已连接（槽位 N）`）——证明 XInput 轮询管线活着；没有这行 = 引擎 Tick 没跑，另查。
- 新增 `tools/build_dll_offline.bat`：与 build_dll.bat 相同但加 `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`，跳过 FetchContent 的 git update（GitHub 不通时离线构建；要求 _deps 已有一次成功构建）。1.0.17 就是网络抽风时用它编出来的。
- 版本 `1.0.17-cpp`；测试保持 393 项全绿；产物 642,048 B；包 `dist/PBZPhotoModeCpp_1.0.17-cpp.zip`（341,603 B）；已覆盖游戏目录 DLL 与 README。

### 下一次验证手柄的步骤

1. 确认 Xbox 手柄**指示灯亮着**（USB 插上后手柄会自动开机；没亮就按一下 Xbox 键）。
2. 进游戏（主菜单即可），等 10 秒看 UE4SS.log：应出现 `手柄扫描正常：...`。
3. 插着手柄进游戏世界，按 **LS+RS**（两个摇杆同时按下）→ 日志应出现 `手柄已连接（XInput 槽位 0）` 和 `进入摄影模式`。

## 手柄机器级体检（2026-10-31 00:1x，tools/check_pad.ps1）

00:13 那局（1.0.17 心跳日志 `暂未发现手柄`）之后，直接在本机跑 `powershell -File tools/check_pad.ps1`：

- **XInput 槽位 0：已连接**，槽位 1-3 未连接 → 手柄此刻可见，模组逻辑没有问题。
- PnP 设备里有 **GeniTech Virtual Gamepad Device + Nefarius Virtual Gamepad Emulation Bus（ViGEm）**，还有「支持 Windows 的 XBOX 360 控制器」→ 用户走的是**虚拟手柄映射**（DS4Windows / 盖世小鸡助手一类），游戏里看到的「Xbox 手柄」是 ViGEm 虚拟设备。
- 结论：00:13 测试时手柄/映射软件不在场（虚拟手柄不存在 + 物理手柄可能被 HidHide 藏了），所以 XInput 全空。**用模组前必须让映射软件开着、手柄在线**。
- 新增 `tools/check_pad.ps1`：随时单独体检 XInput 槽位与 PnP 设备，不用进游戏。

## 1.0.18-cpp（2026-10-31 00:2x：显式加载 System32 的 XInput）

00:23 那局：心跳 `暂未发现手柄`（管线活着），但本机 PowerShell 探测槽位 0 已连接 → **同一系统、游戏进程看不到手柄**。高度怀疑游戏进程加载到了游戏目录/Steam 放进来的同名 xinput 代理 DLL（代理常"加载成功但永远报无手柄"）。

- `load_xinput()` 改为：先用 `GetSystemDirectoryW` 按绝对路径加载系统目录的 xinput1_4/1_3/9_1_0，失败才回退搜索顺序；记录实际加载的模块完整路径。
- 加载日志带路径：`（XInput 已加载：C:\Windows\System32\xinput1_4.dll）`——下次日志看路径就能确认有没有绕开代理。
- 版本 `1.0.18-cpp`；测试保持 393 项全绿；包 `dist/PBZPhotoModeCpp_1.0.18-cpp.zip`；已覆盖游戏目录 DLL 与 README。

### 若 1.0.18 还看不到手柄（按概率排查）

1. 日志里 XInput 模块路径是否 `C:\Windows\System32\...`；不是 → 还有更深的注入。
2. 跑 `tools/check_pad.ps1` 看游戏外能否看到；看不到 → 手柄/映射软件层面的状态（ViGEm 虚拟手柄只在映射软件运行时存在，物理手柄可能被 HidHide 藏了）。
3. 游戏是否走 Steam 启动、Steam Input 对该游戏是否启用；虚拟手柄被映射软件"独占"时 Steam Input 可能不给别的进程转发。

## 1.0.18 实机验收通过（2026-10-31 00:31 UE4SS.log）✅

根因确认：**Steam 对 PBZ 开着「使用 Steam 输入」时，游戏进程里的 XInput 看不到手柄**（用户手柄经 ViGEm 映射软件出虚拟 Xbox 手柄，被 Steam Input 拦下不转发给游戏）。用户在 Steam 库 → PBZ 属性 → 控制器里关掉「使用 Steam 输入」后立即恢复。

日志验收（v1.0.18）：
- `XInput 已加载：C:\Windows\system32\xinput1_4.dll`（显式系统路径加载生效）
- `手柄已连接（XInput 槽位 0）` → 心跳 `已连接（槽位 0）`
- 00:31:41 `进入摄影模式`（LS+RS 触发）
- 相机位置/朝向持续变化而 dx=dy=0、引擎增量=0 → 右摇杆转视角走的就是手柄通道 ✅
- `时间已冻结/已恢复` 两次 → 手柄 X 键冻结 ✅
- FOV 88→126 连续变化且 `滚轮事件=0` → FOV 步进通道（手柄十字键/键盘 FOV 键）首次实机生效 ✅

给 README 排障补一条 Steam Input 的说明（虚拟手柄用户大概率踩同一个坑）。

## 1.0.19-cpp（2026-10-31 01:2x：游戏内菜单改用 UMG 控件）

### 为什么旧菜单永远显示不出来（根因，铁证）

右下角菜单旧实现有两条路，在 PBZ 里**都是死路**：

1. **HUD Canvas 路径**：等 ProcessEvent(ReceiveDrawHUD/DrawHUD) 回调再用 AHUD::DrawText 画。但 BP_PBZHud_C 是纯 UMG，AHUD 原生绘制管线从不执行——1.0.4 到 1.0.8 每次实机日志都是 HUD绘制=0，钩子等不到那个永远不来的回调。强制 bShowHUD=true 也没用（游戏根本不调 DrawHUD）。
2. **PrintString 兜底**：PBZ.exe 是 511 MB 的 UE5 Shipping 单体包，UKismetSystemLibrary::PrintString 的函数体被 #if !UE_BUILD_SHIPPING 整体裁掉——反射调用“成功”（UFunction/参数元数据还在），实际什么都不画。1.0.1 实机已验证；1.0.8 修好 duration 参数名 + 强制 bEnableOnScreenDebugMessages 后依然不显示（用户 2026-10-31 00:15 再次报障“一直显示不出菜单界面”）。1.0.8 注释里“早期原型已证明可用”是未验证假设——_legacy/PBZPhotoMode/测试清单.md 一项都没勾。
3. 两条路都“返回成功”，update_overlay 还会打 游戏内菜单已显示——日志看着正常、屏幕什么都没有，这就是“日志没报错但菜单出不来”的原因。

### UMG 实现（全部反射，不引入新依赖）

src/engine/ue4ss_port.cpp 重写 overlay 部分：

- 动态拼装：UObjectGlobals::NewObject（内部走游戏的 StaticConstructObject，原生构造函数会执行）创建 UserWidget（outer=UWorld）→ WidgetTree（构造器没建就补）→ CanvasPanel（根）+ TextBlock（都 outer 到 WidgetTree，符合 UMG 归属约定）。
- CanvasPanel.AddChildToCanvas(TextBlock) 拿 CanvasPanelSlot，布局右下角：SetAnchors(1,1) + SetAlignment(1,1) + SetPosition(-24,-24) + SetAutoSize(true)——锚点+对齐让定位与文本实际像素尺寸无关。FAnchor 两层嵌套用新的 write_anchor 反射写入。
- FText：KismetTextLibrary::Conv_StringToText 生成，返回值按字节“所有权移交”到 SetText 参数（ParamBlock 裸字节数组释放时不调 FText 析构，目标属性持有引用，无泄漏无悬垂）。
- 生命周期：AddToViewport(ZOrder=10000) **常驻**，显示/隐藏走 SetVisibility(Visible/Collapsed)（umg_visibility_state_ 记状态避免每帧重复调用）；文本变化才 SetText。**退出模式不销毁控件**；换图/GC 后按创建时记录的 GetFullName() 重新查活（umg_live()，不解引用悬垂指针），查不到就重建；每秒 IsInViewport 校验，被游戏清出视口就重新 Add。
- 失败逐步打日志（“UMG 菜单：…”，同类只打一次），真机排障能定位卡在哪一步。
- 删掉了旧的 ensure_overlay_hook / on_process_event / draw_overlay / overlay_via_print_string / build_print_menu_text / ensure_screen_messages_enabled 与相关成员；诊断行 HUD回调=%u 改为 菜单=%s（umg_menu_state：未创建/显示/隐藏）。

版本 1.0.19-cpp（1.0.18 已被手柄修复占用）；产物 644,096 B（含 1.0.18 的 System32 XInput 修复，是超集）；核心层 **393 项测试全绿**；包 dist/PBZPhotoModeCpp_1.0.19-cpp.zip；游戏目录已覆盖最后一次（1.0.19 起交付流程改为只出包，见顶部「交付流程」）。

### 1.0.19 实机验证要点

1. 进游戏按 F2：日志应出现“UMG 菜单已创建：/…”，随后“游戏内菜单已显示（UMG 控件）”。
2. 屏幕右下角应出现琥珀色 ASCII 菜单（默认 Roboto 字体，比旧 PrintString 版大一些）。
3. T 隐藏/显示；F2 退出后菜单消失（Collapsed）；再进不重建日志不刷屏。
4. 每秒诊断行应出现 菜单=显示。若出现“UMG 菜单：…”的 Warn，按提示的步骤名定位。
5. 手柄（1.0.18 已验收）行为不应回归。

### 构建环境备忘（新）

DSH 起的 shell 里 **ProgramFiles / ProgramFiles(x86) 环境变量是空的**，build_dll_offline.bat 靠它们找 vcvars64 会报 “vcvars64.bat not found”。子进程 env 补上这两个变量再跑 bat 即可（编译器在 C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools，ninja 用 VS 自带的）。

## 1.0.19 实机结果（2026-10-31 01:29 UE4SS.log + 截图）

- **UMG 菜单链路实机打通** ✅：`UMG 菜单已创建：UserWidget /Game/Maps/B02/PBZ_Xigu_WP.PBZ_Xigu_WP:UserWidget_2147469865`；每次进模式 `游戏内菜单已显示（UMG 控件）`；控件跨进出复用（无重复创建）；截图里右下角能看到琥珀色菜单文字（NewObject/CreateWidget 式动态 UMG 在本作 Shipping 包可用）。
- 遗留问题：日志 `UMG 菜单：槽位布局没写全`——四个 Set*（SetAnchors/SetAlignment/SetPosition/SetAutoSize）至少一个没生效，菜单文字块顶左角落在屏幕右下角、向右下溢出被裁（截图只看到标题前几个字符和 STATE 行的开头）。
- 本局其余全部正常：F2 进出、暂停/恢复、HUD 隐藏/恢复、Alt+鼠标视角、滚轮 FOV（90→98→66→58→74）、手柄槽位=-1（未插）。
- `UMG 菜单：查活失败，已重建控件` 只在首次创建时打了一次（措辞误导，1.0.20 改为只在真重建时打）。

## 1.0.20-cpp（2026-10-31 01:4x：槽位布局直写 + 回读诊断）

- 布局改为**双保险**：先走四个 Set* UFunction，再**直接写 UCanvasPanelSlot.LayoutData**（FAnchorData{Anchors,Offsets,Alignment}）与 bAutoSize 属性——不依赖 UFunction 参数名。全部发生在 AddToViewport 之前，Slate 槽位在 TakeWidget 时才从 LayoutData 构建，直写必然生效。
- 新增**回读日志** `UMG 槽位回读：anchors=(...) pos=(...) align=(...) auto=...（读取 11111）`——真机一眼核对每个字段；再加一次性 `槽位属性: ...` dump，字段名对不上时直接可见。
- setter/直写分组打 √/×（`槽位布局 setter(锚√/对齐√/位置√/自动×) 直写(...)`）；`查活失败，已重建` 只在真重建时打。
- 按 2026-10-31 起的交付流程：**只出包不部署**。版本 1.0.20-cpp；产物 648,704 B；核心层 393 项全绿；包 `dist/PBZPhotoModeCpp_1.0.20-cpp.zip`（用户用模组管理器自行安装）。

### 1.0.20 实机验证要点

1. 装 zip 进游戏按 F2：看 `UMG 槽位回读` 一行——期望 `anchors=(1,1)-(1,1) pos=(-24,-24) align=(1,1) auto=1`。
2. 屏幕右下角：菜单应完整显示、距边缘 24px。若回读全对但仍被裁，把 `槽位属性: ...` 和截图发回来（下一步改用 GetDesiredSize 绝对定位）。
3. 若出现 `槽位布局 setter(...) 直写(...)` 的 Warn，把那行发回来。

## 1.1.0-cpp（2026-10-31 05:0x：图形化菜单 = ImGui + D3D 交换链钩子）

用户看过多版文字菜单后要 PhotoMode（powerof3）那种**图形化、可交互、可分页**的菜单：滑条调 FOV/移动速度、分页展示快捷键。评估结论：UMG 反射路线做交互控件在 Shipping 下不确定性最大；UE4SS 3.0.1 自带 GUI 只有 DX11/OpenGL 后端且挂在调试控制台里（本作 DX12 画不出来）；所以走 **自建 D3D 钩子 + 独立 ImGui context**（与 powerof3/PhotoMode 同架构，其源码在 `_ref/photomode-src` 可参考）。

### 实现（全部自带依赖，离线构建不受影响）

- **third_party/imgui**：从 `build-dll/_deps/imgui-src`（UE4SS FetchContent 的 1.92.1）拷贝 imgui 核心 + DX11/DX12/Win32 三个后端；CMake 新增 `pbzpm_imgui` 静态库，放在 UE4SS 前面链接（两边各有一份 ImGui，链接顺序保证符号用我们这份）；`IMGUI_IMPL_WIN32_DISABLE_GAMEPAD` 显式禁掉 win32 后端的 XInput（1.0.18 教训：绝不能隐式加载 System32 的 XInput 代理 DLL）。
- **src/engine/imgui_overlay.h/.cpp**（新）：
  - 钩子：`start()` 造 dummy D3D11/D3D12 设备+交换链+命令队列拿 vtable，补丁 `IDXGISwapChain::Present`(槽 8) 与 `ID3D12CommandQueue::ExecuteCommandLists`(槽 10)；DX11/DX12 的 Present 若同地址只补一处（`g_present_hook_shared`）。运行时先试 DX12（UE5 默认，队列从 ECL 钩子捕获）再试 DX11。
  - 渲染：Present 钩子里 NewFrame→`render_game_menu`→Render→DX11 直接 RenderDrawData；DX12 自管 RTV（typeless 格式映射）+命令列表，在捕获的队列上 Execute。每帧只画主窗口（UnrealWindow 优先）的交换链。
  - 输入：不走 WndProc 消息——渲染线程每帧 GetCursorPos/GetAsyncKeyState 直读 + 滚轮由引擎线程转交（`cursor_in_menu()` 判定落在菜单上才转，滚轮不再调 FOV）；UI 捕获鼠标时引擎侧视角/俯仰输入静音。手柄导航：ImGui 快照喂 dpad/A/B/摇杆事件（`InputLogic::gamepad()` 新增只读快照）。
  - 光标常显：ShowCursor 计数按线程算，渲染线程管不到游戏窗口 → 子类化游戏 WndProc，渲染线程投递自定义消息让窗口线程自己 `ShowCursor(TRUE)`+`ClipCursor(nullptr)`。
  - 失败降级：钩子装不上 / 后端初始化连续 240 帧失败 / 菜单打开 3 秒收不到渲染回调（交换链类型不匹配）→ `failed_`，上层自动退回 UMG 文字菜单（v1.0.20 那套保留为兜底）。
  - 线程模型：引擎 Tick 线程 publish(MenuView 快照)/drain(MenuActions)；渲染线程只碰快照，交换走 `view_mutex_`；`ui_menu` 的 ini 写盘禁用（io.IniFilename=nullptr）。
- **src/game_menu.h/.cpp**（新）：三页菜单（相机：状态+FOV/速度/灵敏度滑条+位置朝向+冻结/重置/退出按钮；快捷键：hints 列表；选项：五个开关+手柄参数），中英双份文案（系统 msyh.ttc 加载失败时退英文+ImGui 默认字体）。
- **src/core/menu_view.h**（新）：`MenuView`/`MenuActions` 纯数据桥，引擎线程↔渲染线程的唯一通道。
- **src/core/photo_mode.h/.cpp**：新增 `set_hide_player_live` / `set_block_game_input_live` / `set_hide_game_hud_live`——菜单里中途切开关：改 cfg + 立即作用于引擎 + 与 enter/leave 的 `*_hidden_` 记账联动（回滚正确性有测试）。
- **src/mod_app.cpp/.h**：`on_unreal_init` 里 `overlay_->start()`；T/START 切菜单（`available()` 时图形优先）；ESC 关菜单（渲染线程边沿）；滚轮路由与鼠标互斥；`publish_menu_snapshot()`/`apply_menu_actions()`（动作→set_fov/set_config/live setters/toggle_freeze/reset_view 注入/exit）；诊断行加 `图形=开/关/无`；进模式自动开菜单、退出自动关。
- 版本 `1.1.0-cpp`；产物 **796,160 B**；核心层 **415 项全绿**（新增 22 项实时开关测试）；包 `dist/PBZPhotoModeCpp_1.1.0-cpp.zip`（412,914 B，sha256 8CB2891D...61AC6）。

### 1.1.0 实机验证要点

1. 加载日志：`图形菜单已就绪：钩子已安装（DX11+DX12，运行时自动探测游戏后端）`；失败则 `图形菜单初始化失败...`（此时只验证文字菜单）。
2. 进摄影模式：菜单应**自动弹出**；按 T / ESC / 手柄 START 可开关；每秒诊断行 `图形=开`。
3. 几秒内应出现 `图形菜单：DX11 后端已初始化` 或 `DX12 后端已初始化`（PBZ 大概率 DX12）。若出现 `打开 3 秒未收到渲染回调` 的 Warn，菜单自动退回文字版——把日志发回来。
4. 鼠标应能看到光标；拖 FOV/速度滑条应实时生效；滚轮在菜单上滚动=操作 UI、在菜单外滚动=调 FOV；滑条拖动时按住 Alt 转视角应被抑制，光标移出菜单后恢复。
5. 「选项」页切 隐藏角色/屏蔽输入/隐藏 HUD 应即时生效；退出摄影模式后一切还原。
6. 手柄：START 开菜单后，十字键移动焦点、A 确认、B 返回；调滑条用 A 选中后左右拨十字键。
7. 若菜单能显示但花屏/闪烁，把截图和 `图形菜单：...` 日志行发回来（下一步排查 RTV 格式/多交换链）。

## 1.1.0 实机结果（2026-10-31 05:09）：启动闪退 ❌ → 1.1.1 修复

现象：装 1.1.0 后游戏启动即闪退。日志证据：`图形菜单：钩子已安装（DX11+DX12）`→`图形菜单已就绪`→`加载完成 v1.1.0-cpp`（05:09:26.553）之后**戛然而止**（正常应继续 Lua 模组加载）；无 WER 事件、无 dump（本机 WER 静默）。

定位：崩溃发生在我们 `start()` 完成**之后**、游戏 D3D12 RHI 初始化/首帧窗口——1.1.0 在 `on_unreal_init`（UE4SS 初始化线程、游戏 RHI 还没起来）就创建了 dummy D3D11+D3D12 设备并补了 vtable，与游戏自身的 RHI 初始化撞车。1.0.20 能正常启动，说明只要这个阶段不碰 D3D 就没事。

### 1.1.1 修法（三重防御）

1. **时序延迟**：钩子安装从 `on_unreal_init` 移到 `ensure_overlay_started()`——首次进入摄影模式时在游戏线程上执行（RHI 已就绪）。按 F2 之前进程行为与 1.0.20 完全一致。
2. **导入表清零**：不再链 d3d11/d3d12/dxgi（CMake 移除；PE 导入表已验证无 d3d12/dxgi），D3D11CreateDevice/D3D12CreateDevice/CreateDXGIFactory 改 `LoadLibraryW`+`GetProcAddress` 运行时加载——模组加载阶段对进程零干扰。
3. **可观测 + 逃生门**：`start()` 拆 `start_impl` 包 try/catch；安装过程五步日志（`[1/5] dummy 窗口`→`[5/5] 钩子已安装`），再崩也能定位到步；mod 目录放 `no_imgui_menu.txt` 彻底禁用图形菜单（回退 UMG 文字菜单）。

版本 `1.1.1-cpp`；产物 **801,280 B**；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.1.1-cpp.zip`（414,967 B，sha256 6D0837B8...BB059）。

### 1.1.1 实机验证要点

1. **启动**：应能正常进游戏；加载日志只有 `图形菜单：延迟初始化（首次进入摄影模式时安装钩子）`，没有任何 D3D 相关动作。
2. 按 F2 进入摄影模式：这里才开始安装——应看到 `图形菜单：[1/5]` 到 `[5/5]` 五步日志（任何一步崩/卡住，把最后一行发回来即可定位）。
3. 五步全过 → 菜单自动弹出（ImGui 浮窗）；之后按 1.1.0 的验证清单（滑条/分页/手柄导航/退出还原）。
4. 若 F2 后仍闪退：改放 `no_imgui_menu.txt` 进 mod 目录确认游戏恢复稳定，然后带 `[1/5]...` 之前后的日志回来——需要换钩子安装方式（MinHook 式 trampoline 或复用 UE4SS 的 PolyHook）。
5. 万一整个过程顺利：删掉本节顶部「待实机验证」字样，记录 DX11 还是 DX12 后端。

## 1.1.1 实机结果（2026-10-31 05:17 dump + log）：F2 卡死 ❌ → 1.1.2 修复

时序（日志）：启动正常（延迟化生效✅），F2 后摄影模式全链路正常（相机/暂停/HUD隐藏✅），钩子 [1/5]~[5/5] 全部 OK✅，`已锁定游戏主窗口`、`DX12 后端已初始化`✅——然后卡死，UE4SS 崩溃处理器写了两个 dump（05:17:40.079 / .877，即卡死后 ~1.2s）。两份 dump 质量差（一份缺线程上下文，一份抓到的是写 dump 的线程），但行为证据足够。

**根因（读 1.92 后端源码坐实）**：`imgui_impl_dx12.cpp` 的纹理上传路径在首帧把 CJK 字体纹理用 **我们传给它的队列**（= ECL 钩子捕获的游戏渲染队列）`ExecuteCommandLists` + `Signal`，然后 `WaitForSingleObject(event, INFINITE)` **无限期等 GPU**。而这个调用发生在我们卡住游戏渲染线程的 Present 钩子里——游戏队列被帧工作排满（本作还开着 **FSR3 帧生成** `amd_fidelityfx_framegeneration_dx12.dll` + NVIDIA Streamline，多队列交叉依赖），等待永不完成 → 渲染线程卡死 = 整卡死。

### 1.1.2 修法（三件套）

1. **纹理上传专用队列**：`init_backend_dx12` 自己 `CreateCommandQueue(DIRECT)` 一个空闲队列传给 `ImGui_ImplDX12_InitInfo.CommandQueue`——后端的上传+Signal+等待在这个队列上立即完成；上传的纹理是 device 级资源，游戏队列照常采样。游戏队列只用于我们的 UI 命令列表提交（只 enqueue、从不等待，无死锁可能）。
2. **Present 防重入**：本作带 FSR3 帧生成，Present 可能来自帧生成线程；`render_if_needed` 入口 `in_frame_.exchange` 抢占 + `FrameGuard` 释放，保证同一时刻只有一个线程碰 ImGui。
3. **命令分配器三帧轮换**：原来单分配器每帧 Reset，上一帧还在 GPU 执行时 Reset 非法；改为 3 个轮换。

版本 `1.1.2-cpp`；产物 801,280 B；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.1.2-cpp.zip`（415,166 B，sha256 40725E8E...D7410）。

### 1.1.2 实机验证要点

1. 启动 + F2 流程与 1.1.1 相同（[1/5]~[5/5] 全 OK）；差异在之后：菜单应直接弹出（首帧有 ~0.5-1s 中文字体构建卡顿，属预期）。
2. 若仍卡死：这次日志末尾还是停在 `DX12 后端已初始化` 附近 → 说明卡点在游戏队列提交侧（换思路：我们的 UI 列表也改提交到专用队列 + 等 GPU 完成 fence），把日志发回来。
3. 若菜单弹出但看不见/花屏：FSR3 帧生成可能插手了交换链，把截图 + 日志发回来（下一步试 DX11 后端或绕过帧生成层）。
4. 若一切正常：删「待实机验证」并记录后端（大概率 DX12）。

## 1.1.2 实机结果（2026-10-31 05:25 dump×2）：仍然卡死 ❌ → 1.1.3 判定帧生成插件冲突，稳定优先

日志签名与 1.1.1 完全一致：五步安装全 OK → `已锁定游戏主窗口` → `DX12 后端已初始化` → **20ms 后进程僵死**（253 线程全部停在 ntdll 等待），两份 dump。自写 `deepdump.py`（支持 Memory64List、修正 MINIDUMP_THREAD 偏移）解析：

- 异常线程在 `KERNELBASE+0xc408a` raise **异常码 0x8000**（两份 dump 完全一致）；栈向上：`PBZ.exe+0x1b9e0a60`（游戏代码）← UE4SS 框架帧——**游戏 RHI 的致命路径**（设备移除/RHI fatal 后引擎自爆），不是我们 DLL 里的访问违例。
- 本作同时加载 **FSR3 帧生成（amd_fidelityfx_framegeneration_dx12）+ NVIDIA Streamline（sl.interposer）+ XeSS（libxess）+ DLSS**：这些插件包装交换链并管住 back buffer 的资源状态，我们注入的 `PRESENT→RENDER_TARGET` barrier 与它们的状态机冲突 → D3D12 设备移除 → 游戏 RHI fatal。1.1.2 修的上传队列死锁依然成立且已修，但底下还压着这一层。

### 1.1.3 决策：稳定优先，帧生成环境下自动停用图形菜单

1. **帧生成/插帧插件自动检测**（`amd_fidelityfx_framegeneration_dx12` / `nvngx_dlssg` / `sl.interposer` / `libxess` 任一已加载即判定）：命中时图形菜单不启用，日志 Warn 写明插件名，**回退 UMG 文字菜单**（1.0.20 定位已修、完全稳定）。强行尝试：游戏设置关掉帧生成 + mod 目录放 `imgui_dx12_force.txt`。
2. **字体图集预构建**：`init_imgui()`（中文 TTF 解析+光栅化 ~1s）从首次 Present（渲染线程）挪到 `start()`（游戏线程、F2 时刻），并显式 `io.Fonts->Build()`——渲染线程不再被卡 1 秒。
3. **设备移除护栏**：DX12 每帧渲染后 `GetDeviceRemovedReason()`，FAILED 即 `fail()` 永久退回文字菜单——即使将来再遇设备冲突，也是优雅降级而不是拖崩游戏。

版本 `1.1.3-cpp`；产物 804,352 B；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.1.3-cpp.zip`（416,766 B，sha256 A429AB8C...B49D4）。

### 1.1.3 实机验证要点

1. 启动 + F2 一切照旧；进摄影模式日志应出现 `检测到帧生成/插帧插件（...），图形菜单与它冲突，已自动停用` → 屏幕是文字菜单，**游戏必须完全不卡**。
2. 想试图形菜单：游戏设置关掉帧生成（若有）→ mod 目录放 `imgui_dx12_force.txt` → 重启再 F2。若不卡且菜单弹出 → 帧生成是唯一冲突源，后续可做「感知帧生成开关」的运行时方案。
3. 图形菜单在本作的最终去留取决于 2 的结果：关帧生成能跑 → 保留为进阶功能；仍不行 → 本作标记图形菜单不可用，文字菜单保留。

## 1.1.3 实机结果（2026-10-31 07:43 log + 截图）✅：游戏稳定，文字菜单按设计回退

日志确认：`检测到帧生成/插帧插件（amd_fidelityfx_framegeneration_dx12.dll），图形菜单与它冲突，已自动停用` → 文字菜单显示（截图确认 1.1.3-cpp 版本号），摄影模式进出/暂停/恢复/HUD 隐藏恢复全链路正常，**游戏零卡死**。用户确认帧生成必开，选择走 UMG 原生控件路线。

## 1.2.0-cpp（2026-10-31 08:0x：UMG 交互菜单——帧生成环境下的图形化方案）

**思路**：帧生成把游戏自己的 UI 当普通画面合成，零冲突。用 1.0.19/1.0.20 验证过的反射管线建真正的 UMG 控件（Slider/Button/TextBlock/VerticalBox/HorizontalBox），交互全部**轮询**而非事件绑定（Shipping 下委托绑定不可控）：

- 滑条：每帧读 `Value` 属性 → 变了就当用户拖动 → 应用到 FOV/速度/灵敏度；外部改了（滚轮/快捷键）→ `SetValue` 反写滑条。双向同步用 last_synced 基线防环。
- 按钮：轮询 `IsPressed` 上升沿 = 点击（FREEZE/RESET/EXIT）。
- 鼠标指针：菜单打开期间每帧写 `PC.bShowMouseCursor=true`（游戏拍照模式会藏指针），关闭时恢复 false。
- Esc 关闭菜单；T/START 开关；进模式自动弹出（沿用 menu_visible_ 状态）。
- 位置：右上角锚定（锚 (1,0) 对齐 (1,0) 偏移 (-480,90)），宽 440 高 330，双写布局。
- 菜单优先级：DX12 ImGui（force 进阶）→ UMG 交互 → 文字兜底（三者自动降级）。
- 已知留白：分页（快捷键页）等交互链路实机验证后再铺；滑条 Fill 布局的 SizeRule 枚举写入是尽力写（失败只是滑条窄）。

改动：`ue4ss_port.h/.cpp`（umg2_* 全套：控件构建/查活重建/双向同步/按钮轮询/指针管理；`Umg2Commands` 提到命名空间层——嵌在类里会让前置声明处编译失败）、`mod_app.h/.cpp`（update_overlay 三级优先 + apply_umg2_commands + Esc 沿检测 + 诊断行图形=交互/隐藏/不可用）。

版本 `1.2.0-cpp`；产物 **826,880 B**；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.2.0-cpp.zip`（426,364 B，sha256 53FADFCA...1E40A）。

### 1.2.0 实机验证要点（帧生成开着直接测）

1. F2 进摄影模式：右上角应出现 UMG 面板（标题 PBZ Photo Mode、STATE 行、三行滑条、三个按钮、两行提示）。日志有 `交互菜单已创建：...`。
2. 拖 FOV 滑条：画面 FOV 即时变化，数值标签跟着走；滚轮调 FOV 后滑条应反向同步。
3. 拖 SPEED/SENS 滑条：移动速度/灵敏度即时生效。
4. 点 FREEZE（时间冻结/恢复）、RESET（视角回初始）、EXIT（退出摄影模式）。
5. Esc / T 关菜单；再按 T 重开；退出模式后面板消失、指针恢复隐藏。
6. **关键风险点**：如果面板显示了但滑条拖不动/按钮点了没反应 → 是 Slate 指针路由问题（拍照模式的鼠标捕获抢了事件），日志发回来，下一版改用控件矩形 + 我们自己的鼠标状态做命中判定（方案已备）。
7. 若面板没出现且日志有 `交互菜单：... 回退文字菜单` → 把那几行发回来。

## 1.2.0 实机结果（2026-10-31 08:17 dump）：构建途中访问违例 ❌ → 1.2.1 收缩控件词汇表

F2 后菜单可见 → `ensure_umg2_menu()` 构建途中进程死亡（`交互菜单已创建` 从未打印）。dump：**真正的访问违例**——`0xC0000005` 读 `0xFFFFFFFFFFFFFFFF`，故障地址 `PBZ.exe+0x157d713`，栈链 `PBZ.exe+0x1b9e0a60 ← UE4SS+0x3d5530`（= UE4SS 的 ProcessEvent 派发进游戏代码）——与 1.1.2 的致命链完全同款。**结论：1.2.0 新引入的 VerticalBox/Slider/Button 及其 ProcessEvent（AddChildToVerticalBox/HorizontalBox/Button.AddChild/SetValue/IsPressed 之一）在本作触发游戏代码内的空指针**。无法无符号定位到具体某个调用，且每个都要一轮实机试错 → 收缩词汇表。

### 1.2.1 修法：控件词汇表收缩回已验证安全集 + 自判定点击

- **只用 CanvasPanel + TextBlock**（1.0.19/1.0.20 整场实机验证过的全部操作：NewObject/AddChildToCanvas/LayoutData 直写/SetText/SetVisibility/AddToViewport/IsInViewport/bAutoSize）。VerticalBox/HBox/Slider/Button 全部移除。
- 交互改为**自己的命中判定**（不走 Slate 路由）：布局常量在 `engine/umg2_layout.h`（port 摆控件、mod_app 判命中共用）；mod_app 每帧 GetCursorPos → 面板内坐标 → 查 `kHits[]` 命中区 → 左键按下沿 = 点击 → `Umg2Clicks` 传给 port。
- 数值调整用 `[–]/[+]` 文本按钮（FOV ±fov_step、SPEED ∓20、SENS ∓0.01，带夹紧）；FREEZE/RESET/EXIT 为文本按钮。
- 指针在面板上时：视角/俯仰/滚轮让位给菜单（`umg2_cursor_in_panel_`，上一帧判定）。指针可见性 = 纯属性写（无游戏代码执行），安全。
- `Umg2Commands` 字段改为步进语义（fov_step/speed_delta/sens_delta）。

版本 `1.2.1-cpp`；产物 818,688 B；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.2.1-cpp.zip`（426,364+Δ B，sha256 见打包记录）。

### 1.2.1 实机验证要点

1. F2 进摄影模式：右上角出现文本面板（标题/STATE/三行 [–] 数值 [+]/FREEZE/RESET/EXIT/提示），日志 `交互菜单已创建：...`。
2. 点 FOV 行的 `[+]/[–]`：FOV ±2 即时生效，数值标签变化；SPEED/SENS 同理。
3. 指针移到面板上：相机不再随鼠标转、滚轮不调 FOV；移开恢复。
4. FREEZE 冻结/恢复；RESET 视角回初始；EXIT 退出模式。
5. Esc/T 关开菜单。
6. 若面板显示但点击没反应 → 命中区对位问题（DPI/窗口缩放），把截图发回来微调 kHits/kPanel 常量。

## 1.2.1 实机结果（2026-10-31 08:3x 用户反馈）：面板渲染在左上角、点击全部落空 ❌ → 1.2.2

用户症状：**点击面板区域触发相机上移，无法点到任何控件**（面板本身显示正常）。两个根因，都是 1.2.1 引入：

1. **摆放与命中坐标系不一致**：`ensure_umg2_menu()` 给全部文本写锚点(0,0)+偏移(item.x,item.y)——画布根铺满视口，(0,0) 就是视口左上，所以面板实际渲染在**屏幕左上角**；而 mod_app 命中判定按**右上角**算（`client.right - kPanelFromRight`）。用户点的是左上角可见面板，`px = 光标x - (屏宽-480)` 恒为负 → 命中全落空、`umg2_cursor_in_panel_` 恒 false → 静音不生效。
2. **静音列表漏了 `move_up`**：translate 模式下左右键直接叠加进 `Intent.move_up`（`core/input_logic.h:42` 注释明示），命中失败时左键点击 = 相机上移——正是「点击触发上移」的直接来源。即使命中修好，不补这条点击面板仍会推高相机。

### 1.2.2 修法（2026-10-31）

- **渲染端**（`ue4ss_port.cpp`）：文本摆放改为锚点(1,0)+对齐(0,0)，`Left = item.x - kPanelFromRight`、`Top = item.y + kPanelTop`——与命中判定同一套坐标；负偏移+点锚的直写有 1.0.19 右下角状态标签实机先例。
- **命中端**（`mod_app.cpp`）：窗口选择从 `FindWindowW`（只认第一个匹配，闪屏/隐藏窗口可能抢在前面）改为枚举本进程 UnrealWindow 类、可见、客户区最大的窗口；Slate 单位↔物理像素按窗口 DPI 换算（`GetDpiForWindow`，退 `GetDeviceCaps`）；mod 目录 `umg2_scale.txt` 写一个数可强制缩放（绕游戏自定 UI 缩放曲线）；开菜单打一条 `交互菜单命中映射：窗口/客户区/DPI/缩放/面板像素区` 日志，点不中时对着查偏差。
- **静音端**：`umg2_cursor_in_panel_` 时 `effective.move_up` 一并清零（左右键升降让位给菜单）。
- 坐标契约写进 `engine/umg2_layout.h` 文件头（摆放公式 + 命中公式），防止再次各说各话。

版本 `1.2.2-cpp`；产物见打包记录；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.2.2-cpp.zip`。

### 1.2.2 实机验证要点

1. F2 → T：面板出现在**右上角**（不再左上角）；日志出现 `交互菜单已创建` + 一条 `交互菜单命中映射：...`。
2. 点三行 `[-]/[+]`：FOV ±2、SPEED ±20、SENS ±0.01 即时生效；**点击时相机纹丝不动**（上移问题消失）。
3. 指针在面板上：鼠标转视角/滚轮/升降全被静音；移开恢复。
4. FREEZE/RESET/EXIT 各点一次确认。
5. 若点击仍落空：把 `交互菜单命中映射` 一行 + 实际点击位置（第几行第几个按钮）发回来——偏差方向决定改 `umg2_scale.txt` 强制缩放还是调 kPanel/kHits 常量。

## 1.2.2 实机结果（2026-10-31 08:5x 用户反馈）：命中区仍偏 ❌ → 1.2.3

用户症状：「界面的触发区域还是不准」——面板可见但点击判定区与可见控件错位。判断：**缩放来源错**。1.2.2 用窗口 DPI（GetDpiForWindow/96）推 UI 缩放，但 UE 的 UI 缩放由分辨率曲线决定（UMG 按 Slate 逻辑视口布局，物理像素 = Slate × 缩放），2K/4K 或游戏自定曲线下两者不相等 → 命中区整体偏移/缩放错。

### 1.2.3 修法（2026-10-31）

- **缩放真值来自引擎**：新增 `Ue4ssPort::umg2_viewport_size()`——ProcessEvent `KismetSystemLibrary.GetViewportSize`（与 1.0.x 起大量实机安全使用的 KismetTextLibrary 同族的纯引擎静态工具）拿 Slate 逻辑视口尺寸；开菜单解析一次并缓存：`缩放 = 物理客户区宽 / Slate视口宽`。解析顺序：`umg2_scale.txt` 覆盖 > 视口换算 > DPI 兜底。
- **映射日志升级**：`交互菜单命中映射：窗口/客户区/Slate视口/DPI/缩放(来源)/面板左缘x/顶y`，来源标注 `视口换算`/`DPI兜底`/`覆盖`。
- **未命中日志**：点在面板里却没命中按钮时打 `交互菜单点击未命中：面板内(x,y) 缩放=...`（每秒一条）——不用截图就能拿到偏差数字。
- **命中矩形放宽一圈**（±2~4px、高度 22→26），残余小偏差也能点到。

版本 `1.2.3-cpp`；核心层 415 项全绿；包 `dist/PBZPhotoModeCpp_1.2.3-cpp.zip`。

### 1.2.3 实机验证要点

1. 开菜单日志应出现 `缩放=...（视口换算）`；对照实际位置：日志的「面板左缘x/顶y」应和可见面板边缘吻合（±几个像素）。
2. 点三行 `[-]/[+]` 与 FREEZE/RESET/EXIT：应全部命中；点击不带动相机。
3. 若仍偏：点几下看得见但没反应的位置，把 `交互菜单命中映射` + 几条 `交互菜单点击未命中：面板内(x,y)` 发回来——未命中坐标和真实按钮位置（54,76 / 198,76 / …，见 umg2_layout.h）一对比就是精确的缩放残差，下一版直接修死。
7. 若仍卡死 → 唯一剩余的非安全操作只剩 AddChildToCanvas×N 与 SetText/SetVisibility/AddToViewport（1.0.19/20 验证过），把 log+dump 发回来再深挖。

## 1.3.0-cpp（2026-10-31：WBP 资产菜单——UMG 路线的正解）

**思路**：1.2.0 崩溃的根源是运行时反射拼装控件 + ProcessEvent 新函数；1.2.1~1.2.3 三轮修命中区是自建命中判定的固有问题。1.3.0 改为**离线在 UE 5.7 编辑器里做 WidgetBlueprint 资产**（真滑条/真按钮/分页/中文字体），cook 成 IoStore pak 三件套随模组分发，运行时加载实例化——控件树由引擎自己的序列化器反序列化（游戏加载任何 UI 资产的同一条原生路径），交互走 Slate 原生路由，同时消灭两大问题类。

### 环境事实（本会话探查确认）

- 游戏 `D:\Steam\steamapps\common\PBZ`，exe `platform\steam\PBZ.exe`，**UE 5.7**（FileVersionRaw 5.7.4.0；UE4SS.log `Using engine version: 5.7`）。
- `ProjectPBZ\Content\Paks\` 是 IoStore（.pak+.utoc+.ucas），**`~mods\` 已有 4 个成功加载的 mod pak 三件套**（`_P` 后缀）——分发模式已验证。
- UE4SS 无 LoadObject；**UEPseudo `FSoftObjectPath::SetPath/TryLoad` 已确认从 UE4SS.dll 导出**（UE4SS.lib 符号验证；AssetRegistry 路线，需游戏线程）。备选 `KismetSystemLibrary:LoadAsset_Blocking`（运行时探测）。
- UEPseudo 无 UMG/WidgetBlueprintLibrary 强类型头——全走项目现成的反射通用机制（find_function+ParamBlock+ProcessEvent，与 GetViewportSize/KismetTextLibrary 同族）。
- 本机无 UE 编辑器（Epic Launcher 在 D 盘）；用户决策：装 UE 5.7 编辑器、内置 Noto Sans SC 中文字体、DLL 自动拷 pak、v1 直接做增强版（相机+快捷键两页）。

### 实现

- **`src/core/menu_bridge.h/.cpp`（新）**：WbpBridge 纯逻辑桥（可单测）。C++↔资产只通过 BP 变量对话（契约写在头注释）：读 `FovValue/SpeedValue/SensValue/Command/bDragging`，写回真值/`Frozen`/`StatusId`/Command 清零。语义：滑条值≠基线→命令（config 范围夹紧）；**真值回显只在变化时推**（每帧推会把过期 ext 写进变量，松手帧被当成新输入回退旧值——滑条/相机互相拉扯的自激振荡，测试锁死）；拖动中（bDragging）完全静默；Command 沿语义执行后清 0。
- **`src/engine/ue4ss_port.*`：wbp_\* 家族**（镜像 umg2_\* 生命周期）：`wbp_load_class`（FSoftObjectPath::TryLoad → 备选 LoadAsset_Blocking 探测 → 读 `GeneratedClass`）；`ensure_wbp_menu`（CDO `/Script/UMG.Default__WidgetBlueprintLibrary` + ProcessEvent `Create`，WidgetType 走新的 `set_class` 助手——FClassProperty 存储是裸 UClass* 槽位）；`wbp_show/read_vars/apply/hide/state`——AddToViewport/SetVisibility/bShowMouseCursor/IsInViewport/查活重建全部复用实机验证过的操作；变量读写是属性直写，**零新增 ProcessEvent**（加载+Create 两个之外）。失败分类：世界未就绪可重试；资产缺失/创建失败/变量缺失永久标记不可用，上层自动落回。
- **`src/engine/wbp_layout.h`（新）**：面板矩形常量（与资产锚定一致：右上角，fromRight=484，top=90，460×560），悬停静音判定共用。
- **`src/mod_app.*`**：菜单优先级变四级 **ImGui → WBP 资产 → UMG2 文字面板 → 文字菜单**；缩放解析抽成 `resolve_panel_scale()`（两档悬停判定共用）；`ensure_pak_installed()`（启动时把 mod 目录 `paks\` 三件套拷进 `<游戏>\ProjectPBZ\Content\Paks\~mods\`，大小+mtime 没变跳过，失败只 Warn）；诊断行 `图形=` 增加 WBP 状态（顺带修了 graphic_state 临时 string 悬垂）。
- **配置** `[ui]`：`menu_asset = true`、`menu_asset_path = /Game/PBZPhotoMode/WBP_PBZPhotoMenu`、`menu_pak_autocopy = true`（config.h/.cpp/default_config_text/config.ini.example/dist config 五处同步）。
- **打包**：pack.py 按 dist 目录遍历，`dist/PBZPhotoModeCpp/paks/` 里的三件套自动进 zip；README 增 pak 自动安装说明与 Noto Sans SC（SIL OFL 1.1）字体条目。
- 版本 `1.3.0-cpp`；核心层 **415 → 469 项全绿**（WbpBridge 54 项：自举/推送沿/夹紧/拖动静默/命令沿/未知命令/重置重开）；DLL 编译链接通过（TryLoad 导出验证 ✓）。产物 `build-dll/PBZPhotoModeCpp.dll`。

### 下一步（Phase 3/4，按顺序）

1. **用户装 UE 5.7 编辑器**（Epic Launcher → D 盘，约 30-60GB）。
2. 编辑器侧（computer-use 操作）：新建空白 BP 工程命名 **ProjectPBZ**（必须与游戏工程同名，cook 产物挂载路径才能对上 /Game/）→ 导入 Noto Sans SC → **WBP_PBZPhotoMenu**（增强版：相机/快捷键两页、三条真滑条、FREEZE/RESET/EXIT、全中文；BP 变量与事件按 menu_bridge.h 契约）+ **WBP_Dummy**（隔离「pak 能否加载」与「菜单本体问题」）→ cook（`UnrealEditor-Cmd -run=Cook -targetplatform=Windows`）→ 三件套改名 `PBZPhotoMode_UI_P.*` → `dist/PBZPhotoModeCpp/paks/`。
   **纪律（分发合规）**：WBP 里只准用自建内容 + 开源字体 + 引擎默认材质，**绝不引用游戏 /Game 的任何素材**；`OFL.txt`（Noto Sans SC 许可全文）放进 `dist/PBZPhotoModeCpp/paks/` 一并随 zip 分发。
3. pack.py 出 zip → 实机验收（清单见 1.3.0 验证节，重点：pak 自动安装日志、TryLoad 通道、滑条拖动/按钮/两页切换、悬停静音、失败自动落回）。
4. 风险预案：R1 定制引擎 vs 原版 5.7 cook 产物序列化不兼容（WBP_Dummy 先隔离）；R2 拍照模式鼠标捕获抢 Slate 指针事件（备选反射调 SetInputModeGameAndUI；再不行 WBP 层自动跳过，1.2.x 面板兜底）。

## 1.3.0 Phase 3 完成（2026-10-31 12:2x：WBP 资产制作 + pak 打包 + C++ 适配）

### 资产制作管线（全部无头，零 GUI 操作）

- 工程 `D:\PBZ_UI\ProjectPBZ`（**必须与游戏工程同名**，cook 产物挂载路径才对上 /Game/）。
- **Python 路线判死**：5.7 的 Python 不暴露 WidgetTree（widget_tree 属性反射查不到）→ 改走
  **C++ editor commandlet**（`Source/ProjectPBZ/MakeMenuCommandlet.*`，`UnrealEditor-Cmd -run=MakeMenuCommandlet`）。
- 踩坑全记录：① 空白工程默认无 Python 插件（uproject 加 PythonScriptPlugin）；② uproject 声明 Runtime
  模块但无 Source 会弹窗退出（删 Modules）；③ 安装版引擎是 Shared 构建环境，Target 不能用 Unique，
  且 `CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Error` 要显式对齐；
  ④ `UFontFileImportFactory::FactoryCreateBinary` 内部调 `GEditor->GetEditorSubsystem`，commandlet 里
  GEditor 为空直接 AV（复刻工厂核心：NewObject UFontFace + FontFaceData->SetData + CacheSubFaces）；
  ⑤ `AssetTools.CreateAsset` 在 commandlet 里把资产放进 transient 包（GetOutermost()=None）→ 改
  `FKismetEditorUtilities::CreateBlueprint` 手工建包；⑥ commandlet 无编辑器根集，未保存的 WBP 会在后续
  CompileBlueprint 触发的清理里变成 obj=None（每个资产构建完立即 `UPackage::SavePackage` 底层直存）。
- 资产（声明式，零 BP 逻辑）：`WBP_PBZPhotoMenu`（右上角面板 460×560：标题/两页签/T_Status/三行真滑条
  S_Fov[15-150]/S_Speed[0-2000]/S_Sens[0.01-0.5]+数值标签/三个文本按钮/提示行；PageKeys 13 行快捷键
  默认 Collapsed）+ `WBP_Dummy`（"PAK OK"，隔离 R1）+ Noto Sans SC 字体（OFL.txt 随包）。
  控件命名与 `wbp_layout.h` 常量一致。
- cook：`-run=Cook -targetplatform=Windows`（DirectoriesToAlwaysCook=/Game/PBZPhotoMode）→
  `UnrealPak -create` 单文件经典 pak `PBZPhotoMode_UI_P.pak`（7.3MB，挂载点 `../../../ProjectPBZ/`，
  8 文件含 AssetRegistry.bin——TryLoad 的 AssetRegistry 查询依赖它随包挂载）。
- **契约变更**（相对 1.3.0 初版设计）：资产是声明式的 → C++ 改为滑条 `Value` 属性轮询（零 ProcessEvent）
  + `GetWidgetFromName` 一次缓存 9 个子控件 + `SetValue`/`SetText` 回显（UMG 引擎函数，与 SetText 同族）
  + 按钮命中区表（wbp_layout.h kHits，1.2.x 机器）+ `SetVisibility` 切页。WbpBridge 语义不变
  （滑条值≠基线→命令；真值变化才回显），单测保持全绿。
- 交付：WBP 资产随 1.5.0 包分发（1.3.0 独立包取消——与并行会话的手柄/超宽屏工作合并为一个包）。
  DLL 启动自动把 `paks\*.pak` 拷进游戏 `ProjectPBZ\Content\Paks\~mods\`。

### 1.3.0 WBP 菜单实机验证要点（决定性一轮）

1. **装两个模组**：主模组（DLL，装 `ue4ss\Mods\PBZPhotoModeCpp\`）+ 独立资产模组 PBZPhotoModeUI（pak 按 README 装进 `ProjectPBZ\Content\Paks\~mods\`，1.5.0 起拆分）。启动日志：`pak 通道：模组目录没有 paks\（正常…）`；进游戏等 10 秒无异常（pak 挂载冲突会在这暴露）。
2. F2 进摄影模式 → 日志 `WBP 菜单已创建：…加载通道 TryLoad`；**WBP_Dummy 校验**：左上 "PAK OK"——
   这一行决定 R1（定制引擎能否加载 5.7 cook 资产）。
3. 屏幕右上角出现**中文图形面板**（深色半透明底、两页签、三条滑条）。
4. 拖 FOV 滑条：画面即时变化、数值标签联动；滚轮调 FOV 滑条反向同步；SPEED/SENS 同理。
5. 点 冻结时间/重置视角/退出摄影（文本按钮，命中区判定）；点「快捷键」页签切页。
6. 指针在面板上时相机不跟转；Esc/T 开关；退出模式指针恢复。
7. 失败分支：资产加载失败/子控件缺失/CreateWidget 空 → 日志各有专属 Warn 行，自动落回 UMG2 文字面板
   →文字菜单，摄影功能不受影响。
8. 若 Dummy 显示但主菜单不显示/花屏 → 菜单本体问题（布局/字体），把日志发回来。
9. 若连 Dummy 都不显示 → R1 成立：定制引擎拒载 5.7 cook 包；下一步试 IoStore 三件套或查 pak 白名单。

## 1.4.0-cpp（2026-10-31：手柄后端扩展——DualSense 原生支持，L3+R3 进摄影模式）

**触发**：玩家反映 DualSense 手柄按 L3+R3 进不了摄影模式。日志（2026-10-30 20:56-20:59）：
`加载完成 v1.0.18-cpp ... （XInput 已加载：C:\WINDOWS\SYSTEM32\XInput1_4.dll）`、
`手柄扫描正常：暂未发现手柄`、全程输入诊断行 `手柄槽位=-1`。

### 根因

手柄支持是 **XInput-only**（`win_input.cpp` 只轮询 `XInputGetState`）。DualSense 在 Windows 上
**不是 XInput 设备**（是标准 HID/DirectInput 手柄），四个槽位永远返回未连接 → 模组永远看不到手柄。
游戏本体走自己的输入栈所以能正常玩，只有模组的热键失灵。玩家装的 v1.0.18 与最新的 1.3.0 都一样——
必须加后端，升级不解决。

### 修法（DirectInput8 轮询后端，选型已与开发者确认）

- **选型**：DInput8 `GetDeviceState` 每帧主动拉状态（不可能被事件派发饿死）而不是 Raw Input HID 事件流
  ——本作鼠标 raw input 出过「注册成功但一个事件都不来」（1.0.2 教训）。dinput8.dll 动态加载，
  不链 dinput.lib（`IID_IDirectInput8W`/`GUID_*` 用 `<initguid.h>` 实体化；`DIJOYSTATE2` 格式表手绘，
  新 SDK 已删 `DIDFT_OPTIONAL` 需补定义、`DIJOFS_*` 的 `FIELD_OFFSET` 进不了 constexpr 用 offsetof 自算、
  `rgodf` 是非 const 指针数组不能加 const——三条编译踩坑）。
- **`core/gamepad.h/.cpp`（可测纯逻辑）**：`dinput_button_to_pad`（rgbButtons 0..13 → □╳○△/L1R1/L2R2/
  CREATE/OPTIONS/**L3=LS、R3=RS**/PS，13 触摸板无对应）、`dinput_pov_to_buttons`（45° 扇区→四方向）、
  `dinput_axis_to_state`（0..65535 中心 32767.5，LY/LRz 取反成「正=上」，扳机 LRx/LRy → 0..1）。
- **`win_input.h/.cpp`**：`load_dinput()`（仿 `load_xinput()`，System32 优先）；设备全在 poll() 线程
  惰性初始化（CoInitializeEx + DirectInput8Create + 隐藏合作级窗口）→ `EnumDevices(GAMECTRL)`
  （多设备优先 Sony VID 054C）→ CreateDevice/SetDataFormat/后台非独占合作级/逐轴 DIPROP_RANGE
  0..65535/Acquire；读失败补 Acquire，连续 3 帧失败释放重枚举（对齐 XInput 拔出判定），枚举限流 2 秒。
- **三态合并**：XInput 槽位已锁定 → XInput 快照（Xbox 手柄在两套 API 各出现一次，XInput 优先防重复）；
  否则 DInput 已获取 → DInput 快照；否则空。`pad_connected()` = XInput 槽位 || DInput 在场。
- **诊断**：`pad_backend()`（"XInput 槽位 N" / "DInput: Wireless Controller（VID_054C&PID_…）"）进
  加载行/扫描行/每秒诊断行（`手柄=...`）；一次性 `DInput 手柄已接入：...首读：按钮=0x… 摇杆=… 扳机=… POV=…`
  （原始值，映射对不上真机时一眼定位）；初始化失败也产出一次性诊断（CreateDevice/SetDataFormat/
  合作级/Acquire/量程 分步报）。
- **文档**：随包 README 手柄三处更新（PS 手柄原生支持说明 + 「旧版本槽位=-1 是正常现象请升级」）；
  `docs/README.md` 新增「手柄支持」节；`config.ini.example` 注明 DInput 后端。
  **注意**：随包 README 已有「手柄看不到？先关 Steam 输入」是给 Xbox 手柄的；PS 手柄同样建议停用
  Steam 输入——本模组 v1.0.18 起直读 System32 XInput 绕开代理 DLL，Steam Input 的虚拟手柄不保证可见，
  **不能拿「开 Steam 输入」当 PS 手柄的临时方案**，临时方案只有键盘 F2。

- 版本 `1.4.0-cpp`；核心层 **469 → 503 项全绿**（`test_dinput_map` 34 项：按钮全表/POV 扇区+居中/轴归一化）；
  DLL 编译链接通过。产物 `build-dll/PBZPhotoModeCpp.dll`，包 `dist/PBZPhotoModeCpp_1.4.0-cpp.zip`。

### 1.4.0 实机验证要点（DualSense）

1. USB 与蓝牙各测一次：启动日志应出现 `DInput 手柄已接入：Wireless Controller（VID_054C&PID_0CE6）…`
   与 `手柄已连接（DInput: ...）`；每秒诊断行 `手柄=DInput: ...`。
2. **L3+R3 进/出摄影模式**（本次修复的主目标）；左摇杆移动、右摇杆视角、L2/R2 升降、L1/R1 变速、
   十字键 FOV、OPTIONS 菜单、╳ 冻结、△ 重置（PS 实体键 = config 里的 X/Y 位）。
3. 插 Xbox 手柄再试：日志应回到 `手柄已连接（XInput 槽位 N）`（优先级未破坏）。
4. 若 L3+R3 无效但手柄已连接：把 `DInput 手柄已接入：...首读` 那行发回来——按钮位图会直接暴露
   rgbButtons 实际布局与映射表的偏差（蓝牙描述符与 USB 有差异的可能）。
5. 回归：键鼠各功能不受影响（DInput 后端只在 XInput 无手柄时活动）。

## 1.5.0-cpp（2026-10-31：超宽屏支持——进入摄影模式解除 16:9 画幅锁，消除两侧黑边）

**触发**：超宽屏（21:9/32:9）用户反映进入摄影模式后画面两侧仍有黑边，要求加超宽屏支持。
官方 PC 规格明确覆盖超宽屏（超过 3:1 的比例；21:9≈2.33:1、32:9≈3.56:1 均在官方支持范围内），
正常游玩不应有黑边——问题指向**摄影模式路径上的画幅约束**（拍照/演出常主动锁画幅出构图），
而不是游戏不支持宽屏。社区另有第三方 mod 从相机系统入手做解锁，佐证锁在相机链路上。

### 根因与机制

- 黑边 = 画幅被约束在 16:9 后两侧补黑。UE 里这把锁最常挂在两处：
  ① `APlayerCameraManager.bDefaultConstrainAspectRatio` + `DefaultAspectRatio`——**对任何 ViewTarget 生效**，
  就算 SetViewTarget 换成我们的摄影相机也锁；② 相机组件自己的 `bConstrainAspectRatio`。
- 我们生成的 CameraActor 组件默认不锁；① 是否被游戏打开由真机日志确认（每秒诊断行 `画幅锁=开/关(比例)`）。
  解锁动作 = 进模式时把 `bDefaultConstrainAspectRatio` 写 false（原值存下，退出写回），
  顺手把摄影相机组件的 `bConstrainAspectRatio` 也显式关掉保险；两处都没锁时解锁是无操作、退出不写回。
- 若两处都没锁而黑边仍在：锁在别的机制（游戏 PV 路径的专用约束/UI 层画黑边/LocalPlayer 尺寸覆写），
  按诊断行与截图定位后下一版处理。
- 宽屏 FOV 行为：解除约束后走引擎默认 `AspectRatioAxisConstraint`（MaintainYFOV），垂直 FOV 不变、
  水平视野变宽——正是摄影想要的。FOV 滑条语义（水平 FOV）不变。

### 实现

- **`core/engine_port.h`**：新增 `unlock_aspect_ratio()` / `restore_aspect_ratio()` 两个端口方法
  （失败只告警不阻断进模式，与 hide_hud 同级）。
- **`core/photo_mode.*`**：`ultrawide_unlock` 配置（默认 true）→ enter() 在 HUD 隐藏之后解锁（`aspect_unlocked_` 记账），
  leave() 在 postfx 还原之后写回；幂等退出的空判断加上该标记。
- **`engine/ue4ss_port.*`**：解锁读改写相机管理器属性；只在确实锁着时记原值（没锁=无需写回）；
  还原前校验管理器指针还是进入时那个（防换持有时把旧值写进新管理器）；属性缺失/写入失败打 warning
  （黑边若来自别的机制——比如 UI 层画黑边——真机日志会立刻暴露）。
- **诊断**：每秒诊断行 `画幅锁=开/关(比例)` 追加在相机状态摘要里，真机验证一眼可查。
- **配置**：`[camera] ultrawide_unlock = true`（config.h/config.cpp/default_config_text/config.ini.example/dist config 五处同步）；
  想保留 16:9 构图的用户可关。
- 版本 `1.5.0-cpp`；核心层 **503 → 527 项全绿**（`test_photo_mode_ultrawide` 5 组场景：默认进退/配置关闭/
  解锁失败不阻断/游戏未锁/热重载后仍按进入时还原；happy_path 进退序列断言同步更新）；
  DLL 编译链接通过；包 `dist/PBZPhotoModeCpp_1.5.0-cpp.zip`。

### 1.5.0 实机验证要点（超宽屏）

0. 先分清黑边范围：官方支持超宽屏，**正常游玩**应有黑边吗？让报告者确认平时玩（不在摄影模式）两侧有没有黑边——
   如果正常游玩也有，先查窗口/分辨率/显示器设置，与模组无关；只有摄影模式有黑边才是本版要解的。
1. 21:9/32:9 显示器进摄影模式：日志出现 `画幅解锁：已解除 16:9 锁定（原比例=1.778）` 则两侧黑边应消失、
   画面铺满全屏，每秒诊断行 `画幅锁=关(1.778)`；若日志是 `画幅：相机管理器未锁定画幅，无需解锁`
   但黑边仍在，按第 4 条处理。
2. 退出摄影模式：黑边恢复，日志 `画幅还原：相机管理器已恢复 16:9 锁定`，诊断行回到 `画幅锁=开(1.778)`。
3. 16:9 显示器回归：进出各一次，画面无变化（解锁对 16:9 是视觉无操作），功能不受影响。
4. 若解锁后黑边仍在：看日志——`未锁定画幅，无需解锁` 或属性缺失的 warning 都说明锁在别处
   （游戏 PV 路径的专用约束/UI 层黑边/LocalPlayer 尺寸覆写），把日志和截图发回来定下一版方案。
5. FOV 滑条在超宽屏下的范围体验（15~150 水平 FOV 在 32:9 上是否合适）收集反馈再调。

## 1.5.0 补记（2026-10-31 12:4x：图形菜单资产拆分为独立模组 PBZPhotoModeUI）

用户判断（正确）：pak 资产本就该是独立 pak mod——游戏生态里 pak mod 直接装 `~mods`（现有 4 个先例），
塞进 DLL zip 才被迫加了「DLL 运行时转拷」补丁层。拆分后资产/逻辑独立更新、玩家可只装 DLL（文字菜单兜底）、
OFL 许可跟着资产走。

- **交付双包**：`dist/PBZPhotoModeCpp_1.5.0-cpp.zip`（466KB，纯 DLL）+ `dist/PBZPhotoModeUI_1.5.0-cpp.zip`
  （7.3MB，pak + README.txt + OFL.txt；按 README 装进 `ProjectPBZ\Content\Paks\~mods\`）。
- **DLL 侧**：`ensure_pak_installed` 降级为「开发者通道」——只有玩家手动把 pak 放进本模组 `paks\`
  才转拷（资产迭代调试用）；启动日志 `pak 通道：模组目录没有 paks\（正常…）`。
  `menu_pak_autocopy` 配置键保留，注释四处同步更新。
- pack.py 改双包打包（PBZPhotoModeCpp + PBZPhotoModeUI，同号发布）。核心层 527 项全绿不变。
