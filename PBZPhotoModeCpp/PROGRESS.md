# 进度存档（2026-11-01 更新）

接着做的入口：读这份文件 → 看「下一步」→ 直接干。

## 一句话状态

**C++ 版 1.0.62（分类树韩语节点问号修复：框架默认字体合并 Malgun）已实现**——见「v1.0.62 修了什么」。此前 1.0.53（词典稳定命名键 + en-US 基准词典）已实现——见「v1.0.53 改了什么」。此前 1.0.52（四语言词典外部化：DLL 不再内嵌文本）已实现——见「v1.0.52 改了什么」。此前 1.0.51（dilation 判死 + POV 监测 idle 门控）已实现——见「v1.0.51 修了什么」。此前 1.0.50（POV 微动监测 + MPC 转正确认）已实现——见「v1.0.50 修了什么」。此前 1.0.49（MPC 取值修复：ReturnValue 兜底）已实现——见「v1.0.49 修了什么」。此前 1.0.48（MPC 快照 0 参数逐环节诊断）已实现——见「v1.0.48 修了什么」。此前 1.0.47（冻结治理实机排障：数组判定失灵修复）已实现——见「v1.0.47 修了什么」。此前 1.0.46（方向键/十字键左右：平移改转向）已实现——见「v1.0.46 修了什么」。此前 1.0.45（进入 FOV 取游戏当前镜头 + 进出全对象扫描按类缓存提速）已实现——见「v1.0.45 修了什么」。此前 1.0.44（隐藏锁定敌人的白点标记）已实现——见「v1.0.44 修了什么」。此前 1.0.41（冻结期残余微动治理 + 滚转归零快捷键）已实现——见「v1.0.41 修了什么」。此前 1.0.39 已编译（1.0.38 实机复盘：不进摄影模式已无卡顿；模式内转视角每几秒一卡。三条根因都修了：① Xbox 手柄锁定 XInput 槽位后 DInput 后台线程仍每 2 秒枚举同一设备、SetDataFormat 必败，诊断串反复回游戏线程写 UE4SS.log（一次会话刷 68 行）——现在 XInput 锁定即完全停手（恢复 1.0.37 语义），且相同诊断只发布一次；② 摄影模式内每秒的输入诊断日志（反射摘要+日志落盘）改为 mod 目录放 debug_input_diag.txt 才开启，默认关；③ 字幕收编每秒遍历全对象数组（大场景单次几十毫秒）——世界硬暂停时控件 Tick 冻住、游戏不会重新显示控件，暂停期零扫描；未暂停（dilation 冻结/暂停被解除）才按秒收编，30 秒例行兜底。1.0.38 的 DInput 后台线程化、HUD/提示/血条重试 10 次封顶、SetViewTargetWithBlend/GetInputMouseDelta 反射缓存、语音钩子重试封顶全量保留）。
核心层 **754 项测试全绿**；引擎内图形菜单路线六连判死（PROGRESS v1.6.4 终审），真图形菜单若日后重启只剩 D2 DirectComposition 覆盖窗一条路。**1.0.39 实机已验证：卡顿消失**（不进模式无卡顿、模式内转视角无周期性卡顿、进出还原全部正常、DInput 刷日志归零）。**1.0.40 收尾**（1.0.39 日志复盘的两处小尾巴）：① 暂停期跳过字幕收集后空 widgets 仍会落到「场上没有 USubtitleDisplay 控件」告警并白跑一次候选全扫描（每进程首次重申一次）——现在直接早退；② class_chain_contains 改 FName 池索引比较（原逐节点 GetName 转 FString，入场全量扫描实机 ~270ms 小分配风暴）——入场扫描显著提速。

## 交付流程（2026-10-31 起固定）

- **只出包，不部署**：构建完成后把产物打进 `dist/PBZPhotoModeCpp_<版本>-cpp.zip`（结构：`PBZPhotoModeCpp/{dlls/PBZPhotoModeCpp.dll, languages/*.json, config.ini, enabled.txt, README.md}`），**不要**把 DLL/README/config 覆盖到游戏目录——用户用模组管理器自行安装 zip。1.0.52 起 `tools/pack.py` 会自动把仓库 `languages/` 同步进发行目录。
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

## v1.0.62 修了什么（2026-11-01 分类树韩语节点全显问号：框架默认字体合并 Malgun）

实机（0.5.0+ 分类树框架）反馈：语言切到 한국어 后，左树「PBZ Photo Mode」下的节点名整串 `???`（逐条比对 = 词典里六个分组键的韩文译文，顺序与 schema 一致），而右侧自绘页韩文正常。

**根因**：树节点名由框架用**它自己的字体**渲染——框架启动只按它自己的语言加载一个字体（中文环境 = msyh 雅黑，无谚文字形；`overlay.cpp InitImGui` 候选表 msyh/msyhbd/segoeui 都不含谚文），我们注册的韩语节点名（`node_names_`，词典译文）整串缺字形 → `?`。右侧自绘页正常是因为 `ensure_page_font()` 推了「雅黑 + Malgun」合并字体——但那只对我们画的页面生效，框架画的树管不着。

**修复（客户端侧，框架无需重编译）**：`ensure_imgui()` 指纹校验通过后，往**框架默认字体**（`FontDefault`，缺省 Fonts[0]）按 `MergeMode` 合并 `C:/Windows/Fonts/malgun.ttf`（谚文来自 Malgun，汉字/假名/拉丁仍以雅黑优先，尺寸取 base->LegacySize 保持基线一致）。ImGui 1.92 动态图集按需光栅化，合并后框架自绘的树节点从下一帧起即可显示韩语；一条 Info/Warn 日志记录合并结果。

- 自绘页的专用页面字体保留不动（双保险，行为不变）。
- 核心层 2084 项全绿；DLL 增量编译通过。
- **待实机验证**：韩语下左树节点显示正常（菜单内切语言无需重启）；系统缺 malgun.ttf 时看 UE4SS.log 告警。

## v1.0.53 改了什么（2026-11-01 词典键改稳定命名键，en-US 为基准词典）

按需求把 1.0.52 的「中文原文当键」换成「稳定命名键 + 英文基准」：所有译本直接对照 en-US.json 翻译，键名不再承载任何语言文本。

- **键**：全部 `#` 前缀命名键，共 96 个——18 个原有 #UI 键（#btn_enter、#pose…）不动；6 个分组 `#group_*`；43 个设置项用 `#` + config 键名（`#pad_enabled`、`#fov_default`…）；4 个枚举选项（#freeze_mode_timescale / #freeze_mode_pause / #mouse_vert_translate / #mouse_vert_look）；25 个控制台面板键 `#console_*`。pbzfm_schema_map.cpp 的 53 处中文 label/分组/选项脚本精确替换成键名，字符串字面量零中文校验通过。
- **UE4SS 控制台诊断面板同批迁移**：ui_tab.cpp / mod_app.cpp render_tab_impl 的面板文案全部进词典（与 PBZFM 菜单同一设置的滑条直接复用既有键 #move_speed / #yaw_speed / #pad_look_speed 等；文案不同的才新增 #console_*）；滑条 fmt 里的中文单位（"%.0f 度/s"）改纯 ASCII fmt，单位并入词典标签。`pbzfm_` 改声明期构造（首帧即可用词典），render_tab_impl 每帧节流重扫；`PbzfmClient::lang()` 暴露共享实例。仅「正在等待游戏引擎初始化…」一条保留代码内兜底（初始化窗口期词典未扫描，控制台瞬显）。UE4SS.log 日志与 config.ini 生成注释仍为中文（诊断/文档，非界面文本）。
- **词典**：四份文件键集完全一致（各 96 词条 + `__display`）。en-US.json = 基准（键 → 英文原文）；zh-CN.json 的中文全部来自原先硬编码在代码里的文本（转存进词典）；ja/ko 复用旧译文重新挂键。补译 3 条 1.0.46 改名遗漏（偏航左/右转、方向键转向速度——此前 en/ja/ko 界面这几处一直回落显示中文）。
- **查译链**：当前语言 → en-US（基准兜底）→ 原样返回键名；语言列表 en-US 排第一，语言无法确定时回落 en-US。commit_settings 的日志文案改用解析后文本（不再打原始键名）。
- `menu_language` 默认仍 auto（跟随 PBZFM 菜单语言），PBZFM 也没有时才回落 en-US。
- 核心层 2084 项全绿（schema 回环测试不依赖 label 文本）；DLL 增量编译通过。
- **待实机验证**：PBZFM 菜单与 UE4SS 控制台面板默认显示英文；四语言切换正常；对照 en-US.json 改译文/新增语言文件热生效；缺 languages 目录时告警 + 键名退化。

## v1.0.52 改了什么（2026-11-01 四语言词典外部化：DLL 不再内嵌文本）

PBZFM 自绘页的 i18n 词典原先以原始字符串字面量编译进 DLL（pbzfm_client_lang.cpp 里四段 kJson*，约 320 行），改一个译文都要重编译。按需求改为全部从外部 JSON 加载，DLL 里不再有任何界面文本：

- **新仓库目录 `languages/`**：zh-CN.json / en-US.json / ja-JP.json / ko-KR.json，内容用脚本从原内嵌字面量**逐字提取**并做了一次清理（zh 18 条、en/ja/ko 各 71 条，含 `__display`）；清理项：起草 en/ja/ko 时照抄框架语言文件残留的 **24 个 PBZFM 框架演示页键**（title / page_settings / btn_test / ui_scale / quality 等，与 `_ref/PBZ-Framework-Menu/assets/languages/en-US.json` 逐键重合、译文逐字相同）——本项目代码从不查询它们（T() 只查 #命名键与 schema 中文 label），框架界面文本也由框架自己的 PBZFM\languages\*.json 加载，故整批删除，zh-CN 本就没有。并同步进 `dist/PBZPhotoModeCpp/languages/`；`tools/pack.py` 打包时自动同步，zip 里随包分发。
- **pbzfm_client_lang.cpp 重写**（549 → 约 220 行）：删除 kJson* 四段与 EnsureBuiltins；`RescanLanguages` 成为唯一词典来源（扫 `<mod 目录>\languages\*.json`，2 秒节流热重载保留）；已知四种语言展示顺序优先、其余按 id 追加；`T()` 查译链改为 当前语言 → zh-CN（按 id 查找，不再依赖 langs_[0]）→ 原样返回；`langs_` 为空（目录缺失/全坏词典）时各访问器安全退化，页面显示键名。
- **日志**：接入 PBZFM 后词典数为 0 时打 Warn（提示整个目录安装、勿只拷 DLL）；接入日志的「内置 zh/en/ja/ko 词典」措辞改为「词典从 languages/*.json 加载」。
- **文档**：dist README 新增「PBZFM 菜单语言（外部词典）」一节（改译文/加语言/升级覆盖安装说明）；版本行与测试数同步。
- 核心层测试不受影响（i18n 属 UE4SS 层）：**2084 项全绿**；DLL MSVC+Ninja 增量编译通过。
- **待实机验证**：PBZFM 菜单四语言切换正常；手改 languages/*.json 译文约 2 秒热生效；删掉 languages 目录后告警 + 键名退化；外部新语言（任意文件名）出现在下拉列表尾部。

## v1.0.51 修了什么（2026-11-01 dilation 判死 + POV 监测 idle 门控）

1.0.50 实验包（config 已改 dilation）实机判读，两条：

1. **dilation 冻结在本作判死**：每次冻结后每秒 `检测到时间缩放被游戏改写，已重新冻结`，3 秒后 `冻结压不住，已停止自动重试`——游戏有代码持续把时间缩放强制写回 1.0（绕过 Min/Max 夹紧，疑似每帧直写 WorldSettings.TimeDilation），模组每秒抢一次抢不过。**表现为用户看到的“冻结后人物继续动”。pause 模式不受影响（此前会话几分钟稳定），仍是唯一可用冻结。** 配置已回退 pause。
2. **POV 微动首份数据被相机输入污染**（8.9~16.9 位移 = 用户在转相机），无法下结论。

**1.0.51 修复**：`pov_watch_tick(bool camera_idle)`——PhotoMode 传入本帧是否动过相机（pose_dirty_），非 idle 帧不累计并清空上一帧样本；监测值从此只来自“没人动相机”的连续帧。实机判读标准：**冻结后不碰任何输入，若 `视角微动:位移` 仍非零 = 自定义相机管理器在暂停期推视角实锤**；恒 0.000 且布料仍闪 = 残余源只在材质侧（Time 节点 WPO，反射层无解，只能从材质参数入手——下一步预案：枚举角色布料 MID 的标量参数找风/摆动幅度项）。

- 核心层 2084 项全绿（POV 监测 idle 断言）。

## v1.0.50 修了什么（2026-11-01 POV 微动监测 + 排除 MPC）

1.0.49 实机判读：**MPC 腿转正并排除**——快照 34 集/347 参数（ReturnValue 修复生效），冻结全程 `变化:无`，风源全程 0.00。但用户反馈**布料大部分区域仍闪烁** → 嫌疑收窄到两个：

1. **材质 Time 节点 WPO**（暂停原理上关不掉——材质时钟不随世界暂停；只有 dilation 能冻住它）；
2. **自定义相机管理器残余微动**（PBZPlayerCameraManager 不挂引擎 modifier 数组，但自身代码可能仍在暂停期推视角 → 全画面边缘 shimmer + 阴影爬动）。

**1.0.50 新增 POV 微动监测**（`pov_watch_tick`，受 freeze_diag 门控）：冻结期每帧读相机管理器算出的最终 POV（`ViewTarget.POV`——含自定义管理器加的一切），与摄影相机 Actor 的静态位姿对照；`freeze_diag_sample` 尾部新增 `视角微动:位移x 旋转y°`（上一诊断周期的最大逐帧偏差），诊断行尾带 `[暂停]/[缩放]` 标签。非零 = 实锤有代码在暂停期推视角。

- **判决实验（无需改码）**：config `freeze_mode=dilation` 冻一次——布料安静 = Time 节点 WPO 实锤（Time 随 dilation 走），拍照用 dilation 冻结即可治；照闪 = 看 1.0.50 的 `视角微动` 数值。
- 核心层 2080 → 2083 项全绿（POV 监测的冻结期/解冻/关闭三态断言）。

## v1.0.49 修了什么（2026-11-01 MPC 取值修复：Get 的出参实为返回值）

1.0.48 实机诊断一锤定音：`UltraDynamicWeather_Parameters 数组头[Data=… Num=59 Max=60] 提名=59`——数组遍历与参数名提取**全通**（UDW 天气集合 59 个参数，正是布料/植被摆动的主力嫌疑）；断点在取值环节：`GetScalarParameterValue 签名=object:WorldContextObject,object:Collection,other:ParameterName,float:ReturnValue(ret)`——

**根因**：本作的 `GetScalarParameterValue/GetVectorParameterValue` 把唯一的出参编译成了**返回值**（属性名 `ReturnValue`），没有 `OutValue` 出参。我们按 `OutValue` 读 → 永远失败 → 每个参数被静默 continue → 快照恒 0。

**修复**：
- `mpc_get_scalar`：`OutValue` 读不到回落 `ReturnValue`（旧签名 bool+OutValue 不受影响——OutValue 在前且命中即返回）；
- `mpc_get_vector`：同款回落（读 FLinearColor 的 R/G/B/A）；
- `mpc_set_scalar/set_vector`：值参数名 `Value` 对不上时按位置（第 4 个参数）兜底，类型校验仍由 write_number 把关。

- **待实机验证**：`MPC 快照：34 个集合、N 个参数` N 应 >0（仅 UDW 就有 59 标量 + 向量）；冻结诊断的 `MPC 变化` 开始真实工作——UDW 参数在冻结期若仍在变，就是天气驱动布料的实证，`freeze_mpc` 每帧写回会压住它；核心层 2080 项全绿。

## v1.0.48 修了什么（2026-11-01 MPC 快照 0 参数：逐环节诊断）

1.0.47 实机（UE4SS.log）判读：① `IsA` 修复生效——MPC 首个集合 UltraDynamicWeather_Parameters 的 ScalarParameters 字段类=ArrayProperty，判定链已通；② 但快照仍 0 参数 → 断点在更深的环节；③ **相机管理器定论**：`属性未找到，管理器=PBZPlayerCameraManager`——游戏用自定义相机管理器，属性链里真没有 CameraModifiers（不是 IsA 问题），修饰器腿在 PBZ 关闭，StopAllCameraShakes 照常执行。

MPC 0 参数剩三个候选断点：参数名提取失败（数组遍历）／Kismet 取值失败（函数缺失或签名不符）／集合真空参数（cooked 资产数据不在实例上）。**1.0.48 在快照 0 参数时逐环节打证据**：

- `MPC 诊断：GetScalarParameterValue 签名=…` / `GetVectorParameterValue 签名=…`（缺失则 Warn）——签名不符是「每个参数读值都被静默 continue」的头号嫌疑；
- `MPC 诊断：<集合名> 数组头[Data=%llx Num=%d Max=%d] 提名=N`（前三个集合）——原始内存与 helper 双读：原始 Num>0 而提名 0 → 数组遍历断；提名>0 而快照 0 → 取值断；都 0 → 集合真空参数（此时 MPC 腿需换数据源）。

- **待实机验证**：跑一次冻结，把 `MPC 诊断` 那几行发回来即可定位；核心层 2080 项全绿（只动引擎层诊断）。

## v1.0.47 修了什么（2026-11-01 冻结治理实机排障：IsA 数组判定失灵）

1.0.46 实机（UE4SS.log，v1.0.45-cpp 运行）反馈两条异常：① `相机管理器无 CameraModifiers 数组`（但同管理器 ViewTarget 可读、StopAllCameraShakes 签名校验通过——管理器确是 PlayerCameraManager 系，属性不该缺）；② `MPC 快照：34 个集合、0 个参数`（34 个集合全部 0 参数不可能）。

**根因（一个，解释两条）**：UE4SS 的 `FField::IsA<T>()` = `GetClass().IsChildOf(T::StaticClass())`，其中 `T::StaticClass()` 依赖 UE4SS 的 `FFieldClassRegistrar` 注册表（`IMPLEMENT_FIELD_CLASS` 的 StaticClassStorage）。注册未命中时 StaticClass 返回空 variant，`IsChildOf` 对空恒 false——**IsA 对该类型永远静默 false，无任何报错**。代码库里 float/bool/struct 的 IsA 有老路径喂过（早已验证可用），`IsA<FArrayProperty>` 是 1.0.41 首次使用、首次实机即踩中。

**修复**：新增 `as_array_property(FProperty*)`——先走 IsA，再用字段类名（`prop->GetClass().GetFName()`，纯引擎数据）兜底判定 "ArrayProperty"，命中才 static_cast（类型正确，无 UB）。替换 4 处判定点：`mpc_read_param_names`、`freeze_camera_shakes`、诊断里的 CameraModifiers 与 CameraShakes。

**配套诊断**（防再瞎）：① MPC 快照 0 参数时打首个集合名 + ScalarParameters 字段类名；② 相机管理器缺 CameraModifiers 时打管理器类名 + 该属性字段类名（字段类=ArrayProperty = 判定问题；未找到 = 游戏自定义管理器真没有）。

- **待实机验证**：MPC 快照参数数应 >0（34 个集合的真实参数量）、冻结诊断的 MPC 变化检测开始工作；若相机管理器仍无 CameraModifiers，新日志会给管理器类名。
- 核心层测试 2080 项全绿（本版只动引擎层，测试数不变）。

## v1.0.46 修了什么（2026-11-01 方向键/十字键左右：平移改转向）

需求：摄影模式里方向键左/右（键盘）与十字键左/右（手柄）原来横向平移相机，改为**左右转视角（偏航）**——与上/下俯仰组成完整的“方向键转视角”簇，横移交给 WASD / 左摇杆。

- **实现**：`Intent` 新增 `yaw_axis`（+1 右转 / -1 左转，电平语义与 `pitch_axis` 相同：方向键或十字键按住持续转，无按下沿）；`InputLogic::build` 键盘块 `out.yaw_axis = yaw_right - yaw_left`，手柄块 `+= pad_yaw_right - pad_yaw_left`；`PhotoMode::tick` 在俯仰块后按 **`yaw_speed` × mult × dt** 积分（`wrap_yaw` 回绕，Shift 加速 / Ctrl 减速与俯仰同规则）。
- **配置**：新 `[control] yaw_speed`（默认 **90 度/秒**，非正回退 90；PBZFM 菜单「相机与移动」页滑条 5..270）；`arrow_pan_mult` 废弃（写了记 warning 忽略）。键名 `key_pan_left/right` → `key_yaw_left/right`、`pad_pan_left/right` → `pad_yaw_left/right`，**旧键名在 parse_config 里映射到新字段继续生效**（老 config.ini 自定义键位不丢）；`config_ini_text`/`default_config_text`/`config.ini.example` 同步。
- **UI**：UE4SS 诊断面板「方向键平移倍率」滑条改「方向键转向速度」（度/秒）；键位速查「方向键 左右转视角/上下俯仰」；PBZFM 菜单键位页标签「偏航：左转/右转（按住）」；手柄速查「%s/%s 左右转（按住）」。
- **测试**：核心层 2069 → **2080 项全绿**（新增：旧键名兼容映射、方向键左右偏航 intent 与同按抵消/不再并入 move_right、十字键左右偏航、tick 偏航积分与 Ctrl 减速用例）。
- **待实机验证**：方向键左右与十字键左右转向方向（右 = 右转）与手感（嫌慢/快改 `yaw_speed`）；Shift/Ctrl 变速；旧 config.ini 兼容（改过 `key_pan_*` 的玩家键位应原样迁移）。

## v1.0.45 修了什么（2026-11-01 进入 FOV 跟随当前镜头 + 进出扫描提速）

实机反馈：进入摄影模式**先卡一下**，然后**FOV 变大**。UE4SS.log 实测：首次进入 17:32:02.358 → 03.485「时间已冻结」≈ 1.13s；第二次进入 16.450 → 16.878 ≈ 0.43s（一次性诊断/音频普查不再重跑）。两处根因都定位并修复：

**根因 ① FOV 恒落默认 90（自 C++ v1.0.11 初始提交就有）**：`read_pov` 从相机管理器读到位置+朝向后 `return any;` 提前返回（注释写「FOV 还是走下面的分支统一处理」但代码没落下去），861-874 行的 FOV 分支被跳过，`pov.fov` 恒为 `CameraPose` 默认 90（engine_port.h）。且兜底用的 `DefaultFOV` 本身就是 UE 默认 90，不反映游戏当前镜头。修法：同处调 `GetFOVAngle`（返回当前 CameraCache 的真实 FOV，游戏相机蓝图/瞄准改过的值也在里面）→ 兜底 `CameraCache.FOV`（≤5.4）/ `CameraCache.Lens.FieldOfView`（5.5+）→ 再兜底摄影相机旧值/`DefaultFOV`；`fov_default` 只在全部读不到时生效。新增 `read_return_number` / `read_camera_cache_fov` 两个静态助手。

**根因 ② 进入路径上的全对象扫描**（自动暂停之前同步跑，卡顿即此）：
- 日志空档定位：敌人血条(16.454)→字幕设置(16.739) 的 **285ms = lockon 扫描**（`lockon_absent_` 在 leave 时重置、每次进入必重扫，本场景零命中仍全扫）；字幕开关(16.739)→字幕控件(16.875) 的 **136ms = 字幕全扫**（逐对象 narrow+lower）。
- 修法：判定只依赖类 → **按 UClass 缓存**（`LockonClassVerdict` / `SubtitleClassVerdict`，`unordered_map`；对象几十万、类 ~1 万）。逐对象只剩有效性检查+哈希查找；CDO 跳过改查**对象自身名前缀**（`Default__`），不再逐个 `GetFullName` 外链展开；类名未命中的可视组件才做资产名检查。还原路径 `collect_lockon_superset`（退出时）同步接入缓存。
- 预期：进入/退出的这两段扫描从 ~285ms+~136ms 降到几十毫秒内（待实机验证）。

构建：MSVC 19.44 + Ninja 增量编译通过；核心层测试 **2069 项全绿**。

## v1.0.44 修了什么（2026-11-01 隐藏锁定标记）

实机反馈：锁定敌人后进摄影模式，敌人身上的**锁定白点**仍在——`SetHideUIForPV` 的订阅者、HUD 布局、血条设置（`MonsterHealthBarMode` 只管血条）都不覆盖它。

新配置项 `hide_lockon_marker`（默认开，PBZFM 菜单「隐藏」页同名开关）：进入摄影模式时全量扫描对象数组，按**类名/资产名关键词**（lockon / lock / targetmark / reticle / crosshair / mark / target / aim，`_`/`-` 归一，`block` 排除）收网：

- **UMG 控件**（UserWidget 派生）：类名命中 → Collapsed（原 ESlateVisibility 记账）；
- **可视组件**（Widget/Niagara/Particle/Decal/Sprite/Mesh 派生）：类名或引用资产名（`WidgetClass`/`NiagaraSystem`/`Template`/`StaticMesh`/`SkeletalMesh`/`DecalMaterial`，按类缓存属性）命中 → 组件 bVisible=false 记账；
- 退出摄影模式全部按原值还原（重新枚举做存活校验）；隐藏清单逐条记日志，误收可从日志看见；
- **白点的真实类名离线证不了**：全量扫描零命中时确认完成（进模式后输入被屏蔽、无法再锁定，重扫无益）并打一次性诊断——①可见 UserWidget 清单 ②可视组件类清单 ③玩家 pawn 名下锁定/目标语义组件清单——把日志发回来即可收敛关键词；
- 隐藏失败（有目标但藏不住）才按秒重试（上限 10 次，同 HUD/提示/血条）。

实现注记：`EnginePort::set_lockon_marker_visible`（engine_port.h 注释写明设计依据）+ `Ue4ssPort` 实现（放在字幕节后，复用 `class_chain_contains`/`set_widget_visibility`/`set_component_visibility`/`lower_ascii`）；core 侧 `lockon_hidden_` 记账与 enter/leave 幂等条件、每秒重试块与血条同构。核心层测试新增 `test_photo_mode_lockon_marker`（3 组场景）。

## v1.0.41 修了什么（2026-11-01 冻结期残余微动治理）

实机反馈：冻结画面后布料边缘持续闪烁（运动中冻结几乎必现）、人物脸部阴影处爬动。
诊断结论：冻结=停世界，但渲染与相机管理层每帧照跑——每帧变化源被 TSR/TAA 放大成边缘闪烁。三个变化源三条对策（全部 config 可关，默认开）：

| 变化源 | 机制 | 对策（新配置项） |
| --- | --- | --- |
| 相机修饰器/shake | 真实时间驱动，暂停照常推视角 → 阴影贴图每帧重新对齐 → 脸部阴影爬动 | `stop_camera_shakes_on_freeze`：StopAllCameraShakes(立即) + 遍历 CameraModifiers 逐个 DisableModifier，按冻结前 bDisabled 还原 |
| 风源推材质 WPO | 布料/植被摆动幅度源走材质时间，暂停拦不住 | `freeze_wind`：WindDirectionalSource 的 Strength/Speed/MinGustAmt/MaxGustAmt 存原值归零（钟照走、幅度归零=位移恒零），解冻还原 |
| 实时驱动改写 MPC | 游戏代码每帧改写材质参数集合推 WPO | `freeze_mpc`：冻结抓全量快照（KismetMaterialLibrary 走 CollectionInstance），冻结期每帧写回赢回写入权，解冻释放 |

配套 `freeze_diag`（默认开）：冻结期每秒一条「冻结诊断」日志 = 风源强度/速度 + MPC 参数与上一拍的变化（freeze_mpc 生效时应为「无」，仍在变 = 有驱动源在抢写入权）+ 相机修饰器/shake 状态。用于定位还剩哪个变化源没压住。

实现注记：
- 引擎层新 API：`freeze_camera_shakes / freeze_wind / mpc_take_snapshot / mpc_hold_snapshot / mpc_release_snapshot / freeze_diag_sample`（engine_port.h + ue4ss_port.cpp）；数组遍历用 UE4SS `FScriptArrayHelper_InContainer`，MPC 读写走 `KismetMaterialLibrary.Get/SetScalarParameterValue`（运行时正路，改资产默认值不影响实例）。
- `invoke_set_bool`：单 bool 参数调用带签名校验——DisableModifier 参数写不进去就等于发 bDisable=0（启用），方向相反，宁可不调。
- 硬编码 Time 节点振幅、不引用风/MPC 的材质救不了（反射层关不掉材质时钟）：这类布料用 `freeze_mode=dilation` 兜底（Time 随 dilation 走）。
- 冻结/解冻/退出严格按「相机→风→MPC」冻结、「MPC→风→相机」还原；治理失败不记账、解冻不做对应还原（与 pause 失败回退同语义）。
- 核心层测试新增 `test_photo_mode_freeze_treatment`（6 组场景）。
- 2026-11-01 并入：**滚转归零快捷键**（键盘 `Z` / 手柄 `LB+RB` 同按，用户确认），详见文末「1.0.41 并入」节。

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

## 1.6.0-cpp（2026-10-31：进入摄影模式隐藏交互提示——场景互物体旁的“E”键图标）

**触发**：用户进入摄影模式后截图（2026-10-31 12:04）发现，对准场景互动物（铃铛）时屏幕上的“E”交互提示不会消失。

### 离线情报（PBZ.exe 反射字符串扫描，511MB Shipping 单体包）

- 交互提示本体：`PBZInteractComponent`（源码路径字符串 `System/Character/Components/PBZInteractComponent.cpp`）挂的**世界空间 UMG**——组件属性 `InteractWidgetComponent`（UWidgetComponent），配套 `SetInteractWidgetClass(NewWidgetClass)`、`InteractWidgetData`、`OnInteractWidgetUpdatePosition`。
- 游戏自带设置开关：`bSceneInteractPromptVisible` + `SetSceneInteractPromptVisible` / `GetSceneInteractPromptVisible`（属性群与 bSubtitleBackgroundEnabled 等用户设置为邻）。
- 结论：`SetHideUIForPV` 与 `W_FrontEndHUDLayout_C` 都不覆盖它；且 `freeze_mode=pause` 下游戏 UI 不再刷新，**已经画出来的提示必须把控件组件本身藏掉才会消失**。

### 实现

- **`engine/ue4ss_port.cpp` 新增 `set_interact_prompts_visible(visible)`**，双路径：
  - 路线①（游戏设置开关，管住之后新出现的提示）：运行时按候选类探测第一个带 `bSceneInteractPromptVisible` 属性的实例（跳过 CDO），优先调 `SetSceneInteractPromptVisible`（setter 参数名按候选名尝试，全不中直写属性）；进模式关、退出按进入时读到的原值还原。
  - 路线②（控件组件，管住此刻已画出的）：`FindAllOf(PBZInteractComponent)` → 读 `InteractWidgetComponent`（候选属性名兜底）→ `UWidgetComponent` 调 `SetVisibility(false)`（UFunction 缺失直写 `bVisible`）；进入时保存原始可见性，退出时重新枚举做存活校验后按原值恢复。
  - 两路任一成功即算隐藏成功；全失败打一次性 Warn（真机排障发日志）。
- **`core/photo_mode.*`**：与 `hide_game_hud` 同款——enter 尝试 + 每秒重试（`prompt_retry_accum_`）+ leave 按 `prompt_hidden_` 记账恢复 + 幂等退出条件加 `!prompt_hidden_`。
- **`core/engine_port.h`**：端口新增 `set_interact_prompts_visible`。
- **配置**：`[effects] hide_interact_prompt = true`（config.h/.cpp/default_config_text/config.ini.example/dist config 六处同步）；未加图形菜单实时开关（菜单里暂不放第二个 HUD 类开关）。
- **测试**：FakeEngine 新增 `set_interact_prompts_visible`（prompt_off/prompt_on）与 `fail_prompts`；happy_path 进退时序加入 prompt_off/prompt_on；新增 `test_photo_mode_interact_prompt`（默认进退/配置关闭/失败重试三组）。
- 版本 `1.6.0-cpp`；核心层 527 → **544 项全绿**；DLL 重链接通过；包 `dist/PBZPhotoModeCpp_1.6.0-cpp.zip`（UI 包同号重打）。

### 1.6.0 实机验证要点

1. 站在铃铛旁等“E”出现，按 F2 进摄影模式：提示应立即消失；日志应有 `场景交互提示开关已关闭（owner=...）` 与/或 `已隐藏交互提示控件：数量=...`。
2. `auto_pause_time = false` 时把镜头转走再转回来：不应再弹新提示（设置开关管住了）。
3. F2 退出：提示恢复；日志有 `场景交互提示开关已还原` / `已恢复交互提示控件：数量=...`。
4. 若出现 `候选对象里找不到场景交互提示开关` 或 `场景里没有 PBZInteractComponent` 的 Warn：把日志发回来，候选类名/属性名按实机改死。

## 1.6.0 补记（2026-10-31 13:1x：UI pak 实机启动崩溃，R1 坐实——PBZPhotoModeUI 撤下）

- 用户装上 PBZPhotoModeUI_1.6.0 包（~mods 写入时间 13:05:24）后游戏启动即崩（13:06:19）：
  `Fatal error: ShaderCompiler.cpp(4823) Missing global shader FCullHLODTrianglesVHM_CS's permutation 0, Please make sure cooking was successful.`
- 崩溃发生在 UE4SS 对象构造阶段之前——本模组 DLL 尚未执行任何代码，**与 1.6.0 的 DLL（含交互提示隐藏）无关**；
  上一局（12:38，无 UI pak）同一 DLL 全程正常。
- 机理：UAT 产出的 IoStore 容器**这次成功挂载**了（比 1.3.0 经典单 pak「不被挂载」更进一步），但原版 UE 5.7
  编辑器 cook 的 PackageStore/着色器元数据与本作定制引擎的着色器管线不匹配（FCullHLODTrianglesVHM_CS 是
  VirtualHeightfieldMesh 相关全局着色器，定制引擎没有编译该 permutation）→ 初始化即 fatal。R1 风险坐实，
  且不是「cook 时对齐几个插件开关」能解决的量级：定制引擎着色器差异未知，逐项对齐不可控。
- 处置：
  1. ~mods 里的 `PBZPhotoMode_UI_P.*` 三件套移至 `PBZPhotoModeCpp/_quarantine_UI_pak/`（游戏恢复正常）；
  2. 撤下 `dist/PBZPhotoModeUI_1.6.0-cpp.zip`（绝不能分发给玩家）；主包重打，README 安装说明改为警告；
  3. **WBP 资产菜单路线判定：本作不可用**（除非拿到游戏定制引擎）。四级菜单优先级自动落回
     UMG2 文字面板（1.2.x，实机验证过），摄影功能不受影响；
  4. `dist/PBZPhotoModeUI_1.5.0-cpp.zip` 内容同样会崩（同一资产），若已上 Releases 需下架（待查）。
- 1.6.0 主线不受影响：`hide_interact_prompt` 交互提示隐藏就绪，实机验证按 1.6.0 清单在无 pak 环境进行。

## 1.6.0 补记 2（2026-10-31 下午：R3 纯净容器——剥离着色器库后 WBP 路线重开，待实机）

**触发**：用户以 R2 崩溃截图追问「WBP pak 资产菜单不可用吗」。重新勘验后判定：R2 判死的是
「官方 UAT 容器整包」，**WBP 资产本身从未被检验过**（崩在初始化着色器阶段，早于任何资产加载）。

### R2 崩溃成因坐实（比补记 1 更进一步）

- 容器成分盘点（utoc 目录索引）：3 个资产包 + **2 个 SM5 着色器库**（ShaderArchive-Global/ProjectPBZ-PCD3D_SM5）
  + 433 个引擎内容包（EngineMaterials/Sounds/Sky/…）。
- cook 侧 471 个包的来源：`GameDefaultMap=/Engine/Maps/Templates/Template_Default` + 引擎启动/配置征用
  （cook 日志：Config Accesses 引用 285 个包；`IsCookAll=false`）。换 `GameDefaultMap=/Engine/Maps/Entry`
  只减 27 个包（444）——**闭包大头由配置征用决定，cook 侧减不下来**。
- 致命点：本作是 SM6 定制引擎，**没有 SM5 全局着色器库**；挂载进来的 SM5 库在初始化校验
  （`FCullHLODTrianglesVHM_CS` permutation 0 缺失）即 fatal——与资产、DLL 均无关。

### R3 路线：retoc 从裸资产直构容器（零着色器、零引擎包）

- 工具：**retoc**（trumank/retoc）。**0.1.5 发布版的 to-zen 解析不了 UE 5.7 新增的 2 个包摘要字段**
  （`failed to fill whole buffer`；issue #61、#82——#82 正是另一位《影之刃零》modder 两天前开的），
  修复在 master（d7b6350，未发版）→ `D:\PBZ_UI\retoc_build` 克隆 master（885a8da）本地 `cargo build --release`。
- 产出管线（全程 D:\PBZ_UI）：
  1. `zen_in\ProjectPBZ\Content\PBZPhotoMode\`＝7 个裸 cook 文件（3 包 uasset+uexp + .ufont）；
  2. `retoc.exe to-zen --version UE5_7 zen_in out3\PBZPhotoMode_UI_P.utoc` → **utoc 612B + ucas 17KB**，
     恰好 3 个 ExportBundleData 包 + ContainerHeader，`retoc verify` 通过，目录索引仅 /ProjectPBZ/Content/PBZPhotoMode；
  3. `.ufont`（8.3MB 字体数据）与 AssetRegistry.bin 不属于 zen 包（retoc 静默丢弃），**走经典 .pak 侧**
     （UnrealPak -create paklist_side.txt，8.5MB）——与原版 5.7 UAT「sidecar 不进 zen 容器」行为一致。
- 交付纪律（只出包不部署）：`dist/PBZPhotoModeUI/PBZPhotoModeUI_1.6.0-cpp-r3.zip`（R2 三件套与 1.5.0 zip
  移入 `_quarantine_UI_pak/`，其中 R2 容器另存 `R2_uat_trio/` 留档）；dist/PBZPhotoModeUI/README.txt 改为 R3 说明。
- 后备未启用：R3a「UAT 重打删着色器产物容器」成功产出但含 433 个原生引擎包（115MB，有 /Engine 包 ID
  覆盖游戏自带内容的花屏风险），留在 `ProjectPBZ/Saved/StagedBuilds/`，实机仅作对照。
- 其它：`ProjectPBZ/Config/DefaultEngine.ini` GameDefaultMap 已改 `/Engine/Maps/Entry`（对瘦身无效但无害，保留）；
  本轮临时脚本 `D:\PBZ_UI\r3_cook.bat` / `r3_pak.bat` / `paklist_side.txt`，retoc 构建目录 `D:\PBZ_UI\retoc_build`。

### R3 实机验证要点（装 r3 包后启动游戏即开始）

1. **游戏能正常启动** ＝ 着色器库假说成立（R2 在这一步就崩）。若仍崩：卸下三件套恢复，崩溃截图发回。
2. **UE4SS.log 出现「WBP 菜单已创建…加载通道 …」+ 左上角 "PAK OK"** ＝ 定制引擎能加载 5.7 cook 资产（R1 终审）。
   加载失败会自动落回 UMG2 文字面板，摄影功能不受影响。
3. **右上角中文图形面板出得来、滑条/按钮/两页签可用** ＝ WBP 菜单路线复活；中文显示为方框 ＝ 字体 .ufont
   未从经典 pak 侧读到（记日志，下一轮把字体换装载路径）。

### R3 实机（14:15/14:19/14:31 三次）+ R4 修正轮

- **验证点 1 通过（假说坐实）**：R3 三件套装上后游戏**不再启动崩溃**——R2 死因=SM5 着色器库，定案。
- **新症状：卡死在启动 logo 画面**（挂载后干等，非崩溃）；UE4SS 日志全程正常，DLL 未执行任何相关代码。
- 二分：只删 .pak 留 utoc/ucas **仍卡** → .pak 无罪（AssetRegistry/.ufont 排除），卡点在**容器挂载/store 合并**。
- 取证：容器格式与社区 mod（retoc 同管线、347B 空壳 pak）逐项一致（ReplaceIoChunkHashWithIoHash +
  SoftPackageReferencesOffset + mount ../../../）→ 格式无罪；唯一差异=我们容器引用了 1 个不在容器内的
  /Engine 包（`/Engine/EngineResources/WhiteSquareTexture`，WBP_PBZPhotoMenu 默认笔刷贴图；WBP_Dummy/Fonts
  零外部引用），社区 mod 只引用游戏资产。R2 反证：它的引擎包全自带，mount 走完了才崩在着色器。
  游戏本体日志亦加密（%LOCALAPPDATA%\ProjectPBZ\Saved\Logs 不可读），社区游戏实锤级黑盒。
- **R4**：容器加打 WhiteSquareTexture（1010+898B，无二级引用）→ 4 包 utoc 791B / ucas 18.6KB，verify 过；
  .pak=retoc 自产 347B 空壳（与社区同款）。交付 `dist/PBZPhotoModeUI/PBZPhotoModeUI_1.6.0-cpp-r4.zip`。
  待实机：能进主菜单=R4 假说成立 → 继续 F2 验证 PAK OK（字体本版不参与，方框预期）；仍卡 → 下一轮
  对照 R3a（115MB 自给自足版，在 ProjectPBZ/Saved/StagedBuilds）定位 store 合并的确切条件。

### R4 实机（14:44，决定性证据）+ R5 纯 UMG 探针轮

- **R4 结果：不再卡死**——游戏推进到「加载我们容器里的包」并给出决定性错误：
  `AsyncLoading2.cpp(8884) ObjectSerializationError: /Engine/EngineResources/WhiteSquareTexture -
  Texture2D: Serial size mismatch: Expected read size 894, Actual read size 31`。
- 判读两条：1) **R3 卡死之谜解**——游戏挂载期会**预载 mod 容器内的包**（R3 的 Fonts 包等不到
  .ufont 侧车数据→字体加载无限等待＝卡死；R4 贴图包加载快、先崩）→「容器里只能放定制引擎
  能干净加载的包」成为铁律；2) **原版 cook 的 Texture2D 被定制引擎序列化器拒收**（干净地读了
  31 字节＝它自己的格式，与导出表 894 不符；retoc 数据无损——尺寸来自我们 uasset 头）。
- **R5（纯 UMG 探针）**：MakeMenuCommandlet 三改——MakeText 不动 Font（保留 UMG 默认）、三处
  `SetBrushFromTexture(White)` → 无资源 Box 笔刷+着色（新增 MakeSolid）、Main 跳过字体导入与
  Fonts 落盘；重编 editor → 重跑 commandlet → cook。
- **跨会话第 5 坑（本实录最重要的踩坑）**：重跑 commandlet 前必须**清空
  `ProjectPBZ/Content/PBZPhotoMode/` 旧源资产**——启动期资产注册表异步加载旧包（引用旧 Fonts），
  被死锁规避刷新成「部分加载」态；CreatePackage 拿到同一脏包→新资产构建进去→保存被
  「只加载了一部分」拒收（appError，折腾三轮才钉死）。
- 导入面验证（铁证）：两个 cooked WBP 只引 `/Script/CoreUObject` + `/Script/UMG`（零 /Engine、
  零 /Game/Fonts，连默认字体 Roboto 都没进序列化）——容器 100% 自包含。R5 容器：2 包
  utoc 525B / ucas 15.7KB + 347B 空壳 pak（社区同款），verify 过。交付
  `dist/PBZPhotoModeUI/PBZPhotoModeUI_1.6.0-cpp-r5.zip`（r3/r4 zip 已从 dist 清理）。
- **R5 判据（WBP 路线生死）**：启动不崩 + F2 后 UE4SS.log「WBP 菜单已创建」/左上角 PAK OK
  → WBP 序列化兼容，路线复活（中文方框=预期，字体另案：引用游戏自带中文字体或换字体分发路线）；
  启动即 ObjectSerializationError 指向 UMG 类 → 定制引擎改了 UMG 序列化，WBP 路线判死。

### R5 实机（15:08）+ v1.6.1 DLL 探针

- **R5 启动干净（不崩不卡）**——容器挂载 + 预载全过，纯 UMG 的 WBP 包对定制引擎无害；
  但 F2 时 `TryLoad`/`LoadAsset_Blocking` 双通道都拿不到资产 → 按设计落回 UMG2 文字面板。
- 排除项：帧生成守卫**无辜**（mod_app.cpp 的守卫只拦 DX12 ImGui 浮窗；WBP 是引擎原生绘制，
  `update_overlay` 优先级 2 不受守卫影响——用户环境常开 FSR3 帧生成，该警告每局都有，别被它带偏）。
- **新主假说（统一 R2-R5 全部事实）**：定制引擎的包加载按「包 ID」走——社区 mod 三个全是
  **同名遮蔽游戏既有包**（patch 模式，DodgeWindow 的包就是 /PBZDeflectSystem/... 游戏资产路径），
  所以都能用；我们引入的是**新包名**，进不了引擎的「名字→包 ID」解析 → TryLoad 必然失败。
  R3 卡死/R4 崩溃则是我们 store 条目里的**导入**（按 ID 直达，不经名字解析）分别指向
  等不到侧车的 Fonts（无限等待）与被原版字节遮蔽的纹理（序列化器拒收）。
- **v1.6.1-cpp（DLL 探针轮）**：`wbp_load_class()` 新增通道 0 `StaticFindObject`（若挂载预载已把
  包载进内存，按名直取对象，绕开名字→ID 解析）+ 全通道一行诊断
  （`探针：FindObject(资产)=有/无 TryLoad=… LoadAsset_Blocking=…` 随失败日志一次性输出；
  FindObject 命中时打「包已在内存（挂载预载成立），已绕开名字解析」）。版本 1.6.1-cpp；
  核心层 544 项全绿不变；`dist/PBZPhotoModeCpp_1.6.1-cpp.zip` + `PBZPhotoModeUI_1.6.1-cpp.zip`（UI 三件套=R5 不变）。
- **v1.6.1 判据**：日志出「FindObject 命中」且屏幕出图形菜单 = 绕开解析直接复活；
  探针全无 = 预载不成立，下一轮在 DLL 里做「包 ID 直查/重建名字映射」或评估同名遮蔽路线（需找到
  游戏可安全让位的既有 UI 包路径，红线是绝不能破坏游戏自身 UI）。

### v1.6.1 探针实机（15:26，裁决）+ 新路线发现（puerts）

- **探针裁决：`FindObject(资产)=无 TryLoad=无 LoadAsset_Blocking=无`**——挂载预载不成立（R5 启动干净
  的真相=根本没人加载我们的包），新包名确认进不了引擎的名字→包 ID 解析。**IoStore「新增资产」路线判死**；
  IoStore 在本作只能做「同名遮蔽既有包」（社区三 mod 全是此模式，如 DodgeWindow 遮蔽
  /PBZDeflectSystem/...）。
- **破墙发现**：社区 zFreeMapTeleport 的 582KB .pak 里是一份**明文 JS**
  （`ProjectPBZ/Content/JavaScript/UI/Widget/Map.js`）——本作 UI 层跑在 **puerts** 上
  （`require("puerts"/"ue"/"cpp")`、`UE.` 反射 770 处、`$ref`、CreateWidget/SetVisibility 全套），
  定制引擎带 JS UI 框架且**按文件路径从经典 .pak 读 JS**（社区已验证的交付通道，绕开 IoStore！）。
- exe 佐证：`/Script/JsEnv` 模块、V8 后端、源码路径 `C:\PBZ\ProjectPBZ\Plugins\Puerts`、
  `PuertsServer*`（调试服务器设施）、`NumberOfJsEnv`。DLL 若能拿到游戏运行的 JsEnv 实例并调
  Eval（puerts UJsEnv 的 UFUNCTION），即可在游戏自己的 JS 运行时里 require 我们经经典 pak
  分发的模块、用游戏原生 UI 框架+中文字体搭真滑条面板——**交付与中文两大死结同时解开**。
- v1.6.1-cpp 已出（`dist/PBZPhotoModeCpp_1.6.1-cpp.zip`，544 测试全绿；wbp_load_class 加通道 0
  FindObject + 全通道探针日志——本轮完成历史使命，结论如上）。

### v1.6.2-cpp（puerts 探针轮，待实机）

- 依据用户抉择走 puerts 路线。`Ue4ssPort::puerts_probe()`（进摄影模式时调一次，有结论即静默）：
  1. `FindAllOf("JsEnv")` 数实例 + `/Script/JsEnv.Default__JsEnv` CDO 探测；
  2. 关键 UFunction 盘点（Eval/Start/Stop/Require）；
  3. **Eval 实测**：`globalThis.__pbzm=(globalThis.__pbzm||0)+1` 写入 → 第二次 `Eval("String(globalThis.__pbzm)")`
     回读——FString 参数/返回值按 0x10 布局校验后 memcpy（候选参数名 Code/JsCode/Script/code）；
     回读成功 = JS 运行时完全可用，输出「执行成功 回读=N」。
  4. 实例为 0 时不置 done（JS 环境可能较晚初始化），下次进摄影模式重试。
- 版本 1.6.2-cpp；544 项测试全绿；`dist/PBZPhotoModeCpp_1.6.2-cpp.zip` + `PBZPhotoModeUI_1.6.2-cpp.zip`（UI 三件套=R5 不变）。
- **判据**：日志「puerts 探针：JsEnv 实例=N … Eval=有 … 执行成功 回读=N」→ 下一轮经典 pak 分发
  `Content/JavaScript/UI/Widget/PBZPhotoMenu.js` + DLL/Eval require 注入，用游戏原生框架搭真滑条中文菜单；
  「无 Eval」→ Shipping 剥离执行入口，puerts 路线受阻（届时评估 PuertsServer 调试设施或收工）。

### v1.6.2 探针实机（16:01）+ v1.6.3 修正轮

- v1.6.2 探针读数：`JsEnv 实例=0（CDO=无）`——**连类都没加载进反射**。两个可能：类名不对 / JS 环境
  未创建（用户没开过地图等 JS 界面）。
- exe 反射区取证：**真类名是 `JsEnvImpl`**（puerts 实现类，反射数据与 PuertsMod/NumberOfJsEnv 相邻；
  `Default__JsEnv`/`UJsEnv` 在 exe 中不存在）。
- **v1.6.3**：① 类名候选改 `JsEnvImpl`→`JsEnv`；② 探测改**后台周期制**——mod 每 15 秒一次、不限摄影
  模式（JsEnvImpl 可能在前端/地图 JS 首次使用时才创建），第一次出现即打全量日志 + Eval 实测；
  Eval 跑通（哪怕读不到返回值）即置 conclusive 停止。版本 1.6.3-cpp，544 测试全绿，
  `dist/PBZPhotoModeCpp_1.6.3-cpp.zip`。
- **判据**：日志「puerts 探针：JsEnvImpl 实例=N … Eval=有 … 执行成功」→ JS 运行时可用，下一轮经典
  pak 分发 PBZPhotoMenu.js + Eval 注入搭原生中文菜单；15 秒级持续探测始终 实例=0 → 本局 JS 根本没跑
  （届时请开一次地图 M 键再观察，或评估 JsEnvImpl 在 Shipping 被裁剪）。

### v1.6.3 探针实机（16:20-16:35）+ v1.6.4 自建运行时轮

- v1.6.3 读数：持续探测在 6.5 分钟全程（约 26 周期，含地图内活动）**始终 实例=0**——游戏侧从未创建
  puerts 运行时（用户是否开过地图不可证，但即便开了地图也没出现）。
- 判据链推进：**决定性的下一步 = CDO 在不在**。类在（模块注册、运行时未启动）→ 可**自建**：
  puerts `JsEnvImpl` 自带 Start/Eval，DLL 自己 NewObject + Start + Eval，完全不依赖游戏去启动它；
  两类都不在 → Shipping 未注册 puerts 模块 → JS 路线判死。
- **v1.6.4**：空实例分支加 CDO 诊断（JsEnvImpl/JsEnv 两个 CDO 分列）+ 类在时枚举关键 UFunction
  （Start/Eval/Stop/Require/Reload/NotifyGameThreadReady）+ **自建尝试**（NewObject → Start[参数名
  候选 DebugPort/InDebugPort/Port 置 0] → Eval 实测回读）；自建结果无论成败都置 conclusive（一次尝试，
  不循环）。版本 1.6.4-cpp，544 测试全绿，`dist/PBZPhotoModeCpp_1.6.4-cpp.zip`。
- **判据**：「JsEnvImpl CDO=有 … Eval 执行成功 回读=N —— 自建 JS 运行时可用！」→ 下一轮经典 pak
  分发 PBZPhotoMenu.js + Eval 注入搭原生中文菜单；「两个类都不在」→ Shipping 未注册 puerts，JS 路线
  判死（文字面板定格）；「自建失败/Start 后 Eval 未跑通」→ puerts 在此游戏不可用，同样收束。

### v1.6.4 探针终审（16:45）——puerts 路线判死，引擎内路线全部穷尽

- **终审读数：`实例=0（JsEnvImpl CDO=无，JsEnv CDO=无）→ Shipping 未注册 puerts 模块`**。
  exe 里链了 JsEnv 模块（源码路径/字符串俱在）但其 UClass 注册从未运行——运行时不存在，
  自建无从建起。JS 路线正式判死。
- 至此引擎内路线六连闭环（全部实证）：①经典 pak 不挂载 → ②UAT 整包 SM5 着色器库崩 →
  ③纯净容器卡死（字体侧车等待）→ ④定制序列化器拒收原版纹理 → ⑤纯 UMG 干净但新包名
  进不了名字解析（IoStore 只能同名遮蔽）→ ⑥puerts 交付通道可用但运行时未注册。
  **《影之刃零》Shipping 的定制引擎对本模组而言是彻底黑盒：引擎内无路。**
- 备份路线盘点（PROGRESS 旧记录）：F=ReShade addon（与帧生成冲突风险 + 需装 ReShade）、
  D2=DirectComposition 子窗口覆盖（OS 合成器层、帧生成安全，但全新工程：窗口管理/自绘/输入转发）、
  B+=复用游戏原生控件资产（被⑤的序列化器拒收堵死）。主路线维持 **UMG2 文字面板**（1.2.x 实机验证）。
- 待用户抉择：收工定格 / 文字面板加中文（运行时找游戏 CN 字体 SetFont，一轮小改）/ D2 覆盖窗新工程。

### v1.7.0-cpp（用户抉择：文字面板加中文）

- **UMG2 文字面板中文化**：面板字体从游戏自带 UI 字体整块拷贝——供体优先游戏 HUD
  （`W_FrontEndHUDLayout_C` 下的 TextBlock，游戏 UI 全中文，其 FSlateFontInfo 必然覆盖中文字形），
  `FindAllOf("TextBlock")` 按全名过滤取供体 → 两块 `Font` 属性尺寸校验后 memcpy（不做手工偏移），
  在 AddToViewport 之前完成（Slate 尚未缓存字形）。供体缺失时保持默认字体（英文），不致命。
- **menu_asset 默认改 false**（config.h/config.cpp/config.ini.example/dist config 四处同步）：
  WBP 路线已判死（v1.6.4 终审），默认关闭省去每次进摄影模式的一行失败日志；config 键保留（想试仍可开）。
- 版本 1.7.0-cpp；544 项测试全绿；`dist/PBZPhotoModeCpp_1.7.0-cpp.zip` + `PBZPhotoModeUI_1.7.0-cpp.zip`（UI 三件套=R5 不变，已无用可卸）。
- **实机判据**：进摄影模式后右上角文字面板出现中文（「PBZ 摄影模式/状态/视野/速度/灵敏度/冻结/重置/退出」），
  日志有「字体已换游戏 UI 字体（供体 …）」；若「未找到可拷贝的游戏字体供体」→ 把日志发回（换供体策略）。

### v1.7.1 修正（字体补丁挪到真正的装配路径）

- v1.7.0 实机无效果且两条字体日志都没出现——**字体补丁加错了函数**：加进了 `ensure_umg_menu`
  （1.2.0 的简单装配路径，日志前缀「UMG 菜单：」，本作不可用未走），而真正在用的是 **umg2 装配路径**
  （动态行循环，日志前缀「交互菜单已创建」）。
- v1.7.1：撤掉错位的补丁，改为在 umg2 装配的 TextBlock 循环里逐个拷贝——供体
  `W_FrontEndHUDLayout_C` 下的 TextBlock（`FindAllOf("TextBlock")` 按全名过滤），
  `Font` 属性尺寸校验后整块 memcpy，循环结束打一行结果（成功=中文可用 / 失败=保持英文）。
- 版本 1.7.1-cpp，544 测试全绿，`dist/PBZPhotoModeCpp_1.7.1-cpp.zip`。
- **判据**：F2 后右上角面板出中文（「PBZ 摄影模式/状态…」）+ 日志「字体已换游戏 UI 字体」；
  若仍英文且日志「未找到游戏字体供体」→ 供体过滤策略再议。

### v1.7.2（字符串中文化——v1.7.1 只换了字体没换文字）

- v1.7.1 实机：日志「字体已换游戏 UI 字体」成功执行，但面板仍英文——**根因是文字内容本身就是英文**
  （umg2layout::kTexts 全英文 + 状态行 "STATE : " 拼接），字体换了文字没换。
- v1.7.2 三改：① `umg2_layout.h` kTexts 全部中文化（PBZ 摄影模式/视野/速度/灵敏度/冻结/重置/退出/
  点击 [-][+] 调整；命中区 kHits 按坐标对齐，不受文字宽度影响）；② 状态行显示层翻译
  （core 的 OFF/FROZEN/LIVE 保持英文机器值不动——测试锁定；ue4ss_port 显示层映射
  已冻结/运行中/已退出）；③ **字体清单诊断**（FindAllOf("Font") 名单一次日志）——若 HUD 供体
  字体不含中文出方块，可据此换字体。
- 版本 1.7.2-cpp；544 测试全绿；`dist/PBZPhotoModeCpp_1.7.2-cpp.zip`。
- **判据**：面板出中文（含「状态：已冻结/运行中」）→ 完成；**中文变方块 □□□** → 把「字体清单」
  日志行发回，按名单换含 CJK 的字体对象（一轮收尾）。

### v1.8.0-cpp（用户抉择：移除界面，收工）

- **界面移除**：`[ui] menu = false` 总开关（config.h/config.cpp/config.ini.example/dist config 四处）——
  update_overlay 的 want_menu 加上 `cfg_.menu` 门，任何菜单不再创建/显示；true 可恢复旧文字面板（诊断用）。
  快捷键操作不受影响：F2 进入/退出、F 冻结、R 重置、滚轮 FOV、WASD/QE/Shift/Ctrl 移动（键位均可配）。
- **死路线残留清理**：puerts 探针周期块与成员移除（终审已判死）；WBP 路径 menu_asset 默认 false 保持；
  pack.py 停打 UI 包（UI 三件套已无用，~mods 里可卸）。
- README 警告行改为「图形菜单已移除，纯快捷键操作」。
- 版本 1.8.0-cpp；544 项测试全绿；产物 `dist/PBZPhotoModeCpp_1.8.0-cpp.zip`（仅主模组）。
- **状态定格**：摄影功能全量可用（冻结/隐藏 HUD/交互提示隐藏/超宽屏/手柄/时间控制），无菜单界面。
  真图形菜单若日后重启，唯一剩余路径 = D2 DirectComposition 覆盖窗（OS 合成层，帧生成安全）——
  大工程，从 PROGRESS/记忆可随时接续。

### v1.8.1-cpp（图形菜单代码彻底移除 + 死代码/临时文件清理）

- **彻底移除（v1.8.0 只做了默认关闭，代码还在；本版把代码删干净）**：
  - 删除文件：`engine/imgui_overlay.{cpp,h}`（DX12 ImGui 图形菜单）、`engine/umg2_layout.h`、
    `engine/wbp_layout.h`（命中区表）、`core/menu_bridge.{cpp,h}`（WBP 变量桥）、`core/menu_view.h`、
    `core/menu_model.{cpp,h}`（菜单内容模型）、`game_menu.{cpp,h}`（ImGui 菜单绘制）。
  - `ue4ss_port`：删除 UMG 文字菜单 / UMG2 交互菜单 / WBP 资产菜单 / puerts 探针全部实现
    （v1.8.0 漏删的 1250 行，含 make_ftext_into/write_anchor/set_class/kismet_system_library 等
    菜单专用反射助手与全部菜单状态成员）。
  - `mod_app`：删除 update_overlay 三级菜单优先级机、DX12 覆盖层延迟初始化（帧生成插件检测）、
    umg2/WBP 鼠标命中与缩放机器（umg2_scale.txt）、pak 开发者转拷通道（ensure_pak_installed）、
    图形菜单快照/动作桥；mod_app.cpp 979 → 436 行。
  - **v1.8.0 的门没关严**：DX12 ImGui 菜单（优先级 1）不受 `[ui] menu` 管，进摄影模式仍会自动打开——
    本次随代码一并消失。
- **配置键清理**：`key_menu` / `pad_menu` / `hud_fallback` / `menu` / `menu_asset` / `menu_asset_path` /
  `menu_pak_autocopy` 全部移除（旧 config.ini 里写了这些键只会得到「未知配置项已忽略」告警，无害）；
  `Intent.toggle_menu`、`PhotoMode::menu_visible` 等菜单输入通路一并删除。`[ui]` 只剩 `imgui_tab`（控制台诊断面板）。
- **临时文件清理**：仓库根 `%SystemDrive%/`（脚本路径错误拷出的 Windows 缓存库）、`pakstore.json`
  （游戏 oplog 泄漏）、`PBZPhotoModeCpp/_quarantine_UI_pak/`（隔离的 UI pak 三件套，~290MB）、
  24 个 build/pack 日志。
- 版本 1.8.1-cpp；核心层测试 **470 项全绿**（删除 menu_model/WbpBridge 用例后剩 470；NONE 解绑、
  parse_pad_combo 等用例改挂 pad_freeze）；DLL 全量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.8.1-cpp.zip`。
- **状态**：纯快捷键定格，与 v1.8.0 收工决定一致；图形菜单若日后重启，唯一路径仍是 D2 DirectComposition
  覆盖窗（本次删除不影响那条路线——它本来就是从零新工程）。

### 版本号回退 1.0.20 + dist 冗余包清理（用户指示，2026-10-31）

- **版本号回退**：对外版本号从 1.1.0–1.8.1 回退到 **1.0.20-cpp**——1.1.0–1.8.1 全部是未发布的图形菜单
  迭代（ImGui 直绘 / UMG 交互 / WBP 资产 / puerts 探针），随路线判死整体作废，不对外发布；版本号
  对外回到 1.0.x 线。旧 1.0.20（UMG 文字菜单版）的号被当前构建重用，以本版为准。
- **dist 冗余包清理**：删除 39 个主模组历史 zip（v1.0.0–1.8.1）、6 个已判死 UI 模组 zip 空壳、
  4 个旧版 staging 目录（v1.0.3–v1.0.6）与 PBZPhotoModeUI staging；dist 只留 `PBZPhotoModeCpp/`
  staging 与新包 `PBZPhotoModeCpp_1.0.20-cpp.zip`。
- 上一条 v1.8.1 记录里的产物 zip（1.8.1）随之删除，内容重打包为 1.0.20。
- 版本 1.0.20-cpp；470 项测试全绿；产物 `dist/PBZPhotoModeCpp_1.0.20-cpp.zip`（仅主模组）。

### 1.0.21-cpp：相机滚转（Q/E 改旋转，鼠标左右键升降不变）

- **需求**：新增相机旋转功能——键盘 `Q` / `E` 从升降改为**滚转**（roll，镜头绕视线轴旋转；E=+1 顺时针 / Q=-1 逆时针）；鼠标左键上移 / 右键下移保持不变（translate 模式照旧），手柄 RT/LT 升降照旧。
- **实现**：`Intent` 新增 `roll`（input_logic.h/.cpp：Q/E 不再进 `move_up`）；`PhotoMode::tick` 按 `roll_speed` 积分 `pose.rot.roll`，`wrap_angle_180`（math3.h）把滚转归一化到 [-180,180)；引擎层 `apply_pose` 本来就写 Roll（FRotator 三元组），零改动。R 重置照旧回到进入时的滚转角。
- **配置**：`roll_speed`（`[control]`，默认 60 度/秒，非法值回退 60 并告警；Shift/Ctrl 变速同样生效）；`default_config_text` / `config.ini.example` 同步。
- 版本 1.0.21-cpp；核心层测试 **501 项全绿**（470 + 滚转相关 31 项断言：Q/E intent、与鼠标左右键同帧叠加互不干扰、tick 积分/变速/dt 封顶/±180 环绕、重置含滚转、配置解析与默认文本）；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.21-cpp.zip`。

### 1.0.22-cpp：手柄滚转适配（RB/LB 倾斜，变速移到 L3/R3）

- **需求**：手柄同步适配倾斜/滚转——`RB` / `LB` 滚转相机（RB=+1 顺时针 / LB=-1 逆时针，与键盘 Q/E 同向）。
- **变速让位**：原 LB=加速 / RB=减速移到**摇杆按下**——LS(L3) 加速 / RS(R3) 减速；两杆同时按住是进出模式组合键（LS+RS），此时不触发变速（避免进/出模式的按压窗口里误触变速）。
- **实现**：input_logic.cpp 手柄块 `out.roll += RB - LB`；boost/slow 改为单杆按住且另一杆未按。与键盘同帧叠加时 roll 累加（与 move_forward 同惯例）。RT/LT 升降、十字键 FOV、X/Y/组合键全部不变；固定键位不占 `[gamepad]` 配置（与原 LB/RB 变速同风格）。
- 版本 1.0.22-cpp；核心层测试 **509 项全绿**；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.22-cpp.zip`。

### 1.0.23-cpp：FOV 步进按住连发（十字键/键盘 FOV 键）

- **需求**：十字键上下调 FOV 原先一个按下沿走一格，持续变焦要连点；改为**按住即持续放大/缩小**。
- **实现**：`InputLogic` 每个方向一条按住连发通道（键盘 FOV 键与手柄十字键合成一个方向通道，谁先按下都算同一次按下沿）——按下沿立即 1 格；持续按住 0.35 秒后按 0.08 秒间隔连发（约 12.5 格/秒，配合 `fov_step=2` 即 25 度/秒）；大掉帧时一帧补多格，节奏不变；松开立即停。`build` 增加 `dt` 参数（默认 0.0 兼容旧调用/单测），状态机在 `fov_axis_nudge` 里。`reset()` 清计时但保留 `was_down`，按住十字键进出模式不会误发新按下沿（与 `pad_prev_` 不清同理）。
- **顺手修正**：手柄帮助文案「LB/RB 变速」是 1.0.22 之前的残留，改为 L3/R3 变速；键盘 FOV 键与十字键同享按住连发。
- **测试**：新增 12 项断言——按下沿、延迟窗口内不连发、过延迟补格、一帧补多格、松开即停、键盘通道、按住时 `reset` 不误发。核心层 **521 项全绿**；版本 1.0.23-cpp；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.23-cpp.zip`。

### 1.0.24-cpp：剧情冻结修复（Sequencer 播放器联动 + 冻结期自检）

- **问题**（用户报告）：游戏处于剧情时开启摄影模式，自动冻结时间不生效——画面像冻住了，但角色动作和声音还在继续。1.0.11 的游戏暂停只在普通游玩下真机验证过，剧情没验证过。
- **根因分析**：剧情（过场）系统不走普通玩法 tick——世界暂停/全局时间缩放拦不住 Sequencer 播放器的自主更新与剧情音频；另外部分流程会拒绝 `SetPause`（GameMode/`bPauseable` 之外的拒绝路径），旧版只告警不回退，冻结直接落空。
- **修复（三层）**：
  1. **剧情播放器联动**：冻结时 `FindAllOf(MovieSceneSequencePlayer)`（沿继承链匹配，LevelSequencePlayer 及游戏派生类都能扫到）+ `FindAllOf(LevelSequenceActor)` 读 `SequencePlayer` 属性兜底，对「正在播放」的播放器调 `Pause()`；解冻/退出只恢复「处于暂停态」的（`IsPaused` → `Play()`），不碰游戏自己停掉的。新配置 `freeze_sequences`（`[effects]`，默认 `true`）。
  2. **暂停失败回退**：`toggle_freeze` 里 `set_paused` 失败时自动回退时间缩放（`freeze_dilation`），解冻按实际使用的模式还原；两者都失败才报「冻结时间失败」。
  3. **冻结期自检**：冻结中每秒回读（pause 模式查 `IsGamePaused`，dilation 模式查生效倍率），发现被游戏改写就重新冻结；连续 3 次压不住则停止对抗并告警（避免和剧情系统每秒打摆），状态恢复正常后自动重新武装。手动再冻结会清零计数。
- **诊断**：引擎层每次冻结/解冻打一行「剧情播放器[暂停/恢复]：扫描 N（直扫 X + Actor 兜底 Y），命中 M，执行 K，Wwise=有/无」；扫描到 0 个时提示剧情可能不走 LevelSequence，请带日志反馈。Wwise 探测（`/Script/AkAudio|Wwise.Default__AkGameplayStatics`）为后续处理剧情语音留证据——**已知边界：剧情语音走 Wwise/AudioComponent 时，播放器暂停拦不住它，若实机确认再单独处理**。
- **测试**：核心层 521 → **565 项全绿**——新增剧情联动（冻结暂停/解冻恢复/冻结退出恢复/开关关闭不动）、暂停失败回退、双失败告警、暂停被解除的重新暂停、连续 3 次放弃与重新武装、dilation 被改写重压；进入/退出严格序列断言同步更新。
- 版本 1.0.24-cpp；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.24-cpp.zip`。**待实机验证**。

### 1.0.25-cpp：真机日志修复（无参函数参数块 + Wwise 音频）

- **1.0.24 实机日志分析**（UE4SS.log 2026-10-31 20:22~20:47）：
  - 每次冻结「游戏暂停成功 IsGamePaused=true」，**动作一直能冻结**（用户确认）；
  - 「剧情播放器」多数扫描 0，20:47 起场景里有 1~2 个实例，多次出现 **「命中 1，执行 0」**——找到了正在播放的 SequencePlayer，`IsPlaying` 读得通（有返回值，参数块 size>0），但 `Pause` 调不出去；
  - `Wwise=有`：游戏用 Wwise，UE 混音/SoundClass 管不到它。
- **根因 1（「命中不执行」）**：`Pause()`/`Play()` 是无参 UFUNCTION，`UFunction::GetStructureSize()` 为 0，ParamBlock 缓冲区为空 → `valid()` 恒 false → `invoke` 在 ProcessEvent 之前就返回 false。潜伏 bug：之前调用过的函数全带参数，从未暴露。
  - **修复**：参数块缓冲区至少留 1 字节（`buffer_.assign(size > 0 ? size : 1, 0)`），无参函数的 ProcessEvent 照常执行。
- **根因 2（对话语音时有时无）**：对话语音一部分由场景序列（SequencePlayer 音轨）驱动——被上面的 bug 挡住，修好即随世界一起停；另一部分是 Wwise 直接 post（与组件/序列无关），世界暂停拦不住。**“有时能拦住”= 序列音轨那部分，“拦不住”= Wwise post 那部分。**
- **实现**：
  1. ParamBlock 无参函数修复（见上）；
  2. `pause_sequences` 跳过 Default__ 类默认对象；日志加「类=」（首个命中播放器的类名）与「Ak组件=」计数；命中大于执行时告警一行；
  3. 新增 Wwise 组件诊断（每会话只探测一次）：AkComponent 数量 + 类上存在的控制函数（候选 Stop/AkStopAll/AkStop/StopAll/Pause/AkPause）；
  4. 新配置 `stop_wwise_on_freeze`（默认 **false**）：冻结时对每个 AkComponent 调用第一个存在的停止函数（Stop/AkStopAll/AkStop/StopAll）——**停止不可恢复**，解冻后由游戏自行重播；语音实在压不住时再开。
- **测试**：核心层 565 → **580 项全绿**（新增 stop_wwise_on_freeze 开/关、解冻不恢复、配置解析与默认文本）。
- 版本 1.0.25-cpp；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.25-cpp.zip`。**待实机验证**：
  1. 剧情/对话中冻结，声音是否跟着停（序列音轨部分应已修好）；
  2. 「Wwise 组件诊断」一行探测出什么可用函数——若存在 Pause/AkPause 之类，下一版可做可恢复的音频暂停；
  3. 若语音仍压不住且组件函数只有 Stop 系，可试开 `stop_wwise_on_freeze = true`。

### 1.0.25 并入：进入摄影模式隐藏敌人（怪物）血条

**触发**：用户需求「进入相机模式时隐藏敌人血条 HUD」。1.0.12 的整体 HUD 隐藏（`SetHideUIForPV` + `W_FrontEndHUDLayout_C` Collapsed）覆盖不到它们——头顶血条不在 HUD 布局子树下，也不订阅 PV 事件。

### 离线情报（PBZ.exe 反射字符串扫描，1.6.0 交互提示同款取证）

- 游戏侧唯一的怪物血条总开关：用户设置 **`MonsterHealthBarMode`**（`EPBZMonsterHealthBarMode`：`HideFullHealth` / `AlwaysShow` / `OnlyLockedTarget`），带 `Set/GetMonsterHealthBarMode` + `Set/GetPendingMonsterHealthBarMode` + `OnMonsterHealthBarModeChanged` 一族；名字表邻居是亮度/监视器/动态模糊等设置函数 → 挂在游戏设置类上。
- **没有「总是隐藏」档**：三档里 `OnlyLockedTarget` 压制最强——摄影模式下没有锁定目标 → 血条全部隐藏。
- Boss 血条是另一套（`RegisterBossHPBar` 等挂在 HUD Actor），已被 1.0.12 的 HUD 布局隐藏覆盖，不重复处理。
- `SetHideHud` 属于 Sequencer 播放设置（剧情路径），与血条无关，排除。

### 实现

- **`engine/ue4ss_port.*` 新增 `set_enemy_health_bars_visible(visible)`**（镜像 1.6.0 设置开关路线）：
  - 运行时按候选类探测第一个带 `MonsterHealthBarMode` 属性的实例（跳过 CDO，与 `bSceneInteractPromptVisible` 同款候选名单）；
  - **枚举序数离线定不了**（exe 字符串只有名字没有值）：运行时按名找 `EPBZMonsterHealthBarMode` 的 UEnum 对象（先猜 `/Script/ProjectPBZ.*` 路径，再 `FindAllOf(UEnum)` 兜底），读名字表（UEPseudo `GetEnumNamesAsVector`，UE5.7 走 FNameData 并行数组）按条目名解析 `OnlyLockedTarget` 的数值——不硬编码序数；
  - 隐藏：进模式时 `read_number` 存原模式值 → `SetMonsterHealthBarMode`（参数名按候选试，全不中按第 0 参数兜底）→ 失败直写属性；日志带原模式名（诊断用，按名字表反查）；
  - 还原：退出时按存下的原值走同一对通道写回。
- **`core/photo_mode.*`**：与 HUD/交互提示同款——`hide_enemy_health_bars` 配置（默认 `true`）、enter 尝试 + 每秒重试（`bars_retry_accum_`）、leave 按 `bars_hidden_` 记账还原、幂等退出条件加 `!bars_hidden_`。
- **配置**：`[effects] hide_enemy_health_bars = true`（config.h/.cpp/default_config_text/config.ini.example/dist config 同步）。
- **测试**：FakeEngine 新增 `set_enemy_health_bars_visible`（bars_off/bars_on + `fail_bars`）；happy path 进退序列加 `bars_off`/`bars_on`；新增 `test_photo_mode_enemy_health_bars`（默认进退/配置关闭/失败重试三组）。
- 版本号不变（并入 1.0.25-cpp 同一个包）；核心层 580 → **597 项全绿**；DLL 增量重编链接通过；`dist/PBZPhotoModeCpp_1.0.25-cpp.zip` 重打（含参数块修复 + Wwise + 敌人血条三个功能）。

### 敌人血条实机验证要点

1. 有怪在场上（血条可见）时按 F2：血条应消失；日志应有 `敌人血条显示模式已切到 OnlyLockedTarget（原模式=...，owner=...）`。
2. F2 退出：血条按进入前的模式恢复；日志 `敌人血条显示模式已还原`。
3. `hide_enemy_health_bars = false` 时进出：血条全程不动。
4. 若出现「候选对象里找不到怪物血条模式设置」或「枚举表里没有 OnlyLockedTarget」的 Warn：把日志发回来，候选类名/枚举路径按实机改死。
5. 已知边界：进入瞬间若还锁着一个目标，该目标血条会保留（`OnlyLockedTarget` 语义如此）；下一轮可再加控件级隐藏。

### 1.5.0 资产 pak 第二轮（IoStore 三件套）+ 交付纪律重申

- **第 1 轮实机失败**（12:38 日志）：经典单 `.pak` 在 ~mods 里存在，但 `WBP 菜单：资产加载失败`——本作是
  IoStore 引擎，只挂 `pak+utoc+ucas` 三件套（4 个社区 mod 全是三件套且 .pak 近乎空壳；目录索引在 utoc，
  经典 pak 里的 uasset 包 IoDispatcher 不读）。经典 pak 快捷路线判死。
- **改走 UAT 正规管线**：ProjectPBZ 工程拆 Runtime/Editor 双模块 + 加 ProjectPBZGame target（UAT 强制要求
  Game target）→ `RunUAT BuildCookRun -build -cook -stage -pak -compressed` 产出三件套
  （147MB 诊断版：ucas 130MB 含引擎启动包）。utoc 内确认含 "PBZPhotoMode/WBP_PBZ" 路径串（目录索引在 utoc）。
- **交付纪律重申（用户指示）**：不直接往游戏目录部署任何东西——一律出 zip（主模组 + UI 资产模组两个独立包）
  由用户用模组管理器安装。曾短暂直接部署三件套到 ~mods，已按指示撤回。
- 当前包：`PBZPhotoModeCpp_1.5.0-cpp.zip`（470KB）+ `PBZPhotoModeUI_1.5.0-cpp.zip`（147MB 诊断版，含三件套
  + README + OFL）。**待用户第 2 轮实机验证**：确认 IoStore 格式能被挂载、WBP_Dummy 出 "PAK OK"。
- 已知欠账：147MB 太肥（含引擎启动内容，且理论上有 /Engine 包 ID 与游戏自带内容重名的风险）——格式确认后
  下一轮做瘦身容器（只含 /Game/PBZPhotoMode 三包，目标 ~8MB）。

### 1.0.26-cpp：俯仰视角调节（键盘 Z/X + 手柄十字键上/下）

- **需求**：新增仰俯视角调节——手柄用十字键的上/下键作为快捷键，键盘用 Z 和 X。
- **设计**：与 Q/E 滚转同款的**连续轴**——按住持续转、松开即停（`Intent.pitch_axis`：+1 仰视 / -1 俯视），无按下沿/连发状态机（无状态，`reset()` 无需照顾）。默认 **Z = 仰视 / X = 俯视**（与需求句序"上下 / z和x"平行对应），键位可在 config 里互换。
- **实现**：
  - `Intent.pitch_axis`（input_logic.h/.cpp）：键盘 `keys.pitch_up/pitch_down`（电平）+ 手柄 `pad_combo_down(pad_pitch_up/pad_pitch_down)`（默认 UP/DOWN）相加，与鼠标/右摇杆俯仰可同帧叠加。
  - `PhotoMode::tick`：`pitch_axis * pitch_speed * mult * dt`（`mult` = Shift 加速/Ctrl 减速，与滚转同规则），`clamp_pitch` 夹在 ±`pitch_limit`；**不做 `invert_y`**——那是鼠标/摇杆 Y 轴的方向偏好，俯仰按键方向是明确语义（测试锁定）。
  - 配置：`[keys] key_pitch_up = Z / key_pitch_down = X`；`[gamepad] pad_pitch_up = UP / pad_pitch_down = DOWN`；`[control] pitch_speed = 60`（非正回退默认并告警，与 roll_speed 同规则）；config.h/.cpp/default_config_text/config.ini.example/dist config 同步。
  - **十字键 FOV 让位**：`pad_fov_up/pad_fov_down` 默认 UP/DOWN → **LEFT/RIGHT**（FOV 按住连发机制不变，键盘小键盘 +/- 照旧）。旧 config.ini 若仍写 `UP/DOWN`，十字键上下会同时驱动俯仰与 FOV——新包 config.ini 已改好，README 有说明。
  - `ui_tab` 键位速查加 Z/X 与十字键俯仰。
- **测试**：核心层 597 → **633 项全绿**（默认键位/解析/告警回退/默认文本往返、Z/X intent 与同按抵消、十字键上下俯仰 + 左右 FOV、FOV 连发通道换到十字键左、tick 积分/变速/dt 上限/±89 夹紧/invert_y 语义）。
- 版本 1.0.26-cpp；产物 `build-dll/PBZPhotoModeCpp.dll`（670,720 B）；包 `dist/PBZPhotoModeCpp_1.0.26-cpp.zip`（362,307 B：dlls/config.ini/enabled.txt/README.md）。
- **待实机验证**：1) Z/X 按住抬头/低头方向与手感（默认 60 度/秒，嫌慢改 `pitch_speed`）；2) 十字键上/下俯仰、左/右 FOV；3) 俯仰顶到 ±89 度不应翻越；4) 顺带回归 1.0.25 的剧情冻结/Wwise/敌人血条项。

### 1.0.27-cpp：左右视角平移（方向键左/右 + 十字键左/右）+ 键盘俯仰改方向键

- **需求**：① 新增左右视角平移——手柄十字键左/右、键盘左/右箭头；② 键盘俯仰快捷键从 Z/X 改为上/下箭头。
- **设计**：平移**并入现成 `Intent.move_right` 横移轴**（沿相机右向、`move_speed` 速度、Shift/Ctrl 变速、对角归一化全部自动继承），与 A/D、左摇杆同轴可叠加；俯仰沿用 1.0.26 的 `pitch_axis` 连续轴，只换默认键。
- **键位变化**：
  - 键盘：方向键 `上`=仰视 / `下`=俯视（原 `Z`/`X` 让位闲置）；方向键 `左`/`右`=左/右平移（新增）。四键组成完整视角调节簇。
  - 手柄：十字键 `上/`下`=俯仰（不变）、`左`/`右`=左/右平移（新增）——十字键全部用于视角调节；**FOV 从十字键左/右让位到 `A`/`B`**（`pad_fov_up=A`/`pad_fov_down=B`，按住连发机制不变）。
- **实现**：
  - `KeyMap`：`pitch_up/pitch_down` 默认 VK 改 0x26/0x28，新增 `pan_left/pan_right`（0x25/0x27）；方向键名 LEFT/UP/RIGHT/DOWN 在 vk 表里本就有，`vk.cpp` 零改动。新增 `pad_pan_left/pad_pan_right`（默认 LEFT/RIGHT）。
  - `InputLogic::build`：键盘 `out.move_right += pan_right - pan_left`；手柄块 `+= pad_combo_down(pad_pan_right) - pad_combo_down(pad_pan_left)`。
  - 配置：`[keys] key_pan_left/key_pan_right`、`[gamepad] pad_pan_left/pad_pan_right`；`key_pitch_up=UP`/`key_pitch_down=DOWN`；`pad_fov_up=A`/`pad_fov_down=B`。config.h/.cpp/default_config_text/config.ini.example/dist config 同步。
  - `ui_tab` 键位速查同步。
  - **旧 config 兼容提示**：旧 config.ini 里 `pad_fov_up=LEFT/pad_fov_down=RIGHT` 仍会生效 → 十字键左右同时驱动平移与 FOV；装新包 config.ini 或手动改这两行。
- **测试**：核心层 633 → **649 项全绿**（默认键位/解析/roundtrip、方向键俯仰与同按抵消、方向键平移与 A/D 叠加、十字键左右平移、A/B FOV 与连发通道换键）。
- 版本 1.0.27-cpp；产物 `build-dll/PBZPhotoModeCpp.dll`；包 `dist/PBZPhotoModeCpp_1.0.27-cpp.zip`（363,594 B）。
- **待实机验证**：1) 方向键四簇（左右平移 + 上下俯仰）方向与手感；2) 十字键左/右平移、上/下俯仰；3) A/B 调 FOV（按住连发）；4) 顺带回归 1.0.25/1.0.26 的剧情冻结/Wwise/血条/俯仰项。

### 1.0.28-cpp：音频对象普查（语音压不住的第二轮取证）

- **1.0.27 实机日志**（22:26）：`Wwise 组件诊断：AkComponent=20，可用函数=Stop`——场景里确实有 20 个 Wwise 组件，类上只有一个可用的 `Stop`（无 Pause/AkStop 系）；游戏目录 config 里 `stop_wwise_on_freeze` 仍是默认 **false**，本轮「语音压不住」是在开关没开的情况下测的（开着也只能拦挂在组件上的那部分）。
- **判断**：`UAkComponent::Stop` 停的是组件自己注册的 Wwise 游戏对象；对话语音常由 `UAkGameplayStatics::PostEvent` 直接 post 在**角色 Actor 的游戏对象**上（无组件），组件级 Stop 够不着。UE 侧混音/SoundClass 对 Wwise 无效；全局 Suspend 不走反射。要可恢复地压住语音，得找到游戏自己的音频/对话管理器。
- **实现**：首次冻结时（跟在 Wwise 组件诊断后）做一次**全对象数组普查**（`ForEachUObject` + `IsValidObjectForFindXOf` 守护）——按类名聚合 Ak/Audio/Sound/Voice/Dialog 相关类并打「音频对象普查：<类> x<数>」；对 Manager/Subsystem/Controller/Component/System/Director 形态的类，列出可疑控制函数（stop/pause/mute/silence/volume/suspend/resume/voice/dialog/talk/audio/sound）打「音频类函数：<类> → …」。每会话仅一次。
- 顺带：`ParamBlock` 无参函数修复沿用 1.0.25；本轮无核心层改动。核心层测试 649 项全绿；DLL 增量重编链接通过。
- 版本 1.0.28-cpp；产物 `dist/PBZPhotoModeCpp_1.0.28-cpp.zip`。**待实机验证**：
  1. 把 `stop_wwise_on_freeze` 改成 `true` 冻结一次，看组件上的声音（环境/音乐/部分对话）是否静音；
  2. 把冻结后新增的「音频对象普查」全部行发回来——据此锁定游戏自己的语音/对话管理器与可用函数，下一版做可恢复的音频暂停（或针对性调用管理器停止对话）。

### 1.0.29/1.0.30-cpp：对话语音压住（普查转储 + 游戏自有停止接口）

- **1.0.28 普查回传**（22:41 前后）：对话系统形态确认——Flow 图 `DialogueGraphNew_C x397`/`DialogueNode_C x102`、`STTask_PlayDialogue_C`、BP 函数库 `PlayAudio_C`/`StopAudio_C`、`FromActor_Dialogue_C`；音频子系统 `PBZMRQWwiseAudioSubsystem`/`PBZPersistentAudioSubsystem`/`MapAudioSubsystem`/`PBZSoundFXSubsystem` 各 x1；但转储 cap=4 只轮到 4 个 Ak 组件类。
- **1.0.29 修正转储**：子系统/静态库优先并打全量函数表；新增 `AkGameplayStatics`（Wwise BP 入口）全量函数——确认有 `StopAllAmbientSounds/StartAllAmbientSounds/StopActor/StopAll/GetAkComponent/PostEvent` 等，无全局暂停。
- **1.0.30 实机日志**：`PBZSoundFXSubsystem` 全量函数露出 **`PBZStopAkComponent`/`PBZStopAllSoundsOnActor`/`PBZStopPlayingID`/`PBZPausePlayingID`/`PBZResumePlayingID`/`PBZStopAllSoundsOnActor`**——游戏自己就有按组件/按 Actor/按播放 ID 的停止与暂停恢复接口。
- **用户关键观察**：冻结时若一句话已说完，下一句不会触发；若冻结时一句话正在播，播完后也不会有下一句——**对话推进（Flow 图）已随世界冻结**，残留问题只有「当前正在播的这一句」。单向 Stop 在此场景下不丢内容（当前句停掉；解冻后 Flow 等的是句子结束回调，Wwise Stop 会触发 EndOfEvent，流程继续下一句）。
- **实现（1.0.30）**：
  1. `stop_wwise_on_freeze` 默认改为 **true**（对话框场景实测需要；不想静音可关）；
  2. 冻结静音路径升级：优先调用游戏自有 `PBZSoundFXSubsystem.PBZStopAkComponent`（新增 `invoke_game_stop` 带签名校验——所有参数必须都是对象属性，按名填 component/worldcontext，其余宁可不调），失败回落组件自带 `Stop`；日志带「游戏接口执行 N」；
  3. 普查转储：子系统/静态库类改打全量函数表（cap 120），cap 提到 12。
- **测试**：核心层 649 → **666 项全绿**（默认值翻转的断言同步 + 进入序列含 wwise_stop）。
- 版本 1.0.30-cpp；DLL 增量重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.30-cpp.zip`。**待实机验证**：冻结时对话当前句是否立即停住；解冻后对话是否正常继续下一句；环境音解冻后是否回来自行重播（不回来就把 `stop_wwise_on_freeze` 关掉并反馈）。

### 1.0.30 并入：进入摄影模式隐藏对话字幕

**触发**：用户需求「进入相机模式时隐藏对话字幕」。`SetHideUIForPV` 不管字幕。

### 离线情报（PBZ.exe 反射字符串扫描）

- 游戏字幕显示走**内置 GameSubtitles 插件**（exe 源码路径 `Plugins/GameSubtitles/.../SubtitleDisplay.cpp`，包 `/Script/GameSubtitles`，UMG 控件 `USubtitleDisplay`）；设置侧有 `bSubtitlesEnabled`（`SetSubtitlesEnabled`/`AreSubtitlesEnabled` + Pending 对，名字表邻居是 `RunHardwareBenchmark`/`MouseCaptureMode` 等 GameUserSettings 家族函数）。
- `bSubtitlesEnabled`/`bSubtitlesForcedOff` 同时是 **UEngine 的 GlobalUserParam**（名字表邻居 NearClipPlane/CustomTimeStep 等）——设置对象不在时可退到引擎实例直写。
- `SetSuppressSubtitles` 属 Media/MovieScene 字幕节（视频播放路径），与对话字幕无关，排除。

### 实现

- **`engine/ue4ss_port.*` 新增 `set_subtitles_visible(visible)`，双路径（1.6.0 同构）**：
  - 路线①（设置开关，管住之后新出的字幕）：运行时按候选探测第一个带 `SetSubtitlesEnabled` 函数或 `bSubtitlesEnabled` 属性的实例（跳过 CDO）——存原值 → 调 setter（参数名候选试，失败直写属性）→ 退出还原；设置对象全不在时退到引擎实例（`FindFirstOf(GameEngine/PBZGameEngine/Engine)`）直写 `bSubtitlesEnabled`。
  - 路线②（字幕控件，管住此刻已画出来的）：`ForEachUObject` + **类继承链匹配**（`class_chain_contains`，覆盖原生实例与 BP 子类）收集 `USubtitleDisplay` 实例，记录原 `ESlateVisibility` 后 Collapsed，退出时重新枚举做存活校验后按原值恢复——freeze_mode=pause 下控件 Tick 冻住，光改设置对已显示的字幕不会立即生效，必须藏控件本身。
  - 场上找不到字幕控件时打一次性 Warn 并**列出类名带 Subtitle 的控件类**（游戏若用自制字幕控件，按这份名单改下一版）。
- **`core/photo_mode.*`**：`hide_subtitles` 配置（默认 `true`）+ enter 尝试 + 每秒重试（`subtitles_retry_accum_`）+ leave 按 `subtitles_hidden_` 记账还原 + 幂等退出条件。
- **配置**：`[effects] hide_subtitles = true`（config.h/.cpp/default_config_text/config.ini.example/dist config 同步）。
- **测试**：FakeEngine 新增 `set_subtitles_visible`（subs_off/subs_on + `fail_subtitles`）；happy path 进退序列加 `subs_off`/`subs_on`；新增 `test_photo_mode_subtitles`（默认进退/配置关闭/失败重试三组）。
- 版本号不变（并入 1.0.30-cpp 同一个包）；核心层 **666 项全绿**（含本功能字幕用例与 wwise 默认值翻转断言）；DLL 重编链接通过；`dist/PBZPhotoModeCpp_1.0.30-cpp.zip` 重打（对话语音停止 + 字幕隐藏同一包）。

### 对话字幕实机验证要点

1. 对话进行中（字幕可见）按 F2：字幕应立即消失；日志应有 `字幕设置开关已关闭（owner=...）` 与 `已隐藏字幕显示控件：数量=...`。
2. F2 退出：字幕设置还原 + 控件按原可见性恢复；日志 `字幕设置开关已还原` / `已恢复字幕显示控件：数量=...`。
3. `hide_subtitles = false` 时进出：字幕全程不动。
4. 若出现「场上没有 USubtitleDisplay 控件」的 Warn 且后面列出了别的 Subtitle 控件类名，把日志发回来——下一版把藏控件的类名改死。
5. 已知边界：冻结中对话框流程已停（1.0.30 语音观察），字幕本就不会再更新；本功能主要管「进模式瞬间还挂着的字幕」与「不冻结（live）状态下继续播的对话」。

### 1.0.31-cpp：字幕退出后不恢复（1.0.30 实机 bug 修复）

**实机现象**（UE4SS.log 23:25-23:26，9 个进出周期）：进模式字幕消失 ✅，退出后字幕**永久消失** ❌。日志证据：9 次「字幕设置开关已关闭（owner=PBZSettingsShared）」、**0 次「字幕设置开关已还原」**、0 次「隐藏对话字幕失败」、0 次重试——enter 调用返回 true、`subtitles_hidden_` 正常，leave 走了还原分支但**静默失败**。

### 根因（两处，同一趟实机暴露）

1. **还原被 `saved_` 门误杀（主因）**：`PBZSettingsShared` 上只有 `SetSubtitlesEnabled` **函数**、没有 `bSubtitlesEnabled` **属性**（该属性在 UEngine 上是 GlobalUserParam，不在设置类属性链）。旧代码只在「属性读原值成功」时才置 `subtitle_setting_saved_=true` → 开关被翻转但**原值从未记录** → 退出时还原分支整个跳过、且不打任何日志。多轮进出还会把「当前已是关」当成原值再存一次，错误被固化。
2. **字幕控件路线打空**：场上根本没有 USubtitleDisplay 实例——游戏用的是自制字幕控件 **`GAA_Subtitles_C`**（x1，UserWidget 派生、类名含 Subtitle；诊断 Warn 正好把它列了出来）。1.0.30 的控件收集只认 GameSubtitles 插件类，全程没藏过任何控件。

### 修复

- **`engine/ue4ss_port.*`**：
  - 原值读取改为 `read_subtitles_enabled_state()`：先试属性（引擎实例路径），失败走 **`AreSubtitlesEnabled`**（exe 扫描确认与 setter 成对存在的 getter，`invoke_bool` 读 ReturnValue）；都失败按 **true 兜底**（字幕默认开的语义）。
  - 还原门从 `saved_` 改为 **`touched_`**：只要动过开关就必须还原，与「原值是否读成功」解耦；**还原失败打一次性 Warn**（不再静默——这次的排障成本就来自静默失败）。
  - 关闭日志带原值与「getter/属性都读不到」提示；`find_subtitle_setting_owner()` 的引擎兜底加 `cdo_fallback==nullptr` 门（候选命中时不得被引擎实例覆盖）。
  - 控件收集扩为：USubtitleDisplay 继承链 **或** 「UserWidget 派生 + 类名含 subtitle」（大小写不敏感）——`GAA_Subtitles_C` 从此被覆盖；字幕子系统/资产类不是 UserWidget，不会误收。隐藏日志带控件类名。
- 版本 1.0.31-cpp；核心层测试 666 项全绿（核心逻辑未变，engine 层 bug 无法离线复现）；DLL 重编链接通过；包 `dist/PBZPhotoModeCpp_1.0.31-cpp.zip`。

### 实机验证要点

1. **先恢复现场**：被 1.0.30 卡住的字幕开关需要在游戏设置里手动开回「字幕 开」一次（旧包写坏的状态修复版救不回来），然后进/出一次摄影模式确认不再翻坏。
2. 对话中按 F2：字幕立即消失，日志 `字幕设置开关已关闭（...原值=开）` + `已隐藏字幕显示控件：数量=1（GAA_Subtitles_C）`。
3. F2 退出：字幕恢复，日志 `字幕设置开关已还原` + `已恢复字幕显示控件：数量=1`。
4. 反复进出多轮（旧 bug 要多轮才固化），确认每一轮都有成对的 关闭/还原 日志。
5. 游戏设置里把字幕关掉再进/出摄影模式：退出后字幕应保持关（还原原值而不是被强开）。

### 1.0.32-cpp：对话静音改可恢复音量归零（1.0.30 实机 bug）

- **1.0.30 实机反馈**：冻结时当前句立即停住 ✅；但**退出相机模式后当前句不继续、处于静音状态**——组件 `Stop` 是单向的，句子被杀掉不会从原处续（对话流程等的是句子结束信号，Stop 之后信号语义不受控）。
- **方案（可恢复静音）**：冻结时把所有 AkComponent 的 `SetOutputBusVolume` 归零——**声音继续无声播放，Wwise 生命周期与句子结束回调完全正常**；解冻/退出恢复音量后，当前句从原处继续（短句已自然播完则流程直接续下一句）。组件类没有该函数时才回落一次性 `Stop`（1.0.30 行为）。
- **实现**：`invoke_game_stop`（1.0.30 的游戏接口签名校验，真机 `via_game=0` 从未命中）替换为 `invoke_set_number`（唯一 float/double 参数校验）+ `silence_wwise_components(bool mute)`（mute=true→音量 0，false→音量 1.0）；核心 `wwise_muted_` 记账，解冻/退出在 `pause_off` 之后恢复；`stop_wwise_on_freeze` 语义从「停止」改为「可恢复静音」，默认仍 true。
- 已知边界：音量恢复统一到 1.0（Wwise 默认；游戏若给个别组件设过自定义输出音量，退出后会被重置为 1.0，环境音层次可能略变——实机如发现再改「进入时读原值」方案）。
- **测试**：核心层 666 → **668 项全绿**（静音/恢复成对、冻结退出恢复、关闭开关不碰组件、序列断言 `wwise_mute`/`wwise_restore`）。
- 版本 1.0.32-cpp（1.0.31 为并行字幕修复包，已发布）；DLL 重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.32-cpp.zip`。**待实机验证**：冻结静音、解冻当前句从原处继续、解冻后下一句正常触发。

### 1.0.33-cpp：对话语音真暂停（PlayingID 截获 + 游戏自有暂停接口）

- **1.0.32 实机反馈**：①冻结期间声音只是**变小声**、仍在播（组件 `SetOutputBusVolume(0)` 的语义没吃透——调用全部执行成功但没到完全静音）；②退出时当前句已无声播完，只能从**下一句**出声。音量路线两头都不达标。
- **方案（真暂停）**：`PBZSoundFXSubsystem` 全量函数表里有 `PBZPausePlayingID`/`PBZResumePlayingID`/`PBZSeekOnPlayingID`/`GetAkComponentByPlayingID`——游戏自己就在用 PlayingID 管音频。缺的只是 ID 清单：**钩住 Post 系列函数截获返回值**（UE4SS `RegisterHook(UFunction*, pre, post, custom)`，与 InputAxis 钩子同一 API）。
- **实现**：
  1. `install_voice_hooks()`：钩 `PBZPostEventOnAkComponent(_SoftRef/_Animation/_Animation_SoftRef)`、`PBZPostEventOnActor(_SoftRef)`、`PBZPostEventGlobal(_SoftRef)`、`PBZPostEventAtLocation(_SoftRef)`、`PBZPostAssociatedEvent` 共 11 个函数的 **post 回调**，从 `RESULT_DECL` 读 int32 PlayingID（>0 且 <1000 万的边界校验，bool 返回值会被过滤）；mod_app 引擎 tick 每 2 秒重试直到挂上（日志「对话语音钩子已挂：N 个 Post 函数」）；截获列表去重、留最近 256 个（Wwise 会回收复用 ID）。
  2. `pause_wwise_voices(bool pause)`（替换 `silence_wwise_components`）：冻结→对每个截获 ID 调 `PBZPausePlayingID`；解冻/退出→`PBZResumePlayingID`——**Wwise 原生暂停，解冻当前句从原位置续播**。`invoke_set_number` 的数值签名校验放宽到 int（暂停接口参数是 PlayingID）。
  3. 没截获到 ID 时回落 1.0.32 的组件音量兜底。
- **测试**：核心层 668 项全绿（断言改名 `wwise_pause`/`wwise_resume`，语义不变）。
- 版本 1.0.33-cpp；DLL 重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.33-cpp.zip`。**待实机验证**：
  1. 日志出现「对话语音钩子已挂：N 个 Post 函数」；
  2. 冻结时当前句**完全静音**（不是变小声）；
  3. 退出后当前句**从停住的位置继续出声**（不是只从下一句开始）；
  4. 若仍不达标，把「对话语音暂停/恢复（PlayingID）」两行发回——看截获数与执行数。

### 1.0.33 并入：字幕隐藏改持续压制（1.0.32 实机第二轮反馈）

**实机现象**（UE4SS.log v1.0.32 会话，4 个进出周期）：进游戏字幕就没了（1.0.30 写坏的设置被游戏存盘，遗留状态）→ 进出一次摄影模式后字幕**出现**（退出还原 `SetSubtitlesEnabled(true)` 把遗留的关状态打开）→ 再进摄影模式字幕**一直不消失**。日志侧 1.0.31 的记账已完全成对（4 组关闭/还原、控件 `WBP_FlowSubTitle_C` 藏/恢复各 4 次）——机制没坏，坏的是**一次性隐藏挡不住游戏逐条刷新**。

### 判读

- 对话 UI（Flow 图的 `WBP_FlowSubTitle_C`，1.0.31 诊断发现）只在**创建时**读一次字幕设置；运行中的对话由游戏逐条把控件重新显示。我们进入时藏一次，游戏下一条字幕就把控件改回 Visible/换新实例——一次性隐藏失效。
- `PBZSettingsShared` 上既没有 `bSubtitlesEnabled` 属性也没有 `AreSubtitlesEnabled`（两者都在别处），设置开关写下去的真实语义仍未知 → 加一次性签名诊断（`SetSubtitlesEnabled` 参数名/类型 + owner 全部字幕相关成员），下一版换正确通道。

### 修复（持续压制）

- **进入后每秒重申**（`photo_mode.tick` 不再只在未成功时重试）：引擎侧幂等——
  - 设置开关只在进入首跳写一次（每秒重写同一个 setter 可能有未知副作用），重申时跳过；
  - 控件每秒重申：已管控件被游戏改回 Visible 就再压回 Collapsed（不重复记原值），新实例（对话 UI 重建）收编并记原值。
- **收集分两档**：全量 ForEachUObject 扫描只在进入/退出各做一次（顺带把发现的字幕控件类名记入缓存，如 `WBP_FlowSubTitle_C`）；每秒重申只按缓存类名 `FindAllOf`（类哈希查找，微秒级，不拖帧）。
- **一次性诊断**：`SetSubtitlesEnabled 参数签名：…` + `owner 字幕相关成员：…`（每会话首次进入时打）。
- 版本号不变（并入 1.0.33 同一个包）；核心层 668 项全绿；DLL 重编链接通过；`dist/PBZPhotoModeCpp_1.0.33-cpp.zip` 重打（语音暂停钩子 + 字幕持续压制同一包）。

### 字幕第二轮实机验证要点

1. **先在游戏设置里把字幕开回来**（1.0.30 遗留的关状态；开回来后「进游戏字幕就没了」应消失）。
2. 对话进行中按 F2：字幕消失；日志 `SetSubtitlesEnabled 参数签名：…` 与 `owner 字幕相关成员：…`（每会话一次）+ `已隐藏字幕显示控件：数量=1（WBP_FlowSubTitle_C）`。
3. 对话继续播（不退出摄影模式）：下一条字幕出现后 ≤1 秒内应被压回去（重申生效）。
4. F2 退出：字幕恢复（`字幕设置开关已还原` + `已恢复字幕显示控件：数量=1`）。
5. 若仍不消失：把 `SetSubtitlesEnabled 参数签名` 与 `owner 字幕相关成员` 两行发回来——按真实参数名/成员换下一版通道。

### 1.0.34-cpp：真暂停调用被签名校验拦下（1.0.33 实机：截获 37 执行 0）

- **1.0.33 实机日志**：钩子挂上 ✓（11 个 Post 函数）、截获 37 个 PlayingID ✓、**执行 0** ✗——`invoke_set_number` 只认「唯一 int 参数」，而 `PBZPausePlayingID`/`PBZResumePlayingID` 的实参不止一个裸 int（子系统 BP 函数几乎必带 `WorldContextObject`），37 发全部被签名校验拦下，整轮静音落空（回落也没触发：执行 0 时直接 return 了）。
- **实现**：
  1. `invoke_pause_id`（自适应填参）：数值参数填 PlayingID（只允许一个）；对象参数名字含 `worldcontext` 的填 world；其余参数（bool/string/非 worldcontext 对象）才放弃；
  2. 执行 0 时打一次 `PBZPausePlayingID 参数签名=…`（`describe_params`：类型:名 全列），然后**继续走音量兜底**——不再让静音落空；
  3. 挂钩成功时顺手把两个暂停接口的真实签名打进日志（每会话一次），下一轮不用猜。
- **测试**：核心层 668 项全绿（核心逻辑未变）。
- 版本 1.0.34-cpp；DLL 重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.34-cpp.zip`。**待实机验证**：冻结静音 + 解冻原位置续播；若「参数签名」行显示的形态出乎意料（比如带 bool/enum），照签名定制下一版填参。

### 1.0.34 并入：字幕设置开关每秒打摆（1.0.33 重申逻辑的结构 bug，实机日志钉死）

**实机现象**：字幕仍不消失。日志证据（v1.0.33 会话，单次摄影模式内）：`关闭(33.614) → 还原(34.622) → 关闭(35.628) → 还原(36.649) → 关闭(37.663)` ——**精确每秒交替**，与 tick 的 1 秒重申周期完全吻合。

### 根因

- **结构 bug（主因）**：路线①的门写成 `if (!visible && !subtitle_setting_touched_) {…} else if (subtitle_setting_touched_) {还原}`——每秒重申以 `visible=false` 进入且此时 `touched_=true`，两个条件都不满足的意图（跳过）落空，掉进 **else if 的还原分支**：重申第一次反而把开关还原，下次重申再关闭……设置开关每秒 关/开 打摆，字幕自然一直显示。还原分支必须只在 `visible==true` 时可达。
- **诊断落地**（1.0.33 加的签名诊断立功）：`SetSubtitlesEnabled 参数签名：Value(bool)`（我们的参数填充一直写对了）；**设置类上的真属性名是 `bEnableSubtitles`**（此前找的 `bSubtitlesEnabled` 是引擎 GlobalUserParam 的名字，不在设置类属性链——原值读取失败、只能按默认兜底的根因）。

### 修复

- **`engine/ue4ss_port.cpp`**：隐藏与还原按 `visible` 彻底分开（`if (!visible) { if (!touched_) {关闭} }` / `else if (visible && touched_) {还原}`）——重申落在隐藏分支内时开关保持关闭不动；新增 `find_subtitle_flag_property()`（先 `bEnableSubtitles` 后 `bSubtitlesEnabled`）；原值读取与还原都走真属性；**写后回读验证**：关闭后读 `bEnableSubtitles`，若仍是开就用属性强写一次，日志带 `回读=关/开-未生效!`。
- 版本号不变（并入 1.0.34 同一个包）；核心层 668 项全绿；DLL 重编链接通过；`dist/PBZPhotoModeCpp_1.0.34-cpp.zip` 重打（语音填参自适应 + 字幕打摆修复）。

### 字幕第三轮实机验证要点

1. 对话中按 F2：字幕消失；日志 `字幕设置开关已关闭（…原值=…，回读=关）`——**回读=关** 是本轮的判据（若出现 `回读=开-未生效!`，说明 setter 写的是 pending/别的通道，把那行发回来）。
2. 对话继续播：后续字幕 ≤1 秒内被压回（重申生效）。
3. F2 退出：字幕恢复，且**只出现一次** `字幕设置开关已还原`（不再有周期内的还原/关闭交替）。
4. 游戏设置里字幕开关本身应保持玩家进入前的状态（真属性读原值已生效）。

### 1.0.35-cpp：真暂停按真实签名填参（1.0.34 签名诊断落地）

- **1.0.34 实机日志**：`PBZPausePlayingID 参数签名=int32:PlayingID,float:FadeTime`、`PBZResumePlayingID 参数签名=int32:PlayingID,float:FadeTime`（每会话一行，1.0.34 加的签名打印立功）——两个数值参数，1.0.34 的填参只允许一个，全部被拦（截获 50 执行 0），冻结只走了音量兜底（用户实测「变小声继续播」）。
- **实现**：`invoke_pause_id` 按真实签名填参——名字含 `playingid` 填截获 ID、含 `fade` 填 FadeTime（**0=立即暂停/立即恢复**，冻结/解冻都要即时）、含 `worldcontext` 填 world；其余参数才放弃。调用处传 `fade=0.0`。执行 0 回落音量兜底的逻辑保留。
- 版本 1.0.35-cpp；核心层 668 项全绿；DLL 重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.35-cpp.zip`。**待实机验证**：冻结时当前句完全静音（真暂停）+ 退出后当前句从停住的位置原样续播。

### 1.0.35 并入：字幕压制扩到对话 UI 本体 + 可见控件清单诊断

**1.0.34 实机日志（好消息+新谜题）**：`字幕设置开关已关闭（…原值=开，回读=关）`——打摆修复生效、开关真值验证通过；`WBP_FlowSubTitle_C` 也已 Collapsed——**但字幕仍显示**。结论：用户看到的字幕文本不画在已压制的控件里。结合音频普查时的 `WBP_FlowDialog_C`（对话 Flow 图 UI 本体，x2），文本大概率画在对话框本体上。

### 修复（1.0.35 并入）

- **压制范围扩到对话 UI 家族**：控件匹配规则在「USubtitleDisplay 继承链 / UserWidget+类名含 subtitle」基础上，加 **UserWidget+类名含 `flowdialog`**（`WBP_FlowDialog_C` 即对话 UI 本体：对话框+说话人+文本都随它隐藏；摄影模式屏蔽输入，本就不需要交互）。缓存类名机制照旧（快速重申覆盖）。
- **每 10 次重申做一次全量扫描**：对话中途新出现的控件类（新对话 UI）也能被收编（此前只有进入时一次全量，中途出现的新类要等下次进出才被管）。
- **一次性「可见 UserWidget 清单」诊断**：进入后列出场上所有 Visibility≠Collapsed 的 UserWidget 派生类（每类带数量，cap 40）。若对话框压制后字幕仍显示，真身必然在这份名单里——下一轮直接指认，不再猜类名。
- 版本号不变（并入 1.0.35 同一个包）；核心层 668 项全绿；DLL 重编链接通过；`dist/PBZPhotoModeCpp_1.0.35-cpp.zip` 重打（语音签名填参 + 字幕对话 UI 压制）。

### 字幕第四轮实机验证要点

1. 对话中按 F2：**整个对话 UI（对话框+字幕）应消失**；日志 `已隐藏字幕显示控件：数量=N（…WBP_FlowDialog_C…WBP_FlowSubTitle_C…）`。
2. 对话继续播：后续字幕保持消失（重申收编）。
3. F2 退出：对话 UI 与字幕恢复原状。
4. 若仍显示：把一次性日志「可见 UserWidget 清单：…」整行发回来——从清单指认真正的文本控件类，下一版按名压制。

### ✅ 字幕隐藏实机验证通过（2026-11-01 00:59 日志，用户确认「功能正常」，v1.0.36 包）

- 判据全绿：`字幕设置开关已关闭（…原值=开，回读=关）`；`已隐藏字幕显示控件：数量=N（WBP_FlowDialog_C ×多 + WBP_FlowSubTitle_C）`——FlowDialog 判定坐实（对话 UI 本体有多达 5-6 个实例，全部收编）；退出成对还原且数量严格匹配（还原 N=隐藏 N）。
- 打摆/永久消失两代 bug 均不再复现；「可见 UserWidget 清单」诊断未触发（首战成功，留作后备）。
- 演化小结：1.0.30 设置翻转（无原值记录→永久消失）→ 1.0.31 真属性+回读门（打摆暴露）→ 1.0.33 重申机制（门写法又打摆）→ 1.0.34 visible 分离+回读验证（打摆灭，但文本在对话框本体）→ 1.0.35 扩到 FlowDialog 家族 ✅。

### 1.0.36-cpp：语音静音双保险（覆盖面封死）

- **用户疑问**：「通过 ID 来暂停声音的话，那别的角色不还是没效果吗」——钩子挂在游戏音频总入口 `PBZSoundFXSubsystem` 上，**所有**角色的声音（枪声/脚步/环境点声/各角色语音）的 ID 都会被截获、冻结时一起暂停，并非只管对话。但疑问指出了两个真实缺口：①钩子装好前（启动头 2 秒）已发出的声音（如开局音乐）不在名单；②绕过子系统直接走 Wwise 集成层的声音抓不到。
- **封堵**：
  1. **加挂集成层入口**：`UAkGameplayStatics::PostEvent/PostEventAtLocation/PostAndWaitForEndOfEvent` + `UAkComponent::PostAkEvent/PostAssociatedAkEvent`（类级钩子一次覆盖所有实例，与子系统钩子同一 RegisterHook API）；同一 ID 多入口重复截获由去重集吸收；
  2. **ID 名单上限 256→1024**（保留最近 512），开局音乐这类长命循环不脱名单；
  3. **两层同开**：`pause_wwise_voices` 改为每次冻结同时执行「ID 真暂停 + 组件音量归零」，解冻同时「ID 恢复 + 音量还原」——静音不依赖任何单一通道，没截获到的声音由音量层压住。
- 版本 1.0.36-cpp；核心层 668 项全绿；DLL 重编链接通过；产物 `dist/PBZPhotoModeCpp_1.0.36-cpp.zip`。
- **✅ 实机验证通过（2026-11-01 00:59 日志，用户确认「功能正常」）**：钩子 16 个全挂（子系统 11 + 集成层 5）；冻结「对话语音暂停（PlayingID）：截获 38，执行 38」+「组件静音（音量兜底）：38/38」；解冻成对恢复（38/38 + 38/38）。全场景静音与原位置续播均达标。遗留边界（未反馈异常，保持观察）：音量兜底恢复统一 1.0、PlayingID 复用错停的理论风险（未观察到）。

### 1.0.37-cpp：方向键/十字键与视角减速（2026-11-01 用户需求）

需求原文：降低键盘按住上下左右箭头的移动速度与手柄按住十字键上下左右的移动速度，大幅降低键盘按住 Ctrl 的视角移动速度与手柄按住右摇杆的视角移动速度。

改动（全部在核心层，引擎层未动）：
- **方向键/十字键平移减速**：此前方向键左右与十字键左右把 ±1.0 并入 `move_right`，与 WASD/左摇杆同用 `move_speed`。新增 `arrow_pan_mult`（默认 **0.5**），在 `InputLogic::build` 里只对方向键/十字键的平移分量折算——WASD/左摇杆速度不变。范围保护 [0, 3]，越界回退 0.5。
- **方向键/十字键俯仰减速**：`pitch_speed` 默认 60 → **30 度/秒**（该速度现在只有方向键上/下与十字键上/下在用，Z/X 已在 1.0.27 移除）。
- **Ctrl/RS 视角精调**：此前 `slow_mult` 只作用于移动/垂直/俯仰/滚转，**鼠标视角与右摇杆视角完全不受 Ctrl 影响**。新增 `slow_look_mult`（默认 **0.1**），`PhotoMode::tick` 里 `intent.slow` 为真时鼠标 look 与手柄 `pad_look_*` 一并乘该倍率——键盘按住 Ctrl、手柄按下右摇杆 RS（slow 语义，LS+RS 组合键除外）都生效。范围保护 (0, 1]，越界回退 0.1。
- **右摇杆基础转速**：`pad_look_speed` 默认 180 → **60 度/秒**（全行程；范围保护 [10, 720] 不变，回退文案同步改）。
- **UI**：UE4SS 诊断面板（`ui_tab.cpp`）新增「Ctrl视角倍率」「方向键平移倍率」滑条；手柄视角速度滑条 [30,540] 保持兼容。
- **配置文本**：`config.h` 默认值、`config.cpp` 解析/范围保护/`default_config_text`、`config.ini.example`、`dist/PBZPhotoModeCpp/config.ini` 全部同步；README（根 + dist）键位表与说明更新。
- **测试**：核心层 668 → **686 项全绿**。新增/修改：默认值断言（30/0.1/0.5/60）、解析与越界回退、默认文本往返、方向键平移折算（±arrow_pan_mult）、十字键平移折算、新增 `test_photo_mode_slow_look`（Ctrl 下鼠标视角 1.0→0.1 度、RS 按下下右摇杆 25→2.5 度）。注意 `pad_look_speed` 默认值断言在两处（parse_combo 的往返段里也有一份）。
- 版本 1.0.37-cpp；DLL 重编链接通过（735,744 B）；产物 `dist/PBZPhotoModeCpp_1.0.37-cpp.zip`（382,140 B，只出包未部署）。

### 1.0.37 实机验证要点

1. 方向键左右平移应明显慢于 WASD（半速）；十字键左右同理（左摇杆不变）。
2. 方向键上/下、十字键上/下俯仰速度约为原来一半。
3. 按住 Ctrl 移动鼠标（Alt+鼠标）：视角应只有约 1/10 转速，松开恢复；手柄按下右摇杆 RS 不松、再拨右摇杆同理。
4. 右摇杆正常视角转速约为原来的 1/3（180→60 度/秒）。
5. **若用户现有 config.ini 显式写了 `pitch_speed = 60` 或 `pad_look_speed = 180`，需手动改成 30/60 或删掉这两行**——显式值会盖过新默认。

### 1.0.41 并入：滚转归零快捷键（2026-11-01，用户确认：键盘 Z / 手柄 LB+RB）

需求原文：是否需要新增快捷键用于重置相机旋转。结论：加，但语义定为「滚转归零（水平校正）」而不是第二个整体重置——R 已整体还原位置/朝向/FOV，Q/E 滚转后想回正只有按 R（丢构图）或盲调（面板此前没有滚转读数）。

改动（核心层 + UI，引擎层未动）：
- 键盘 `Z`（`key_roll_reset`，可改）；手柄 `LB+RB` 同按（`pad_roll_reset`，可改）。选 LB+RB 是零冲突组合：两肩键同按本来滚转增量互抵为净零（RB−LB=0），拿按下沿做归零不占任何现有按键。
- 语义：只把 `roll` 置 0，位置/俯仰/偏航/FOV 不动（`PhotoMode::tick` 新分支，日志「滚转已归零（位置/俯仰/偏航/FOV 不变）」）；与 R 同帧时 R 先整体还原快照、再归零滚转。
- 诊断面板「朝向」行补滚转读数（`ui_tab.cpp`，此前只有俯仰/偏航）；键位提示、手柄帮助行同步。
- 顺手修两处 1.0.27 起过期的注释（俯仰键 Z/X → 方向键）：`input_logic.h` Intent 注释、`input_logic.cpp` 俯仰注释。
- 配置文本：`config.h` 默认值、`config.cpp` 解析/`default_config_text`、`config.ini.example`、dist `config.ini`（gen_config 重新生成）全部同步；README（根 + docs + dist）键位表更新。
- 测试：核心层 735 → **754 项全绿**。新增：默认值断言（Z / LB+RB）、keymap 解析（roll_reset='Z'）、键盘 Z 按下沿 → reset_roll（松开不复发、与 reset_view 不串扰）、手柄 LB+RB 同按 → reset_roll 且滚转增量净零、PhotoMode 滚转归零场景（roll 15→0，位置/俯仰/偏航/FOV 不变，日志断言）。
- DLL 已重编链接通过（build-dll/PBZPhotoModeCpp.dll，产物含新日志串）；已按交付流程打包 `dist/PBZPhotoModeCpp_1.0.41-cpp.zip`（只出包未部署）。

实机验证要点：
1. Q/E 滚歪画面后按 `Z`：地平线应立即回正，站位、朝向、FOV、画面构图全部不动。
2. 手柄 RB/LB 滚歪后 `LB+RB` 同按一次：同上；注意是**同时按住两肩键**（单击即触发，不必长按）。
3. `R` 行为不变（整体回到进入时）；同帧连按 R 和 Z 极端场景不必专门测（先还原快照再归零，日志两条都有）。
4. UE4SS 控制台面板「朝向」行应显示三项：俯仰/偏航/滚转。

### 1.0.42 并入：接入 PBZ-Framework-Menu 游戏内菜单框架（2026-11-01）

需求：接入 https://github.com/lin-414/PBZ-Framework-Menu （影之刃零模组菜单框架：DirectComposition 覆盖窗 + ImGui + C API/schema 系统，不碰游戏交换链、不影响 FSR3 帧生成）。框架侧注册 API 已就绪（sdk/pbzfm.h），其 README「下一阶段 5」点名把本模组的 ImGui 诊断面板接成第一个客户端。

改动（新增文件 + 接线，引擎层与摄影模式核心逻辑未动）：
- `third_party/pbzfm/pbzfm.h`：框架 SDK 头内置拷贝（纯 C ABI；客户端运行时 `GetModuleHandleW(L"PBZFM.dll")` + GetProcAddress 解析，零链接依赖）。
- `src/pbzfm_schema_map.h/.cpp`（新增，随 `pbzpm_core` 编译与测试）：「菜单 schema ↔ Config 字段」唯一映射表——10 个键盘键位、11 个行为/隐藏开关、6 个冻结增强开关、2 个枚举（冻结方式 / 鼠标竖直行为）、16 个数值参数；一张表同时驱动注册、播种、写回与单测。
- `src/pbzfm_client.h/.cpp`（新增，仅 DLL 构建）：后台线程轮询 PBZFM.dll（250ms × 2400 次 ≈ 10 分钟，UE4SS 的 mod 加载顺序不定；超时安静退回纯快捷键）；注册「PBZ Photo Mode」分区 =「设置」schema 页 +「运行状态」自绘页（widget API：状态/位姿/键位速查 + FOV 滑条 + 进出/冻结/重置/重载按钮，不依赖框架的 ImGui 版本）。
- `mod_app`：实现 `PbzfmClient::Host` 五入口（mod_dir / current_config / submit_config / submit_command / log）；`pbzfm_apply_pending()` 在引擎 Tick（持 mutex_）消化——配置换入 + `build_keymap` 重建 + 三个实时开关（hide_player / block_game_input / hide_game_hud）就地生效，动作全部走既有路径（`handle_toggle` / `toggle_freeze` / tick 的 reset_view 分支 / `set_fov` / `reload_config`）；每帧发布诊断快照；析构先停客户端线程。
- `config.cpp/h`：`default_config_text` 重构为 `config_ini_text(const Config&)`（原硬编码的 12 个布尔值改为随 c. 序列化），新增 `save_config_file`——菜单写回与「缺省自动写出」共用同一份序列化器，格式与旧文件完全一致。
- **值的流向（config.ini 是唯一真源）**：启动时用 config.ini 现值播种菜单（框架的存储恢复时序有缺陷——ConfigCreate 时条目还没加，data/pbzphotomode.json 不会在启动回灌，本接入不受影响且顺带规避）；菜单改动 → 客户端镜像 → 写回 config.ini + 提交引擎 Tick 应用；手动改 config.ini 后菜单点「重新载入」即同步（sync_from_config 重播种）。
- **线程模型**：框架渲染/变更回调只在框架线程提交数据（命令队列 + pending config + 快照镜像，各自小锁）；所有引擎交互回到引擎 Tick 线程执行。播种/同步用 syncing_ 屏蔽 OnChange 防回环；键捕获给出键名表表达不了的键（如鼠标侧键）时拒绝应用并回滚菜单显示值。
- **踩坑**：pbzfm.h 拉进 `<windows.h>`，其 `max` 宏打坏之后才解析的 UE4SS 头（UnrealString.hpp 的 `std::numeric_limits<int32_t>::max()` 直接编译炸）→ pbzfm_client.h 里 NOMINMAX + undef min/max 兜底；Host::log 内联实现解引用了仅前置声明的 Ue4ssPort → 实现挪到 cpp。
- 测试：核心层 **2003 项全绿**。新增 `test_pbzfm_fields_roundtrip`（字段表元数据/键唯一/读改写回环/不可命名键拒绝/越界夹紧/默认键名可解析）与 `test_pbzfm_config_writeback`（config_ini_text 全字段零告警往返 + save/load 一致 + 与 default_config_text 同源）；build_tests.bat 同步加编译单元。
- 版本 1.0.42-cpp；DLL 重编链接通过（build-dll/PBZPhotoModeCpp.dll，829,952 B）。

实机验证要点：
1. 装 PBZFM（其 dist 包解到 `ue4ss\Mods\`）+ 本模组，进游戏按 PBZFM 呼出键（默认 F8）：侧栏应出现「PBZ Photo Mode」，含「设置」「运行状态」两页；日志有「已接入 PBZFM 菜单框架」。
2. 设置页改「移动速度」「进入时自动暂停时间」「进入/退出键」：改键位立即生效；改隐藏类开关后进摄影模式应按新值执行；改动后 `PBZPhotoModeCpp\config.ini` 应已同步更新。
3. 摄影模式进行中：运行状态页 FOV 滑条实时变焦；冻结/恢复、重置视角、退出按钮等效对应快捷键；位姿读数随相机走。
4. 手动改 config.ini → 菜单点「重新载入 config.ini」→ 菜单显示值应变成文件值。
5. 卸载/不装 PBZFM：日志出现「未检测到 PBZFM 菜单框架（可选组件）」，纯快捷键行为与 1.0.41 完全一致。

### 1.0.42 跟进：菜单多语言（2026-11-01）

框架侧（PBZ-FM 仓库）按方案落地：磁盘语言表加载修复（Resolve 补 .json 后缀）、语言下拉扩到 zh-CN/en-US/ja-JP/ko-KR（各带内嵌兜底）、韩语 UI 优先加载 Malgun Gothic、侧栏/页签/schema 六处 label 渲染过 I18n::T()（文本即 key，查不到原样返回）。配套回归测试在框架 tests/i18n_test.cpp。

客户端侧配合改动（本仓库）：
- **schema 条目/分组/页名**：注册时用中文原文当 key，配合框架语言文件的「文本即 key」条目翻译，客户端零改动。55 条客户端条目（分组 6 + 条目 39 + 页名 2 + 分区名 1 + 枚举选项 4 + 版本行等）已按 en/ja/ko 生成于 `_ref/pbzfm-language-packs/{en-US,ja-JP,ko-KR}.json`（含框架自带 24 key 的完整文件，已按 key 清单与 dump 输出逐字校验；注意框架的扁平 JSON 解析器不支持注释）。
- **「运行状态」自绘页**：渲染回调里画的固定文本不走框架 T()，客户端自己翻——`pbzfm_client.cpp` 内置 zh/en/ja/ko 四套文案表，语言读 `ue4ss\Mods\PBZFM\PBZFM.ini` 的 `[framework] language`（与本模组目录同级；自绘页每次渲染至多每秒重读一次，热切语言即时跟随；读不到保持 zh-CN）。
- 枚举选项文本目前框架未过 T()（RenderSchemaForm 的 SCH_ENUM 只翻译了 label），选项暂以原文显示；框架把 `opts.push_back(i18n.T(o.c_str()))` 补上后，语言包里的 4 条选项条目即自动生效。
- 实机验证增补：切 en-US/ja-JP/ko-KR 后，PBZ Photo Mode 分区名下的「设置/状态」页与设置项应翻译；运行状态页按钮/状态行应跟随 PBZFM.ini 的 language（改完语言保存后 1 秒内刷新）。
- **（已被 1.0.43 取代）**：此路线实机未生效，1.0.43 起客户端自治 i18n，见下节。

### 1.0.43 重构：PBZFM 设置页改客户端自绘 + 自治 i18n（外部词典，2026-11-01）

背景：1.0.42 的「框架语言文件 + 文本即 key」路线实机未生效（依赖框架侧语言文件正确安装与加载时序，排障链路长）。用户决定：i18n 全部由本项目实现，语言切换做进本项目在 PBZFM 里的设置页。

改动（schema 路线整体退场）：
- **弃用框架 schema 注册页**：ConfigCreate / ConfigAddXxx / ConfigOnChange / 播种 SetXxx 全部移除，PBZFM 的 `data/pbzphotomode.json` 不再使用（残留文件无害）。config.ini 仍是唯一真源、菜单写回它。
- **设置页/状态页改客户端自绘**：走 SDK 的 `PBZFM_GetImGuiContext()` 通道直接画进框架帧。两边 `third_party/imgui` 均为 1.92.1（IMGUI_VERSION_NUM 19210），`static_assert` 钉死版本；`ensure_imgui()` 用 `BackendPlatformName == "pbzfm_dc_overlay"` 指纹校验上下文，不匹配只告警一次并跳过自绘页。我们的 imgui 静态副本各持 current-context 指针，SetCurrentContext 只影响本线程语义，与 UE4SS 控制台面板互不干扰。
- **pbzfm_client_lang.h/.cpp（新增，仅 DLL 构建）**：客户端词典管理器。
  - 内置 zh-CN/en-US/ja-JP/ko-KR 嵌入 JSON（95 条/语言；zh 只含 `#命名键`，设置项 label 的 key 本身就是中文、identity 兜底）；
  - **外部词典**：`PBZPhotoModeCpp\languages\*.json`——扁平 UTF-8 JSON，key = 中文原文（与注册 label 一字不差），`__display` = 菜单显示名（可选，缺省文件名）；同名 id 覆盖内置，新文件名即新语言；
  - 查译链：当前语言 → 内置 zh（`#` 键兜底）→ 原样返回；解析器与框架同款扁平语义（**不支持注释**），坏词典整份跳过保留现状；
  - 重扫 2 秒节流（自绘页每帧调用），设置页有「重新载入词典」按钮强制重扫。
- **设置页内容**：语言下拉（含「跟随 PBZFM 菜单」项）+「重新载入词典」+ 全部 39 项设置。键位捕获客户端自实现：镜像框架输入层的 VkToImGuiKey 映射（两边同为 1.92.1 布局），遍历候选 VK 查 `IsKeyPressed`，5 秒超时自动取消（Esc 会直接关菜单）。提交语义与原 schema 相同：Bool/Enum/键位即时提交，Float 松手提交（IsItemDeactivatedAfterEdit），每次提交写回 config.ini + submit_config。
- **状态页**：状态/按钮/提示/位姿格式全部走 `#` 命名键词典。
- **语言持久化**：config.ini 新增 `[ui] menu_language`（默认 `auto`=跟随 PBZFM.ini 的 language；或词典 id）。核心层 parse（空值报错）/config_ini_text/往返测试同步。
- 页签文本注册时固化、由框架渲染，用四语并写的中性标签「设置 · Settings」「状态 · Status」；分区名保持 "PBZ Photo Mode"。
- 框架侧已做的 T() 渲染对本模组不再关键（我们不再注册 schema 条目），语言包文件也不再需要；`_ref/pbzfm-language-packs/` 留档作废。
- 版本 1.0.43-cpp；核心层测试 **2005 项全绿**（menu_language 往返 2 项新增）；DLL 重编通过；dist 重打包。

外部词典格式示例（`PBZPhotoModeCpp\languages\de-DE.json`）：

    {
      "__display": "Deutsch",
      "进入/退出摄影模式": "Fotomodus betreten/verlassen",
      "移动速度（单位/秒）": "Bewegungsgeschwindigkeit (Einheiten/s)"
    }

实机验证要点：
1. F8 → PBZ Photo Mode →「设置 · Settings」页首行「语言」下拉：切 en-US/ja-JP/ko-KR 应当帧生效（不需要框架语言文件）；「跟随 PBZFM 菜单」时随 PBZFM 的语言走。
1a. **韩文字体**：框架启动时只按 PBZFM 自己的语言加载一个字体（中文环境 = 雅黑，无谚文），语言下拉里的「한국어」与整页韩语会显示 ?。1.0.43 起自绘页懒建「雅黑 + Malgun Gothic」合并字体（谚文来自 Malgun，汉字/假名/拉丁以雅黑优先，动态图集按需光栅化），画页面前 PushFont、结束 PopFont；字体大小用 `LegacySize`（18px）与框架视觉一致并同样受界面缩放影响。系统缺 Malgun 时告警一次、韩语退化显示。
2. 选定的语言写回 config.ini `[ui] menu_language`；重启游戏后保持。
3. 往 `PBZPhotoModeCpp\languages\` 放一个新词典（如 de-DE.json），不重启、点「重新载入词典」或等 2 秒，下拉里应出现并可用。
4. 键位捕获：点键位按钮显示「按下新按键…」，按 F7 应绑定成功（5 秒不按自动取消）；被游戏/菜单占用的 Esc 会直接关菜单。
5. 改设置项 → config.ini 同步更新、摄影模式内实时生效（与 1.0.42 相同）。

### 1.0.54 并入：适配 PBZ-Framework-Menu 0.4.0（SDK v2，2026-11-01）

框架从接入基线（i18n 8b68f3a）前进到 0.4.0：SDK v2 纯增量（菜单事件/和弦热键/生命周期/纹理/侧栏搜索/主题 JSON），ImGui 仍为 1.92.1。核对结论与适配：

- **破坏点（已修）**：SDK 内置头同步到 0.4.0 版 `third_party/pbzfm/pbzfm.h`。新版 `PBZFM_LoadApi` 语义变化——成功返回 1（旧版 0）、版本判定改 `GetAbiVersion() >= PBZFM_ABI_VERSION`（v2 框架报 2；旧头用 ==1 判定会在 0.4.0 上永远加载失败）、v2 新导出解析为可选字段（旧框架上为 NULL，须判空调用）。我们只用了必需核（SetSection/AddSectionItem/GetImGuiContext 等），签名全部未变，同步头后即兼容 0.3.x 与 0.4.0。
- **采用 v2 菜单事件**：注册 `OnMenuEvent`（判空），关菜单当帧置 `menu_closed_tick_`，`render_key_field` 入口消费——改键捕获跨开/关菜单不再残留「按下新按键…」（原有 5 秒超时兜底保留）。日志行附框架版本（`VersionString` 可选，缺失退回 ABI 版本号）。
- **核对未变项**：ImGui 1.92.1/19210（自绘通道与 static_assert 不受影响）；`VkToImGuiKey` 映射逐字未变（键位捕获镜像仍有效）；自绘页渲染契约未变（RegistryMutex 持锁下 BeginTabItem → pg.fn）；`PBZFM.ini` 的 `[framework] language` 键未变（menu_language=auto 跟随仍有效）。框架新增的侧栏搜索/主题 JSON/纹理 API 与本模组无交互。
- 版本 1.0.54-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 对着 0.4.0 框架：日志应出现「已接入 PBZFM 菜单框架 v0.4.0…」（不再是永远「未检测到」）。
2. 开/关一次菜单后再进设置页：改键按钮不应停留在「按下新按键…」状态。
3. 其余行为（自绘两页、词典、韩文字体、写回 config.ini）与 1.0.53 一致。

### 1.0.57 修复：0.5.0 分类树点击分组节点崩溃 + 树节点名显示原始键（2026-11-01）

实机（crash_2026_11_01_21_53_33）确认两处 1.0.55/56 引入的问题：

- **崩溃（1.0.56 分组子节点点击即崩）**：`RenderGroupThunk` 的 userdata 携带的是**组名**（`#group_keys` 等静态字符串），但 thunk 里把它 cast 成了 `PbzfmClient*` 并当 this 调 `render_group_page`——成员 `std::mutex::lock()` 对着 .rdata 里的字符串地址上锁，dump 落在 `RtlAcquireSRWLockExclusive` 读 0x1（近空指针），与转储完全吻合。修复：实例指针走文件级 `g_client_self`（start() 登记，本 mod 进程单实例），thunk 只把 userdata 当组名传给 `render_group_page`。
- **树节点名显示原始键**：1.0.44–53 起字段表的组名已是 `#命名键`（`#group_keys` → 「快捷键（键盘）」靠词典翻译），而树节点名由框架原样渲染、不过 T()。修复：注册前先 `refresh_language()`（menu_language，auto=跟随 PBZFM.ini），用**翻译后的名字**注册节点——「PBZ Photo Mode/<设置>/<快捷键（键盘）>/…」；新增词典键 `#node_settings`/`#node_status`（四份词典同步，99 条/语言），「设置 · Settings」双语拼接废弃。注意：**树节点名注册后固化**，游戏内切语言不重命名（重启生效），页内内容仍实时切换。
- 版本 1.0.57-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 对 0.5.0 框架：展开「PBZ Photo Mode → 设置」应见六个分组子节点，**点击任一分组不再崩溃**，页面只显示该组设置项。
2. 树节点名应显示当前语言的文字（中文环境 =「设置」「快捷键（键盘）」等，不再是 #group_keys，也没有中英拼接）；切换 UI 语言后树节点名要重启游戏才变，页内内容即时变。
3. 状态页、语言页、键位捕获、写回 config.ini 行为不变。

### 1.0.58：树节点名随语言热改名（2026-11-01 实机反馈「切英文左侧不同步」）

1.0.57 的树节点名注册后固化，切语言只有页内内容变。利用 0.5.0 的 `RenameSection`（v2 可选导出，按路径寻址、改名保留子树）实现热改名：

- 注册时把每个节点的当前显示名记进 `node_names_`（"settings"/"status"/"#group_*"），分组键序记 `group_keys_`；
- `refresh_language()` 每帧比对生效语言 id（menu_language 或 auto 跟随的 PBZFM.ini），变化时 `rename_tree_for_language()`：**先改分组**（路径含旧父名，必须用旧名寻址）、再改「设置」与「状态」兄弟节点；改名成功才更新 node_names_（失败静默保留原名，不刷重试）；结束重设调用线程 current（框架文档建议）。
- 旧框架判空跳过（0.4.x 回退页签的页名是页 label，API 改不了，维持注册时语言）。
- 版本 1.0.58-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 语言下拉切 English：左侧树应立即变为 PBZ Photo Mode → Settings →（Keyboard bindings / Behavior / Hide… / Camera & movement / Gamepad）+ Status；切回中文同步变回。
2. auto 模式下改 PBZFM 自己的语言，1 秒内本模组树节点跟随。
3. 改名后各节点点击仍正常渲染（回归 1.0.57 的崩溃修复）。

### 1.0.59：语言独立成顶层节点（2026-11-01 实机反馈「语言选项只在点开设置时出现」）

分类树调整：语言下拉（+ 重新载入词典）从「设置」分类页拆出，独立为顶层节点「语言」（`#node_language`，四词典同步：语言/Language/言語/언어）；「设置」变为**纯分类**——不再注册页面，仅由六个分组子节点的注册路径自动创建（点击只做展开/收起）。改名热更新覆盖 language 节点。0.4.x 回退整页不变（语言切换仍在页首）。

- 版本 1.0.59-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 侧栏顶层应为「语言 / 设置 / 状态」三项；语言项点击即见语言下拉，不再需要先点开设置。
2. 「设置」点击只展开/收起分组；六个分组页渲染与提交不变。
3. 语言改名热更新覆盖「语言」节点本身（切 English 后应变 Language）。

### 1.0.60：分组子节点升为顶层，删除「设置」分类（2026-11-01 用户要求）

分类树拍平一层：「PBZ Photo Mode」下直接是 语言 / 快捷键（键盘） / 行为 / 隐藏（进入摄影模式时） / 冻结增强 / 相机与移动 / 手柄 / 状态 八个顶层节点（无「设置」中间层）。分组注册路径、热改名路径同步去掉 settings 父段；`node_names_` 不再记录 settings（词典键 `#node_settings` 保留未用）。0.4.x 回退整页不变。

- 版本 1.0.60-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 顶层应为八个节点（语言 + 六分组 + 状态），点分组直接渲染；语言热改名覆盖除品牌根外的全部顶层节点。

框架 0.5.0：侧栏改分类树（SKSE Menu Framework 3 的 PageTree 风格），注册 API 走嵌套路径（'/' 分段、缺失中间节点自动创建），v3 为**可选导出**（SetSectionPath/AddSectionItemPath/ConfigCreatePath），必需核与 v1/v2 全部未动。

- **无需强制适配**：v1 单段注册保持根节点语义，旧客户端在 0.5.0 上照常工作（我们的节点变「多页节点」，内容区仍是页内页签）。内置 SDK 头同步到 v3（纯增量，OnMenuEvent/VersionString 等 v2 可选字段不变）。
- **采用分类树（判空回退）**：`AddSectionItemPath` 可用时挂成三级树——「PBZ Photo Mode」→「设置 · Settings」（分类页，只剩语言切换 + 词典重扫）→ 六个分组子节点（快捷键/行为/隐藏/冻结增强/相机与移动/手柄，各自只画本组字段）；「状态 · Status」为根的另一个子节点。旧框架自动回退为根节点 + 页内页签（整页 = 语言 + 全部分组分隔线）。
- 版本 1.0.55-cpp；DLL 重编链接通过；dist 重打包。

实机验证要点：
1. 对 0.5.0 框架：侧栏「PBZ Photo Mode」应显示展开箭头，展开后见「设置 · Settings」「状态 · Status」两个子节点；日志报框架 v0.5.0。
2. 对 0.4.0/0.3.x 框架：回退为根节点 + 页签，与 1.0.54 一致。
3. 框架仓库工作区尚有未提交的 src/layout.h、src/menu.cpp 改动（树渲染迭代中），若其后行为再变需复核。
