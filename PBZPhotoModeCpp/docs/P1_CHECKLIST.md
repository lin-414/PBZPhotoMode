# P1 核实清单

目标：拿到 UEPseudo 后，把 `src/engine/ue4ss_port.cpp` 与 `src/mod_app.cpp` 里带 `/// P1` 的地方
对准真实 API，编译通过并在游戏里跑通。**上层 `src/core/` 不用动**（217 项测试已锁住行为）。

## A. 构建链路

| # | 事项 | 怎么看 |
| --- | --- | --- |
| A1 | 确认 UE4SS 版本与游戏内 DLL 一致 | 对比 `ue4ss\UE4SS.dll` 的版本与 `_ref/ue4ss-src` 的 tag |
| A2 | 子模块拉全（UEPseudo、patternsleuth 等） | `git submodule status` 无前缀 `-` |
| A3 | `add_subdirectory(UE4SS)` 的 target 名 | UE4SS CMakeLists 里 `add_library(UE4SS SHARED …)` |
| A4 | C++ 标准 / 运行库与本仓库一致 | 两边都用 C++20、`/MD` |

## B. ue4ss_port.cpp 的 12 个辅助函数

按文件内出现顺序，逐个对照 UE4SS 源码里的等价实现（优先看 UObject 绑定层源码，
它们展示了“按名字找对象 / 读属性 / 调函数”的官方写法）。

| 函数 | 要做的事 | 候选 API（按可用性优先级） |
| --- | --- | --- |
| `find_object` | 按路径找对象 | `UObjectGlobals::static_find_object` / `find_uobject` |
| `find_first_instance` | 场景里第一个某类实例 | `UObjectGlobals::find_first_of` / `find_all_of` |
| `is_valid` | 判存活 | `UObjectGlobals::is_valid` / `obj->IsValidLowLevel()` |
| `call_function` | 调无返回值 UFunction | `obj->ProcessEvent(fn, params)`，`fn = GetClassPrivate()->get_function(FName(...))` |
| `get_bool` / `get_double` | 读属性 | `GetValuePtrByPropertyNameInChain<FBoolProperty/FFloatProperty>` |
| `get_vec` / `set_vec` | 读写 FVector 属性 | `FStructProperty` + `RC::FVector` 的 `X()/SetX()` |
| `get_rot` / `set_rot` | 读写 FRotator 属性 | `FStructProperty` + `RC::FRotator` 的 `Pitch()/SetPitch()` |
| `now_seconds` | 单调时钟 | 本仓库已改用 `std::chrono`，此函数可删 |

## C. 每个 EnginePort 方法要核对的引擎函数

| 方法 | 引擎函数 | 备注 |
| --- | --- | --- |
| `read_pov` | `APlayerCameraManager::GetCameraLocation/GetCameraRotation/GetFOVAngle` | FOV 在 UE5 是 `float`，返回结构体里占 8 字节时要注意对齐 |
| `spawn_camera` | `UWorld::SpawnActor`（或 `StaticConstructObject_Internal` + `FinishSpawning`） | **当前是占位实现**，必须替换 |
| `attach_view` | `APlayerController::SetViewTargetWithBlend(AActor*, float, EViewTargetBlendFunction, float, bool)` | 5 个参数，早期原型踩过 4 参的坑 |
| `restore_view` | `SetViewTarget(nullptr)` | 语义 = 回到自己的 Pawn |
| `apply_pose` | `AActor::SetActorLocationAndRotation(FVector, FRotator, bool, bool)` | bool 后要补 4 字节对齐 |
| `apply_fov` | `UCameraComponent::SetFieldOfView(float)` | 相机组件属性名 `CameraComponent` |
| `destroy_camera` | `AActor::K2_DestroyActor()` | 退出时务必调用 |
| `set_ignore_move_input` | `APlayerController::SetIgnoreMoveInput(bool)` | **只屏蔽移动**，别用 SetIgnoreLookInput（会连带屏蔽鼠标） |
| `set_player_hidden` | `AActor::SetActorHiddenInGame(bool)` | 作用于 Pawn |
| `set_hud_visible` | `UWidget::SetVisibility(ESlateVisibility)` on `W_FrontEndHUDLayout_C` | 本作 HUD 是 UMG；`AHUD::SetActorHiddenInGame` 仅作兜底 |
| `get/set_time_dilation` | `UGameplayStatics::Get/SetGlobalTimeDilation` | CDO 路径 `/Script/Engine.Default__GameplayStatics` |
| `set_paused` | `APlayerController::SetPause(bool)` | 仅 `freeze_mode = pause` 时用 |

## D. mod_app.cpp 的接线点

| 位置 | 要做的事 | 说明 |
| --- | --- | --- |
| `mod_directory()` | 拿 mods 目录 | `UE4SSProgram` 的 mods 目录接口 |
| `bind_input()` | 把按键/鼠标喂进 `InputLogic` | 需要 keydown+keyup（脚本层的 RegisterKeyBind 只有 keydown）；鼠标位移与滚轮走 `InputAxis` 或 Win32 输入 |
| `bind_hooks()` | 引擎 Tick 与 `PlayerController:InputKey/InputAxis` | 参照已验证的输入方案：`InputKey` 里有 `FKey`，`InputAxis` 里有 `Delta` |
| `on_update()` | 切游戏线程 | 读写 UObject 必须在游戏线程；用 UE4SS 的 game-thread task 接口包一层 |
| `render()` | ImGui 绘制 | 已经用 `ImGui::` 写好，确认 UE4SS 侧的 `ImGui` 头与版本 |

## E. 验收标准

1. `tools\build_tests.bat` 仍全绿（核心层没被改坏）
2. DLL 能编译、能被 UE4SS 加载，日志出现 `加载完成，版本 1.0.0-cpp`
3. 游戏内 `F2` 进入：视角切到自由相机、角色消失、角色输入被屏蔽
4. `Alt + 鼠标` 能转视角；`WASD/QE` 能移动；滚轮能改 FOV
5. `F` 冻结后画面静止但相机仍可动；再按 `F` 恢复
6. `F2` 退出：视角、角色、输入、时间全部还原，无残留
7. ImGui 面板里的滑块/按钮与快捷键效果一致

---

## P1 结果（2026-10-30，已编译通过）

构建链路：MSVC 19.44.35229.0 + Ninja + `-DCMAKE_BUILD_TYPE=Game__Shipping__Win64`
（UE4SS 自定义构建类型，不是 `Release`）→ `build-dll/PBZPhotoModeCpp.dll`（535,552 字节）。
`dumpbin /imports` 的 71 个 UE4SS 符号全部命中游戏内 `UE4SS.dll` 导出表。

候选 → 实际采用（B/C/D 三张表逐项落实）：

| 清单项 | 实际做法 |
| --- | --- |
| `find_object` | `UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/..."))` |
| `find_first_instance` | `UObjectGlobals::FindFirstOf(STR("PlayerController"))`（按类名，含继承） |
| `is_valid` | `obj->GetClassPrivate() != nullptr`（**UEPseudo 无 `UObjectGlobals::IsValid`**） |
| `call_function` | 反射参数块：`UFunction::GetStructureSize()` + `TFieldIterator<FProperty>` 按名写值 + `obj->ProcessEvent(fn, block)` |
| `get_vec/set_vec`、`get_rot/set_rot` | 同样走属性反射，按 `FStructProperty::GetStruct()` 递归；元素大小自适应 float/double |
| `spawn_camera` | `UWorld::SpawnActor(UClass*, FVector const*, FRotator const*)`，UClass = `/Script/Engine.CameraActor` |
| `attach_view` | `PlayerController` 的 `SetViewTargetWithBlend`（NewViewTarget / BlendTime，按名写参，不猜 4/5 参） |
| `apply_pose` | `AActor::K2_SetActorLocationAndRotation(FVector, FRotator, bool, FHitResult&, bool)` |
| `apply_fov` | 直接写相机组件属性 `FieldOfView`（float/double 自适应）—— 早期原型就是卡在这一步 |
| `destroy_camera` | `AActor::K2_DestroyActor()` |
| `set_ignore_move_input` | `PlayerController::SetIgnoreMoveInput`（**只屏蔽移动**） |
| `set_hud_visible` | 枚举 `W_FrontEndHUDLayout_C`（`PBZHUDLayout` 子类）→ `UWidget::SetVisibility(Collapsed)`，记录/恢复原 `Visibility`；`PlayerController.MyHUD` 隐藏作为兜底 |
| `get/set_time_dilation` | GameplayStatics CDO 的 `Get/SetGlobalTimeDilation` |
| `mod_directory()` | 走 `CppUserModBase` 提供的路径接口 + `config.ini` |
| `bind_input()` | **改为 Win32 输入**：`WH_MOUSE_LL` 钩子 + `GetAsyncKeyState` 轮询（UE4SS C++ 只有 keydown，无 keyup，无法表达“按住 Alt”） |
| `bind_hooks()` | 只挂 `Hook::RegisterEngineTickPreCallback`（InputKey/InputAxis hook 因参数块布局随版本变动而放弃） |
| `on_update()` | 在引擎 Tick 回调里直接跑（已是游戏线程），读写 UObject 无需再切线程 |
| `render()` | `register_tab` + `UE4SS_ENABLE_IMGUI()`；另用 `KismetSystemLibrary::PrintString` 画右下角 ASCII 兜底 |

验收：E1 ✅（217 项全绿）、E2 ✅（编译通过、符号匹配）；E3–E7 待实机验证。

