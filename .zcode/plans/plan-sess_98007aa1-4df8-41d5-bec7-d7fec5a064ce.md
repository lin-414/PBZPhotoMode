# 方案 E：离线制作 WBP 资产，替换运行时拼装的 UMG 菜单（v1.3.0-cpp）

## Context

1.2.x 的交互菜单是运行时用反射拼装 CanvasPanel+TextBlock + 自建命中判定：1.2.0 拼新控件崩溃、1.2.1~1.2.3 连续三轮修命中区偏移仍未实机确认。方案 E 改为**离线在 UE 5.7 编辑器里制作真正的 WidgetBlueprint 资产**（真滑条/真按钮/分页/中文字体），打成 IoStore pak 放进游戏 `~mods`，C++ 运行时加载实例化。控件树由引擎自己的序列化器反序列化、输入走 Slate 原生路由——同时消灭「运行时拼装赌命」和「命中区坐标对齐」两大问题类。

已核实的环境事实：
- 游戏 `D:\Steam\steamapps\common\PBZ`，UE **5.7**（5.7.4.0），`ProjectPBZ\Content\Paks\` 是 IoStore（.pak+.utoc+.ucas），`~mods\` 已有 4 个成功加载的 mod pak 三件套（模式已验证）。
- UE4SS 无 LoadObject；UEPseudo 提供 `FSoftObjectPath::SetPath/TryLoad`（AssetRegistry 路线，需游戏线程）；备选 `KismetSystemLibrary:LoadAsset_Blocking` 运行时探测。
- 现有可复用设施：ParamBlock/invoke（ue4ss_port.cpp:294-405）、CDO+ProcessEvent 样板（umg2_viewport_size :2052、make_ftext_into :1524）、AddToViewport/SetVisibility/bShowMouseCursor/IsInViewport/查活重建（1.0.19 起实机验证）、read/write_number + value_ptr（BP 变量轮询直接可用）、菜单四级优先级骨架（update_overlay mod_app.cpp:392-522）、1.2.3 的缩放求真逻辑（find_umg2_window/umg2_dpi :53-109）。
- 本机无 UE 编辑器；Epic Launcher 已装（D 盘，234GB 空闲）。

用户决策：装 UE 5.7 编辑器（C++ 并行开工）／内置 Noto Sans SC 中文字体／DLL 启动自动拷贝 pak＋文档兜底／v1 直接做增强版（含快捷键页）。

## Phase 0 — 基线

仓库自 1.0.18 后从未提交（1.0.19~1.2.3 全在工作区）。先 `git add -A && git commit` 固化 1.2.3 基线，再开工。

## Phase 1 — 编辑器安装（用户动作，与 Phase 2 并行）

Epic Launcher → Unreal Engine 5.7 安装到 D 盘。装好后由我通过 computer-use 操作编辑器（Phase 3）。

## Phase 2 — C++ 侧（立即可做，资产未就绪时优雅降级）

### 2.1 引擎层 `src/engine/ue4ss_port.h/.cpp`：新增 wbp_* 家族（镜像 umg2_* 模式）

- **加载** `wbp_find_class(path)`：`FSoftObjectPath::SetPath + TryLoad`（仅 `mode_->active()` 时、游戏线程）拿到 WidgetBlueprint 对象 → `read_object` 读 `GeneratedClass` 属性得到 UClass*。TryLoad 若链接失败，备选运行时探测 CDO `/Script/Engine.Default__KismetSystemLibrary` 的 `LoadAsset_Blocking` UFunction（GetFunctionByNameInChain + ParamBlock，同 GetViewportSize 样板）。日志注明走的哪条通道。
- **创建** `wbp_create(cls)`：CDO `/Script/UMG.Default__WidgetBlueprintLibrary` + find_function `Create`，参数 WorldContextObject=world()、WidgetType=cls（新增 `set_class` 助手处理 FClassProperty——现 write_object 只认 FObjectProperty）、OwningPlayer=PC → ReturnValue=UUserWidget*。
- **生命周期** `ensure_wbp_menu / wbp_tick / wbp_hide / wbp_available / wbp_state / wbp_live`：完全复用已验证操作——AddToViewport(ZOrder 10000)、SetVisibility、bShowMouseCursor、每秒 IsInViewport 校验、GetFullName 查活重建、GC 后路径查活。
- **交互零新增 ProcessEvent**：C++ 只通过 BP 变量与资产通信——读 `FovValue/SpeedValue/SensValue/Command/bDragging`（find_property+value_ptr+read_number），写回浮点变量与 `Frozen` bool（write_number）、状态文本走 make_ftext_into 写 FText 变量。BP 内部（Tick/OnValueChanged/OnClicked）负责渲染与取值，C++ 不碰子控件、不调 GetWidgetFromName。

### 2.2 核心层 `src/core/menu_bridge.h/.cpp`（新，纯逻辑可单测）

`WbpBridge::step(BP变量快照, 外部真值) → 命令集 + 写回集`：滑条值≠上次应用值→下发 set_fov/set_config（clamp 后写回夹紧值）；`Command` 整数沿语义（执行一次即写回 0）；`bDragging` 时抑制写回防抖；外部变化（滚轮/快捷键）→写回变量由 BP Tick 同步滑条显示。仿 InputLogic 风格，新增 ~20 项测试。

### 2.3 接线 `src/mod_app.cpp/.h`

- 菜单优先级变四级：ImGui → **WBP 资产菜单（新）** → UMG2 文本面板 → 文字菜单；WBP 层调用 bridge step 后走既有 apply 路径（set_fov/toggle_freeze/reset/exit/live setters）。
- 悬停静音：复用 1.2.3 命中/缩放机器（find_umg2_window/umg2_dpi/umg2_viewport_size），面板矩形常量放新文件 `src/engine/wbp_layout.h`（与资产锚定一致，模式同 umg2_layout.h）。
- 启动时（on_unreal_init）自动拷贝 pak：GetModuleFileName 定位模组目录 → 上溯 4 级到游戏根 → 拷 `assets/paks/PBZPhotoMode_UI_P.{pak,utoc,ucas}` 到 `ProjectPBZ\Content\Paks\~mods\`（内容变化才覆盖；目录不存在/失败只 Warn 并打手动步骤，不阻断）。

### 2.4 配置与打包

- `[ui]` 新增 `menu_asset = true`、`menu_asset_path = /Game/PBZPhotoMode/WBP_PBZPhotoMenu`、`menu_pak_autocopy = true`（四处同步：config.h/.cpp/default_config_text/config.ini.example + 往返测试）。
- `tools/pack.py`：把 `assets/paks/` 三件套打进 zip（`PBZPhotoModeCpp/paks/`），README 更新（自动拷贝说明 + 手动兜底步骤 + Noto Sans SC OFL 许可条目）。
- 版本 `src/core/version.h` → `1.3.0-cpp`；`tools/build_tests.bat` 全绿（415 → ~435）；`tools/build_dll.bat` 产物进 zip。

## Phase 3 — 编辑器侧（编辑器装好后，computer-use 操作）

1. 新建空白 Blueprint 工程命名 **ProjectPBZ**（必须与游戏工程同名，保证 cook 产物挂载路径 `ProjectPBZ/Content/...` 对上 /Game/），无 Starter Content。
2. 导入 Noto Sans SC（SIL OFL）字体资产。
3. **WBP_PBZPhotoMenu**（增强版）：右上角锚定（与 wbp_layout.h 常量一致）；页签「相机 / 快捷键」（按钮切页可见性，BP 内部）；相机页=FOV/SPEED/SENS 三条真滑条+数值标签+FREEZE/RESET/EXIT 按钮+状态文本；快捷键页=hints 列表；全中文文案。BP 逻辑：滑条 OnValueChanged 写变量+标签、MouseCaptureBegin/End 写 bDragging、按钮 OnClicked 写 Command=1/2/3、Event Tick 同步滑条显示（!bDragging 时）与状态文本。
4. **WBP_Dummy**（仅一行 "PAK OK" 文本）用于隔离「pak 能否被本作加载」与「菜单本体问题」。
5. Cook：`UnrealEditor-Cmd.exe <uproject> -run=Cook -targetplatform=Windows` → `Saved\Cooked` 取三件套 → 重命名 `PBZPhotoMode_UI_P.pak/.utoc/.ucas` → `assets/paks/`。

## Phase 4 — 构建与实机验证（一轮）

`build_dll.bat` + `pack.py` → `dist/PBZPhotoModeCpp_1.3.0-cpp.zip`（用户模组管理器安装，DLL 启动自动拷 pak）。

实机验收点（UE4SS.log + 屏幕对照）：
1. 启动：`pak 自动拷贝完成（3 个文件）`；F2 后 `WBP 资产已加载（TryLoad/LoadAsset_Blocking）`、`WBP 菜单已创建`、WBP_Dummy 校验行。
2. 菜单为中文图形面板、两页签可切换；拖 FOV 滑条画面即时变化、数值标签联动；滚轮调 FOV 后滑条反同步；SPEED/SENS 同理。
3. FREEZE/RESET/EXIT 各按钮生效；指针在面板上时相机不跟转（悬停静音）；Esc/T 开关；退出模式后指针恢复、无残留。
4. 失败分支：资产加载失败/创建失败 → 自动落回 UMG2 文本菜单（现有行为不变），日志逐步定位卡点。

## 风险与对策

- R1 端游定制引擎 vs 端游 5.7 cook 产物序列化不兼容 → WBP_Dummy 先行隔离；失败则评估升级/降级 cook 选项。
- R2 拍照模式鼠标捕获可能抢 Slate 指针事件（1.2.0 未验证点）→ 备选反射调 `SetInputModeGameAndUI`；再不行 WBP 层自动跳过、UMG2 面板仍兜底。
- R3 新增 ProcessEvent 面最小化（仅 Create＋加载通道两个新调用，与已验证的 Kismet 静态函数同族）。
- R4 TryLoad 未从 UE4SS.dll 导出 → 编译期即知，切 LoadAsset_Blocking 探测链。
- R5 IoStore cook 流水线首次跑通的问题 → Phase 3 内迭代，不阻塞已完成的 C++ 侧。

## 验证

- 单元：`tools/build_tests.bat` 415 → ~435 全绿（新增 WbpBridge/配置往返）。
- 构建：`tools/build_dll.bat` 通过（Game__Shipping__Win64）。
- 实机：Phase 4 验收清单，一轮完成；任一失败按日志分支回到对应 Phase 修复。