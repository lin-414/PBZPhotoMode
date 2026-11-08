## 冻结期布料闸门 + 恢复侧硬化：修「衣物部件离身拉条」（2026-11-08，工作区变更）

**背景与定案**：用户实机报「衣服部件离人物身体有距离、像被拉扯一样连着身体」（截图 19:00:29 / 19:04:33），与 UE4SS.log 对齐后用户确认是 mod 造成。机制：冻结期游戏侧布料管理按渲染可见性/距离翻转状态，可能把布料翻回活模拟——活的 Chaos solver 对着被 bPauseAnims/bNoSkeletonUpdate 钉死的骨骼，飘带被越拉越远；拉伸态一旦被再挂起就烤进世界，解冻后玩法里仍是坏姿态（19:04:33 那张是两场摄影模式之间的正常玩法画面，佐证坏状态跨冻结存活）。日志旁证：每场恒有「播放中 1」的网格（冻结期确实有活 tick）；BP_NPC_Xukong_HuanXiang 的 CA_XuKong_Cloth02/03、Hair01、HeadOther 四组件每场「SuspendSimulation 未生效」；19:04:36 场挂起 10 个、0.8 秒后只恢复 4 个（is_ok 静默丢弃，泄漏不可见）。mod 此前的洞：①只在进入瞬间挂一次布料，冻结期零复核；②恢复侧丢弃无日志。

**修复**：
- **冻结期布料闸门**（`FreezeMotionPort::cloth_gate_tick()`，core 每秒一次、仅布料冻结生效期间跑，放在冻结期每帧块的 MPC 写回之后）：复核冻结时登记的**全部**布料组件（含原本被游戏挂起的、挂起失败的），发现仍在模拟 → 立即重挂，恢复权随之转到我们（解冻由我们 Resume，不再依赖游戏侧翻回）；重挂仍失败 → `SetComponentTickEnabled(false)` 兜底（签名校验单 bool 通道，与相机修饰器同款；模拟不再被驱动），每组件 Warn 一次带全名（不刷屏）。
- **恢复侧硬化**：闸门停用的 Tick 先还原再 Resume（不还原 Tick，Resume 了模拟也不走）；「挂起过却随对象销毁丢弃」计数打 Info 日志（若数字频繁且对象其实还活着 = is_ok 误判，可据此跟进）；恢复失败原因区分「Resume 调用未生效（反射未命中）」vs「恢复后回读仍挂起」。
- **冻结侧失败原因区分**：「Suspend 调用未生效（反射未命中）」vs「调用成功但回读仍非挂起态」——下次日志能直接定死 CA_XuKong 那批组件是哪类；挂起失败的组件同步登记进闸门盯防名单（它们最容易被翻成活模拟）。

**改动面**：`engine_port.h`（cloth_gate_tick 虚函数，默认实现返回 0）、`ue4ss_port.h`（ClothComponentState + suspend_failed / tick_disabled_by_us / gate_warned）、`ue4ss_port_micro_motion.cpp`（闸门实现 + 冻结/恢复路径改造）、`photo_mode.h/.cpp`（cloth_gate_accum_ 累加器；闸门在解冻/退出后不跑）、`tests/fake_engine.h`（cloth_gate_tick 桩 + 计数）。无新配置项、无四语言改动（闸门日志走中文原串，与其余引擎日志一致）。

**测试**：核心层 **6,916 项全绿**（+16：闸门 1s 节流按 dt 累计、解冻停跑、再冻结恢复、退出停跑、freeze_cloth=false 不跑、auto_pause_time=false 不跑）。DLL 重建通过（引擎侧闸门/invoke_set_bool/结构体扩展仅在 DLL 目标编译，过 DLL 构建才算编译验证）。

**发行**：`PBZPhotoModeCpp_2.1.1-cpp.zip`（965,404 字节，用户「打包2.1.1」授权；version.h 2.1.0-cpp→2.1.1-cpp，DLL 随版本串重建，2,297,344 字节，sha256 前 16 `ebf99ecc9138c95f`）——**2.1.0 终态 + 本条布料闸门修复**，本条之外无其他代码变更。包内核验：包内 DLL 与 build-dll 逐字节一致、config.ini = config.ini.example、README = docs/README、包内 DLL 版本串 `2.1.1-cpp`、四语言词典齐全；核心层 6,916 项全绿。NEXUS 文案四处安装包名同步 2.1.1-cpp。dist 里 2.1.0 系 preview 包与 2.1.0 正式包保留不动。

**待实机复验**：① 长冻结（≥3 分钟）盯飘带——修复前「冻结中慢慢变长/下垂」，修复后应纹丝不动；② 若日志出现「冻结期布料闸门：组件仍在模拟…」= 抓到现行且已兜底，日志带组件全名，发回即可定位元凶；③ 关注两条新恢复日志的频率：「还原 N 个被闸门停用的 Tick」「N 个已挂起组件随对象销毁丢弃」；④ 拉条已出现时，开关一次时间冻结（Resume 重初始化布料）应能当场复位。

**实机已验证（2026-11-08 20:24 场，UE4SS.log，2.1.1-cpp 包）**：拉条未再出现，用户确认，本条闭环。三条新链路全部按设计工作——① 失败原因区分定性元凶：飞贼 `CA_Male_Type_E_Cloth_01`、驯犬人 `CharacterMesh0` 均为「调用成功但回读仍非挂起态」（游戏自定义布料拒绝 Suspend，与 CA_XuKong 同类），闸门兜底停 Tick，其中驯犬人一件解冻时「还原 1 个被闸门停用的 Tick」完整还原；② **实锤冻结期翻回**：21:11:34 场挂起 16、解冻恢复 17——多出的 1 个是闸门秒检抓到「游戏侧挂起过、被翻回活模拟」的组件重挂成功（拉伸根因路径实机坐实且被拦截）；③ 恢复对账全平（26/26、22/22、15/15）。已知无害项：闸门停 Tick 的组件若解冻前随对象销毁则静默丢弃（飞贼一件，普通怪战斗中销毁），「还原数=0」属预期；若频繁出现停过却对不上，再给销毁路径加计数。

## 审查修复出包：滑条松手提交死码 + tagged 还原 neutralize 兜底 + 两处收尾（2026-11-08，工作区变更）

**背景**：用户终审「全部修复后出包」。先对 11-08 全部并行批次的工作区 diff（39 文件 ~3,400 行）做全量审查，修复四件后按 version.h 现值出正式包。

**P1 滑条「松手提交」死码（回归，`pbzfm_client.cpp` Float 分支）**：重构滑条提交语义时引入。`pending_commit` 是**帧内局部变量**，只在 `changed && IsItemActive()` 的帧置位并在同一帧被 `pending_commit && IsItemDeactivatedAfterEdit()` 消费——而这两个 ImGui 状态互斥（激活中 ≠ 刚失活）；且 vendored ImGui 1.92.1 的 `SliderBehaviorT` 在松手帧先 `ClearActiveID` 再返回（imgui_widgets.cpp `if (!MouseDown[0]) ClearActiveID()` 位于取值更新之前），松手帧 `changed` 恒为 false。两条路都堵死：鼠标拖动（原生回退与框架 SliderFloatEx 同病）与键盘微调只走 `submit_config_preview`（引擎生效、菜单镜像保持），`commit_settings` 永不执行——**config.ini 不写回、无「已应用」日志、mirror generation 不 bump，游戏重启后拖过的值全部回退**；只有 Ctrl+点击输入框与重置按钮还能正常落盘。修复：删掉 pending_commit 概念，`IsItemDeactivatedAfterEdit()` 无条件 `commit_settings`（松手帧草案里就是最后一次拖动值）；Ctrl+点击场景由「changed 且轨道非活动即提交」分支覆盖，且该帧 `DeactivatedItemData.ID` 是输入框不是滑条，两分支不会对同一次编辑双提交。

**P2 tagged 还原补 neutralize 兜底（`ue4ss_port_micro_motion.cpp` 渲染对照还原）**：via_tag 快照此前只走 `unset_cvars_tagged` + 回读校验，而 11-08 取证学费②（tuning.h）已实锤 **UnsetConsoleVariablesWithTag 对模组 tag 确定性无效**——一旦走到 exec 失败换 tagged 通道的 cvar（r.Streamline.DLSSG.Enable / r.PBZ.SuppressFrameGen 那批），解冻还原就会恒败、退避重试耗尽、「渲染对照未恢复」告警且覆盖残留到游戏重启。补兜底：解绑失败退回 **neutralize**——同一 tag 写回快照原值 + 回读校验（Set 通道已实证可用），值还原即记账释放，tag 留在注册表里把原值钉住（Set 覆盖语义；代价=游戏此后对同一 cvar 的主动调整会被钉住，取证记录在案的已接受取舍），打 Info 日志「tag 解绑未生效（11-08 取证同款），已按同 tag 写回原值 neutralize」。DOF 门控的 `tag_unset` 维持原样（11-07 有解绑确认的实机记录，该批 cvar 游戏不逐帧重设）。

**P3a `film_grain` 补登记 Deprecated（`config.cpp`）**：hotfix33 判死时期它列在 legacy 清单（kLegacyLookKeys）里物理清除；2.1 复活批次删清单转正色差时漏了它——旧 config.ini 的残留键退化成通用「未知配置项已忽略」。补专属弃用提示（胶片颗粒 pass 被 r.Tonemapper.Quality 档位跳过，2.1 复活只回了色差）。

**P3b 保存预设后下拉定位（`pbzfm_client.cpp`）**：保存成功后 `preset_selected_ = paths.size()` 假设新文件（编号 max+1）排本帧列表末尾；目录里有合法名但非法序号的文件（如 `look_preset_backup.ini`）时它们排序在后，新预设插中间 → 下拉高亮错到杂牌文件。修复：保存成功后重枚举一次、按路径定位新项下标（刚写完的文件目录迭代器立即可见），找不到再退回末尾。仅显示问题，套用/删除本就有路径锁定保护。

**改动面**：`pbzfm_client.cpp`（Float 提交分支 + 保存定位）、`ue4ss_port_micro_motion.cpp`（还原 neutralize 分支）、`config.cpp`（film_grain 登记）。四语言/头文件/schema 无改动。

**测试**：核心层 6,900 项全绿不变（渲染线程与引擎 SEH 侧不进测试编译面，改动必须过 DLL 构建才算编译验证）；DLL 重建通过（**2,293,248 字节**，sha256 前 16 `bc31ac26d39ecc3f`）。

**发行**：`PBZPhotoModeCpp_2.1.0-cpp.zip`（963,121 字节，用户「全部修复后出包」授权；版本号取 version.h 现值，覆盖 dist 里 11-07 的同名旧产物）——**11-08 全部并行批次的合并终态**：F8 菜单换皮（药丸/SliderFloatEx/Combo，API 3→4）+ 预设终态（保存+下拉+两击删除）+ 调色·进阶三件 + 锐化/超采样整线移除 + NLP 并入总开关 + PV 总开关写后回读 + 输入锁堆叠校正。包内核验：config.ini = config.ini.example、README = docs/README、包内 DLL 与 build-dll 字节一致、neutralize 新串与 film_grain 弃用串均在包内 DLL。框架侧 v4 三导出（CloseMenu/GetApiVersion/SliderFloatEx）已在 exports_baseline 登记（0 missing），无需重建。

**待实机复验**：① F8 菜单拖任一滑条松手 → config.ini 值已更新 + 「PBZFM 菜单：…已更新」日志一行（此前只有预览无落盘）；② 键盘方向键微调、重置按钮同链路回归；③ 若日志出现「tag 解绑未生效…已按同 tag 写回原值 neutralize」= P2 兜底生效且值已还原；④ 保存预设后下拉立即高亮新「预设 n」；⑤ 11-08 各批次遗留的实机复验项（CHANGELOG 各条目所列）随本包一并复核。

## 风格预设下拉行新增「删除预设」按钮：两击确认删文件（2026-11-08，工作区变更）

**背景**：预设终态（保存 + 下拉套用）留下的收尾缺口——删除此前要去 mod 目录手动删 `look_preset_<编号>.ini`（look_preset.h 注释与两份 README 都写着「手动删」）。补进菜单：删除按钮放预设下拉同一行，删的是下拉当前选中项，不必再开目录找文件。

**交互（防误删，无额外弹窗）**：两击确认——第一击按钮换「确认删除？」文案进入确认态，5 秒不点自动复位（与改键捕获 `kCaptureTimeoutSec` 同节奏）；确认目标按**文件路径**锁定而非下标，确认期间换下拉选中即失效，不会删错项。第二击 `std::filesystem::remove` 删文件本体：成功提示「已删除 预设 n」+ Info 日志，失败显示系统错误消息 + Warn 日志；删除成功清下拉选中（防残留索引指向列表里的下一项）。无预设/未选中时按钮禁用。删除后的编号不回收（保存永远取现有最大编号 + 1），外部残留同名文件不会被新预设覆盖。下拉宽度按删除按钮两态文案的最大值留位，按钮换字时下拉宽度不跳。

**改动面**：`pbzfm_client.cpp`（render_look_presets 删除按钮区 + 两态量宽；枚举注释「手动删」同步）、`pbzfm_client.h`（`preset_delete_target_` / `preset_delete_deadline_` 两成员，渲染线程独占归属不变）、`core/look_preset.h`（语义注释：删除入口 + 编号不回收）、四语言（各 +3 键 `#preset_delete` / `#preset_delete_confirm` / `#preset_deleted`，148→151 键）、根 README 与 docs/README 预设描述同步。

**测试**：核心层 6,900 项全绿不变（预设 UI 不进测试编译面）；DLL 重建通过（2,291,712 字节；pbzfm_client.cpp 仅 DLL 目标编译，改动必须过 DLL 构建才算编译验证——产物字节级确认新日志串已在）。

**发行**：`PBZPhotoModeCpp_2.1.0-cpp-look-preview7.zip`（962,902 字节，用户「打包」授权）——含并行批次「锐化/超采样整线判死移除」合并态；staging 同步刷新：config.ini 去 `dof_nlp_enable` 残键（=config.ini.example 现态）、README 带删除按钮描述；包内六项核验过（DLL 新串/DLL 与 build-dll 字节一致/config 无残键/四语言删除键/README）。

**待实机复验**：① 下拉行布局（保存/下拉/删除一行放下）；② 两击确认节奏（5 秒复位、确认中换选中即失效）；③ 删除后下拉即时少一项、消息行提示、日志两行（Info 成功/Warn 失败）。

## 锐化/超采样整线判死移除（2026-11-08，工作区变更，用户终审）

**裁决（preview6 净态实测，UE4SS.log 14:32 会话）**：① 锐化 `r.Tonemapper.Sharpness` 写入通道无效（GetFloat 恒 0；判死闩 3 次即静默，闸门/节流/退避全部按设计工作）；② 超采样 `r.ScreenPercentage` **写入有效**（290% 回读 verified、tag 顶住动态分辨率基线 66.7、退出「写回原值」零失败），但**画面无可见增益**——本作 DRS/最终上采样管线不消费该 cvar 的有效值；写入有效 ≠ 有画面价值。用户终审：无效果 → 整线移除。

**通道学费留档（tuning.h，游戏更新后重启取证时照抄）**：写一次靠 tag 顶住（DOF 闸门模型，勿抄渲染对照的逐帧压回）；`UnsetConsoleVariablesWithTag` 对模组 tag 确定性无效 → 释放 = 同 tag 写回快照原值 + 回读校验（neutralize）；动态分辨率基线（66.7）非常量，写入前必读。

**摘除面**：`config.h/.cpp` 两键（登记 Deprecated——旧 config.ini 残留报一次性「已弃用」而非「未知配置项」）、`tuning.h` 常量删除 + 判死记录、`engine_port.h` PhotoCvarPort 删除、`photo_mode.h/.cpp` 通道生命周期删除（enter/tick/重申链/attempt_restore_all）、`ue4ss_port.h` 成员 + `ue4ss_port_photo_cvars.cpp` 实现文件删除、CMakeLists、`mod_app.cpp` 渲染前重申钩子 + 预览旁路回退、`pbzfm_schema_map.cpp` 两滑条、四语言两键（148 键）、测试两函数 + fake 桩删除。风格预设/三槽位（`core/look_preset.h`）不受影响。

**测试**：核心层 7,159 → **6,900 项**全绿；DLL 重建（2,291,712 字节）；`config.ini.example` 与 `test_count.txt` 重新生成。

## 超采样无法退出/持续卡顿：Unset 确定性无效，释放改「写回原值」（2026-11-08，工作区变更）

**现象（preview5 实机，UE4SS.log 14:16~14:21 会话）**：①超采样提交 300% 后点重置回 100 触发释放 → `解绑失败（保留标签待重试）` 连绵；②退出摄影模式「仍有 1 项未能还原」→ 恢复重试全败 → **再进被拒**（「上次退出的残留改动仍未还原，本次进入已取消」）——用户侧 = 「无法退出相机模式」；③300% 覆盖（基线 66.7 动态分辨率的 ~20 倍像素量）解不掉 → 卡顿持续到退出之后。

**根因**：`UnsetConsoleVariablesWithTag` 对本模组的 tag 在本作**确定性无效**——`SetConsoleVariableTagged` 参数命中、SEH 无异常、写入生效（300% 回读 verified），但 Unset 跨会话每一次都解不掉（`HasConsoleVariablesWithTag` 恒真）。游戏侧该接口对模组来源的 tag 就是空操作。

**修正**：release 弃用 Unset，改**「写回原值」（neutralize）**——同一 tag 再写回首次覆盖前快照的原值（Set 通道已实证可用），回读 == 原值即视为释放；tag 保留在注册表里复用（不换序号，避免积累死标签），现值已等于原值时直接记账释放。副作用：动态分辨率被惰性 tag 钉在快照采样值上（量级即原值本身，可接受）。锐化判死闩/告警节流/core 退避在 preview5 实机均按设计工作（14:18 段干净）。

**附带澄清**（用户报告「拖动画面无明显变化」）：拖动中按预览旁路设计不写引擎（中间值 = 全场景重分配，上一卡顿根因），松手提交才写——拖动无实时反馈是预期行为；300% 提交后卡顿恰证明值已生效（20× 像素量），锐度增益主观不明显，待净态对比复验。

**测试**：核心层 7,159 项全绿不变；DLL 重建（2,304,000 字节）；发行 `PBZPhotoModeCpp_2.1.0-cpp-look-preview6.zip`（966,174 字节）。

**待复验（下一包后）**：① 提交 >100 → 重置回 100 → 日志无解绑失败、画面即时回基线、帧率恢复；② 退出摄影模式干净（无恢复重试）、再进正常；③ 净态对比 66.7 基线 vs 150/200 的锐度增益——**若无可见增益，超采样整线按判死移除**（写入有效但无画面价值即无存在意义）。

## 拖动超采样滑条后异常卡顿：逐帧拉锯判明 + 写一次模型 + 判死闩（2026-11-08，工作区变更）

**现象（用户实机，UE4SS.log 13:23~13:27 会话）**：拖动超采样滑条后异常卡顿。日志三实锤：
① 锐化拖动期间每帧一条 `r.Tonemapper.Sharpness 写入未生效（当前=0 目标=0.25~0.83）`（~30 条/秒，引擎侧 Warn 无节流）；② 超采样写入本身成功（首启行 `r.ScreenPercentage=112（原 66.7）`），但**画面基线 66.7 是游戏动态分辨率**——初版抄渲染对照的「每帧回读+漂移重写」模型，回读到游戏重设值再重写 = 每帧 66.7↔目标 拉锯 = 全部场景渲染目标逐帧重分配，实机亚 1 帧（解绑重试 ~3s 间隔正是 0.3fps 的 tick 周期）；③ `r.ScreenPercentage tagged 覆盖解绑失败` 连绵到退出之后（亚帧率下逐 tick 重试的放大表现）。

**修正（四件）**：
1. **引擎模型改「写一次靠 tag 顶住」**（DOF 闸门同模型）：目标变了才写，不逐帧回读——tagged 覆盖本身顶得住游戏改写；渲染对照的逐帧压回是给「写入不生效」的 cvar 准备的兜底节奏，套在这里就是每帧重分配灾难。
2. **判死闩**：写入回读校验连续失败 3 次 = 该 cvar 写入通道无效（不存在/被游戏钉死），会话级不再尝试不再告警。**r.Tonemapper.Sharpness 实测属此列（tonemapper 家族第 4 项判死，取证补进 tuning.h）**；滑条/配置键保留，游戏更新后按记录重启取证。
3. **告警节流**：写入失败只打前 2 次 + 判死那一次；解绑失败前 2 次 + 每 10 次。
4. **core 失败退避 0.5s**（复用 kDofApplyRetryBackoffSec）+ **超采样滑条拖动预览旁路**（`submit_config_preview` 把 pct 钉在已提交值——每个中间值都是全场景重分配，拖动中按住、松手 commit 才写一次）。

**改动面**：`ue4ss_port_photo_cvars.cpp`（apply_photo_cvar 写一次模型 + 判死闩 + 节流；release 节流）、`ue4ss_port.h`（PhotoCvarSnapshot 加 write_failures/release_failures；photo_cvar_*_dead_ 会话闩；实现签名加 dead 引用）、`photo_mode.h/.cpp`（apply_photo_cvars_if_needed(dt) + 退避 + enter 重置）、`mod_app.cpp`（submit_config_preview pct 旁路，带锁读已提交值）、`tests/main.cpp`（失败重试测试适配退避节奏）。

**测试**：核心层 **7,159 项**全绿（= NLP 并入批的 7,157 + 本批失败重试测试 +2；用分段计数探针逐段审计过：所有段落静态断言数 = 运行时数，字段表 O(n²) 段 4,319 条与字段数 85 精确吻合）；DLL 重建（2,306,048 字节）；发行 `PBZPhotoModeCpp_2.1.0-cpp-look-preview5.zip`（966,968 字节；含并行批次 NLP 并入/F8 换皮/PV 加固的合并态）。

**待复验（下一包）**：① 拖动超采样滑条全程流畅（预览旁路 + 无逐帧拉锯），松手后 112/150 上屏；② 滑条回 100 后 cvar 解绑干净（不再出现解绑失败连绵）；③ 锐化滑条拖动不再刷 Warn（前 2 条后静默，3 次后判死一行）；④ 超采样定格出图链路照常。

## NLP 过场渲染档删除独立开关、并入景深总开关（2026-11-08，工作区变更，版本号发布时定）

**决策依据（用户两向实测 + 代码取证）**：①NLP 关 + 总开关开 → 无任何景深——证伪了门控代码里「NLP 失败不否决，DOF 可能仅靠 ShowFlag/画质闸门就能渲染」的备用假设（与旧四通道结论自洽：pass 只在档位下存在）；②NLP 开 + 总开关关 → 原生过场景深在画面里且参数调不动（上一条目已修为严格跟随）。结论：档位才是 DOF pass 的真开关，独立键的唯一现存作用是把总开关变成空操作（纯混淆源）；取证使命已兑现完毕（11-07 崩溃二分用它排除 NLP 原生所有权处理，第 16 轮原生 setter 才是崩因修复），且 gates-only 对照格已证无测试价值。NLP 的全局副作用（SSS/原生 cvar 重设机器/冻结期原生更新）与 DOF pass 本就不可分割，开关从未提供隔离能力——564 出货形态即「总开关=档位+闸门+组件通道」一体。`dof_bokeh_gate` 保留（只影响散景形状，不产生误导状态）。

**改动面**：`config.h/.cpp`（`dof_nlp_enable` 字段/loader/写出器删除，loader 按十键先例登记 `Deprecated`——旧 config.ini 残留该键报一次性「已弃用：已并入景深总开关」而非「未知配置项」）、`config.ini.example` 重新生成、`pbzfm_schema_map.cpp`（DOF 组 7→6 字段）、四语言（各删 `#dof_nlp_enable`，151→150 键）、`engine_port.h`（DofPort 删 `set_nlp_enabled` 虚函数——内部接口非 PBZFM_Api 导出面，不受尾部追加铁律约束）、`ue4ss_port.h/.cpp`（`nlp_enable_config_` 成员删除，NLP 分支条件 = `need_dof && !active && !exhausted`，注释同步取证记录）、`photo_mode.cpp`（`nlp_owed` 去 cfg 条件；`set_nlp_enabled` 调用删除）、`tuning.h`（2.1 复活块追加 11-08 取证：gates-only 无画面实锤 + 并入决策 + bokeh 保留理由）。

**测试**：核心层 7,258 → **7,157 项**全绿（键级自动断言随键删除收缩 -101；`test_dof_nlp_follows_master` 四场景改写为无独立键语义；PBZFM 景深组字段数断言 7→6）；DLL 重建通过（2,305,024 字节）；`config.ini.example` 与 `test_count.txt` 重新生成。

**行为变化（实机复验点）**：总开关开 = cvar 闸门 + NLP 档 + 组件通道一体（景深真正出画面）；总开关关 = 全无（含原生档位释放）。旧配置里 `dof_nlp_enable=true` 的用户升级后无感（总开关开即有档位）；`dof_nlp_enable=false` 用户升级后总开关开将首次看到真实景深（此前 gates-only 无画面）——若属判死期误开请改回 `dof_enabled=false`。

## F8 菜单换皮：药丸开关 / 自绘滑条 / 框架下拉全量接入（2026-11-08，工作区变更）

**背景**：PBZFM 0.7.0 给框架自身页面做了界面美化（自绘药丸开关、带进度填充滑条、风格化下拉），但摄影菜单设置页一直是裸 ImGui 直调（方框 Checkbox、"开/关"文字按钮、原生滑条/下拉）——客户端页是游戏里的主门面，观感与框架页割裂。框架 0.7.0 已把 `PBZFM_Checkbox` 接到药丸开关、`PBZFM_Combo` 接到风格化下拉，客户端捆绑的 SDK 头 Api 结构里成员早已就位，缺的只是接线与一条带 flags 的滑条导出。

**内容**：
- Bool 行与画面效果独立开关行改走 `api_.Checkbox`（框架药丸开关）；开关行用隐藏标签 `##toggle_<key>` 只画药丸，`EndGroup` 后 `IsItemHovered` 覆盖整组，tooltip 语义不变，原手算按钮宽度的代码删除。
- 滑条改走 v4 可选导出 `api_.SliderFloatEx`（自绘滑条：标签画进轨道、金色进度填充、Ctrl+点击弹输入框；`PBZFM_SLIDER_LOGARITHMIC` 供对焦距离 1..100000cm / 对焦步进两条对数滑条）。旧框架无该导出（可选成员为 NULL）时回退原生滑条并恢复"标签在左"旧行布局。
- Enum / 语言选择 / 风格预设三处下拉改走 `api_.Combo`（标签在轨道内、值在右、鎏金箭头、选中行朱砂条）；语言页 `SetNextItemWidth` 窄下拉给"重扫词典"按钮留位，风格预设逐帧物化 items 指针数组。
- 提交语义不变：拖动实时预览（`submit_config_preview`，不落盘）、松手 `IsItemDeactivatedAfterEdit` 完整提交；框架滑条拖动中 `MarkItemEdited`（框架侧配套），弹窗改值（轨道非活动项）按「changed 且非拖动态即提交」兜底。

**改动面**：`src/pbzfm_client.cpp`（render_language_controls / render_field_widget / look 预设下拉）、`third_party/pbzfm/pbzfm.h`（与框架 sdk 头逐字节同步：`PBZFM_API_VERSION` 3→4、`SliderFloatEx` 成员 + `PBZFM_SLIDER_LOGARITHMIC`，全部尾部追加）。核心层零改动。

**测试**：核心层 7,258 项全绿（控件绘制不进测试编译面）；DLL 重建链接通过。框架侧回归全绿 + ABI 编译守卫通过 + exports 基线检查「0 missing」（框架仓库同期改动，见其 VERIFIED.md）。

**待实机复核**：F8 菜单各页观感与操作——药丸开关点击（含点标签切换与 knob 缓动）、滑条拖动预览+松手落盘、对焦距离/步进对数滑条手感（300~1500cm 可达）、Ctrl+点击输入框、三处下拉与语言切换、风格预设选择；画面效果独立开关的禁用滑条（BeginDisabled 包裹自绘滑条）不得响应拖动。若渲染异常先比对框架 0.7.0 自身页面是否同样异常（区分框架控件 vs 客户端接线）。

## PV 总开关写后回读 + 所有权判定：退出后血条不恢复的防御加固（2026-11-08，工作区变更）

**背景**：实机反馈「进入摄影模式退出后血条 UI 一直处于消失状态」。全量排查结论：进出记账机制本身对称（隐藏/恢复逐条成对、失败保留记账、退避重试覆盖所有标志；`ue4ss_port_hud_ui.cpp` 非本次工作区改动，排除新批次回归），**唯一单点故障是 PV 总开关的恢复 fire-and-forget**——`PBZUIManagerSubsystem::SetHideUIForPV(false)` 调出去就算成功、无状态回读：假成功时 `pv_hide_active_` 清账、`hud_hidden_` 清、重试循环永不启动，而血条等 PV 订阅者（`OnHideUIForPVChanged` 广播）只认这条广播，HUD 根控件回写救不回它们——恰好对应「界面都回来了、只有血条不回来」。mod 564（Soul's Cinematic Studio）`visibility.js` 对同一开关的实现（先读 `IsHideUIForPV` 原值、写后回读、`ownsNative` 所有权判定）是同接口的正确姿势参照。

**内容**：
- 新 helper `read_ui_manager_pv_hidden`：`IsHideUIForPV` 无参 getter 读 `ReturnValue`（`read_return_number`）；getter 不可用（函数不存在/反射读不到）时 `*available=false`，调用方退回旧的 fire-and-forget 语义（行为与加固前一致，不因旧版缺函数而坏）。
- 恢复侧（`set_hud_visible(true)` ①）：写 `SetHideUIForPV(false)` 后回读，仍为隐藏 → 保留 `pv_hide_active_` + `ok=false` 交退避重试（Warn 一次）；回读确认才清账（日志带「IsHideUIForPV 回读确认」）。
- 隐藏侧（`set_hud_visible(false)` 主路径）：**所有权判定**——写之前已是隐藏态且非本 Mod 接管中（游戏自己/其他 mod 藏的）→ 目标已达成但不记账，退出时不写 false，避免把别人的隐藏顺手恢复掉；例外 `pv_hide_active_` 已为 true（上一会话恢复失败的残账）照常重写并保持记账，「重进摄影模式再退出」自愈路保持可用。**写后回读**：确认才记账（返回 true）；回读不一致本轮不算成功且不记账（返回 false 让 core 按秒重试，Warn 一次带「把这条日志发回来」）。

**改动面**：`engine/ue4ss_port_hud_ui.cpp`（helper + 恢复① + 隐藏主路径三处）、`engine/ue4ss_port.h`（`pv_foreign_logged_`/`pv_verify_failed_logged_` 两成员，紧跟 `pv_hide_active_`）。核心层零改动。

**测试**：DLL 重建通过；核心层 7,258 项全绿（与改动前一致，engine 层不进测试编译面）。

**待实机复验**：正常进出摄影模式（日志应出现「IsHideUIForPV 回读确认」）、退出后血条恢复。若血条仍不恢复，日志三选一直接裁决：「回读确认」+血条没回来＝广播后游戏订阅者自身不恢复（另案取证）；「已调用但 IsHideUIForPV() 仍为隐藏」＝恢复侧假成功实锤（重试链路兜底）；「已调用但 IsHideUIForPV() 仍为 false」＝隐藏侧假成功（进模式时血条本就不该消失）。

## NLP 过场渲染档严格跟随景深总开关（2026-11-08，工作区变更，版本号发布时定）

**实机取证（用户报告）**：`dof_nlp_enable=true` 时不开景深总开关，画面里也有随镜头移动的景深，且对焦/光圈参数调不动。

**根因（两处，代码实锤）**：
1. `ensure_dof_gate` 的 NLP 分支只看 `nlp_enable_config_`，**不看 `need_dof`**——胶片（色差）建控也会顺手请求档位。NLP 档是**全局渲染档**，请求成功游戏原生过场景深立即在画面里渲染（游戏自己的对焦逻辑、随镜头移动）；组件通道 `Scale=0` 只压掉本 mod 写的那一份，压不掉游戏原生档位的，本 mod 参数自然调不动。
2. 主开关会话内从开转关（按键 / F8 菜单 live 映射 `pbzfm_schema_map`）只把组件通道 Scale=0，NLP 档按「门控保留至退出」设计一直留着，原生景深继续渲染。

**修正（档位严格跟随景深总开关）**：
- 引擎 `ue4ss_port_dof.cpp`：NLP 分支加 `need_dof` 硬前提（胶片建控绝不开档）；开启方向失败一次即置 `nlp_enable_exhausted_` 会话闩（接口缺失/类缺失/参数不匹配/AV 全是会话内永久性失败，避免每帧解冻重请求；`release_dof_gate` 清零，重进摄影模式重新探测）；新增 `release_nlp_profile()`（自带暂停态解冻窗口，与 `release_dof_gate` 同一纪律；幂等，未开时直接成功）。
- 接口 `engine_port.h` DofPort 尾部追加三虚函数（默认空实现，出包铁律）：`nlp_profile_active()` / `nlp_enable_exhausted()` / `release_nlp_profile()`。
- core `photo_mode.cpp`：①`apply_dof_if_needed` 在参数等值早退**之前**做关向同步——主开关关着且档位还开着就走解冻窗口释放（成功日志「NLP 过场渲染档已释放」），失败按 `kDofApplyRetryBackoffSec` 退避重试，退出路径兜底；②`ensure_dof_ready` 早退条件追加 `nlp_owed`（need_dof + 配置开 + 档位未开成功 + 未 exhausted 时放行补建一次）——覆盖按键 `G` 与 F8 菜单两条主开关转开路径；补建复用同一所有权 token（RF_Standalone 进程内存续，564 同款语义）；③主开关关闭日志措辞同步。

**测试**：核心层 7,225 → **7,258 项**全绿（新增 `test_dof_nlp_follows_master` 四场景：①主开关关+色差建控不请求 NLP（取证复现路径）；②开→关立即释放且只发一次→再开欠账补建；③开启失败置 exhausted 不再重试、参数下发不受影响；④释放失败退避重试不每帧重入）；`config.ini.example` 与 `test_count.txt` 重新生成；四语言 `#dof_nlp_enable` 描述同步（补跟随语义）。

**待实机复验**：`dof_nlp_enable=true` + 总开关关 + 色差任意：进入摄影模式画面应**无任何景深**；总开关开→关：原生景深应随关闭立即消失（日志出现「NLP 过场渲染档已关闭（所有权标记释放）」）。

## 风格预设改版：保存预设 + 下拉套用（2026-11-08，工作区变更，用户决策）

**内容**：接上条「风格预设按钮下线」——「画面效果」组页改为「**保存预设**」按钮 + **预设下拉框**：保存 = 当前 9 风格字段 + 5 开关写入 `<mod>/look_preset_<编号>.ini`（编号 = 现有最大编号 + 1，**绝不覆盖已有预设**；删除 = 去 mod 目录手动删文件）；下拉逐帧枚举全部 `look_preset_*.ini`（外部增删即时反映），显示名「预设 n」，**选中即整表套用**（含开关，滑条跳值就是反馈）。空列表显示占位并禁用；文件为空/无 [look] 段拒绝并提示（防静默套默认值）。文件格式与键名不变（真源 `core/look_preset.h`）。UI 演进全程：5 写死按钮 → 3 固定槽位导入导出 → 本形态，三次均为用户决策。

**改动面**：`pbzfm_client.cpp/.h`（render_look_slots → render_look_presets：新增枚举/编号助手 `list_look_presets`/`next_look_preset_path`/`preset_index_from_path`，保存按钮 + ImGui::Combo（items getter 形式，空表占位禁用），`preset_selected_` 成员尾部追加）、四语言（-3 键 `#preset_export/import/saved`，+4 键 `#preset_save/name/none/saved_to`，`#preset_loaded/missing` 文案改 `%s`，各 151 键）。

**测试**：核心层 **7,225 项**全绿不变（客户端改动不进测试编译面）；DLL 重建通过（2,301,952 字节）。

## 风格预设按钮下线（2026-11-08，工作区变更，用户决策）

**决策**：「画面效果」组页的 5 个风格预设按钮（黑白/电影感/冷调/暖调/褪色）整线移除——写死初值整表套用会覆盖用户手动调好的滑条组合，替代不了微调。**三槽位导入导出保留**（导出/导入自己调好的风格快照，价值独立；`look_preset_<1..3>.ini` 文件格式与键名不变）。已发行的 `look-preview4` 包内仍带这排按钮，下一个包起消失。

**摘除面**：`core/look_preset.h`（kLookPreset*/LookPresetValues/apply_look_preset 删除；look_preset_ini_text/parse_look_preset_ini 保留——槽位文件真源）、`pbzfm_client.cpp/.h`（render_look_presets → render_look_slots，预设按钮循环删除，日志措辞改「风格槽位」）、四语言（各 -5 键 `#preset_bw/cinematic/cool/warm/faded`）、测试（预设表断言删除，槽位往返/坏文件拒绝保留）。锐化/超采样两滑条与 PhotoCvarPort 通道不受影响。

**测试**：核心层 7,251 → **7,225 项**全绿；DLL 重建通过（2,293,248 字节）。

## 锐化/超采样 cvar 通道 + 风格预设与三槽位导入导出（2026-11-08，工作区变更，版本号发布时定）

**内容**（后处理路线图 B 组 + D 组）：
- **锐化**（`[look] sharpness`，0~1.5，0=不覆盖）：`r.Tonemapper.Sharpness` tagged cvar 常驻覆盖，摄影模式内每帧 diff 重申（游戏可能每帧重设渲染 cvar，压回范式照抄渲染对照 render_reassert_tick）。
- **超采样**（`[look] screen_percentage`，100~300，100=不覆盖）：`r.ScreenPercentage` 同通道常驻。**定格出图语义：F7 定格挂起渲染线程后无新帧，必须在定格前把百分比调到 >100，定格住的最后一帧才是高分辨率帧**；`freeze_render_tuning` 目标表不碰该 cvar（已核实无冲突）。
- **风格预设**：PBZFM「画面效果」组页一排 5 按钮（黑白/电影感/冷调/暖调/褪色），**整表套用** 9 个风格字段 + 开关全开（非叠加，任意底表可套）；**不碰锐化/超采样**（渲染管线设置非风格）。真源 `core/look_preset.h`（header-only：预设表/套用/导入导出文本全纯函数，核心层直接测试；避开新建 .cpp 的 CMakeLists 与 build_tests.bat 双源列表手工同步）。
- **三槽位导入导出**：`<mod>/look_preset_1/2/3.ini`（只含 [look] 的 9 风格字段 + 5 开关），组页 3 组导出/导入按钮；导出原子写（临时文件+替换），导入按文件原样套用（含开关）、与导出严格往返；空槽/坏文件拒绝并提示（防「静默套默认值」）。按钮不走 schema（PbzfmField 无「一次写多字段」类型），直接改 draft 走 commit_settings 既有管线（mirror + saver 队列 + submit_config）；**PBZFM_Api/框架零改动**。

**通道实现**：新 `PhotoCvarPort`（engine_port.h 两虚函数，默认空实现，聚合类尾部追加）+ PhotoMode 生命周期（`apply_photo_cvars_if_needed` 值去重下发 / `reassert_photo_cvars_if_needed` 挂 mod_app 渲染前重申链 / attempt_restore_all 逆序解绑 + 退避重试；与 look 同款「进入恢复默认」）。引擎端新文件 `ue4ss_port_photo_cvars.cpp`：每项独立标签（`PBZPM.PC.<n>`，重试复用未解绑标签防泄漏，DOF 闸门同款纪律），写入 `SetConsoleVariableTagged` + 回读校验（读走 `GetConsoleVariableFloatValue` + SEH，dof.cpp 同款防御），中性值即时解绑交还游戏；每帧常态出口 = 目标没变且现值没漂则不写。

**改动面**：`tuning.h`（kSharpness*/kScreenPercentage* 六常量 + 通道/定格语义注释）、`config.h/.cpp`（[look] 两键：schema/解析/reset_look_defaults/config_ini_text；**不进 LookOptions**——cvar 通道与 PPV 通道分离）、`core/look_preset.h`（新）、`engine_port.h`（PhotoCvarPort + 聚合）、`photo_mode.h/.cpp`、`engine/ue4ss_port.h`（override + PhotoCvarSnapshot 成员）、`engine/ue4ss_port_photo_cvars.cpp`（新，CMakeLists 登记）、`mod_app.cpp`（重申链 +1 行）、`pbzfm_schema_map.cpp`（画面效果组 +2 滑条）、`pbzfm_client.h/.cpp`（render_look_presets + 原子写/读小文件助手 + preset_msg_ 成员）、四语言（各 +12 键）。

**测试**：核心层 6,946 → **7,251 项**全绿（config 键默认/夹紧/往返/reset 纳入；photo cvar 生命周期：进入恢复默认不下发、去重、重申门控、中性解绑、失败记账重试、退出还原；预设表逐字段 + 任意底表可套 + 渲染管线键不受影响 + 导入导出往返 + 坏文件拒绝）；DLL 重建通过（2,294,272 字节）；`config.ini.example` 与 `test_count.txt` 重新生成；发行 `PBZPhotoModeCpp_2.1.0-cpp-look-preview4.zip`（961,725 字节；随包 config.ini 为暂存快照不含两新键，装机后新键走中性默认）。

**待复验（preview4）**：
- **锐化画面可见性**——tonemapper 家族有「写入成功 ≠ 画面变化」判死前科（胶片曲线/白平衡/颗粒，见 tuning.h 判死记录），回读校验只证明 cvar 认账不证明渲染器消费；无效则按 hotfix34 先例判死移除。
- 超采样：150/200 实机上屏与掉帧幅度；「定格前开启 → F7 定格 → Win+PrintScreen 系统截屏清晰度提升」全链路。
- 预设 5 按钮套用/滑条跳值/重置手感；三槽位导出导入（含空槽与坏文件提示）；sharpness/screen_percentage 滑条与重置。

## 黑位双向发白：偏移 W 通道修正（2026-11-08，preview3，工作区变更）

**复验发现**：preview2 实机黑位**正负两方向都发白**——推翻 preview1 时「正偏移抬黑发灰=物理行为」的误判（当时只看到了正向）。方向无关 → 嫌疑从值本身转向写入端常量：`set_vector` 对所有 FVector4 恒写第 4 通道 W=1.0，而 ColorOffset 是加算字段（引擎默认 `(0,0,0,0)`），乘算字段（饱和/对比/增益/伽马，默认 `(1,1,1,1)`）写 W=1 无碍，黑位激活时写进的 `(v,v,v,1)` 里那个与方向无关的常量 1 正是双向发白的头号嫌疑（游戏自定义烘焙若消费 W 通道即中招）。

**修正**：`set_vector` 增加 `off_rgb` / `w_value` 参数——乘算字段维持 `(1,1,1,1)` 不变；黑位激活写 `(v,v,v,0)`、失活写回引擎默认 `(0,0,0,0)`。取证结论更新进 `tuning.h`（含 preview1 误判的更正记录）。

**裁决待 preview3**：双向发白消失 → 黑位实锤存活；仍发白 → 游戏自定义烘焙对偏移另有滥用（如 abs()），黑位判死、调色·进阶收成两件（增益/伽马）。核心层 6,946 项全绿不变（纯引擎写入端修正）。

## 调色·进阶实机探针：13 → 3 判死收缩（2026-11-08，工作区变更，版本号发布时定）

**探针（用户实机，UE4SS.log 01:01~01:09 单会话）**：preview1 包逐项拖动验证。写入/回读/POV 诊断三类日志全部按设计工作；裁决矩阵：

| 裁决 | 项 | 证据 |
| --- | --- | --- |
| ✅ 存活（3） | `color_gain` / `color_gamma` / `color_offset` | 用户确认亮度/伽马实时生效；黑位写入回读正常（"变很白"=正偏移物理行为：+抬黑发灰，±0.5 满偏即大幅洗白——非 bug，范围收到 ±0.2） |
| ❌ 字段不存在（4） | ColorCorrectionHighlights/Midtones/Shadows、BloomSoftKnee | 日志 884 行：`这些字段在本 build 里不存在` 一次性 Warn（反射层查无，UE5 裁剪） |
| ❌ 写入成功引擎不消费（6） | FilmSlope/Toe/Shoulder、WhiteTemp/Tint、BloomThreshold | 字段存在、游戏 POV 全部 覆盖0（排除压制）、体积回读正确，但画面零变化：斜率 0.45~2.0 共 975 次覆盖1 写入、趾部 912、肩部 877、色温 2199~12693K 双向 833、色调 774、泛阈值 -0.9~7.9 共 691 次（7.9 足以掐灭一切 bloom 仍无变化） |

**结论**：本 build 的 ColorGrading/tonemapper 管线只消费每通道经典五件（饱和/对比/伽马/增益/偏移）——胶片曲线、白平衡、泛光阈值在 shipping 裁剪掉，与 DOF pass 判死同型（写入成功 ≠ 引擎肯画）。当日按 hotfix34 先例整线移除十项，取证结论写入 `tuning.h` 顶部，游戏更新后可重启取证。

**改动面**：`tuning.h`（十组常量删除+判死注释；黑位范围 ±0.5→±0.2）、`look.h`（十字段+active 删除，`color_grading_active`/`color_pro_active`/`any_active`/`look_options_equal` 同步）、`config.h`/`config.cpp`（十键删除；**十个键名登记为 `Deprecated`**——preview1 用户的旧 config.ini 里残留这些键时报一次性「已弃用」而非「未知配置项」）、`photo_mode.cpp`、`ue4ss_port_camera_view.cpp`（写入/回读收缩；POV 一次性诊断保留三件存活字段，供游戏更新后复查 POV 压制）、`pbzfm_schema_map.cpp`（调色·进阶页 13→3 条滑条）、四语言（各 -10 键，黑位标签补方向提示）、测试同步收缩。

**测试**：核心层 8,135 → **6,946 项**全绿；DLL 重建通过；`config.ini.example` 与 `test_count.txt` 重新生成；发行 `PBZPhotoModeCpp_2.1.0-cpp-color-preview2.zip`（DLL 2,264,064 字节）。

**待复验（preview2）**：黑位负方向压黑手感（±0.2 新范围）；色差/暗角等存量项无回归。

## 调色·进阶 13 项（2026-11-08，工作区变更，版本号发布时定；同日探针后收缩为三件，见上节）

**内容**：画面效果新增「调色·进阶」菜单组，13 个新调色滑条全部接入已验证的自建后处理体积通道（与饱和/对比同一 `bOverride_*` + 值模式），走 `[look]` 段：

| 配置键 | 引擎字段 | 范围（中性=不覆盖） | 语义 |
| --- | --- | --- | --- |
| `color_gain` | ColorGain（FVector4） | 0~4（1） | 亮度增益 |
| `color_gamma` | ColorGamma（FVector4） | 0.25~4（1） | 伽马 |
| `color_offset` | ColorOffset（FVector4） | -0.5~0.5（0） | 黑位 |
| `highlights` / `midtones` / `shadows` | ColorCorrection{Highlights,Midtones,Shadows} | 0~2（1） | 分区调色增益 |
| `film_slope` / `film_toe` / `film_shoulder` | FilmSlope / FilmToe / FilmShoulder | 0.25~2（0.88）/ 0.25~1（0.55）/ 0.05~0.5（0.26） | 胶片曲线（tonemapper） |
| `white_temp` / `white_tint` | WhiteTemp / WhiteTint | 2000~15000 K（6500）/ -1~1（0） | 白平衡（烘进 LUT） |
| `bloom_threshold` / `bloom_soft_knee` | BloomThreshold / BloomSoftKnee | -1~8（-1=引擎自动）/ 0~1（0.7） | 泛光阈值/软膝 |

**关键设计（中性值语义）**：胶片曲线/白平衡/软膝的「不覆盖」值不是恒等值（引擎默认 0.88/6500/0.7，游戏实际调了多少未知）——滑条停在**中性值时不下发 `bOverride_*`**（该项交还游戏），离开中性才写覆盖+绝对值。这与 hotfix33/34 的教训互为对偶：写入成功≠引擎肯画，而「写了中性值」也≠无害。13 项均无独立开关（沿用色差先例：中性即关闭），每次进入摄影模式随 `[look]` 一起恢复默认；`FilmBlackClip/WhiteClip` 明确不做（切点字段风险高收益低）。

**改动面**：
- `core/tuning.h`：13 组 Min/Max/Default 常量（config.cpp schema 与 pbzfm_schema_map 滑条范围的共同真源）+ 中性值语义注释。
- `core/look.h`：`LookOptions` 13 个字段 + `*_active()`（偏离中性超 eps 即活跃）+ `color_grading_active()`（LUT 钉 1 依据）/ `color_pro_active()`；`look_options_equal` 扩全。
- `core/config.h` / `config.cpp`：13 个配置键（schema 行、`config_ini_text` 输出与注释、`reset_look_defaults`）。
- `core/photo_mode.cpp`：`apply_look_if_needed` 透传 13 字段；首启日志增加「画面效果·进阶」行。
- `engine/ue4ss_port_camera_view.cpp`：`set_look` 写入 6 个 FVector4 + 7 个标量；**`ColorGradingIntensity` 钉 1 条件扩展**（白平衡与增益/伽马/黑位/分区同烘进 LUT，任一活跃即钉）；回读段扩 13 项；**新增一次性 POV 诊断**——调色项首次活跃时读游戏镜头 POV 对 13 字段的 `bOverride_*` 标志+原值（`画面效果·进阶：游戏 POV 覆盖标志 …`）。这是通道判据：某项覆盖=1 → 体积写入被游戏镜头 POV 压住（暗角/泛光当年即此因），下轮按 `apply_look_to_camera_pov` 闩锁模式扩 POV 重申通道；全部覆盖=0 → 体积通道直接生效。
- PBZFM：新分组 `#group_color_pro`（调色·进阶），13 条滑条紧跟「画面效果」页；无开关滑条。
- 四语言各 +14 键（组名 + 13 标签）。
- `ue4ss_port.h`：+`look_pov_pro_logged_` 诊断闩。

**不做**：新字段的 POV 重申通道（等探针判据）、FilmBlackClip/WhiteClip、独立开关。

**测试**：`test_config_color_pro_keys`（解析/越界夹紧/往返/reset + 中性值语义钉对：white_temp 6500 不活跃 / 7000 活跃）；`test_color_pro_group_isolated`（13 项全在 `#group_color_pro`、全无开关、默认值落在范围内）；`test_photo_mode_look_defaults_on_enter` 扩（进入归中性 + 实时开关往返）；默认值/解析样例/`default_config_text` 往返同步扩。`switch_fields==5` 断言不动（新字段无开关）。核心层 6,579 → **8,135 项**全绿；DLL 全链构建通过（27/27）。

**实机验证未做**：发布前逐项拖滑条（PBZFM 实时预览），核对三类日志——`画面效果：这些字段在本 build 里不存在…`（字段缺失）、`画面效果：回读 …`（写入确认）、`画面效果·进阶：游戏 POV 覆盖标志 …`（通道判据）；不渲染项按上表归属分诊（泛光族→扩 POV 重申；调色族→按缺失/回读日志定位）。

## 审查修复批次：下线功能残留清零 + 门控 tag 泄漏 + NLP AV 会话闩 + 默认值对齐（2026-11-08，工作区变更）

全量代码审查（~2000 行工作区 diff）后修复的问题，按优先级：

**功能/配置面**：
- `config.ini.example` 删除 `key_pick_confirm` / `key_pick_cancel` 两行——点击取焦整线下线时漏清的残留键，用户照抄会得到「未知配置项已忽略」警告。
- `docs/README.md` 景深章节按当日终局重写（2.1 复活 → 同日判死、代码保留默认关闭、`dof_nlp_enable`/`dof_bokeh_gate` 为取证开关）；键位表删掉中心取焦（T）/ 世界焦点锁定（B）/ 点击取焦（V）/ 胶片颗粒四行，`[keys]`/`[gamepad]` 可配项清单删掉 focus_center/focus_lock/focus_pick/pick_confirm/pick_cancel 与 `pad_focus_pick` 句；`ForceUpdateCameraView` 表述改为「刷新依赖暂停期常规相机更新」。
- **`dof_nlp_enable` 默认值 true → false**：原注释「正常保持 true（564 同款机制）」与同日崩溃二分终局（NLP 冻结期踩同址崩溃家族）及 example(false) 自相矛盾，新生成的 config.ini 会把用户引向崩溃关联路径。config.h 注释、`config_ini_text` 生成注释、example 注释、四语言 `#dof_nlp_enable` 标签全部同步为「默认关闭，判死期间保持关闭，日后复查再开」。

**引擎侧缺陷**：
- **DOF tagged cvar 标签泄漏**：`ensure_dof_gate` 每次重试都 `++serial` 换新标签——上一轮部分建成（某项 gate_write 校验失败）的旧标签已在游戏 tagged-cvar 注册表生效，release 只解绑 active 标签，旧标签的覆盖（如 `ShowFlag.DepthOfField=1`）残留到游戏重启。修复：重试复用同一未解绑标签（同标签重写全部闸门）；解绑成功清空字符串，下次建控用新序号；release 对「非空未 active」的残留标签也解绑；`ensure_dof_gate` 返回不可用前就地解绑部分建成的残留（此时 core 已置 unavailable 闩，退出路径不会再有机会）。
- **NLP AV「本会话不再尝试」落地**：原日志宣称不再尝试但无标志，胶片闸门补建等路径会再次请求 NLP、再次触发 SEH 拦下的访问违例。新增 `nlp_av_disabled_` 会话闩（只闸开启方向；关闭方向必须保留给退出流程重试，否则门控解不干净），告警文案按方向区分。
- **无退避的逐帧重试**：`apply_dof` 持续失败时每帧重入 SEH 写入路径。新增 `kDofApplyRetryBackoffSec=0.5` 退避（`apply_dof_if_needed` 收 dt）；`ensure_dof_ready_impl` 的接管回退/独立相机 spawn 失败分支现在也置 `dof_unavailable_` 闩（原来只有门控缺失分支有），消除每帧 Warn 刷屏。新增 FakeEngine `fail_dof_apply` 开关与退避时序测试。

**注释/死代码清理（下线功能残留）**：
- 删除 `dof_math.h` 零调用者的 `focus_distance_to_point()`（世界焦点锁定残留）及 `math3.h` include。
- 清理孤儿/过时注释：`photo_mode.h`（点击取焦函数注释悬在 `frozen()` 上、`dof_supported_` 旧名、「拾取态状态机」「世界焦点锁定」孤儿注释、dof_frame_tick 的 ForceUpdateCameraView 表述）、`photo_mode.cpp`（tick 注释「中心取焦、焦点锁定、点击取焦」、handle_focus_intent「释放世界焦点锁定」、日志「色差/颗粒」→「色差」）、`ue4ss_port_dof.cpp`（头注释「LineTraceSingle 取焦」、两个空段落标题）、`engine_port.h`（dof_frame_tick 上方挂着中心取焦 raycast 注释、`色差/颗粒`→`色差`）、`tuning.h`（胶片颗粒「2.1 复活」表述改为「复活→再下线」终态）、`mod_app.h`（「进入/取焦共用」）、`pbzfm.h`（CloseMenu 行尾误粘的 IsMenuOpen 注释）。

**计数对齐**：上一条目（下线批次）声称「测试 6,674 项全绿」与实际不符（实测 6,568）——`test_count.txt` 已漂移；本批修复后由 `tools\update_test_count.bat` 重新生成，当前 **6,579 项**全绿（新增退避测试 +11），DLL 重链接通过。

## F7 定格期间 N 卡截屏快捷键判死（2026-11-07，工作区变更）

**现象**：F7 画面定格（挂起渲染线程的真·最后一帧）期间，NVIDIA App/ShadowPlay 截屏快捷键（`Alt+F1`）静默无响应。

**根因（只此一条）**：N 卡截屏捕获管线跑在游戏进程内（`nvspcap64.dll`），必须等到**下一次 `IDXGISwapChain::Present`** 才能抓帧；定格把渲染线程挂起后永远没有新帧 → 捕获请求悬空、快捷键静默失败（等一帧不会来的画面）。排查中排除了另一嫌疑：`nvspcap64.dll` 等 overlay 线程本就在定格跳过名单里（`mod_app.cpp:209-218`，定格是「除跳过名单外全部挂起」，第三方 overlay 线程被保留），线程没被挂起，死在无帧可抓。这是「连渲染线程一起挂起才是真·最后一帧」设计的固有代价，N 卡侧无解；Steam `F12` 等依赖游戏 Present 的截屏键同理。

**替代（已写入两处 README）**：定格期间用系统级捕获——`Win+PrintScreen`（存 `图片\屏幕截图`）或 Xbox Game Bar `Win+Alt+PrtScr`；走 DWM 合成器、不依赖游戏出帧，定格期间可用，画质为桌面合成结果（8bit SDR PNG）。

**备选（已评估、暂缓）**：模组自带「定格截屏」键——定格 watcher 线程在挂起期间本来活着（轮询 F7 恢复 + `RegisterHotKey` 消息兜底，`mod_app.cpp:112-250`），顺带轮询新键位，触发时用 DXGI Desktop Duplication 从 DWM 合成器抓当前屏面（不依赖游戏线程与新 Present，抓到的正是定格帧）。设计要点：新 `[keys]` 键位（评估时默认 F9）、输出 `<mod>/screenshots/`、stb_image_write 写 PNG（`stbi_write_png_to_func` 回调 + `ofstream` 二进制落盘，规避 ANSI fopen 中文路径问题；d3d11/dxgi 入口 `GetProcAddress` 动态解析，与 PBZFM 客户端同款零链接依赖）、HDR 桌面（R16G16B16A16_FLOAT/scRGB）色调映射成 8bit SDR PNG、复制失败退 GDI BitBlt、结果走原子变量由引擎 Tick 恢复后代日志（watcher 定格期间不写日志的既有铁律不动）。用户决策（2026-11-07）：暂不加代码，只补文档；日后要做按此设计直接落地，无需重新推导。

**改动面**：根 `README.md` F7 bullet、`docs/README.md` 键位表 F7 行 + 表下备注、本条目。纯文档，无代码/配置/语言文件/测试变化。

## 点击取焦整线移除 + 中心取焦/焦点锁定下线 + 胶片颗粒下线（2026-11-07，2.1.0）

**决策**：点击取焦（V+光标+LMB/RMB）实测价值不足（取焦射线在实机多次落空）且与「镜头上升」默认绑定冲突
（确认帧漏键），整线移除；随后中心取焦（T，沿视线 raycast）与世界焦点锁定（B，每帧视线投影跟焦）一并下线。
**最终取焦体系 = 纯手动**：对焦距离步进（`,`/`.`，Shift 粗调 Ctrl 微调）+ 光圈步进（`[`/`]`）——可预测、
无射线、无每帧跟焦状态。手柄 pad_focus_center/lock/pick 三个占位键一并摘除（pad_focus_near/far 保留）。

**摘除面（两轮合计）**：`[keys]`/`[gamepad]` 五键加载+写出、KeyMap/Intent 对应字段、DofPort
`set_pick_cursor/hide_pick_cursor/finish_pick_cursor_restore/focus_trace_center/focus_trace_screen` 五个
虚函数与 `dof_line_trace` 助手、光标/捕获快照、photo_mode 拾取机器与焦点锁定每帧重算、
attempt_restore_all 拾取步、PBZFM「点击取焦」按钮与 BeginPickFocus 动作、schema 五键位、
四语言词典对应键。旧 config 残留键自然忽略。测试 6,568 项全绿（test_count.txt 当时写的是 6,674，与实测不符，已于 2026-11-08 审查批次重新生成对齐）。

**胶片颗粒下线（用户决策）**：实机无效果（r.Tonemapper.GrainQuantization 闸门 + 组件 FilmGrainIntensity 全写成功但画面零变化），整线移除——config `film_grain`、DofParams/组件写入、胶片闸门 GrainQuantization、PBZFM 滑条、四语言键；**色差保留**（SceneFringeQuality 闸门与组件写入不动）。旧 config 残留 `film_grain` 键解析时忽略。

**附带收益**：取焦退出路径曾是第 4 处崩溃（SetViewportMouseCaptureMode，crash_2026_11_07_12_40），
整线移除后该风险面清零；LMB 现在只剩「镜头上升」单一语义。

## 崩溃二分终局：DOF 复活判死（2026-11-07，工作区变更）

**矩阵闭合**：三种门控变体全崩、三连逐字节同址（`call rdx`→全零页 0x143DB4F、RSP/RAX 跨会话相同）——
①NLP on+bokeh on ②NLP off+bokeh on ③NLP off+bokeh off（仅 ShowFlag/画质/kernel+组件通道；
末轮按下 W 相机移动后崩）。`dof_enabled=false` 稳定。

**结论**：本作 shipping 的 gameplay 渲染管线里 DOF pass 是被剥离/空壳的路径——cvar 与组件设置都能写入，
但强行使能后渲染 worker 必踩零函数指针（与旧取证「gameplay 档位无 DOF pass」自洽：不止不跑，强跑就崩）；
NLP 过场档（564 同机制）在冻结期同样踩同址崩溃。**DOF/色差/颗粒以当前 C++ 通道形态判死**，
`[dof] dof_enabled=false` 为稳定态；`dof_bokeh_gate`/`dof_nlp_enable` 二分开关保留供日后复查
（游戏更新/找到原生拍照模式 DOF 通道时重启取证）。

**收尾（同日）**：`dof_enabled=false` 后摄影模式卡顿消失——**卡顿与崩溃同根**（均为 DOF 激活的
渲染路径，含 bokeh/NLP 无关变体）。影之刃零无自带拍照模式，无原生 DOF 通道可借。进模式卡顿排查
（当日另一议题）至此终结：卡顿、崩溃、FG 抑制失效三案均已定案。

## 崩溃二分：dof_bokeh_gate 取证开关（2026-11-07，工作区变更）

**结论链**：`dof_enabled=false` 实测崩溃消失 → **崩溃触发源=DOF 栈**（此前第 5/6 次崩溃逐字节同址：
`call rdx`→全零页 0x143DB4F、RSP/RAX 跨会话相同；NLP 开关无关——`dof_nlp_enable=false` 跳过 NLP 后照样崩，
NLP 假设出局）。崩溃线程=自定义小栈 worker（栈内容=步长 0x30 记录区、序列号 0x20 步进递增），
渲染器/驱动邻近形态；bokeh gate cvar（`r.DOF.{Gather,Recombine}.EnableBokehSettings`、
`r.DOF.Recombine.Quality`）开出 shipping 可能无实现的路径 = 嫌疑首位。

**改动**：新增 `[dof] dof_bokeh_gate`（默认 **false**）——false 时整组 bokeh cvar 跳过，DOF 仍走
ShowFlag/画质/kernel 半径闸门+组件通道（散景形状退化为默认圆盘）；true 恢复原行为。写出器同步
（`dof_nlp_enable` 上轮已补）。日志判据：`景深门控：dof_bokeh_gate=false，跳过 bokeh 组 cvar（崩溃二分）`。

**测试矩阵**（全部配合 `dof_nlp_enable=false`）：
- `dof_enabled=true + dof_bokeh_gate=false` → 崩？不崩=bokeh 组实锤，DOF 以「无 bokeh」形态回归；
  崩=嫌疑转移组件通道（下轮再分：seed memcpy / blend 权重 / FocusSettings 写入）。
- `dof_enabled=false` → 稳定基线（已实测）。

## FG 抑制写入通道兜底：exec 失效自动走 tagged cvar（2026-11-07，工作区变更）

**背景**：用户实测进入摄影模式后异常卡顿。冻结期帧生成抑制（`freeze_framegen_off=true`）的两个
cvar（`r.Streamline.DLSSG.Enable=0`、`r.PBZ.SuppressFrameGen=1`）写入后回读不变——DLSSG 全程
保持开启。对照历史：**2026-11-03 实机同一代码、同 before 值写入成功**（CHANGELOG 1.0.76 期日志），
本次 `ue4ss_port_micro_motion.cpp` 未改动，排除代码回归；指向 exec 通道
（`UKismetSystemLibrary::ExecuteConsoleCommand`，本项目已记录其「返回成功但 cvar 不变」实机先例）
在本作**间歇性失效**。

**改动**（实验与修复一体：跑一次游戏日志即可终审判据）：

- `ue4ss_port_internal.h` 新增共享 tagged 通道：`write_cvar_tagged` / `unset_cvars_tagged`
  （走 `PBZBlueprintFunctionLibrary.SetConsoleVariableTagged / UnsetConsoleVariablesWithTag`，
  即 DOF 门控在同一构建实证可用的通道；`seh_process_event` 兜底）。
- 渲染对照写入（`freeze_render_tuning(true)`）：exec 写入回读失败时自动改走 tagged 通道并回读验证。
  每条结果都有判据日志——成功 = `exec 通道未生效，已用 tagged 通道写入（回读已验证）`；
  tagged 也不行 = `exec 与 tagged 通道都未生效（游戏可能在从内部状态重设，写入通道无关）`。
- 快照结构（`RenderCvarSnapshot`）增 `via_tag/tag` 字段：tagged 写入的项在解冻/退出用
  `UnsetConsoleVariablesWithTag` 还原（值自动回到打标签前原值），重申（`render_reassert_tick`）
  按当年写入成功的通道压回。
- 判读：tagged 成功且后续无「渲染对照重申」日志 → 通道问题，FG 抑制恢复有效；tagged 成功但频繁
  重申 → 游戏每帧重写（同标签可赢回，观察卡顿是否随之消失）；tagged 也失败 → 该 cvar 受游戏
  内部状态机管控，需换机制（另行取证）。

## 景深/对焦 + 色差/胶片颗粒复活（2026-11-07，2.1.0）

**一句话**：判死两年的引擎景深、判死一个版本的色差/胶片颗粒，全部复活。缺的从来不是写入通道，
而是两把游戏自带的「渲染档位开关」——机制由 Soul's Cinematic Studio（Nexus #564）在同一游戏构建上
用 JS 实证，本版在 UE4SS 反射层用 C++ 复刻。

### 机制（详见 `core/tuning.h` 顶部改写后的取证结论）

- **NLP 过场渲染档**：`PBZLevelSequenceManager.SetNLPProfileCinematicEnable(瞬态LevelSequence, true)`。
  游戏的「过场渲染配置」，DOF/SSS 等 pass 只在该档位下存在；瞬态序列只作所有权标记、不播放
  （RF_Standalone 防 GC，进程内复用同一个 token，退出用同一 token 关档）。
  旧取证「四条通道写入成功画面零变化」与此完全自洽：gameplay 档位里根本没有 pass 可跑。
- **Tagged cvar 覆盖**：`PBZBlueprintFunctionLibrary.SetConsoleVariableTagged / UnsetConsoleVariablesWithTag`。
  写 `ShowFlag.DepthOfField`（当年反射拿不到的总开关）、`r.DepthOfFieldQuality=2`、
  `r.DOF.Kernel.Max{Fore,Back}groundRadius`、`r.DOF.{Gather,Recombine}.EnableBokehSettings`、
  `r.DOF.Recombine.Quality`；胶片组抬 `r.Tonemapper.Quality=5`、`r.SceneColorFringeQuality`、
  `r.Tonemapper.GrainQuantization`、`r.LUT.Size`、SDR 下 `r.BackbufferQuantizationDitheringOverride`。
  每次写入用 `GetConsoleVariableFloatValue` 回读校验，写不进的项逐条告警（NLP 档位本身可能已带 DOF pass）。
- **画面通道**：spawn 出的相机 Actor 的相机组件 `PostProcessSettings`（权重 1）；首次写入前把管理器 POV
  的 PostProcessSettings 整份 memcpy 过去保留游戏原调色（SEH 兜底，hotfix31 同款）；CineCamera 存在时
  额外写 FocusMethod/ManualFocusDistance/CurrentAperture 元数据；参数变化后 `ForceUpdateCameraView`，
  且 DOF/胶片活动期间每帧强制刷新（世界时钟停住时原生镜头不自动刷新）。

### 功能面

- 景深核心套件：总开关（`G`）、对焦距离步进（`,`/`.`，最小步进= max(配置, 距离×10%)，Shift/Ctrl 变速）、
  光圈步进（`[`/`]`）、中心取焦（`T`，沿视线 LineTraceSingle）、世界焦点锁定（`B`，每帧按视线投影重算，
  手动步进自动解锁）、点击取焦（`V`：显示光标+释放捕获，移动/视角抑制，LMB 反投影取焦/RMB 取消，
  `key_pick_confirm/cancel` 可配）。
- 色差/胶片颗粒：`[look]` 两键复活（0=不覆盖），与景深共用门控与组件通道；每次进入摄影模式随 [look] 恢复默认。
- PBZFM 菜单滑条实时预览：数值滑条（画面五项/色差/颗粒/景深参数/手柄速度）拖动中即推引擎
  （`Host::submit_config_preview`，引擎 Tick 应用、下一帧上屏），松手才写盘+打日志；预览路径不写
  config.ini、不打「已应用」日志，实时开关仍走差异比对、重复应用零副作用。
- **修复：色差/颗粒调节无效果**（2026-11-07 12:40 实机）：`ensure_dof_ready` 早退条件只看
  `dof_gate_active_`——首启建控时色差/颗粒为 0（0=不覆盖），胶片 cvar 闸门
  （`r.SceneColorFringeQuality`/`r.Tonemapper.GrainQuantization`/`r.Tonemapper.Quality`/`r.LUT.Size`）
  整块跳过且同会话永远补不上；之后调大滑条只有组件 PP 写入成功，引擎质量闸门关着 → pass 不渲染。
  现在胶片闸门未建且需要时放行补建一次（impl 幂等，DofPort 新增 `film_gate_active()` 查询）。
- **取焦退出崩溃加固**（12:40:39.457 dump：DEP-EXEC 野跳 0x143DB4F，与 11:20 同族，纯属性直写
  后 ~2 帧游戏原生代码踩野指针，dump 无栈无法回溯）：退出取焦态拆两步——本帧只隐藏光标，
  下一帧 tick 开头恢复鼠标捕获模式，逐步打点（「光标已隐藏」→「捕获模式已恢复」），下次崩溃看
  最后一条日志即可锁定触发写入；`begin_pick_focus`/`attempt_restore_all` 补完挂起的两步还原。
- 后端联动：启用景深/胶片时若在接管模式，当场走 `fallback_from_takeover` 同款路径切独立相机
  （不碰用户的后端配置，下次进入按配置）；景深/胶片生效期间 PBZFM 切回接管会被拒绝并提示。
- 恢复语义：`dof_gate_active_`/`pick_mode_` 进入 `has_pending_restore`，leave 逆序
  （拾取态光标 → 胶片 tag → 景深 tag → NLP 关档），失败走既有 1/2/4/8s 退避重试。
- 诊断：启用后第 8 帧一次性回读 NLP 状态/`ShowFlag.DepthOfField`/`r.DepthOfFieldQuality`/
  `r.DOF.Recombine.Quality`/`r.SceneColorFormat`/`r.SSS.Quality` 与组件 PP 回读值（`景深诊断：…`）。

### 配置与键位

- `[dof]` 段复活：`dof_enabled/dof_focal_distance(cm)/dof_fstop/dof_sensor_width(mm)/dof_focus_step/dof_aperture_step`；
  键位复活并新增：`key_dof_toggle/focus_center/focus_lock/focus_pick/pick_confirm/pick_cancel` +
  原 `key_focus_near/far`、`key_aperture_open/close`；手柄沿用旧默认（LB+BACK 拉近 / LB+START 拉远 /
  RB+BACK 开光圈 / RB+START 收光圈；LB/RB 单按的滚转连带是旧版同款行为），新增键默认 NONE。
- 判死时期（hotfix32/34）进 legacy 清单的键名全部转正：`kLegacyDofKeys`/`kLegacyLookKeys` 清空，
  legacy 机制保留；旧 config.ini 残留这些键时按原值继续生效，不再被清除。
- PBZFM 菜单：`#group_dof`「景深与对焦」节点复活（1 开关 + 5 滑条，距离/步进/F 值为对数滑条），
  `[keys]` 增 10 行键位项，`[look]` 增色差/颗粒两滑条；四语言词典同步（143 键）。

### 工程面

- 新文件：`src/core/dof_math.h`（DofParams/视线投影/比例步进/clamp，全纯函数）、
  `src/engine/ue4ss_port_dof.cpp`（DofPort 实现：门控/组件通道/取焦/光标/诊断）。
- `engine_port.h` 新增 `DofPort` 子接口（ensure/release gate、apply_dof、两条 trace、set_pick_cursor、
  dof_frame_tick），`EnginePort` 聚合继承；`PhotoMode` 新增 apply_dof_if_needed / handle_focus_intent /
  ensure_dof_ready / exit_pick_mode / dof_frame_tick。
- `mod_app` 的 GameViewportClient Tick 之后写点新增 `mode_->dof_frame_tick()`。
- 测试：`test_config_legacy_purge` 改写为 `test_config_revived_dof_keys`（复活键解析/夹紧/往返/reset 语义）；
  `test_dof_group_isolated` 反转为正向断言（6 个 dof 字段全在 #group_dof、对数标志、色差颗粒归 look 组）；
  `default_config_text` 往返断言更新。核心层 5,329 → **7,058 项**全绿（PBZFM 字段循环随字段数增长 + 景深生命周期 2 案）。

### 实机修复记录

- 2026-11-07 第一次实机（UE4SS.log 01:21 段）：菜单开关/后端切换/tagged cvar 接口全部正常，
  卡在 NLP token 类路径——`/Script/MovieScene.LevelSequence` 在本作不存在（ULevelSequence 属于
  LevelSequence 模块）。已改为 `/Script/LevelSequence.LevelSequence` 为主、MovieScene 兜底。
- 2026-11-07 第二次实机崩溃（crash_2026_11_07_01_37_35.dmp，UE4SS.log 01:37 段）：NLP 开启调用返回
  后 ~1ms 崩在 PBZ.exe+0x43AE0D4 的虚表调用（`call [rax+0x660]`，读 -1）——时间线与游戏原生更新
  处理 NLP 所有权请求吻合。本轮三项应对：
  1. token 的 Outer 从 UWorld 改为 **GameInstance**（与 564 的 native.js `context()` 完全一致，
     这是 564 与我们仅剩的构造差异）；
  2. 门控顺序重排：tagged cvar 闸门全部先行，NLP 档最后开——崩溃若再发生，闸门已生效且死点唯一；
  3. 新增取证开关 `[dof] dof_nlp_enable`（默认 true）：设 false 可实测「仅 cvar 闸门」是否足以让
     DOF 出画面；首启逐步打点（写 DOF 闸门 → 写胶片闸门 → 请求 NLP 档）便于定位。
- 2026-11-07 第三次实机仍同址崩溃（crash_2026_11_07_01_51_00.dmp：RIP/RCX/R8 与上次完全一致），
  排除 Outer 因素（已对齐 564 的 GameInstance，dumpstack 复核 `c` 即 GameInstance）。剩余唯一
  结构差异是调用时序：564 在世界运行时开 NLP（先开档、最后才 SetGamePaused），我们在进入即冻结
  后才开档——游戏原生更新在暂停态处理所有权请求会崩。本轮修复：ensure_dof_ready 拆出 impl 并在
  外层包「解冻 → 建门控 → 恢复冻结」窗口；release_dof_gate 的 NLP 关档同样在解冻窗口内进行。

- 2026-11-07 第四轮（crash_2026_11_07_02_02_17.dmp）：解冻窗口生效——NLP 在世界运行时成功开启、
  重冻结完整跑完（「时间已冻结」）后 0.6ms 仍崩，且三份 dump RIP/RCX/RAX 完全一致。结合
  PBZ.exe 仅一个 174MB 的 .idata 节（加壳保护，静态反汇编不可信），把嫌疑从「NLP 时序」转移到
  「NLP 之后的下一步原生调用」（apply_dof 的 ForceUpdateCameraView 是唯一执行游戏代码的
  ProcessEvent）。本轮把景深链路的全部原生调用（cvar 读/写、tagged set/unset/has、NLP 开关、
  ForceUpdateCameraView）统一 SEH 罩住（detail::seh_process_event，注释记载本作在 MPC 场景
  出过同款「读 -1」崩溃），崩溃变成日志；NLP 失败不再否决门控（任一组闸门生效即继续）。
  ForceUpdateCameraView 被拦截后自动停用，相机刷新由暂停期常规更新兜底。

- 2026-11-07 第五轮（crash_2026_11_07_02_12_21.dmp）：SEH 立功——「ForceUpdateCameraView 触发访问
  违例（已拦下并停用）」，**三次同址崩溃的元凶实锤**；拦下后景深全链路正常（诊断行 showflag=1、
  组件 FOC/F/scale 全部写入），用户实测了 G 开关与点击取焦（光标→LMB→「焦点已获取，景深已开启」），
  最后崩在 exit_pick_mode 的 SetViewportMouseCaptureMode 恢复（PBZ.exe+0x3CE8432，与前三处不同址）。
  本轮把 DOF 模块剩余的全部裸 ProcessEvent（Get/SetViewportMouseCaptureMode、GetMousePosition、
  DeprojectScreenPositionToWorld、LineTraceSingle、K2_GetActorLocation）统一 SEH 罩住，捕获模式恢复
  失败时放弃还原（重进摄影模式复位），不再可能带崩游戏。

- 2026-11-07 配套框架更新：PBZ-FM 新增 v3 可选导出 `PBZFM_CloseMenu`（g_closeRequest 延迟到帧边界，
  页面渲染回调内可安全调用）。本 Mod 的 PBZFM「景深与对焦」组新增**「点击取焦」按钮**：点击即关菜单
  并在引擎线程进入取焦态（与 V 键同一条 `begin_pick_focus` 路径，含幂等/门控前置/降级日志）；
  旧框架上按钮退化为提示文案（需更新框架）。四语言 +51 键位测试全绿；`PbzfmAction::BeginPickFocus`
  经既有命令队列消化（mod_app 引擎线程），未激活时静默忽略。

- 2026-11-07 第六轮（crash_2026_11_07_10_45_55.dmp）：**从 PBZFM 菜单进入摄影模式崩溃**——链路全部
  正常跑完（含被拦下的 ForceUpdateCameraView），崩溃在其后 1.7ms，同族 -1 读（PBZ.exe+0x3CD96FC）。
  与 run 4（F2 进入、存活 7 秒）的唯一差异是菜单开着；框架 release 构建（无引擎后端）已排除框架侧
  调用。修复：`PbzfmAction::ToggleMode`/`BeginPickFocus` 在菜单可见时**先 request_close_menu 并把
  命令推迟一帧**（重试上限 10 次，超限告警后照旧执行），使菜单进入与 F2 进入完全同态；
  PbzfmClient 新增 menu_open()/request_close_menu()。

- 2026-11-07 第七轮（crash_2026_11_07_11_20_08.dmp，PBZFM 已升 v0.7.0）：菜单→进入→DOF 开关→
  「点击取焦」按钮→自动关菜单→光标显示→LMB trace 未命中告警——**全链路第一次完整走通**；崩溃在
  取焦未命中退出取焦态 ~2 帧后（EXEC 到加壳区，垃圾函数指针）。恢复鼠标捕获模式改走
  `UGameViewportClient.MouseCaptureMode`（0x3BA）**直接属性读写**——GameplayStatics 那对函数内部
  就是读写它，直写不执行任何游戏代码；取焦流程（显示/恢复光标+捕获）自此零游戏原生调用。

- 2026-11-07 第八轮（UE4SS.log 11:19 段，无崩溃，功能缺陷）：**画面效果重置/开关按钮画面不动**——
  滑条拖动全程可见（暗角 0.57→0.37 泛光 12.20 都进了画面），重置/开关后体积回读全对
  （`回读 暗角=0.00(覆盖0)`）但画面停在旧值。根因：暗角/泛光的**相机 POV 重申通道只开不关**——
  定格/独立相机下 POV 不被 UpdateCamera 重建（这正是该通道需要每帧重申的原因），`apply_look_to_camera_pov`
  在 `any_active()==false`（或两项同时失活）时早退停止重申，上一帧写进 POV 的「bOverride=1+旧值」
  永久残留，把已清零的体积压住；体积回读恰好是诊断盲区，把问题盖住了。修复：POV 通道加**残留闩锁**
  （`pov_vignette_latched_/pov_bloom_latched_`），失活后继续把 bOverride 回写成 false（写成功才落闩，
  失败下帧重试），两个闩锁都落下才恢复早退；撤残留首见打一行 Info 日志补上诊断盲区。
  饱和/对比/冷暖三项一直只走体积、无 POV 通道，不受此 bug 影响。

### 实机验证清单（发布前必须跑）

1. 接管模式进入 → 按 `G`：日志应有「已切换到独立相机后端」「NLP 过场渲染档已请求开启」、闸门写入无
   「写入未生效」告警、第 8 帧 `景深诊断` 行；小 F 值 + 近对焦距离画面变糊。
2. `,`/`.`/`[`/`]` 步进；`T` 中心取焦；`B` 锁定后移动相机焦点跟随；`V` 点击取焦（光标出现/消失）。
3. `[look]` 色差/颗粒非零：画面出现色散/颗粒，回读 Tonemapper.Quality=5。
4. 退出摄影模式：世界正常、NLP 关档日志、config.ini 不被重写；暗角/泛光（体积通道）不受影响。
5. 若不糊：收集 `景深诊断` 行与 `闸门 … 写入未生效` 告警定位（档位/总开关/画质档三者谁被改掉）。
6. 第八轮回归：菜单里把暗角/泛光拉到明显值 → 点重置/开关关掉 → **画面必须立刻回默认**
   （定格状态下也要生效）；日志出现一次「已把相机 POV 上的残留覆盖回写为 覆盖0」。

## 2.0.4-cpp（2026-11-06）

**修复：退出摄影模式后无法移动视角（输入锁残留）。**

### 根因

- UE 的 `AController::SetIgnoreLookInput` / `SetIgnoreMoveInput` 是**堆叠计数**（每次调用 +1/-1），
  不是布尔开关；并且 `IgnoreLookInput` / `IgnoreMoveInput` 是 `uint8` **非 UPROPERTY**，反射层读不到计数。
- 2.0.3 及以前进入摄影模式时固定调用一次 `SetIgnoreLookInput(true)`，退出时只配对一次
  `SetIgnoreLookInput(false)`。如果进入前游戏自己已经持有一把视角锁（暂停/过场/菜单等场景），
  计数会从 2 变 1，退出后 `IsLookInputIgnored()` 仍为 true，表现就是**角色能走但鼠标完全转不动视角**。
  `SetIgnoreMoveInput` 的堆叠计数同理，严重时角色移动也会被卡住。

### 修复

- 进入屏蔽前先读 `IsLookInputIgnored()` / `IsMoveInputIgnored()` 记录原始布尔锁状态；
  退出时按 `plan_ignore_lock_restore()` 校正：
  - 进入前未锁、退出仍锁 → `ResetIgnoreLookInput()` / `ResetIgnoreMoveInput()` 清掉全部残留；
  - 进入前已锁、退出被减成未锁 → 补回一把 `SetIgnore*(true)`；
  - 读不到锁状态 → 直接 `Reset*` 兜底，绝不把玩家留在“视角卡死”状态。
- 校正动作写入日志（`恢复输入屏蔽：Look 锁残留已校正（进入前=未锁，退出时=锁定）`），
  下次实机日志可直接确认命中；进入时若游戏本来就持有锁也会记录一行。
- 核心层新增 `plan_ignore_lock_restore` 的 4 组真值表断言（5,325 → **5,329 项**全绿）。

## 2.0.3-cpp（2026-11-06）

**画面效果操控细化：每个滑块独立重置 + 画面效果五项独立开关，且五项每次进摄影模式恢复默认。**

### 菜单

- 所有数值滑块（相机与移动 / 画面效果 / 手柄等）行尾新增**独立「重置」按钮**：单击把该滑块恢复为
  编译期默认值（默认值直接由 `Config{}` 读取，不在菜单里另抄一份），并立即写回 `config.ini`。
- 「画面效果」五项（暗角 / 饱和度 / 对比度 / 泛光 / 冷暖）行首新增**独立开关按钮**（开 / 关）：
  - 关 ≠ 清零：只停用这一项，滑块值保留；再打开立刻恢复原值，方便 A/B 对比；
  - 重置按钮会把该行恢复为默认值并重新打开开关；
  - 开关状态持久化在 `[look]` 段的 `vignette_enabled` / `saturation_enabled` / `contrast_enabled` /
    `bloom_enabled` / `temperature_enabled` 五个布尔键里（默认全 true）。
- 画面效果页顶部新增提示：这五项每次打开摄影模式都会恢复默认参数。

### 进入摄影模式时恢复默认

- `PhotoMode::enter()` 在进入成功前调用 `reset_look_defaults()`：五项数值归中性值
  （暗角 0 / 饱和 1 / 对比 1 / 泛光 0 / 冷暖 0），五个独立开关归默认开。
- `mod_app::handle_toggle()` 在进入成功后把同一份默认值写回 `config.ini` 并调用
  `PbzfmClient::sync_from_config()` 刷新菜单显示——不会出现「引擎已用默认值、菜单还显示上次拖的值」。
- 进入失败时撤回预置默认值，保持用户原来的菜单设置不变。

### 配置与兼容

- `[look]` 新增五个 `*_enabled` 键，全部随 `config_ini_text()` 序列化 / 解析 / 原子写回；
  旧 `config.ini` 没有这些键时按默认 true 处理，无需手动迁移。
- 四语言词典新增「重置 / 开 / 关 / 开关提示 / 五项默认提示」，并修正「画面效果（暗角）」组名
  （组内实际是五项，不再只写暗角）。

### 测试

核心层 5,155 → **5,325 项**断言全绿（新增：进入摄影模式时 [look] 五项恢复默认值 + 首次 tick
下发中性 LookOptions；所有 Float 滑块默认值重置；五项独立开关读写与重置；配置解析 / 序列化往返）。

## 2.0.2-cpp（2026-11-06）

**两处实机问题收口：处决/过场中暂停后的角色定格，以及超宽屏暂停变 16:9。**

### ① 处决动画中进入摄影模式 → 退出后角色模型卡住

现象：处决/过场演出中按 F2 进入摄影模式（自动暂停）再退出，角色模型保持定格，
只能重启游戏；有时暂停本身也会让角色随机冻住。

根因是「演出瞬时状态被当成进入前的原值」：

- `AWorldSettings.TimeDilation` / `CinematicTimeDilation` 在处决演出里会被压到接近 0
  （Sequencer slomo / 命中停顿），原代码只在 `<= 0` 时兜底，`0.0005` 这类值会被存成
  “进入前倍率”，退出时写回去 → 角色永久慢动作/定格；
- 演出还会给玩家角色的 `AActor.CustomTimeDilation` 和 `SkeletalMeshComponent.bPauseAnims` /
  `bNoSkeletonUpdate` 留下暂停状态；演出被摄影模式暂停后走不到自己的收尾，这些值就永久保留。

修复：

- `PhotoMode::enter()` 与 `toggle_freeze()` 统一把 `< 0.1` 的时间倍率视为演出瞬时值，按 1.0 还原；
- 退出摄影模式或按 F 解冻时，引擎侧 `recover_character_after_photo_mode()` 只对玩家 Pawn
  （及其骨骼网格）清理 `CustomTimeDilation`、`bPauseAnims`、`bNoSkeletonUpdate`，并清理
  `AWorldSettings.CinematicTimeDilation` / `TimeDilation` 的接近零异常值；场景里原本静止的
  NPC/物件不动。清理幂等，恢复失败的后台重试也会重复执行。

### ② 21:9 超宽屏每次暂停随机变 16:9，退出也回不来

根因：进入摄影模式时若 `bDefaultConstrainAspectRatio` 正好是 false（正常游玩超宽屏状态），
旧实现认为「没锁，无需解锁」而不保存原值；随后 `SetPause`（暂停菜单/处决演出）把画幅锁
打开，摄影模式里变成 16:9。退出时因为没保存过原值，`restore_aspect_ratio()` 直接返回，
16:9 锁永久留下。

修复：

- 进入时无论 `bDefaultConstrainAspectRatio` 是 true 还是 false 都保存原值和比例；
- `PhotoMode::reassert_aspect_ratio_if_needed()` 每帧在 `GameViewportClient Tick` 之后、
  渲染之前把锁压回 false（`mod_app.cpp` 接线），暂停/演出中途再锁也能立即恢复超宽；
- 退出时按进入前状态还原：原本就是超宽 → 写回 false；若进入时的 16:9 锁来自正在播放的
  过场/处决，则视为演出瞬时锁，不当作永久状态写回。

### 测试

核心层 5,133 → **5,155 项**断言全绿；新增超宽屏每帧重申、处决/过场残留恢复、接近零时间
倍率不写回、恢复失败重试等用例。

## 2.0.1-cpp（2026-11-06）

**PBZFM 菜单打开期间暂停摄影模式输入** —— 修复「在菜单里按鼠标左键，相机触发上升」。

根因：PBZFM 的覆盖菜单与游戏在**同一个进程**里，`Win32InputSource::foreground_is_ours()` 只比对
`GetForegroundWindow()` 的进程 PID，所以菜单打开时它仍然返回真；而鼠标键走 `GetAsyncKeyState`
全量轮询（VK 1..255），菜单里的左键直接命中 `key_vertical_up`（默认 `MOUSE_LEFT`）→ 相机上升。
同理，菜单里的鼠标移动/滚轮也会转相机、改 FOV。

本版每帧向框架查询 `PBZFM_IsMenuOpen`（v1+ 导出），菜单可见期间把输入源置为「全部未按下」：
键盘、鼠标移动、滚轮、手柄一起停；菜单关闭后恢复。关闭后的首帧只同步物理键状态、不产生按下
边沿（用鼠标点关闭按钮不会让相机跳一下）。框架未安装或版本过旧（无 `PBZFM_IsMenuOpen`）时
行为与之前完全一致。

核心层测试 **5,133 项**全绿；DLL 编译 exit 0。

## 2.0.0-cpp 正式版（2026-11-05）

2.0.0-cpp-hotfix1 ~ hotfix35 的全部成果收进一个**不带 hotfix 后缀**的版本号，作为 2.0 线的正式发布版。
日志与 UI 里的版本串从此就是 `2.0.0-cpp`，发行包 `PBZPhotoModeCpp_2.0.0-cpp.zip`
（`PBZPM_HOTFIX 35 → 0`，编译期宏自动去掉 `-hotfixNN` 后缀）。

### 功能总览

**摄影模式本体**

- F2（或手柄 LS+RS）进出摄影模式；世界冻结（硬暂停 / 时间缩放两条路）、帧冻结、本体隐藏，
  HUD / 交互提示 / 血条 / 锁定白点 / 字幕逐项隐藏，退出时全部按原值还原。
- 相机：位姿 / 朝向 / roll / FOV / 垂直位移**全部键位可配**（默认 WASD + 鼠标，Q/E 滚转，
  方向键与十字键上下）；`Shift` 粗调、`Ctrl` 微调；按住连发（0.35s 后 12.5 格/秒，掉帧补格）。
- 两条相机后端：**接管当前相机**（UUU 式，每帧写 `CameraCache.POV`）或 spawn 独立 CineCamera；
  接管失败自动回退，注销 / 换图时作废接管与后处理快照。
- 画幅解锁（超宽）、CineCamera 焦距联动、出图通道。
- 多语言：中 / 英 / 日 / 韩（`languages/*.json`，可自行改文案，DLL 不内嵌文本）。

**画面效果（`[look]` 段，五项全部实机验证生效）**

| 项 | 键 | 范围 | 验证 |
| --- | --- | --- | --- |
| 暗角 | `vignette` | 0~4 | 拖到 2.17 见四周变暗 |
| 饱和度 | `saturation` | 0~2（1=原样） | 0.16 明显去色；修好 `FVector4` 的 X/Y/Z/W 后确认 |
| 对比度 | `contrast` | 0~2（1=原样） | 同上 |
| 泛光 | `bloom` | 0~20（0=不覆盖） | 1.5~16 明显 |
| 冷暖 | `temperature` | -1~1（0=不覆盖） | ±0.94 可见 |

全部写进**自建的无界高优先级后处理体积**（本作 gameplay 视图里唯一被证实能出画面的后处理通道）；
中性值即把 `bOverride_*` 置回 false 交还游戏；退出摄影模式还原。

**PBZFM 图形菜单（可选）**

装 [PBZ-Framework-Menu](https://github.com/lin-414/PBZ-Framework-Menu) 后可在游戏内实时调参并写回
config.ini；不装则纯快捷键，零依赖。

### 明确不做的四件事（都试过，取证在案）

| 项 | 结论 | 证据位置 |
| --- | --- | --- |
| 引擎景深 / 手动对焦 | 本作 gameplay 视图不渲染引擎景深：四条通道（相机 POV / 自建体积 / 关卡体积 / ViewTarget.POV）全部写入成功、画面零变化。**功能已整体删除**（hotfix32） | `core/tuning.h` 顶部 |
| 色差 | `r.SceneColorFringeQuality=0`，且该 cvar 优先级高于任何游戏设置级写入，console 命令在本作不执行。**滑条与配置键已删**（hotfix34） | 同上 |
| 胶片颗粒 | 与色差同一档位；补 `FilmGrainTexture` 噪声图 + `r.FilmGrain=1` + 强度 6.85 仍无颗粒。**已删**（hotfix34） | 同上 |
| 引擎内真图形菜单 | ImGui / D2 覆盖窗路线六连判死，只保留 PBZFM 外部菜单 | PROGRESS v1.6.4 |

### 安装

1. 解压 `PBZPhotoModeCpp_2.0.0-cpp.zip` 到 `<游戏目录>/platform/steam/ue4ss/Mods/`
   （得到 `PBZPhotoModeCpp/` 目录，含 `dlls/PBZPhotoModeCpp.dll`、`languages/`、`config.ini`、`enabled.txt`）。
2. 无额外依赖：游戏自带 UE4SS 3.0.1。
3. 首次进游戏会按需生成 / 清理 `config.ini`：旧版残留的 `[dof]` 段、色差、胶片颗粒键会被**原子重写**
   清掉，日志一行 `配置清理：已从 config.ini 删除 N 个已废弃键并重写文件（…）`。

### 数字

- 核心层测试 **5,133 项**全绿（当前值见 `docs/test_count.txt`，由脚本生成）
- DLL **2,155,008 字节**；显示名 `PBZ Photo Mode (C++) 2.0.0-cpp`
- DLL 内 `2.0.0-cpp` 出现 2 次，且已**搜不到任何 `hotfix` 字样**（正式版号干净）

## 2.0.0-cpp-hotfix35 旧配置真清理：命中废弃键就重写 config.ini

hotfix34 把色差/胶片颗粒整条删了，但旧 `config.ini` 里残留的键只是「提示一次然后忽略」——文件里
那几行还在，还得用户自己手动删。用户定调：「config.ini 不会留着旧键，都清干净一点」。

本版改成**物理清理**：

- `ConfigLoadResult` 新增 `legacy_keys`：解析时命中 `kLegacyDofKeys` / `kLegacyLookKeys` 里的键就
  按原样记下来，不再只是打一条「请自行删掉」的提示。
- `PhotoModeMod::reload_config()` 拿到非空 `legacy_keys` 后，用当前规范格式**原子重写一次
  config.ini**（`save_config_file`：先写 `.tmp` 再 rename，读盘方永远读到完整文件），把 `[dof]` 段、
  `key_focus_*` / `key_aperture_*` / `pad_focus_*` / `pad_aperture_*`、`chromatic_aberration`、
  `film_grain` 从文件里彻底删掉，并打一行：

```
配置清理：已从 config.ini 删除 5 个已废弃键并重写文件（dof_enabled, dof_fstop, chromatic_aberration, film_grain, …）
```

- 告警文案同步改成「已忽略并将从 config.ini 清理」，不再让用户自己动手。
- **只在真的命中废弃键时才写盘**：干净的配置不会每次启动都白重写一遍（有单测盯着这一条）。
- 重写用的是规范序列化 `config_ini_text()`，所以文件里同时也不会有任何未知键/拼错的键残留
  —— 一次清理，文件就是标准格式。

### 测试

核心层 5,124 → **5,133 项**全绿：新增 `test_config_legacy_purge` —— 5 个废弃键全部收集、只按组告警
（不逐键刷屏）、重写文本里没有 `[dof]` / `chromatic_aberration` / `film_grain`、有效值（`vignette = 2`）
保留、干净配置再次解析不会触发清理。

## 2.0.0-cpp-hotfix34 色差与胶片颗粒整条删除（菜单 / 配置 / 标签 / 引擎写入）

hotfix33 已经把这两项判死并标了「本作不支持」，但滑条还在菜单里、配置里还留着键。用户定调：
**按景深那套做法整条删掉**——不会出画面的滑条，留着就是误导。

### 删了什么

| 层 | 内容 |
| --- | --- |
| PBZFM 菜单 | 「画面效果」组里 `色差` 与 `胶片颗粒` 两条滑条（`pbzfm_schema_map.cpp` 的 `Float(kLookGroup, …)` 两项） |
| 配置 | `[look]` 段的 `chromatic_aberration` / `film_grain` 两键（`Config` 成员、schema 表、`default_config_text()` 输出全部去掉） |
| 词典 | 四语言的 `#chromatic_aberration` / `#film_grain` 两个标签键 |
| 引擎 | `set_look()` 里 `SceneFringeIntensity` / `FilmGrainIntensity` 的写入、`ChromaticAberrationStartOffset=0` 的兜底、下发/回读日志里的这两个字段，以及 `log_look_gates()`（取证行已改由 `core/tuning.h` 的注释承担） |
| 结构 | `LookOptions` 的 `chromatic_aberration` / `film_grain` 两个字段与对应 `*_active()`、`any_active()`、`look_options_equal()` 判定 |

### 旧配置兼容

沿用 `[dof]` 的处理方式：`parse_config` 新增 `kLegacyLookKeys`，命中时合并成**一条** warning 后静默忽略：

```
配置里的色差/胶片颗粒已移除（本作这两个后处理 pass 被引擎档位钉死，永远不会出画面），已忽略；
可以把 [look] 里的 chromatic_aberration / film_grain 一并删掉
```

不会出现「未知配置项已忽略」刷两行。

### 现在 `[look]` 只剩五项，全部实测生效

| 项 | 状态 |
| --- | --- |
| 暗角 `vignette` | 生效（拖到 2.17 见四周变暗） |
| 饱和度 `saturation` | 生效（hotfix32 修好 `FVector4` 的 X/Y/Z/W 成员名后确认） |
| 对比度 `contrast` | 生效（同上） |
| 泛光 `bloom` | 生效（1.5~16 明显） |
| 冷暖 `temperature` | 生效（±0.94 可见） |

取证结论（为什么色差/颗粒在本作不可能做出来）已完整写进 `core/tuning.h` 顶部，包括
`r.Tonemapper.Quality=2` / `r.SceneColorFringeQuality=0` 的档位依据、抬档被 cvar 优先级丢弃、
console 命令不执行、UE4SS 无 `IConsoleManager`、以及补噪声纹理后颗粒仍不出现 —— 后来者不用重走。

### 测试与打包

核心层 5,291 → **5,124 项**断言（少掉的 167 项是字段表/校验类用例里这两项的覆盖），全绿。
`config.ini.example` 的 `[look]` 段现在正好五项。

## 2.0.0-cpp-hotfix33 胶片颗粒判定「本作不支持」，删掉那套纹理补丁

hotfix32 的实机反馈：**饱和度/对比度生效了**（日志 `回读 … 饱和=1.01(覆盖1) 对比=…`），
但胶片颗粒仍然没有。

日志把颗粒那套尝试的每一步都记下来了：

```
画面效果：胶片颗粒纹理 = Texture2D /Engine/EngineMaterials/Good64x64TilingNoiseHighFreq.Good64x64TilingNoiseHighFreq（命中关键词 noise）
画面效果：回读 … 颗粒=6.85(覆盖1) …
```

结论很清楚：噪声纹理挂上了、`FilmGrainTexelSize=1` 写了、`r.FilmGrain=1`（pass 总开关是开的）、
强度一路拉到 6.85，画面里一粒颗粒都没有。颗粒 pass 与色差 pass 同属被引擎「色调映射档位」钉死的
那一档（`r.Tonemapper.Quality=2`、`r.Tonemapper.GrainQuantization=0`），而本作把这个值钉在比任何
游戏设置级写入都高的优先级上（hotfix31 抬档实测无效，见 hotfix32 复盘）。

### 本版改了什么

- **删除**颗粒那套纹理补丁：`attach_film_grain_texture()`（进模式时全场景扫 `Texture2D` 挑
  grain/noise 贴图）+ `film_grain_texture_` / `film_grain_scanned_` 两个状态成员 +
  `FilmGrainTexture` / `FilmGrainTexelSize` 的写入。它已经被实测证伪，留着只会每次进模式
  扫一遍全场景纹理。
- 颗粒强度（`FilmGrainIntensity`）照常写入 → 与色差一致，按「参数记录」保留；
  **菜单标签四语言标注「本作不支持」**，`config.ini.example` 的 `[look]` 注释同步改写。
- `log_look_gates()` 保留为取证行：
  `画面效果诊断（画面效果首次写入）：r.Tonemapper.Quality=… r.SceneColorFringeQuality=… r.Tonemapper.GrainQuantization=… r.FilmGrain=…`，
  以后换引擎版本一行就能复核。
- 色差那条 `ChromaticAberrationStartOffset=0` 的写入保留（同样只在引擎肯画时才起作用）。

### `[look]` 七项的真实状态（本版收口）

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 暗角 `vignette` | **生效** | 拖到 2.17，四周变暗 |
| 泛光 `bloom` | **生效** | 1.5~16 都明显 |
| 冷暖 `temperature` | **生效** | ±0.94 可见 |
| 饱和度 `saturation` | **生效** | hotfix32 修好 X/Y/Z/W 成员名后确认 |
| 对比度 `contrast` | **生效** | 同上 |
| 色差 `chromatic_aberration` | 不支持 | `r.SceneColorFringeQuality=0`，钉死在更高优先级 |
| 胶片颗粒 `film_grain` | 不支持 | 纹理挂上、强度 6.85、`r.FilmGrain=1`，仍无颗粒 |

要像景深那样把这两条从菜单/配置里整条删掉，说一声即可。

### 测试与打包

核心层 **5,291 项**全绿（本版无新增用例：改的全是引擎层写入与文案）。

## 2.0.0-cpp-hotfix32 景深功能整体移除（菜单 / 配置 / 按键 / 引擎写入 / 核心状态机）+ 画面效果两处真 bug 修复

hotfix25 就已经确认「本作 gameplay 视图不渲染引擎景深」，但当时只标了「本作不支持」，把滑条与配置
留作「参数记录」。这次把话说透：**一个不会产生任何画面变化、也不会被任何功能消费的滑条，
留在菜单里就是骗人**。用户指出「图示配置都没有效果」之后，整个功能端到端删除。

### 删了什么

| 层 | 内容 |
| --- | --- |
| PBZFM 菜单 | 「景深（手动对焦）」整页：总开关 + 对焦距离 / 光圈 / 传感器 / 对焦步进 / 光圈步进 五条滑条；`[keys]` 里 4 个对焦/光圈键位项 |
| 配置 | `[dof]` 段 6 个键；`key_focus_near/far`、`key_aperture_open/close`；`pad_focus_*`、`pad_aperture_*` |
| 输入 | `Intent.focus_nudge/aperture_nudge`、4 组连发状态、组合键让位逻辑（LB+BACK 等不再是特殊组合） |
| 核心 | `apply_depth_of_field` / `log_dof_write_if_due` / 旧配置迁移 / 不可用重探 / `dof_*` 全部状态与 getter |
| 引擎接口 | `EnginePort::set_depth_of_field` / `restore_depth_of_field` / `clear_depth_of_field` |
| 引擎实现 | `write_post_process_dof`（+ SEH 包装）、`snapshot_takeover_post_process`、`restore/clear_depth_of_field`、`takeover_pp_*` 快照、景深字段候选与诊断 |
| 其它 | `core/dof_math.h`、4 个语言词典里的 11 个标签键、1,106 项单测断言 |

### 自建后处理体积保留（改名）

「画面效果」那七项仍然要靠自建的无界高优先级体积（本作唯一会被采纳的后处理通道），
所以 `ensure_dof_volume` / `destroy_dof_volume` / `dof_volume_` 保留下来并改名为
`ensure_post_volume` / `destroy_post_volume` / `post_volume_`，注释与日志也从「景深专用」改成「画面效果」。
体积的创建参数（`bEnabled` / `bUnbound` / `Priority=10000` / `BlendWeight=1`）保持原样。

### 旧配置兼容

老用户的 `config.ini` 里通常还留着 `[dof]` 段与四个键位。现在这些键会被识别为「已移除」，
合并成**一条**提示（不是逐键刷屏）后静默忽略：

```
配置里的景深相关项已废弃（本作不支持引擎景深，功能已移除），已忽略；
可以把 [dof] 段与 key_focus_*/key_aperture_*/pad_focus_*/pad_aperture_* 一并删掉
```

### 数字

- DLL 2,202,624 → **2,159,616 字节**（-43,008）
- 核心层测试 6,397 → **5,291 项**（删掉 1,106 项景深断言），全绿
- `config.ini.example` 里已无 `[dof]` / 对焦 / 光圈键位

### 结论

景深在本作不可实现，Mod 侧不再提供任何相关入口。想要虚化请用外部后期（NVIDIA Ansel 的景深、
ReShade 的 DOF）。这条结论与取证链保留在 `core/tuning.h` 顶部与本文 hotfix25~32 各节，后来者不必重走。

### 同版夹带：画面效果四项失效的两处真 bug

hotfix31 给「画面效果」加了写后回读，实机日志直接把两件事指认出来：

```
画面效果：回读 暗角=0.08(覆盖1) 色差=2.92(覆盖1) 颗粒=7.63(覆盖1) 泛光=16.00(覆盖1) 饱和=<读不到> 对比=<读不到> 颜色分级=1.00(覆盖1)
画面效果诊断（抬档前）：r.Tonemapper.Quality=2 r.SceneColorFringeQuality=0 r.Tonemapper.GrainQuantization=0 r.FilmGrain=1 r.DepthOfFieldQuality=2
画面效果：抬档后 cvar 没变（色差质量 0→0，色调映射质量 2→2，应用=已调用）
```

**① 饱和度/对比度从来没写进去过（本 Mod 的 bug）。** `ColorSaturation` / `ColorContrast` 在 UE5 里是
`FVector4` = `TVector4<double>`，成员名是 **X/Y/Z/W**；`FLinearColor`（冷暖用的 `SceneColorTint`）才是
R/G/B/A。hotfix29~31 一直按 R/G/B/A 找这三个通道，`struct_member` 全部返回 `nullptr`，写入被静默跳过
三个通道 —— 所以滑条拖到 0.16/0.60 也不会有任何变化。现在按 X/Y/Z/W 写，读回用同一套名字，日志能
看到 `饱和=0.16(覆盖1)`。

**② 胶片颗粒缺噪声纹理。** UE5 的颗粒是 tonemapper 里拿 `FilmGrainTexture` 叠出来的（配
`FilmGrainTexelSize` 与 `FilmGrainIntensity`），本作没有任何体积设置过这张纹理，纹理为空时强度写再大
也画不出颗粒。本版在场景纹理里挑一张名字含 `grain/noise/perlin` 的（跳法线图与 flow），挂到自建体积上，
日志 `画面效果：胶片颗粒纹理 = <资源名>（命中关键词 …）`；挑不到就明确告警。

**③ 色差：判定为「本作不支持」，菜单标签照此标注。** 色差 pass 由 `r.SceneColorFringeQuality` 开，
本作把它钉在 0，而且钉它的优先级高于任何「游戏设置」级写入：hotfix31 走
`UGameUserSettings.SetPostProcessingQuality(3)` → `Scalability::SetQualityLevels`，回读 cvar 一个都没变
（UE5 的 cvar 有 `ECVF_SetBy*` 优先级，Scalability/GameSetting 都比 ConsoleVariablesIni/DeviceProfile 低，
这种写会被直接丢弃）。console 命令在本作不执行（hotfix25 判死），UE4SS 也不暴露 `IConsoleManager`
（已核对其头文件），所以 Mod 侧没有合法开关。抬档代码按判死处理**整体删除**，只留一行
`画面效果诊断（…）：r.Tonemapper.Quality=… r.SceneColorFringeQuality=…` 供后来者复核。

## 2.0.0-cpp-hotfix31 输入手感补齐 + 景深死代码清零 + 画面效果四项失效修复

hotfix30 之后把 backlog 里剩下的几件事一次做完，不留尾巴。

### 一、对焦/光圈按住连发（与 FOV 共用同一套状态机）

以前只有 FOV 键能按住连发，对焦/光圈只能一格一格点。现在六个方向（FOV 增/减、对焦近/远、
光圈开/关）全走同一个 `RepeatState`：

- 按下沿立即 1 格，按住 **0.35 秒**后每 **0.08 秒**补 1 格（≈12.5 格/秒）；
- 大掉帧时一帧补多格（`while (hold >= next)`），节奏不随帧率漂移；
- 键盘键与手柄组合（`LB+BACK` 等）合成**同一条方向通道**，谁先按下都算同一次按下沿；
- 按住时进出摄影模式（`reset()`）不会误发新按下沿，也不会立刻连发。

### 二、Shift 粗调 / Ctrl 微调

步进乘 `boost_mult`（默认 4）或 `slow_mult`（默认 0.3），与移动/滚转同一套倍率，不再有第二份配置。
关键顺序：**变速乘在比例步进之后**——否则比例下限会把 Ctrl 的微调吃掉（30cm 的 30% 仍是 9cm，
但如果先把 25cm 乘成 7.5cm 再取 max(…, 30cm)，微调就白按了）。

### 三、对焦比例步进：远距离自动放大

实际步进 = `max(配置步进, 当前对焦距离 × 10%)`（`tuning::kDofFocusStepProportion`）：

| 当前对焦 | 一步 |
| --- | --- |
| 1.2m | 25cm（配置步进，近距离保持精确） |
| 3m | 30cm |
| 10m | 1m |
| 100m | 10m |

以前 100m 外一步 25cm，按住连发也要按几分钟才追得上目标；现在 10 步左右就到。
`dof_focus_step` 的含义因此从「每次步进」变成「**最小**步进」，`config.ini.example` 的注释同步改写。

### 四、景深死代码清零（本次改动最大的一块）

hotfix30 只删了径向模糊，四条实验通道与它们的支撑设施还留在代码里（用 `constexpr false` 挡着）。
本版连同开关一起删干净：

**删除的 20 个函数 / 整块**：通道③（关卡体积写入）、通道④（玩家相机组件）、通道⑤（ViewTarget.POV
写入与每帧重申）、`ShowFlags` 内存采样与探针、`console` 命令闸门（`r.DepthOfFieldQuality` /
`ShowFlags.DepthOfField` 那套，本作 console 根本不执行）、一次性景深探针（黑白/暗角/强力景高三段）、
材质普查、MPC 景深候选参数 dump、`describe_dof_settings/volume` 回读串、`exec_command` /
`verify_exec_channel`。

**结果**：`ue4ss_port_camera_view.cpp` 3,046 → **1,926 行**（净减 1,120），`ue4ss_port.h` 825 → **679 行**
（-146），DLL 2,225,664 → **2,202,624 字节**（-23,040）。`set_depth_of_field` 从 106 行收到 52 行，
只剩「通道① + 通道② + 参数记录」；`kDofExperimentalChannels` / `kDofQualityTarget` 两个常量一并删除，
`tuning.h` 里保留完整的取证结论（后来者不用重走一遍）。

### 五、SEH 兜底：景深写入不再可能带崩游戏

`write_post_process_dof` 全程走反射（字段名运行时查、基址算出来），一旦对不上就是直接读坏内存。
新增 `write_post_process_dof_guarded()`：内层一个只含 POD 局部变量的 `__try` 函数（MSVC C2712 限制），
拦下 `EXCEPTION_EXECUTE_HANDLER` 后返回失败并只告警一次：

```
景深写入触发了一次非法内存访问（已拦下，游戏未受影响）；本局不再尝试写景深
```

4 个调用点全部改走 guarded 版本。注意语义：**只兜住这一次写入**，不试图重试、不静默。

### 六、世界换代时清掉接管相机

以前 `invalidate_world_scoped_state()` 清了对象缓存与快照，但**没清接管状态**：换图后
`takeover_active_` 仍是 true、`takeover_pp_baseline_*` 还是旧世界的值，退出时会把旧世界的
后处理参数写进新世界的相机（等于把新地图的相机设置改坏）。

现在世代一变：作废接管与快照（**不写回**）、清掉位姿/失配计数，并置 `takeover_render_lost_`
让 core 走既有回退路径（`fallback_from_takeover` → spawn 独立相机），日志
`世界换代：接管当前相机已失效（…不再写回），本次将回退独立相机`。

### 七、画面效果四项不生效：根因是引擎「后处理质量」档位

hotfix30 的实机日志 + 用户的 config.ini 把最后一块拼图给全了。滑条明明拖到位（色差 **1.98** /
颗粒 **7.68** / 饱和 **0.74** / 对比 **0.81**），画面毫无变化，而同一次会话里暗角、泛光、冷暖都正常：

| 项 | 值 | 结果 |
| --- | --- | --- |
| 暗角 | 0.08（拖到 2.17 验证过） | **生效** |
| 泛光 | 1.50 | **生效** |
| 冷暖 | 0.16 | **生效** |
| 色差 | 1.98 | 无变化 |
| 胶片颗粒 | 7.68 | 无变化 |
| 饱和度 | 0.74 | 无变化 |
| 对比度 | 0.81 | 无变化 |

同一个体积、同一套 `bOverride_*` + 数值，生效与失效泾渭分明 —— 所以既不是「值太小」，也不是
「没写进去」。翻开引擎的 `BaseScalability.ini` 就清楚了：

```
[PostProcessQuality@0]/@1        [PostProcessQuality@2]/@3/@Cine
r.SceneColorFringeQuality=0      r.SceneColorFringeQuality=1
r.Tonemapper.Quality=0 / 2       r.Tonemapper.Quality=5
r.Tonemapper.GrainQuantization=0 r.Tonemapper.GrainQuantization=1
```

本作读到的 `r.Tonemapper.Quality` 就是 **2**：色差 pass（要 `r.SceneColorFringeQuality≥1`）、
胶片颗粒、以及颜色分级 LUT 相关项在这个档位下被引擎**整段跳过**。暗角/泛光/SceneColorTint 写在
tonemapper 里但不受这些档位管辖，所以只有它们三项正常 —— **失效四项正好是「受档位管辖」那一拨**。

本版改动：

- **写后回读**：画面效果每次值变化，先下发，再把体积里 7 项（+ 颜色分级强度）的实际值与覆盖标志
  读回来打一行 `画面效果：回读 …`。从此日志能分开「没写进去」（反射 bug）与「写进去了引擎不画」
  （档位问题）。
- **颜色分级强度兜底**：饱和/对比走引擎的颜色分级 LUT，用到这两项时把 `ColorGradingIntensity`
  钉成 1（LUT 内容仍由引擎按当前设置生成）。
- **色差起始偏移**：同时把 `ChromaticAberrationStartOffset` 钉成 0，让 RGB 分离从画面中心开始。
- **抬后处理档位**：console 命令在本作不执行（Kismet 与直连 PlayerController 两条通道回读都不变，
  hotfix25 已判死），所以走游戏自己的设置对象 `UGameUserSettings.SetPostProcessingQuality(3)` →
  `ApplyNonResolutionSettings()`（内部即 `Scalability::SetQualityLevels`）。**成败一律以 cvar 回读判定**：
  `r.SceneColorFringeQuality` / `r.Tonemapper.Quality` 任一变大才算抬上去，日志写明前后值；没抬动
  立刻把原档位写回去，不留半套改动。退出摄影模式（画面效果全部回中性）自动还原原档位。

新增日志（出现即说明走到了新路径）：

```
画面效果诊断（抬档前）：r.Tonemapper.Quality=2 r.SceneColorFringeQuality=0 r.Tonemapper.GrainQuantization=0 r.FilmGrain=? r.DepthOfFieldQuality=?
画面效果：已把游戏后处理质量临时抬到 3 档（色差质量 0→1，色调映射质量 2→5），色差/颗粒/饱和/对比现在应该能看见了；退出摄影模式还原
```

抬不动会长这样：

```
画面效果：抬档后 cvar 没变（色差质量 0→0，色调映射质量 2→2，应用=已调用）——档位这条路在本作不通，色差/颗粒/饱和/对比只能按「写了但引擎不画」如实记录
```

代价：抬档是游戏自己的「后处理质量」设置，会连带提高 AO / 泛光 / 运动模糊等质量，**只在用到
色差/颗粒/饱和/对比时抬，退出还原**；暗角/泛光/冷暖不受影响。

### 测试

核心层 6,365 → **6,397 项**全绿：新增对焦/光圈按住连发（含掉帧补格、松开即停、按住时 reset 不误发）、
`focus_step_scaled` 比例步进（近距离/中距离/远距离/负值/比例关掉）、PhotoMode 层 Shift/Ctrl 变速与
一帧三格放大。画面效果这一块全是引擎层写入，核心层无新增用例。

> hotfix32 复盘：本节⑦里「抬后处理档位」这条**实测无效**（回读 cvar 一个都没变，被 cvar 优先级挡掉），
> 该路径已整体删除；同时发现 `ColorSaturation/ColorContrast` 的成员名写错（本该 X/Y/Z/W），
> 饱和度/对比度其实是**一个字节都没写进去**。两件事的结论与修法见 hotfix32 的
> 「同版夹带：画面效果四项失效的两处真 bug」。

## 2.0.0-cpp-hotfix30 景深收口：删除径向模糊伪虚化实验，代码零残留

hotfix28 埋的日志把最后一格拼上了：

```
伪虚化诊断：40 个后处理体积里，带 WeightedBlendables 的有 0 个：<无>
伪虚化：使用材质 Material /PBZVFXPack/.../M_FX_RadialBlur.M_FX_RadialBlur
伪虚化：开始自建槽位（借不到现成的，改用 FMemory 分配）
伪虚化：自建槽位失败 → Array 取址失败
```

- 全场景 **40 个后处理体积的 `WeightedBlendables` 一个都不带条目**，没有现成槽位可借；
- 自建槽位时连 `FWeightedBlendables.Array` 的地址都取不到（反射层拿不到这层嵌套数组的取址），
  材质也就永远挂不上去。

也就是说这条替代路线在**第一步（挂载）** 就断了，不是「挂上了但不够糊」。继续投入只能靠
硬编码内存布局去猜 Shipping 版的字段偏移，风险远大于收益，且本作的 ShowFlags 通道已经证明
走不通（console 命令不执行）。**决定到此为止**。

### 本版改了什么

- **删除** `attach_radial_blur_material()` / `detach_radial_blur_material()` / `scan_weighted_blendables()`
  及其全部状态成员与日志（engine 层 -233 行，头文件 -21 行），连同 `#include <Unreal/FMemory.hpp>`；
  不留任何死代码或「以后可能有用」的钩子。
- `tuning::kDofExperimentalChannels` 的注释补齐伪虚化的结论，作为后来者的取证记录。
- **config.ini.example 的 `[dof]` 段注释改写**：明确写出「本作 gameplay 视图不渲染引擎景深、
  以下参数仅为记录」，不再宣称「100~500 最能看出虚化」这类与事实相反的话。
- 菜单滑条的「（本作不支持）」标注（hotfix27 起）保持不变。
- hotfix29 的 `[look]` 画面效果（暗角/色差/颗粒/饱和/对比/泛光/冷暖）走另一条已验证通道，**不受影响**。

### 最终结论（写给用户）

本作在 gameplay 视图下不产生任何引擎侧景深，Mod 侧没有合法手段打开它。景深/对焦滑条保留为
参数记录（照常写入、照常回读、退出还原），不影响任何其它功能。这不再是待办项。

## 2.0.0-cpp-hotfix29 画面效果全套：暗角 / 色差 / 胶片颗粒 / 饱和度 / 对比度 / 泛光 / 冷暖

hotfix28 已经把「自建后处理体积」这条通道跑通了（暗角探针实测生效）。其余几项用的是**同一套机制**：
`FPostProcessSettings` 里各自一对 `bOverride_XXX` + 值，写进同一个体积即可，所以一次性补齐。

### `[look]` 段（7 项，全部走同一个自建体积）

```ini
[look]
vignette = 0                 ; 暗角 0~4（0=关闭）
chromatic_aberration = 0     ; 色差 0~5（0=关闭，SceneFringeIntensity）
film_grain = 0               ; 胶片颗粒 0~10（0=关闭，FilmGrainIntensity）
saturation = 1               ; 饱和度倍率（1=原样，0=黑白，2=更艳）
contrast = 1                 ; 对比度倍率（1=原样）
bloom = 0                    ; 泛光强度（0=不覆盖，1~5 明显，BloomIntensity）
temperature = 0              ; 冷暖 -1~1（0=不覆盖，SceneColorTint 乘算）
```

中性值就是「关闭」：暗角/色差/颗粒/泛光/冷暖写 0、饱和/对比写 1 时，
对应项的 `bOverride_` 会被置回 `false`，等于把这一项交还给游戏自己的设置。

### 菜单

PBZFM 的「画面效果」组里现在有 7 条滑条，拖完即时生效（每帧比较参数结构，变了才下发）。

### 实现

- 新增 `src/core/look.h`：`LookOptions`（7 项 + 中性判定 + 相等判定）；
- `EnginePort::set_vignette` 升级为 `EnginePort::set_look(const LookOptions&)`；
- 引擎层逐项写 `bOverride_*` 与值：标量（VignetteIntensity / SceneFringeIntensity / FilmGrainIntensity /
  BloomIntensity）直接写数；`ColorSaturation` / `ColorContrast` 是 `FVector4`，写 R=G=B=值、A=1；
  冷暖写 `SceneColorTint`（R=1+0.18t、G=1-0.02t、B=1-0.22t）；
- 某字段在本 build 里不存在时只告警一次并列出名字，其余项继续生效。

### 注意

- 这些字段是**绝对覆盖**：例如把泛光拉到 3，会覆盖游戏自己的泛光值（不是叠加）。想恢复就把滑条拖回 0/1。
- 如果某一项在游戏里本来就被相机侧的 PP 覆盖（景深就是这么被按掉的），那一项可能看不到变化；
  日志会打印实际写入情况。已知能出画面的：暗角。

核心层测试 **5,816 → 6,365 项**全绿。

## 2.0.0-cpp-hotfix28 新增：屏幕暗角（Vignette）——本作 gameplay 视图里实测可用

景深这条线走不通，但**同一轮探针里第 2 段的暗角是明确生效的**：

```
后处理探针 2/3（自建体积）：接下来约 1.5 秒画面【四周会变暗】  → 用户：只有四周变暗 ✓
```

也就是说「写进自建 `PostProcessVolume` 的 `VignetteIntensity`」是**唯一被实机证明能出画面**的后处理通道。
本版把它做成正式功能。

### 配置（新的 `[look]` 段）

```ini
[look]
; 暗角强度（0=关闭，1~3 明显；走后处理体积通道，本作 gameplay 视图实测有效）
vignette = 0
```

- 范围 0~4，0 表示关闭；
- 走的是自建的无界高优先级后处理体积（priority=10000），和景深用的是同一个；
- 关闭时把 `bOverride_VignetteIntensity` 置回 false，退出摄影模式恢复原状。

### 菜单

PBZFM 菜单新增一组 **「画面效果（暗角）」**，里面一条 `暗角强度（0=关闭）` 滑条，
拖完即时生效（每帧检查配置值变化，变化才下发，不做无谓写入）。

### 实现要点

- `EnginePort::set_vignette(double)`：新增虚函数，引擎层实现写体积的
  `bOverride_VignetteIntensity` / `VignetteIntensity`；
- `PhotoMode::apply_look_if_needed()`：值变化才下发；失败只告警一次且下帧继续重试
  （体积要等世界就绪）；
- `PhotoMode::leave()` 里写回 0，退出即恢复；
- 语言包四种语言都加了 `#group_look` 与 `#vignette` 文案；配置解析的 section 白名单加了 `look`。

核心层测试从 **5,728 项涨到 5,816 项**（新增的配置/菜单往返覆盖），全部通过。

## 2.0.0-cpp-hotfix28 这次改了什么（自建槽位没走到头 —— 每一步都加上日志）

hotfix27 的日志只有两行：

```
伪虚化诊断：40 个后处理体积里，带 WeightedBlendables 的有 0 个：<无>
伪虚化：使用材质 Material /PBZVFXPack/VFX/A_Materials/Template/Original/M_FX_RadialBlur.M_FX_RadialBlur
```

材质找到了，但**自建槽位那一段一声不响就返回了** —— 说明在「取 `WeightedBlendables` → 取 `Array` →
取 `Inner` → 取 `Object/Weight`」这条链上有一环拿不到，而那几个分支当时都是静默 return，没法定位。

本版把这条链的每一步都拆开打日志：

```
伪虚化：开始自建槽位（借不到现成的，改用 FMemory 分配）
伪虚化：自建槽位失败 → <具体哪一步>
```

可能的分支（都会原样写出来）：
`体积上没有 Settings 结构` / `FPostProcessSettings 里找不到 WeightedBlendables` /
`FWeightedBlendables 里找不到 Array` / `Array 不是 FArrayProperty` / `Array 没有 Inner` /
`FWeightedBlendable 里找不到 Object/Weight` / `算不出元素大小` / `FMemory::Malloc 返回空` /
`写入 blendable 元素失败`。

拿到具体那一行，就知道 5.7 里这个结构的真实字段名/类型，下一步不用再猜。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix27 这次改了什么（借不到槽位 → 用引擎自己的分配器造一个）

hotfix26 的结果很干净：

```
伪虚化诊断：40 个后处理体积里，带 WeightedBlendables 的有 0 个：<无>
伪虚化：使用材质 Material /PBZVFXPack/VFX/A_Materials/Template/Original/M_FX_RadialBlur.M_FX_RadialBlur
伪虚化：所有后处理体积的 WeightedBlendables 都是空的，借不到槽位
```

材质找到了，但**游戏里 40 个后处理体积的 `WeightedBlendables` 全是空的**，没有现成槽位可借。

### 本版：自己造一个槽位（用引擎的分配器，不是 CRT 的）

`TArray` 的内存必须由引擎分配器分配——UE5 Windows 默认是 Binned2，拿 `malloc` 的内存交给它
`Free`/`Realloc` 会崩。本版改用 UE4SS 暴露的 **`RC::Unreal::FMemory::Malloc/Free`**（走游戏自己的 GMalloc）：

1. 从 `TArray<FWeightedBlendable>` 的 `Inner` 结构算出元素大小（正常是 16 字节：`UObject*` + `float`）；
2. `FMemory::Malloc(元素大小)`，清零；
3. 写入 `Object = M_FX_PP_RadialBlur`、`Weight = 1.0`；
4. 把数组头设成 `{Data=我们的指针, Num=1, Max=1}` —— 引擎就会把它当成一条普通 blendable 来混合；
5. 退出/关景深时**先把数组头清空**（让引擎不去 free 我们的指针），再自己 `FMemory::Free`。

顺序是先清空、后释放，所以体积销毁时数组已经是空的，不会触发二次释放。

### 风险与回退

- 这一步会往引擎的 PP 混合链里塞一个自建数组。如果材质本身要求额外参数，效果可能过强或没有；
- 万一崩：把 `config.ini` 的 `[dof] enabled` 改成 `false`（或直接删掉 config 里的 `[dof]` 段）再进游戏即可，
  景深关闭时本版完全不碰这条路径。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix26 这次改了什么（径向模糊伪虚化：借游戏自己的 PP 材质）

引擎景深已确认在 gameplay 视图里不出画面（hotfix25 的结论），但**后处理体积的 `WeightedBlendables`
是走过引擎 PP 混合链的**——暗角就是这条链生效的证明。所以换个思路：

> 把游戏自己的后处理材质 **`M_FX_PP_RadialBlur`**（材质普查里找到的）挂到后处理体积的
> `WeightedBlendables` 上，让引擎替我们做「中心清晰、四周发糊」。

### 怎么挂（不自己分配 TArray）

往 `TArray<FWeightedBlendable>` 里 append 需要 `FMemory::Malloc`，反射层做这个风险高。
本版改成**借槽位**：扫场上所有 `PostProcessVolume` 的 `WeightedBlendables.Array`，
只要某个体积里已经有一条（游戏自己的），就复用它的第 0 个槽位——

- 记下原来的 `Object` 和 `Weight`；
- 写入我们的材质与权重 1.0；
- 退出/关景深时按原值写回。

日志会打：

```
伪虚化诊断：N 个后处理体积里，带 WeightedBlendables 的有 M 个：<体积名>=<条数> …
伪虚化：使用材质 /Game/…/M_FX_PP_RadialBlur
伪虚化：已把 M_FX_PP_RadialBlur 挂到体积 X 的第一个 blendable 槽位（原为 Y 权重 0.00），退出还原
```

如果所有体积的 `WeightedBlendables` 都是空的，会明确写
`伪虚化：所有后处理体积的 WeightedBlendables 都是空的，借不到槽位`，那时再决定要不要冒险自己分配内存。

### 需要说清楚的两点

1. **这不是景深**。径向模糊是「从画面中心向外糊」，不是「按距离糊」。前景和背景会一起被糊；
   拍特写、拍有纵深的构图时观感更像「镜头柔焦」而不是「大光圈」。
2. 材质可能有自己的参数（MPC），权重 1.0 的效果强弱只能实机看——**糊得太狠或没反应都可能**，
   到时候按日志再调权重。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix25 收口：把景深标注为「本作不支持」，关闭实验通道

hotfix24 的日志给出了最后一块拼图：

```
景深诊断：ViewTarget.POV 的 fstop 在两次 tick 之间从 1.84 变回 40.00 → 这份 POV 每帧被游戏重建
```

即便**每帧重申**，游戏仍会在下一帧把 `fstop` 写回 `F40`（针孔＝全合焦），画面依旧不糊。

### 最终结论（证据链完整）

| 尝试 | 结果 |
|---|---|
| 相机 `CameraCache.POV` 的 PP | 无反应（第 1 段黑白从未出现） |
| 自建无界体积（priority=10000） | **暗角生效**，但同体积的光圈（88px）无效 |
| 关卡自带体积（3 个，一定在 `UWorld::PostProcessVolumes`） | 写入成功，无效果 |
| `ViewTarget.POV`（每帧重申） | 写入成功，游戏每帧写回 `F40`，无效果 |
| `r.DepthOfFieldQuality` / `r.DOF.*` | 全是健康值（2 / 默认），且 console 命令在本作**完全不执行** |
| `ShowFlags@0xB8` | 整局一位不变 → 确认**不是**真的 `FEngineShowFlags`，`bit20=1` 作废 |
| 材质普查（6864 个） | 无 DOF/bokeh 后处理材质，只有 `M_FX_PP_RadialBlur` 等径向模糊 |

**这款游戏的 gameplay 视图不渲染引擎景深**，且没有任何反射层可达的开关能打开它
（唯一剩下的 `View.Family->EngineShowFlags.DepthOfField` 拿不到，console 命令也不执行）。
过场里的背景虚化走的是另一条路径（相机/过场的 PP 或引擎在过场期间另开 DOF），与本 Mod 无关。

### 本版改动

1. **UI 标注**：`#dof_enabled` / `#dof_focal_distance` / `#dof_fstop` / `#dof_sensor_width` 四个标签
   在四种语言里统一追加「本作不支持 / unsupported in this game / 本作非対応 / 본작 미지원」。
2. **关闭实验通道**：`tuning::kDofExperimentalChannels = false`。四条实验通道、探针、ShowFlags 采样、
   每帧重申全部停用——它们换不来任何画面变化，只会每帧去写游戏对象，是风险不是功能。
   日后若找到打开 ShowFlags 的办法，把这个常量改回 `true` 即可复活全部代码。
3. **一次性说明**：第一次下发景深时打一条日志，把上面的结论直接写给用户看，并把「参数怎么调才有虚化」的提示保留。

**景深相关的配置项、滑条、物理换算、日志全部保留**（参数照常记录、照常写回 config.ini），
只是不再声称它能产生画面效果。

核心层测试 **5,728 项**全绿。

## 2.0.0-cpp-hotfix24 这次改了什么（通道⑤ 改为每帧重申 —— 最后一个还没试过的时序假设）

hotfix23 的两条结果：

1. **`ShowFlags@0xB8` 整局一次都没变**。游戏在整段会话里总会翻一些 show flag 位（视图模式、HUD、过场），
   它一次不变，说明这块内存**不是**真的 `FEngineShowFlags` —— 之前读到的 `bit20=1` 一并作废。
   换句话说：我们从头到尾**没有真正看到过** `ShowFlags.DepthOfField` 的状态。
2. **材质普查**：6864 个材质对象里，名字带模糊的只有
   `M_FX_PP_RadialBlur` / `M_FX_RadialBlur` / `M_ShadowBlur` / 一堆 `*_BeforeDof` 的雨雪粒子材质，
   **没有任何 DOF/bokeh 类后处理材质**。所以「用游戏自己的模糊材质替代」也只能是径向模糊，不是景深。

### 但审计历史日志时发现一个还没试过的时序假设

两次进摄影模式，`ViewTarget.POV` 的 focal 分别是 **221** 和 **362** —— 说明这份 POV 是**被游戏反复重填**的。
而 hotfix21~23 里，通道⑤ 只在「开启景深 / 拖滑条 / 探针第 3 段」时写：

- 进入时的写入 → 下一帧被游戏的 `F40` 覆盖回去；
- 探针第 3 段那 2.5 秒里确实是每帧写（`apply_strong_dof` 在 tick 里），但如果视图在**更晚**的
  `CalcSceneView` 里才重填这份 POV，那连每帧写也留不住。

不管哪种，**「每帧重申」这个动作之前从没在通道⑤ 上做过**（相机 POV 那条一直是每帧写的）。
本版补上：`reassert_view_target_dof()` 每帧把当前景深写进 `ViewTarget.POV`，探针第 3 段期间改写成强力参数。

同时新增一行诊断，直接回答「写进去的东西能不能活过一帧」：

```
景深诊断：ViewTarget.POV 的 fstop 在两次 tick 之间从 0.10 变回 40.00 → 这份 POV 每帧被游戏重建，只有每帧写才留得住
```

- **没有这行** → 我们每帧写的值确实活着（说明它一直是每帧重建的，而之前只写一次所以全被抹了）——
  那这一版就该出效果；
- **有这行** → 连每帧写都会被更晚的一步抹掉，`ViewTarget.POV` 也不是最终来源，引擎 DOF 这条路到此为止。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix23 这次改了什么（88px 打进四条通道仍不糊 → 收束：确认这块内存是不是 show flags + 找游戏自己的模糊材质）

hotfix22 把 `fstop=0.1 / 对焦 300cm / 传感器 200mm`（≈88px）同时写进了**四条通道**：

1. 相机 `CameraCache.POV` 的 `PostProcessSettings`
2. 自建的无界 `PostProcessVolume`
3. 关卡自带的 `PostProcessVolume`（3 个，写入成功）
4. `APlayerCameraManager::ViewTarget.POV`（写入成功）

**画面依旧完全不糊。** 而同一个自建体积上的 `VignetteIntensity` 是明确生效的。
这就把「值不对 / 通道不对 / 被覆盖」三条全部排除干净了 —— 只剩一种可能：

> **这个视图里根本没有 DOF pass。** `DiaphragmDOF::IsEnabled` 的四个条件我们只能确认三个（`fstop>0`、`focal>0`、
> `r.DepthOfFieldQuality=2`），剩下那一个 `View.Family->EngineShowFlags.DepthOfField` 在反射层拿不到；
> 而 console 命令在本作里完全不执行（hotfix19 已证），连「用引擎自己的命令打开它」这条路也断了。

### 本版做两件事收尾

**① 验证 `0xB8` 那块内存到底是不是 `FEngineShowFlags`**

之前只是「读出来长得像」：采样 `UGameViewportClient + 0xB8` 的 16 字节，**每帧比对**，变了就打一行。
游戏在换视图模式/进过场/切 HUD 时必然会翻一些 show flag 位——

- 出现 `景深闸门：ShowFlags@0xB8 发生变化 → …` → 这块内存是真的 flags，那 `bit20=1` 就可信，
  说明 DOF 是被别的机制关的（view family 上的额外开关）；
- 整局一次不变 → 它根本不是我们要找的结构（很可能只是静态数据/填充），`bit20=1` 那条也就作废。

**② 材质普查：本作是不是用自己的后处理材质做的「过场背景虚化」**

扫描场上所有 `Material` / `MaterialInstanceConstant` / `MaterialInstanceDynamic` / `MaterialInterface`，
把名字里含 `blur` / `dof` / `bokeh` / `coc` / `distort` 的列出来：

- 找到类似 `M_PP_Blur` / `MI_DOF` 的资产 → **有戏**：把它作为 `WeightedBlendables` 挂到我们的体积上，
  用游戏自己的模糊材质实现「背景虚化」，绕开引擎 DOF；
- 什么都没有 → 过场的虚化来自别的机制，引擎 DOF 这条路到此为止，我会把景深滑条标注为「本作不支持」，
  不再让你反复试。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix22 这次改了什么（找到那个把光圈按住的值了：`ViewTarget.POV` 里写着 F40）

hotfix21 的日志第一次把「谁在覆盖光圈」印出来了：

```
景深诊断：ViewTarget.POV 的景深原值 focal=221(override=1) fstop=F40.00(override=1) sensor=23.8(override=1)
景深：已把当前光圈/焦距/传感器写进相机管理器 ViewTarget.POV（通道⑤，写入=成功）
```

**游戏在 `APlayerCameraManager::ViewTarget.POV.PostProcessSettings` 里显式写着 `F40`**（对焦 2.21 m、传感器 23.8 mm），
三项 `bOverride_*` 全是 1。F40 是「针孔」——按公式算，背景虚化约 **0.04px**，等于全部合焦。

而这份 POV 的混合顺序在**体积之后、weight=1**，所以：

- 写关卡体积的光圈 → 被它按回 F40；
- 写自建体积的光圈 → 同样被按回 F40；
- 写相机 `CameraCache.POV` 的 PP → 根本不是视图用的那份；
- 只有暗角这类**它没覆盖**的字段能穿过去。

这解释了此前所有实验的结果，一个不漏。

### 但画面还是不糊——因为两件事同时成立

1. 通道⑤ 现在是我们写的了（`写入=成功`），可**当时用的还是滑条里的值**：
   `fstop=F1.8 / 对焦 678cm / 传感器 286mm` → 按公式只有 **约 3px**，肉眼基本看不出来；
2. 探针第 3 段的 90px 强力值当时**没写进通道⑤**，所以又被那份 F40 按了回去。

### 本版

把**通道⑤ 加进探针第 3 段**：`fstop=0.1 / 对焦 300cm / 传感器 200mm`（≈88px）现在会写进
相机 POV、自建体积、关卡自带体积 **和** `ViewTarget.POV` 四条通道。2.5 秒后还原。

如果这一段糊了，就说明引擎 DOF 一直是好的，只是被 F40 按着——接下来只需把滑条的默认值/范围调到位，
让常规取值就能看出虚化（当前默认 ≈5px，太弱）。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix21 这次改了什么（通道⑤：相机管理器的 ViewTarget.POV —— 视图 PP 的最后一条可能来源）

hotfix20 的探针结果只有一行，但很关键：

```
景深诊断：玩家 Pawn/控制器身上没找到 CameraComponent，通道④不可用
```

玩家的 `BP_Renewal_Hun_C` 身上没有引擎原生 `CameraComponent`（相机多半在更深的组件/蓝图里，反射层不容易按名字找），
所以「相机组件里写着 focal=0」这个猜测没被验证也没被排除。

### 换一条更好找、也更像的路：ViewTarget.POV

把已知事实列一下：

- 写 `APlayerCameraManager::CameraCache.POV` 的 `PostProcessSettings` → 毫无反应；
- 写自建 `PostProcessVolume` 的 `PostProcessSettings` → 生效（暗角）；
- 但同一个体积里的**光圈/焦距**（fstop=0.1、90px）→ 不生效。

UE 里视图的 `FMinimalViewInfo` 还有一份来源：**`APlayerCameraManager::ViewTarget.POV`**。
它由 `UpdateCamera` 从 ViewTarget 的相机组件填好；而本 Mod 进入摄影模式时会**暂停世界**，
`UpdateCamera` 不再运行 —— 于是这份 POV 一直停在「进摄影模式之前」的状态，
它很可能就带着游戏关 gameplay DOF 的写法（`bOverride_DepthOfFieldFocalDistance=1` + `focal=0`），
并且**在体积之后以 weight=1 生效**——正好解释「体积的暗角有效、体积的光圈无效」。

### 本版做的事

新增**通道⑤**：直接定位 `PlayerCameraManager.ViewTarget`（`FTViewTarget`）→ `POV`（`FMinimalViewInfo`）
→ `PostProcessSettings`，快照其中的景深三项，然后写入当前的焦点/光圈/传感器；退出按快照还原。

第一次遇到时打一行诊断，直接给结论：

```
景深诊断：ViewTarget.POV 的景深原值 focal=0(override=1) fstop=F4.00(override=0) sensor=24.6(override=0)
```

- `focal=0(override=1)` → **元凶确认**：游戏就是用这份 POV 关掉 gameplay 景深的，而我们现在把它改掉了；
- `override=0` → 这份 POV 也不是元凶，那景深就只剩 view family 的 `ShowFlags`（反射层碰不到），该换方案。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix20 这次改了什么（新增通道④：玩家相机组件——最可能是它在抹掉光圈）

hotfix19 的日志给出了一个比景深本身更基础的结论：

```
exec 通道验证：r.Tonemapper.Quality 原=2，Kismet 写 0 回读=2，直连 PC 回读=2
             → 两条通道都无效：之前所有 cvar/ShowFlag 命令其实都没执行
```

也就是说，过去十几轮里「抬画质档 / 抬 CoC 半径 / 打开 `ShowFlag.DepthOfField`」**全部是空转**——
`UKismetSystemLibrary::ExecuteConsoleCommand` 和直连 `APlayerController::ConsoleCommand` 在本作里都不生效。
这不是景深的原因，但必须记下来，免得继续在 cvar 上打转。

### 为什么转向「玩家相机组件」

把探针结论摆在一起：

- 写 **相机 `CameraCache.POV`** 的 `PostProcessSettings` → **完全没反应**（第 1 段黑白没出现）；
- 写 **自建体积** 的 `PostProcessSettings` → **明确生效**（第 2 段四周变暗）；
- 但同样写进体积的 **光圈/焦距**（fstop=0.1、90px）→ **完全没糊**。

「体积的暗角有效、体积的光圈无效」只能有一种解释：**在体积之后，还有一步把 DOF 字段单独覆盖掉了**。
而 UE 的混合顺序里，体积之后正好是 `ViewInfo.PostProcessSettings`（weight=1）——
本作的 `ViewInfo` 很可能是在 `CalcSceneView` 时从 **ViewTarget 的相机组件**重新填的（晚于我们的 Tick），
而那个相机组件里极可能写着 `bOverride_DepthOfFieldFocalDistance=1` + `DepthOfFieldFocalDistance=0`
——这正是「关掉 gameplay DOF」的标准写法（`FPostProcessSettings` 构造函数注释原话：
*Intentionally invalid to disable DOF by default*）。

### 本版做的事

新增**通道④**：遍历世界里的 `CameraComponent`，挑出属主是玩家 Pawn / PlayerController 的那些，
读取并快照它们 `PostProcessSettings` 里的景深三项，然后把当前的焦点/光圈/传感器写进去（退出按快照还原）。

第一次遇到时会打一行诊断，直接给出结论：

```
景深诊断：玩家相机组件 <名字> 的景深原值 focal=0(override=1) fstop=F4.00(override=0) sensor=24.6(override=0)
```

- 看到 `focal=0(override=1)` → **元凶确认**：游戏就是靠这一条关掉 gameplay 景深的，而我们现在把它改掉了；
- 看到 `override=0` → 这条通道不是元凶，景深另有限制（那就只剩 view family 的 ShowFlags，反射层碰不到）。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix19 这次改了什么（90px 强力景深照样不糊 → 转头查「console 命令到底执行了没有」）

hotfix18 的结果：**三条通道同时写 fstop=0.1 / 对焦 300cm / 传感器 200mm（约 90px），画面依旧完全不糊。**

到这一步，“通道不对”“参数太弱”两条都排除了。但同一份日志里出现了一个更基础、也更可疑的现象：

```
景深质量：r.DepthOfFieldQuality 2 → 3 … | 回读=2
景深闸门：ShowFlag.DepthOfField 命令执行=ok/ok，但内存里没有任何一位变化
```

这两条放在一起看，最像的解释**不是**「cvar 被钉住」，而是：**这些 console 命令根本没有被真正执行**
（`UKismetSystemLibrary::ExecuteConsoleCommand` 返回成功 ≠ 命令被执行）。如果真是这样，那么：

- 之前所有「抬画质档 / 抬 CoC 半径上限 / 打开 `ShowFlag.DepthOfField`」都是空转；
- 而 `ShowFlag.DepthOfField 1` 恰好是过去唯一有可能真正打开 DOF 的一条。

### 本版做三件事

1. **验证 exec 通道**：拿一个确定可回读的 cvar `r.Tonemapper.Quality` 当探针——读原值 → 写目标值 → 回读 →
   还原，然后打一行：
   `exec 通道验证：r.Tonemapper.Quality 原=2，Kismet 写 0 回读=…，直连 PC 回读=… → …`
2. **验证不过就切通道**：新增直连 `APlayerController::ConsoleCommand` 的实现，之后所有 cvar/ShowFlag 命令
   都走这条验证过的通道。
3. **在可用通道上重做两件事**：① 再翻一次 `ShowFlag.DepthOfField 0/1` 并对比内存，实测真实位索引；
   ② 用游戏自己的画质组 `sg.DepthOfFieldQuality 4` 试一次，看能不能把 `r.DepthOfFieldQuality` 推上去。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix18 这次改了什么（换掉深度模糊段：用「不可能看不见」的强力景深测通道）

hotfix16 的探针结论，经用户确认后是干净的：

- 第 1 段（**相机 POV** 写 `ColorSaturation`）→ 无任何变化；
- 第 2 段（**自建体积** 写 `VignetteIntensity`）→ 明确的「四周变暗」。

**（相机 `CameraCache.POV` 的 PostProcessSettings 在本作 gameplay 视图里不被采纳，自建体积那条通道是活的。）**

顺着这条线回看历史日志，发现一个更关键的事实：**过去所有「极端光圈」的尝试（fstop 0.08 / focal 421cm /
sensor 164mm 算出 200px）全部写在相机 POV 这条死通道上**，从来没进过真正生效的体积通道。而热启动这次
探针跑的时候，用户的光圈是 `fstop=F1.8 / focal=678cm / sensor=286mm`——按公式只有 **3px** 左右，
就算 DOF 正常跑也几乎看不出来。

所以本版把探针第 3 段从「深度模糊」（`DepthBlurRadius` 语义没查实、且同样写在死通道上）换成**强力景深**：

```
fstop = 0.1   对焦 = 300cm   传感器 = 200mm     → 约 90px（1920 宽基准）
```

并且**同时写进三条通道**：相机 POV、自建体积、关卡自带的 `PostProcessVolume`（后者一定在
`UWorld::PostProcessVolumes` 里）。哪条通道活着都能出效果，2.5 秒后自动还原。

判读只有一句话：**第 3 段背景糊了 = 引擎 DOF 在 gameplay 视图里确实是跑的，之前只是写错了通道 / 参数太弱；
没糊 = 这个视图里 DOF 被引擎侧关掉了，得换方案。**

同时保留 hotfix17 的两项：关卡自带体积也承载光圈（退出按快照还原）、以及用 `ShowFlag.DepthOfField 0/1`
命令实测真正的位索引与当前值。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix17 这次改了什么（写错通道了：改用关卡自带体积 + 实证 ShowFlag 位索引）

hotfix16 的探针日志把问题指向了一个很朴素的地方：

**① 引擎质量闸门全部排除**

```
景深闸门(普查)：gq.DepthOfFieldQualityLevel=4 r.DepthOfFieldQuality=2 sg.DepthOfFieldQuality=0
                r.PostProcessing.MoveDOFAfterUpscaling=0 r.DOF.Gather.ResolutionDivisor=1
                r.DOF.Gather.RingCount=4 r.DOF.Recombine.Quality=1 r.DOF.TemporalAAQuality=1
                r.DOF.Scatter.BackgroundCompositing=2 r.DOF.Recombine.MinFullresBlurRadius=0.1000 …
景深闸门：游戏画质·景深质量=4（已经是高档位，不是这道闸门）
景深闸门：r.DepthOfFieldQuality 写 3 回读还是 2 → 被钉住了
```

游戏自己的「景深质量」已经是 4（最高档），`r.DOF.*` 也全是引擎默认的健康值。质量档这条线到此为止。

**② 探针证明：相机 POV 这条通道在本作里写了没反应**

- 第 1 段（相机 POV 的 `ColorSaturation`）→ 用户看不到任何变化；
- 第 2 段（自建体积的 `VignetteIntensity`）→ 用户看到了明显变化。

也就是说：**之前十几轮把景深写在「相机 CameraCache.POV 的 PostProcessSettings」上，而这条通道在本作的
gameplay 视图里很可能根本没被采纳。** 这能一次性解释「写入成功、回读正确、画面永远不变」。
（`CameraCache.POV` 至少还被用于位姿，否则相机接管早就不能用了。）

**③ 但「深度模糊」也没糊** —— 它同时写在了相机 POV 和自建体积上，走的是 `DepthBlurRadius > 0`
这条不看光圈的分支。所以要么它也被写着不看，要么自建体积并没有真的进渲染。

### 本版两件事

**A. 新增通道③：关卡自带的 `PostProcessVolume`**

这些体积**一定**在 `UWorld::PostProcessVolumes` 里，是唯一「确定会被引擎采集」的通道。
之前我们只是把它们覆盖的 DOF 摘掉（避免压住相机通道），现在反过来：既然它们已经被快照，
就直接把当前的光圈/焦距/传感器写进去，让最终后处理里一定出现我们这组值；退出时
`restore_level_dof_overrides()` 按快照原样写回。日志会打一行
`景深：已把当前光圈/焦距/传感器同时写进关卡自带的 N 个后处理体积（这条通道一定在世界里）`。

**B. 用引擎自己的命令实测 ShowFlag 位索引**

「第 20 位是不是 DepthOfField」一直是猜的（而且上一版已经证明 Shipping 下位映射不可靠）。
本版改成实测：读一次内存 → 执行 `ShowFlag.DepthOfField 0` → 再读一次 → 执行 `ShowFlag.DepthOfField 1`
→ 再读一次，对比哪一位跟着翻转：

- 有翻转 → 日志写 `真实的 DepthOfField 位就是 wordX:bitY，原本=N`，这条从此不用猜；
- 没有翻转 → 日志写「命令无效或这些位不可写，ShowFlags 这条路到此为止」，也不再浪费轮次。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix16 这次改了什么（撤回 ShowFlags 内存写入 + 加一根「深度模糊」探针）

hotfix15 打包后做了一轮 UE 源码核对，查出三件必须马上改的事：

**① ShowFlags 的内存写入是错的，已全部撤回（安全修复）**

UE 的 `FEngineShowFlags` 只有在 `PLATFORM_USE_SHOWFLAGS_ALWAYS_BITFIELD` 时才是一整块纯位域；
Shipping 构建里 `SHOWFLAG_FIXED_IN_SHIPPING` 的那些开关会退化成 `static const bool`（**没有存储**）。
也就是说「第一个 32 位字 = flag 0~31、第 20 位 = DepthOfField」这个映射在成品包里根本不成立，
实机读到的 `07FFFFFF FFFFFBFF FFFFFFFF FFFFFFFF` 更像是读到了相邻数据。
hotfix14/15 会在这块内存上写 bit20 —— 一旦读数里那一位是 0，就等于改了一个不确定的字段。
本版：**只读不写**，hex 照旧打一行留档，结论改由下面的探针判定。hotfix15 的包作废。

**② 相机 POV 的后处理在体积之后生效，优先级压不过它**

UE 的混合顺序是：后处理体积 → **相机 POV 的 `PostProcessSettings`（weight=1，最后生效、覆盖体积）**
→ 相机动画 PP → `UpdatePhotographyPostProcessing` → 立体覆盖。
所以 hotfix13 花力气「摘掉关卡体积的 DOF 覆盖」其实是不必要的（保留不影响正确性，退出会写回）。
同时也说明：**能顶掉我们 DOF 的只可能是相机 POV 之后那几步**（相机动画 / 摄影模式后处理）。

**③ `DepthOfFieldScale` 关不掉 DiaphragmDOF**

`EndFinalPostprocessSettings` 只在 `r.DepthOfFieldQuality<=0` 或 ShowFlag 关掉时把 `DepthOfFieldScale` 置 0，
而这个字段在 `DiaphragmDOF.cpp` 里出现 **0 次**（它是 CircleDOF 专用的）。所以不用再怀疑它。

### 新增：探针第三段 —— 深度模糊（`DepthOfFieldDepthBlurRadius`）

`DiaphragmDOF::IsEnabled` 其实有两条互斥的放行分支：

```
(Fstop > 0 && FocalDistance > 0) || DepthBlurRadius > 0
```

第二支**完全不看光圈**：`DepthOfFieldDepthBlurRadius > 0` 就够。这正好可以用来区分两种完全不同的病因：

- 如果焦距/光圈是在「进渲染前」被某一步顶掉的 → 深度模糊照样会虚化；
- 如果深度模糊也没反应 → 本作的 DOF pass 根本没被启用（例如 `bSupportsDiaphragmDOF=0` 写在了
  `DataDrivenShaderPlatformInfo` 里，这会让 `DiaphragmDOF::IsSupported()` 直接返回 false，**没有任何告警**）。

探针现在是三段：A 整屏黑白（相机 POV）→ 停顿 → B 四周变暗（自建体积）→ 停顿 →
**C 背景 3 米外糊掉（`DepthOfFieldDepthBlurRadius=400`、`Amount=1.0`，同时写相机和体积）**。

判读：C 段糊了 = 引擎 DOF 活着（问题在焦距/光圈被顶掉）；三段都没反应 = 本作没跑 DiaphragmDOF，
引擎景深这条路走不通，得换方案。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix15 这次改了什么（闸门每 2 秒复核 + 两条后处理通道分开测）

hotfix14 的日志给出了四条都成立、但拼不到一起的事实：

1. `景深闸门：EngineShowFlags@0xB8 = 07FFFFFF FFFFFBFF FFFFFFFF FFFFFFFF` —— bit20（DepthOfField）**本来就是 1**。
   上一版按「低 12 位全 1、12~16 为 0」判形状（那是 5.3 的声明顺序推出来的），实机形状不满足，
   于是每次都被判成「形状不符、没敢写」——**把真正的结论（开着）埋在了误报后面**。
2. `景深质量：r.DepthOfFieldQuality 2 → 3 … | 回读=2` —— 写 3 回读还是 2：这条 cvar 被**钉住**了，
   写了等于没写。之前几版把它当「抬档成功」在记账，其实一直是空转。
3. `sg.DepthOfFieldQuality=0`，而包里存在 `gq.DepthOfFieldQualityLevel`、`gq.PostProcessQualityLevel` 等
   一整族 `gq.*` —— 本作把「景深质量」做成了**自己的独立画质档**（游戏设置里那一项就是它）。
4. 实机现象：进摄影模式后**暗角生效 → 闪一下 → 效果全没了**，之后无论怎么拖光圈/传感器都没有虚化。

`DiaphragmDOF::IsEnabled` 的四个条件（ShowFlags、`r.DepthOfFieldQuality>0`、`fstop>0`、`focal>0`）
从游戏线程这边看全都满足，所以只剩两种可能：**闸门在进入之后被改写**，或者**我们对「哪条后处理通道活着」的判断从一开始就是错的**。
本版把这两件事同时做成可判读的：

### 一、引擎侧闸门每 2 秒复核（`tick_dof_gates()`）

之前只在「改值那一刻」查一次闸门，查完就再也不看；实机上闸门完全可能在进入后几百毫秒被画质系统改写。

- **一次性普查行**（11 个相关 cvar 打一行，省得再一轮轮猜）：
  `景深闸门(普查)：gq.DepthOfFieldQualityLevel=… r.DepthOfFieldQuality=… sg.DepthOfFieldQuality=… r.PostProcessing.MoveDOFAfterUpscaling=… r.DOF.Gather.ResolutionDivisor=… r.DOF.Gather.RingCount=… r.DOF.Recombine.Quality=… r.DOF.TemporalAAQuality=… r.DOF.Scatter.BackgroundCompositing=… r.DOF.Recombine.MinFullresBlurRadius=… r.DepthOfField.MaxSize=…`
- **游戏自己的画质档** `gq.DepthOfFieldQualityLevel`：低于 3 就抬到 3、回读确认，摄影模式期间每 2 秒补位（退出还原）。
  日志写 `景深闸门：游戏画质·景深质量=N → 已抬到 3，回读=M`，或 `已经是高档位，不是这道闸门`。
- **`r.DepthOfFieldQuality` 被钉住**：写不动只告警一次，明确写「不再依赖它」。
- **`ShowFlags.DepthOfField` 被改回去**：每 2 秒重读 word0，发现被清就补位并告警
  `景深闸门：ShowFlags.DepthOfField 进入之后被关掉了 → 已每 2 秒补位`；一直开着则写一行「一直保持开着」。
- ShowFlags 的形状判定改成「位很密、四个字都不为 0」（实机 `07FFFFFF FFFFFBFF FFFFFFFF FFFFFFFF` 通过），
  不再因为 5.3 的声明顺序假设误报，也不会因此不写内存。

### 二、两条后处理通道分开测（探针重做）

上一版探针 A/B 两段改的是**同样三个旋钮**（过曝+暗角+去色），实机上只能看到「闪一下」，
根本分不清是「相机 POV」还是「自建体积」在起作用——判定表因此一直读不准。现在：

- **A 段（1.5 秒）只改 `ColorSaturation`** → 画面**整屏变黑白**；
- 中间留 **1 秒**画面全正常，作对照；
- **B 段（1.5 秒）只改 `VignetteIntensity`** → 画面**四周变暗**。

判读一句话：先黑白后暗角 = 两条通道都活；只有黑白 = 只有相机通道；只有暗角 = 只有自建体积
（说明相机 POV 的后处理压根没被采纳，那景深写在那里当然永远没反应）；都没有 = 两条都没进渲染。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix14 这次改了什么（最后一道闸门：ShowFlags.DepthOfField 位）

hotfix13 的日志给出了两条硬结论：

1. 关卡体积的 DOF 覆盖确实存在并被摘掉了：
   `景深：已临时摘掉关卡体积的 DOF 覆盖 …（原 fstop=F7.1 focal=0 sensor=24.6）` ×3，
   之后 `景深诊断 … DOF覆盖=<无>`。**fstop 原本是 F7.1**（远大于我们写的 0.3）——这条链成立。
2. 但摘掉之后，focal=421cm / fstop=F0.08 / sensor=164mm（公式算 **203px** 背景虚化）依然**毫无虚化**。

也就是说：后处理通道是活的（暗角生效）、结构是对的（2208 字节 / 588 字段）、该覆盖的也摘了，
剩下的只能是**引擎根本不建 DOF pass**。UE5 里 `DiaphragmDOF::IsEnabled` 的最后一道条件是
`View.Family->EngineShowFlags.DepthOfField`；查 UE5 的 `ShowFlagsValues.inl`：

- `DepthOfField` 是 `SHOWFLAG_ALWAYS_ACCESSIBLE`（不是 `FIXED_IN_SHIPPING`）→ **shipping 里是真实存储位**，
  默认由构造函数 `Memset(0xff)` 全开，位置按各 `ALWAYS_ACCESSIBLE` 的声明顺序排：
  `0 PostProcessing … 11 OnScreenDebug 12 VisualizeNanite 13 VisualizeLumen 14 VisualizeSubstrate
  15 VisualizeGroom 16 VisualizeVirtualShadowMap 17 PointLights 18 SpotLights 19 RectLights → 20 DepthOfField`。
- 12~16 在 `Init()` 里被显式关闭，所以 word0 的正常形状是「低 12 位全 1、12~16 为 0、20 以上全 1」——
  这个形状足够确认偏移猜对了。

本版新增 `probe_dof_showflag()`：

- 从 `UGameViewportClient` 实例（偏移 `0xB8`，UE4SS 解出）读 `EngineShowFlags` 的前 8 个 32 位字并打印 hex；
- 用上面的形状判断偏移是否正确（同时兼容 `bool` 布局的平台）；
- 若确认 `DepthOfField`（bit 20）是**关**的 → 直接置位并回读确认，同时打一行
  `景深闸门：ShowFlags.DepthOfField 原本被关掉（word0 少 bit20）→ 已置位`；
- 本来就开着 → 明确写「不是这道闸门」，继续查其它方向；形状不符 → 只在日志里留 hex，**不写内存**。

同时新增 DOF 采样参数读数（`r.DOF.Gather.RingCount` / `r.DOF.HalfRes` / `r.DOF.Recombine.Quality`）：
有些画质档位会把 gather 环数压到 0，那时 DOF pass 照样跑但采不到样本，表现和「通道没生效」一模一样；
读数 <3 时会抬到 3 并记录基线，退出还原。

核心层测试 **5,728 项**全绿（本版同样未动核心逻辑）。

## 2.0.0-cpp-hotfix13 这次改了什么（暗角生效 = 通道是活的 → 真凶是关卡体积盖掉了 fstop）

hotfix12 的探针结果给出了决定性事实：

- `后处理探针：目标结构 字节=2208 字段=588` —— 写的**就是真正的 `FPostProcessSettings`**，结构没错；
- 属性名核对：只有 `ExposureCompensation` 不存在（5.7 改名），其余都在；
- 用户反馈：**进入摄影模式时四周变暗**（`VignetteIntensity=3` 生效）→ **相机后处理通道确实进了渲染**。

也就是说：通道通、结构对、值写进去了，但 `ColorSaturation=0` 没看到、景深也没有。
唯一能同时解释「暗角生效、去色无效、景深无效」的机制是**后处理字段的来源被更高优先级的覆盖顶掉**：
本关有两个**有界** `PostProcessVolume` 显式覆盖了 `DepthOfFieldFstop`（priority 5 / 1，日志里一直有），
它们把自己的光圈值盖在相机通道之上；而暗角没有任何体积覆盖，所以只有暗角看得见。
光圈一旦被盖成体积里的值，CoC 就被压到 ~1px，「完全没有虚化」也就顺理成章。

改法：

- 开启景深时枚举世界里所有 `PostProcessVolume` / `PBZPostProcessVolume`，对**覆盖了 DOF 三项**
  的体积：快照原值（三项 flag + 三项数值）→ 摘掉 `bOverride_DepthOfFieldFocalDistance / Fstop /
  SensorWidth`，让相机通道（本 Mod 写的值）成为唯一来源。
- 关闭景深 / 退出摄影模式 / 世界换代时按快照写回，并打一行 `关卡体积的 DOF 覆盖已还原 N/M 个`。
- 每个被处理的体积打一行 `景深：已临时摘掉关卡体积的 DOF 覆盖 …（原 fstop=F… focal=… sensor=…）`，
  下一份日志能直接看到「体积里原本的光圈是多少」——如果它是 F8~F16 这类值，就证实了这条链。

核心层测试 **5,728 项**全绿（本版同样未动核心逻辑）。

## 2.0.0-cpp-hotfix12 这次改了什么（黑白/红也没出现 → 先证伪「我们写的到底是不是后处理结构」）

hotfix11 的探针日志（12:18:51）显示 A（相机通道去色）、B（自建体积染红）都执行了，
用户反馈**画面完全没有变化**。结合 hotfix8 里同一条通道把虚化算到 33px 也没反应，
现在必须排除一种更基础的可能：**我们写的那个结构/那些属性名，在这个 build 里根本不起作用**。

本版探针改成「自证 + 多重旋钮」：

- **结构自证**：打出被写结构的字节大小与字段总数。真正的 `FPostProcessSettings` 有两百多个字段；
  如果这个数字小得离谱，就说明我们写的不是后处理结构（那一切「写了没反应」都解释得通）。
- **三重旋钮同时改**（只改相机通道）：`ExposureCompensation=+6`（画面发白）、
  `VignetteIntensity=3`（四周发暗）、`ColorSaturation=0`（去色）。任何一个生效都看得见；
  同时避免「某个属性名在 5.7 里改名 → 静默 no-op → 看起来什么都没发生」。
- **属性名自检**：写之前先查 `bOverride_*` / 值属性是否存在，缺哪个就打印出来，
  并把该结构里所有含 `color / exposure / vignette / bloom / depthoffield` 的真实字段名列出来。
  这样下一份日志能直接判定是「属性名不对」还是「后处理没进渲染」。
- 结束语同时给出三条判读（A/B 都没看到 → 后处理设置没进渲染，引擎 DOF 走不通，得换思路）。

核心层测试 **5,728 项**全绿（本版未动核心逻辑）。

## 2.0.0-cpp-hotfix11 这次改了什么（闸门全开、虚化算到 33px 仍无变化：改为分辨「哪条通道真的进了渲染」）

hotfix8 的日志（2026-11-05 11:52:40–11:54:18）把所有已知闸门都排除了：

- `r.DepthOfFieldQuality=2`、`CoC半径上限=0.0250/0.0250`、`override` 全 1、`blendWeight=1.00`、
  自建体积 `unbound=1 priority=10000 blend=1.00`（体积在关卡 8 个体积里优先级最高）
- 独立相机模式下 ViewTarget 确实是我们的 `CineCameraActor`（`管理器 ViewTarget=CineCameraActor`）
- 参数按薄透镜公式算到 **16.6~33.2px@1920**（sensor 304mm / F0.72 / 对焦 538cm），画面仍然完全没变化

也就是说：数值、覆盖标志、混合权重、体积优先级全对，但**要么引擎 DOF 根本没跑**，
**要么我们的后处理写入通道没有进渲染**。这两条路的修法完全不同，必须先分开。

本版只做这件事（进摄影模式后约 2.4 秒，一次性）：

- **探针 A（1.2s，相机通道）**：只把相机通道的 `ColorSaturation` 压到 0 → 画面变黑白。
  看到 = 相机后处理通道确实进了渲染。
- **探针 B（1.2s，自建体积通道）**：只把体积的 `ColorGain` 改成强红 → 画面全红。
  看到 = 这条体积通道被后处理栈采集了。
- 判读（日志里也写了）：两个都没看到 → 相机后处理通道没进渲染，要换写入位置；
  只看到 A → 体积没被采集，景深得写进关卡自己那 8 个体积；只看到 B → 相机通道没被采纳。
- 新增一次性 `MPC 景深候选参数：…` 日志：本作屏幕后处理大量走 MPC（34 个集合、347 个参数），
  若引擎 DOF 被关、景深由自家后处理材质实现，参数名会露在这里
  （按 blur/dof/focus/aperture/coc/bokeh/soft/depth 过滤）。

核心层测试 **5,728 项**全绿（本版未改核心逻辑；数字变化来自 schema 数值范围放宽后
参数校验用例的取值区间变大）。

## 2.0.0-cpp-hotfix10 这次改了什么（景深配置独立节点）

- PBZFM 设置页把 `[dof]` 六项从「相机与移动」分组拆出来，单独成节点 **景深（手动对焦）**：
  `dof_enabled`、`dof_focal_distance`、`dof_fstop`、`dof_sensor_width`、`dof_focus_step`、`dof_aperture_step`。
- 新增词典键 `#group_dof`（zh-CN / en-US / ja-JP / ko-KR）；节点顺序：快捷键 → 行为 → 隐藏 → 冻结增强 → 相机与移动 → **景深** → 手柄。
- 配置键名、范围、默认值与运行逻辑完全不变，纯菜单分组调整；滑条刻度（对焦距离线性 300~1500cm、光圈/步进对数）保持原样。
- `tests/test_pbzfm_validation.cpp` 新增断言：6 个 `dof_*` 字段必须全部属于 `#group_dof`，其它字段不得混入该分组。
- 核心层 **5,728 项**测试全绿。

## 2.0.0-cpp-hotfix9 这次改了什么（按键全量可配 + 删除 F9 截图）

目标：**模组里不再有写死的按键**，并把本作本来就不可用的 F9 截图整套删掉。

配置新增（默认值全部保持旧行为，老 config 不改也能跑）：

| 区 | 新增键 | 默认 |
| --- | --- | --- |
| `[keys]` | `key_move_forward/back/left/right` | `W` / `S` / `A` / `D` |
| `[keys]` | `key_boost` / `key_slow` | `SHIFT` / `CONTROL`（通用名，左右两侧都算） |
| `[keys]` | `key_free_look` | `ALT`（`free_look_requires_alt=false` 时不使用） |
| `[control]` | `wheel_fov` / `wheel_fov_invert` | `true` / `false` |
| `[gamepad]` | `pad_roll_left/right`、`pad_boost/slow` | `LB`/`RB`、`LS`/`RS` |
| `[gamepad]` | `pad_vertical_up/down` | `RT` / `LT`（单键绑定保留扳机模拟量行程） |

- **输入层改为 1..255 全量 VK 轮询**（`win_input.cpp`）。旧实现用一张手写白名单 `kPolledKeys`，
  漏了 `OEM_4`/`OEM_6`——默认 `[`/`]` 光圈键能解析、能在菜单里捕获，但永远收不到按下沿；
  任何改到 `; , . / \ '` 等键的绑定也会静默失效。现在配置成任何 VK 都能生效。
- **删除 F9 无 UI 截图**：本作 Shipping 导出表没有 `FScreenshotRequest::RequestScreenshot`，
  该路径只会打一条「截图能力不可用」，却带着导出表解析、焦点判断、逐帧 `GetAsyncKeyState(VK_F9)`。
  连同 `ue4ss_port` 里的截图 API 解析/请求/焦点函数与成员一并移除（净删约 150 行）。
  摄影模式内 UI 仍由本模组隐藏，截图请用外部工具（NVIDIA / Steam / Xbox Game Bar）。
- **PBZFM 设置页补全键盘键位**：原来只映射 14 项（丢 `key_frame_freeze`、对焦、光圈五键），
  现在 26 项全在菜单里可捕获改键；新增 `wheel_fov` / `wheel_fov_invert` 开关。
- **状态页 / UE4SS 控制台速查改为动态**：显示当前生效的移动/滚转/变速/FOV/升降键名，
  不再写死 `WASD`、`Shift/Ctrl`、`Alt`、`LMB/RMB`、`RT/LT`、`L3/R3`。
- 四语言词典补 14 个新标签，`#help_look` / `#console_help_move` / `#console_help_pad2`
  改成带占位符的动态格式。
- 测试：`tests/main.cpp` 新增默认值、改键、手柄改绑、滚轮开关/反向断言；核心层 **5,720 项全绿**。

## 2.0.0-cpp-hotfix8 这次改了什么（真因是物理量：默认参数与上限根本虚不了）

hotfix7 的自检日志（2026-11-05 11:17）把引擎侧的怀疑全部排除了：

- `r.DepthOfFieldQuality=2`（>0，DOF pass 是开的；把游戏画质里的景深拉到最高也一样，值没变）
- `CoC半径上限=0.0250/0.0250`（正是引擎默认值，没有被压小）
- 相机通道 `override focal=1 fstop=1 sensor=1`、`blendWeight=1.00`；体积通道 `unbound=1 priority=10000 blend=1.00`
- `enabled=<na>`：`DepthOfFieldEnabled` 这个字段在本作（UE5.7）**不存在**，那条闸门排除
- 顺带发现 `exec` 对 `r.DepthOfFieldQuality` 无效（回读仍是 2），但既然值本来就 >0，不影响结论

**真正的原因在物理量。** UE5 的 DiaphragmDOF 是薄透镜模型，本作 FOV 固定，
等效焦距完全由「传感器宽度」反推（`f = 0.5·sensor/tan(hFOV/2)`），虚化像素 ≈
`0.5·CoC/sensor×屏宽`。按玩家真实的落点算：

| 参数（日志原值） | FOV 90° 下的背景虚化 |
| --- | --- |
| focal=578cm F1.6 sensor=60mm | **2.1px** |
| focal=907cm F1.6 sensor=19.9mm | **0.44px** |
| focal=484cm F1.6 sensor=60mm | **2.5px** |
| 旧默认 F4 / 35mm / 500cm | **0.42px** |
| 用户 config：focal=51311cm（513 米）F18.4 / 68mm | **0.00px** |

也就是说：即使景深通道完全正常，这些数值也看不出任何虚化（2px 以下肉眼基本等于没有）。
要明显虚化（≥10px@1920）在 FOV 90°/sensor 60mm 下需要 F≤0.5、或把传感器加到 150~400mm、
或把对焦拉近到 1~3 米。

改法：

- **放宽物理上限**：光圈 0.5→**0.05**（范围 0.05~32，滑条改对数刻度，F0.5 落在约 37%），
  传感器宽度 100→**400mm**（虚化像素与它成正比，它同时代表等效焦距）。
- **默认参数改成「第一次进就能看见」**：`300cm / F0.5 / 60mm` ≈ **9.7px@1920**
  （旧默认 0.42px）。`config.ini.example` 的注释也改成按物理量解释。
- **日志直接给物理量**：`景深写入：… 预计背景虚化≈X.Xpx@1920 …`；低于 2px 时再打一条
  可执行提示（调小 F、调大传感器、拉近对焦）。
- **旧配置迁移**（每次启动一次）：进摄影模式时若判断这组值一定看不出虚化
  （对焦>15m / F>8 / 传感器<10mm，且估算<0.5px），纠正成默认可用值并打印原值；
  只改内存里的 cfg，写回交给既有的 config 保存流程，用户之后照常调。
- **黑白探针**（每进程一次，约 1.5 秒）：进摄影模式时把画面短暂压成黑白再恢复，
  用来区分「虚化太弱」与「相机后处理通道根本没进渲染」——这两种情况的下一步完全不同。
- 新增 `core/dof_math.h`（薄透镜换算，纯函数、无 UE 依赖）；`tests/main.cpp` 增加 12 项断言
  （默认值必须是「看得见」的、三个方向单调性、非法输入归零、旧默认确实 <2px、迁移行为）。

核心层测试 **4,524 项**全绿。

# 变更史（PBZ Photo Mode — 影之刃零摄影模式 / C++）

> 本文件由 `docs/README.md` 拆出（2.0.0 仓库清理）：README 只留面向用户的文档，
> 62 节逐版本「这次改了什么」按**原顺序**整体迁到这里，正文未做任何改动。
> 面向用户的安装与使用文档见 [README.md](README.md)；
> 更详细的取证链（每次真机日志的根因分析、已判死路线）见仓库根的 [PROGRESS.md](../PROGRESS.md)。

## 2.0.0-cpp-hotfix7 这次改了什么（写入一切正常但画面没虚化：先钉住引擎的景深质量档）

第四次实机反馈仍然是「调数值没反应」。这一轮不再猜对焦参数，先把两种完全不同的失败分开：

- **已有证据说明通道是通的**：`post_process_override_tick()` 每帧把 AA / 动态模糊 / GI / 反射 /
  胶片颗粒覆盖成 0，实机有效 → 相机 POV 的 `PostProcessSettings` 写入通道是活的；
  自建 `Priority=10000` 的无界体积也建得起来，关卡 6 个体积都不覆盖 DOF → 后处理栈没在抢景深。
  字段写对了、通道通了，唯独画面没有虚化。
- **闸门在引擎层**：用户目录 `GameUserSettings.ini` 里 `gq.DepthOfFieldQualityLevel=1`。
  UE5 里能让 DOF 直接消失的闸门只有三条，而且都会被低画质档位压到「关」：
  ① `r.DepthOfFieldQuality <= 0`（`DiaphragmDOF::IsEnabled` 直接 false，引擎不建 DOF pass；
  1~4 是同一套 DiaphragmDOF，只是采样质量）；② `ShowFlag.DepthOfField == false`（同样直接 false）；
  ③ `r.DOF.Kernel.MaxForeground/BackgroundRadius` 被压得很小（引擎默认 0.025，Low 档会到 0.006），
  把 CoC 半径夹没。另外 UE 5.4+ 的 `FPostProcessSettings::DepthOfFieldEnabled` 是 per-view 总开关，
  默认 true，被置 false 时同样完全不虚化。

改法（拍照期间把三道闸门调到可用，退出全部还原）：

- 每次开启景深先读 `r.DepthOfFieldQuality`，低于 `tuning::kDofQualityTarget`（3 = High）就执行
  `r.DepthOfFieldQuality 3` 并回读确认；`ShowFlag.DepthOfField 1`；两条 `r.DOF.Kernel.Max*Radius`
  低于 0.025 就抬到 0.025（各自记基线）。退出摄影模式 / 关景深 / 切后端 / 世界换代销毁体积时
  还原成进入前的值；还原失败会报给 core 的退避重试环（1s/2s/4s）再试。
- `write_post_process_dof()` 顺手写 `bOverride_DepthOfFieldEnabled` / `DepthOfFieldEnabled`（5.4+ 才有，
  字段不存在时是 no-op）：POV 的 PP 每帧由 `UpdateCamera` 整份重建，所以这两项不需要还原。
- 新增一次性「景深自检」日志（每个景深生命周期只打一轮）：
  - `景深自检：r.DepthOfFieldQuality=… sg.DepthOfFieldQuality=… CoC半径上限=…/…`（-1=读不到）
  - `景深自检：相机通道 focal=… fstop=… sensor=… | enabled=… | override focal/fstop/sensor/enabled=… | blendWeight=…`
  - `景深自检：体积通道 … | unbound=1 priority=10000 blend=1.00`
  - 读不到的字段一律印 `<na>`（不再印 0，避免把「字段不存在」误读成「值被抹平」）。
  下一份日志即可判定是「没写进去」还是「写进去了没人采纳」，不用再猜。
- 进摄影模式那条渲染状态诊断加读 `r.DepthOfFieldQuality` / `sg.DepthOfFieldQuality`。
- `get_cvar_int` / `exec_console_command` 从 `ue4ss_port_micro_motion.cpp` 的匿名命名空间提到
  `detail`（相机后端也要用），行为不变。
- 闸门记账按「宁可多还原、不可少还原」写：读不到 CVar / exec 失败都**不锁死**、下次改值继续试；
  只要命令真发出去过就按「已改动」记账（回读不一致也还原）；只有**还原成功**才清基线，
  否则下次退出/关景深再试；「本次没改过档位」时基线立即作废（否则会把几个会话前的旧档位写回去，
  等于篡改玩家画质设置）；世界换代时清 `dof_gates_applied_` 让新关卡重新抬档（基线保留）。
  体积通道的失败告警改成一次性，拖滑条不再刷屏。
- 说明：`DepthOfFieldScale` / `FocalRegion` / `Near|FarTransitionRegion` 在 UE5 桌面渲染器里不参与
  CoC 计算（`FPhysicalCocModel::Compile` 只读焦点/光圈/传感器/挤压因子），所以这轮不再怀疑它们。

核心层测试 **4,505 项**全绿（新增 2 条：`kDofQualityTarget` 必须落在 2..3——低于 2 拿不到物理景深
管线，4 是 UE5 的 Cinematic 档、多数引擎只认 0..3）。

## 2.0.0-cpp-hotfix6 这次改了什么（对焦距离滑条窗口直接收到 300~1500cm）

用户建议：既然出效果的范围只在 300~1500cm，**直接把滑条范围改成这段**就行——比换刻度更直观。

采纳，并保留两边的能力：

- **菜单滑条窗口**：`对焦距离` 的滑条范围改为 `tuning::kDofMenuFocalMin/Max`（300~1500cm）。
  窄区间用**线性**刻度（1 像素 ≈ 12cm）比对数更顺手，因此 hotfix5 加在这个字段上的对数刻度撤掉。
- **[dof] 配置与按键步进仍是 1..100000cm**：微距（<3m）与远景（>15m）仍然可以通过
  `config.ini` 或 `PAGE_UP/PAGE_DOWN` 调；菜单只铺最常用的 3~15m。
- 侧效应（可预期）：旧配置里的窗口外值（比如之前测试留下的 2622）打开菜单会贴在滑条右端，
  一旦拖动即夹到 1500；不动滑条则运行时仍用原值。
- `对焦步进`（1..5000cm）保持对数刻度不变——它是「每按一次走多远」，不是对焦位置本身。

补断言：`dof_focal_distance` 必须落在 `kDofMenuFocalMin..Max` 且为线性；`dof_focus_step` 必须是对数。
核心层测试 **4,503 项**全绿。

## 2.0.0-cpp-hotfix5 这次改了什么（对焦距离滑条拖不到可用值，画面自然没景深）

hotfix4 的诊断日志（2026-11-05 10:38）把问题彻底定死了：

- **体积通道正常**：`景深：已生成高优先级无界后处理体积（priority=10000）` 出现，且场上 6 个关卡体积里
  只有两个 **有界** 体积覆盖了 `fstop`（priority 5 / 1，`unbound=0`），其余 4 个都没有覆盖 DOF——
  也就是说后处理栈没有在抢我们的景深，hotfix4 的体积通道也建起来了。
- **真正的问题在数值**：这次所有下发值都是 `focal=2622~86142cm`、`fstop=F16~20.6`，或
  `focal=57304cm + fstop=F1.8`。按 DOF 物理：对焦点在 26~860 米、光圈收到 F16~20 时超焦距只有几米，
  **整个场景都在景深内**；而对焦点在 57 公里时近景物距远小于对焦距离、CoC 趋近 0，同样全清晰。
  也就是说用户每一次落点都落在「物理上不可能有虚化」的区间。
- **为什么拖不到可用值**：`对焦距离` 的滑条是 **1..100000cm 线性刻度**，按菜单里滑条的实际宽度，
  1 像素 ≈ 500cm——可用的 300~1500cm 只占滑条最左侧 0.3%~1.5%，鼠标根本无法稳定落点。
  `对焦步进`（1..5000cm）同理。

修法：

- `PbzfmField` 新增 `logarithmic` 标志；`dof_focal_distance` 与 `dof_focus_step` 改用**对数滑条**
  （ImGui `SliderFlags_Logarithmic | AlwaysClamp`）。对数刻度下 500cm 位于滑条 54% 处，
  拖动整条滑条即可平滑覆盖 1cm~1km。
- 其余 Float 字段保持线性（`fstop` 0.5..32、`sensor` 1..100mm 等跨度小，线性更直观）。
- 补一条断言：凡 `logarithmic` 的 Float 字段下界必须 > 0（ImGui 对数滑条硬约束），
  并锁死上述两个 DOF 字段必须为对数刻度，防止以后被改回线性。
- hotfix4 的 Info 级诊断（体积建立 / 关卡体积覆盖清单 / 每次下发的 focal·fstop·sensor）全部保留。

核心层测试 **4,502 项**全绿。

## 2.0.0-cpp-hotfix4 这次改了什么（数值改得动、但画面没有景深）

用户实机反馈（hotfix3 日志 10:24）：滑条已经能改、`config.ini` 也写进去了（实测落到
`dof_focal_distance=51311`、`dof_fstop=18.4`、`dof_sensor_width=68.1`），但**画面完全没变化**。
日志里既没有 `景深/对焦不可用`，也没有任何写入失败——说明三个数值与三个 `bOverride_*` 都写进了
`PostProcessSettings`，问题出在渲染端没有采纳。

根因（UE 后处理混合语义）：

- 最终后处理 = **相机/POV 的 `PostProcessSettings` 打底**，再按优先级把场上每个 `APostProcessVolume`
  的 `bOverride_*` 字段**盖上**。相机上的设置没有优先级、只是基座；关卡里只要有一个体积覆盖了
  `DepthOfField*`，写在相机/POV 上的手动对焦就永远进不了画面——表现正是「数值全对、画面不动」。
- 接管后端写的是 `CameraCache.POV`，独立后端写的是自建相机组件，两者都逃不出这条规则。

修法：新增**通道②——景深专用后处理体积**。

- 首次下发景深时生成一个 `PostProcessVolume`，`bUnbound=1`、`Priority=10000`、`BlendWeight=1`，
  把 DOF 三项 + 三个 `bOverride_*` 写进它的 `Settings`；高优先级保证压过关卡体积。
- 相机/POV 通道（通道①）保留：两条通道任一成功即视为景深可用；退出摄影模式 / 关闭景深 /
  切后端 / 世界换代时销毁体积，不留残留。
- 体积生成失败（类缺失、属性写不进）只 Warn 并回退到通道①，不影响摄影模式本身。

同时补上能一锤定音的诊断（全部 Info 级，默认 `log_level=info` 就能看到）：

- `景深：已生成高优先级无界后处理体积（priority=10000）…` —— 体积通道是否建立。
- `景深诊断：关卡后处理体积 <全名> | unbound=… priority=… blend=… | DOF覆盖=focal fstop …` ——
  一次性列出场上所有体积的优先级与它们覆盖了哪些 DOF 字段，直接看出是谁在压我们。
- `景深写入：focal=…cm fstop=F… sensor=…mm backend=…` —— 菜单改值/按键改值后实际下发的数值
  （0.5 秒限流，原来是 Debug 级看不到）。

核心层 4,498 项测试全绿（`log_dof_write_if_due` 从 Debug 升到 Info，测试断言同步更新）。

## 2.0.0-cpp-hotfix3 这次改了什么（PBZFM 菜单滑条拖不动，松手回弹）

用户实机反馈（F8 → PBZ Photo Mode → 相机页）：拖动 `对焦距离` / `光圈 F 值` / `传感器宽度` / `对焦步进` /
`光圈步进` 这些滑条，松手后数值弹回原位，调不了。日志里能看到每次松手都提交了
`PBZFM 菜单：<字段名> 已更新` 与 `config.ini 已原子写回`，但写进去的是旧值。

根因在自绘页的草案生命周期，与 [dof] 本身无关（所有 Float 滑条都中招，只是此前没人拖过）：

- 每个页面回调都从 `mirror_` 拷一份**帧内局部** `draft`，Float 滑条按设计「拖动中只落 draft、松手才提交」
  （`render_field_widget` 的注释与实现一致）。但局部 draft 每帧重建 → 拖动的中间值每帧被丢弃；
  松手那一帧 `ImGui::SliderFloat` 已无位移、返回 false，`draft` 里仍是旧值 → `commit_settings(draft)`
  把**旧值**写回 config.ini，表现就是「回弹回原位」。
- ImGui 的拖拽是逐帧增量（`g.DragCurrentAccum` 应用后即扣减），宿主每帧把绑定变量重置回旧值，
  滑条就只能动一帧的位移——这正是「拖不动」的观感。

修法：

- 页面草案改为跨帧保持：新增 `take_draft()` / `store_draft()`，页面渲染完把 draft 写回 `mirror_`；
  松手提交时 draft 里就是拖动后的值。
- 加 `mirror_generation_` 代次：`sync_from_config`（外部重载）与 `commit_settings`（提交）都会自增，
  `store_draft` 发现代次变了就放弃写回，避免「拖动中的旧草案」覆盖同帧发生的配置重载。

影响面：PBZFM 菜单里**所有 Float 滑条**（相机/移动/手柄/冻结各组）一并修好；Bool / 枚举 / 键位捕获
本来就是即时提交，不受影响。核心层测试 4,498 项不变、全绿（该修复位于 `PBZPM_WITH_UE4SS` 的渲染线程代码，
不进核心层测试二进制）。

## 2.0.0-cpp-hotfix2 这次改了什么（景深/手动对焦缺陷修复）

对 2.0.0 的景深/手动对焦做了一轮专项审查（引擎写入路径 + 核心状态机 + 输入映射 + 测试 + 文档一致性），
修掉 6 处会实际影响功能的问题：

- **手柄调焦会把画面滚歪**：`pad_focus_*` / `pad_aperture_*` 四个组合键都带 LB/RB，而这两个肩上键同时是滚转键——
  按住调一格对焦，相机就以 `roll_speed` 持续滚转（0.2 秒歪 12°），构图必坏。现在这四个组合键按住期间
  肩上键只当修饰键、不参与滚转；松开即恢复滚转（`LB+RB` 滚转归零不含 BACK/START，不受影响）。
- **一次写失败 = 整局对焦失效**：`dof_available_` 一旦为 false，tick 里的按键分支永不进入，且没有任何重试路径，
  对焦/光圈键从此静默无响应，只能重进模式自救。新增每秒重探（`tuning::kDofRetryIntervalSec` / `kMaxDofRetries`），
  环境恢复即自动可用（日志 `景深/对焦延迟可用（重探成功）`），连续 10 次失败才彻底放弃并留一条 Error。
- **退出还原写回的是玩家最后调的值**：接管快照与运行值共用同一组 `takeover_pp_*` 变量，
  `enter()` 里紧接着的第一次下发就把快照覆盖成 config 值，`restore_depth_of_field()` 写回的自然是「玩家最后调的值」
  而不是进入前的原值（日志却报「无残留」）。现拆成 `takeover_pp_baseline_*`（只在快照时写）与
  `takeover_pp_focal_distance_` 等运行值两套存储；并补上「会话内关景深→再打开」时重新快照，
  避免上一次 restore 消费掉快照后退出无值可写回。接管自检失败时同时作废快照，避免回退独立相机后
  退出拿它去写游戏 POV。
- **后端热切换丢掉会话内对焦**：切换成功后用 `cfg_` 重新下发，把用户已调好的焦距/光圈打回 config 初值，而
  自动回退路径用的是会话值，两条路径语义相反。现在一律用会话值；且切换失败/回滚分支也会重新下发景深
  （此前回滚出来的后端「有相机但没景深」，要等玩家按第一次对焦键才自愈）。
- **独立相机下关景深不生效**：`set_config` 只在接管模式还原，独立相机组件上的三个 `bOverride_*` 没人撤，
  菜单关掉后画面继续虚化到退出。新增 `CameraViewPort::clear_depth_of_field()`（`Ue4ssPort::clear_depth_of_field`），
  独立相机路径显式撤销覆盖。
- **景深写失败被报成「应用相机位姿失败」**：DOF 结果并进了 `write_takeover_pov()` 的位姿返回值，移动镜头时
  每帧一条误导性 Warn，把真正的原因盖住。DOF 改为独立失败计数 + 限流日志，不再影响位姿写入的成败判定。

另外：

- `[dof]` 的合法范围集中到 `core/tuning.h`（config 解析 / 按键步进 / PBZFM 菜单 / 测试共用一份），不再四份手抄。
- 补 54 项断言：手柄景深组合键不触发滚转（含「只按 LB/RB 仍然滚转」的反向用例）、
  写失败后重探恢复并记账为已应用、重探到上限后 Error 只打一次、对焦距离经 tick 顶到 1/100000 边界、
  独立相机关景深撤销、后端热切换保留会话对焦值、切换失败回滚重新下发景深且配置回滚到实际后端
  （含「还视角失败」与「设置 ViewTarget 失败」两条分支）。
- 核心层 **4,498 项**测试全绿（2.0.0 为 4,444 项）。

## 2.0.0 这次改了什么（冗余清理 + 缺陷修复 + 相机后端热切换）

这一版不加玩法，主要是把审查出来的冗余、以及「同一事实存多份真源并互相漂移」清掉，顺手修掉漂移已经造成的缺陷。

**修掉的缺陷（7 条）**

- **解冻级联分叉**：`toggle_freeze` 里风场/相机晃动两段丢弃了 `freeze_wind` / `freeze_camera_shakes` 的返回值并无条件清标志，
  与相邻的 WPO/布料/自定义布料三段不一致 → 恢复失败后退出路径不再重试，布料与植被可能**永久停在冻结态**。
  已统一为「成功才清标志，失败保留并 Warn」。
- **手柄回退键位错 3 处**：`KeyMap` 的 `pad_*` 初值（ini 解析失败时的回退值）与 `Config` 默认值漂移 ——
  `pad_focus_near` / `pad_focus_far` 漏了 LB 肩键、`pad_aperture_open` 把 **RB 写成 LB**，
  恰好触犯 `config.h` 自己「必须带肩键避免单按 BACK/START 误触」的注释。已对齐并加不变量测试锁死。
- **发行包配置不是代码默认值**：`config.ini.example` 是手抄件且实为「本次测试包」配置 ——
  `freeze_mode=dilation`（默认应是 `pause`）、`freeze_dilation` 差 10⁵ 倍、`freeze_diag=true`，
  还把 `freeze_aa_off` / `freeze_lumen_off` / `freeze_dlss_off` / `freeze_tonemapper_dither_off` 等实验开关全设为 `true`。
  改为由 `tools/gen_config.bat` 从 `default_config_text()` 生成，与代码逐字一致。
- **菜单里坏键名显示 `?`**：自绘控件把读值逻辑内联重写且漏了 `key_fallback`，14 个 fallback lambda 从未生效。改为统一走 `pbzfm_read_field`。
- **4 个零消费者配置项**：`imgui_tab` 补上唯一消费点（控制是否注册控制台面板）；`restore_fov_on_exit` / `restore_view_on_exit` / `dry_run` 删除
  （`dry_run` 是旧计划里的 P6 干跑模式占位，从未实现）。
- **三个 `.bat` 在中文 Windows 上实际是半坏的**：LF 行尾 + 中文注释会让 cmd(936) 的双字节配对吃掉换行、把下一行当命令执行；
  另修掉合并脚本里 `shift` 重设 `%0` 导致 `%~dp0` 解析错目录的问题。统一 ASCII 注释 + CRLF。
- **PBZFM 菜单事件只注册从不反注册**：原注释称「框架没有反注册 API」与 SDK 事实不符
  （`pbzfm.h` 明确 `OnMenuEvent` 返回的 id 就是给 `RemoveMenuEvent` 用的），已补上反注册。

**新能力**

- **相机后端可热切换**：PBZFM 菜单里的「接管当前相机」现在**当场生效** ——
  关掉会立刻拆接管、spawn 独立相机并切 ViewTarget；打开会立刻还视角、销毁独立相机、改接管 POV。
  任一步失败自动回滚到原后端；切换后重新下发当前 FOV 与景深。
- **接管缺失前置钩子时自动降级**：`GameViewportClientTickPost` 注册失败时，本会话自动按 `takeover_current_camera=false` 处理并 Warn
  （早期只报 Error 让用户自己改配置，结果是镜头完全不动）；菜单里也不会再允许把接管打开。
- **版本号可表达热修序号**：`version.h` 新增 `PBZPM_HOTFIX` 与 `PBZPM_VERSION_FULL`，日志/UI/生成的 ini 统一打印完整版本串
  （此前 29 轮 hotfix、52 个发行包的日志里都是同一个 `1.0.75-cpp`，无法定位构建）。

**清理**

- 删除与 UE4SS FetchContent 副本 **md5 全等**的 `third_party/imgui/backends/`（6 文件 / 2,763 行），只留真正编译的 4 个核心源文件。
- `docs/README.md` 829 → 195 行：62 节逐版本变更史整体迁入本文件，打进包的《安装说明》从 116 KB 降到 22 KB。
- 工具脚本 7 → 4 个：删 `pack.ps1`（已被 `pack.py` 取代）、`build_dll_offline.bat`（合并为 `build_dll.bat --offline`）、
  `dump_labels.bat`（0 引用）、13 个一次性 `_*.py`；`gen_config` 修好并迁入 `tools/`（新克隆可直接用）。
- 代码侧删除死函数 4 个（`sync_mode_config` / `time_freeze_active` / `reassert_interval` / `dinput_append_diag`）、
  接口层死方法 2 个、整组零调用的下标 i18n API、6 个只写不读状态、音频一次性对象普查（133 行）、
  F9 落盘观测层（106 行，机制本身保留）；`ui_tab` 收敛为只读诊断面板，可写设置唯一入口改为 PBZFM 菜单页。
- 仓库卫生：删 4 个过期构建目录（1,831 MB）与两份 `%SystemDrive%/` 事故残留（1.88 MB）；`.zcode/` 移出版本库；`.gitignore` 去重。

- 构建状态：核心层测试 **4,444 项全绿**；DLL 编译 exit 0。
- DLL：**2,140,672 字节**，SHA-256 `6300f926a21795443e6db27ddbff5a52aadab60dcdc18a4ee544cba33cdf244b`
- 发布包：`dist/PBZPhotoModeCpp_2.0.0-cpp.zip`（**882,139 字节**，SHA-256 `8c3cf68faf611c7a22972b558e4504652e2f791ce0169e224985ccbb76b006b2`）
  结构：`PBZPhotoModeCpp/{dlls/PBZPhotoModeCpp.dll, languages/*.json, config.ini, enabled.txt, README.md}`，解压到 UE4SS 的 `Mods/` 即可
- 版本串核验：DLL 内 ASCII `2.0.0-cpp` ×2、UTF-16 `2.0.0-cpp` ×1（UE4SS ModVersion），旧串 `1.0.76-cpp` 残留 0 处

## 1.0.76-cpp-hotfix29 这次改了什么（F7 只挂游戏引擎线程，避开桌面输入钩子）

- hotfix28 实测仍反馈“F7 期间 Ctrl+P 截图卡顿”。这张日志给出两个关键事实：
  1. F7 现在能手动恢复：三次定格分别是 **4172 ms / 43360 ms / 6047 ms**，不再是固定的 20,016 ms 安全阀值；
  2. PixPin 日志里 Ctrl+P 进入截图到截图窗口显示只要 **170 ms**（13:19:24.835 → 13:19:25.005），截图工具本身并不慢。
  因此剩余问题更像是 **F7 仍挂起了其他第三方全局钩子/覆盖层线程**（Steam/NVIDIA/输入法/其他 DLL 的 WH_KEYBOARD_LL/WH_MOUSE_LL），导致按键投递延迟或输入卡顿。
- hotfix29 改为**白名单挂起**：F7 只挂 `PBZ.exe`、`MRTCEngine.dll`、`AkSoundEngineDLL.dll` 这些游戏引擎模块内的线程；其余第三方覆盖层、输入钩子、驱动辅助线程全部保留。PBZFM 与本模组输入钩子线程仍保留显式跳过作双保险。
- 恢复日志新增：`画面定格已恢复：恢复 N 个线程（跳过 M 个非游戏/输入钩子线程）...`——下一版排障可直接确认跳过数。
- 注意：如果此版发现画面不再完全静止，说明渲染线程不在上述白名单模块内；把日志发我，我会补模块或提供“全线程”回退开关。
- 构建状态：核心层测试 **4398 项全绿**；DLL 编译 exit 0。
- DLL：1,340,416 字节，SHA-256 `f0cbdbecbdcb10971062d678097ea1a5aec97c93b529eae4473cb7bb9ec3b5e6`。
- 预置包 `PBZPhotoModeCpp_1.0.76-cpp_hotfix29_framefreeze_preset.zip`：解压即用。
- 发布包 `PBZPhotoModeCpp_1.0.76-cpp_hotfix29.zip`（与 `_framefreeze_preset` 同内容）。

## 1.0.76 这次改了什么（修复：进入相机模式自动冻结后，布料件整体换位）

用户反馈（2026-11-04 13:02 日志，hotfix27）：「按下 F2 进入相机模式自动冻结后，人物服装的布料皮肤会在一次卡顿后更改位置」——布料件整体挪位、褶皱形状基本不变，每次进入稳定复现，进入后约半秒变一次、之后固定。

定位：进入冻结的 63ms 内 mod 连写了 8 组“一次生效”的状态，其中两处会把 Sway/布料一次性推成另一种姿态；而它们都在 420ms 渲染 CVar 排空卡顿前落地、卡顿后第一帧才共同可见：
1. `r.MorphTarget.Enable=0` / `r.MorphTarget.ForceUpdate=0` 被塞在 `freeze_wpo_velocity_off` 的“关速度输出”批次里——它是全局关 morph 求值，布料姿态修正/皮肤 morph 会一次性消失（与本批用途无关，属于 A/B 诊断开关的副作用）。
2. `pbz.Sway.SkipSkeletalKinematicBoneUpdate=1`——停掉 Sway 的骨骼写出后，布料网格回到动画姿态，表现为整件布料换位、褶皱形状不变（与用户描述吻合）。

本版改动：
- 从“关速度输出”批次移除 `r.MorphTarget.Enable/ForceUpdate`；保留 `r.Velocity.EnableVertexDeformation`、`r.Velocity.EnableLandscapeGrass`、`r.HairStrands.ForceVelocityOutput`、`r.Nanite.SkeletalMesh.AllowWPO`、`r.HairStrands.Simulation`。
- 不再写全局 CVar `pbz.Sway.SkipSkeletalKinematicBoneUpdate`。冻结时改用 `ForEachUObject` 全对象扫描“类名含 Sway”的对象（前两次冻结、硬编码类名未命中时各一次）：真实类名/实例写进日志（`Sway 全对象扫描：...`），能停 tick 的组件停 tick（保留最后一次写出的骨骼姿态），解冻恢复。
- 布料挂起失败日志带组件全名（截断尾部），可直接看出挂不住的是玩家还是 NPC。
- 渲染 CVar 写入逐项计时：单项 ≥50ms 打 `渲染 CVar 写入耗时：<cvar> <ms> ms`，用来定位进入冻结时的卡顿来源（本次实测卡顿 368–454ms，落在该批 CVar 写入窗口）。
- 核心层 4398 项测试全绿；DLL 编译 exit 0。

判读：进入摄影模式后布料不再“整体换位”即修复。若冻结期布料又开始缓慢摆动/闪烁，把新日志里 `Sway 全对象扫描：...` 那一行发回来（会给出真实类名与实例名），下一版按类名定点停 tick。

## 1.0.75-hotfix28 这次改了什么（F7 定格与 Ctrl+P 截图冲突：输入钩子线程不再被挂起）

- 现象：F7 定格后按 Ctrl+P 截图会卡顿，截图后时间暂停失效。UE4SS.log（hotfix27 实测）里可以直接看到一条：
  `画面定格已恢复：恢复 320 个线程，定格 20016 ms`——**20 秒安全阀自动恢复**，说明 F7 的恢复键在定格期间没有送达；这正是“截图后时间暂停失效”的直接原因。
- 根因：PBZFM 在游戏进程里装了 `WH_KEYBOARD_LL`/`WH_MOUSE_LL`，本模组也装了 `WH_MOUSE_LL`；低级钩子的回调只派发到“安装钩子的那个线程”。hotfix27 的 F7 把这些线程一并挂起后，Windows 的 LowLevelHooksTimeout 机制会让整个桌面的键鼠输入卡住：
  - Ctrl+P 的按键事件和截图框选的鼠标移动都卡住 → 表现为“Ctrl+P 截图会卡顿”；
  - 再按 F7 恢复也被卡住 → 20 秒安全阀自动解冻 → 表现为“截图后时间暂停失效”。
- 修复：
  1. `ProcessFreezer` 增加跳过线程/模块名单：F7 时排除 `PBZFM.dll` 的全部线程与 `PBZPhotoModeCpp.dll` 的输入钩子线程（raw input / WH_MOUSE_LL 所在线程）；**游戏自己的渲染/音频/逻辑线程仍然全部挂起，屏幕仍是最后一帧**。
  2. 恢复线程前清空冻结期间累积的鼠标位移/滚轮，避免截图框选的鼠标轨迹在恢复瞬间把相机带跑。
  3. `frame_freeze_timeout_seconds` 默认从 20 秒改为 **60 秒**，给一次截图框选留足时间（仍可配 5~300 秒）。
- 使用注意：Ctrl+P 对应 PixPin/N 卡截图时请用**区域截图（桌面捕获）**，不要用“窗口截图”——F7 期间游戏窗口线程是停住的，窗口捕获要向窗口发同步消息，必然等超时；桌面区域截图不受影响。
- 构建状态：核心层测试 **4398 项全绿**；DLL 编译 exit 0。
- DLL：1,330,688 字节，SHA-256 `ac748da17e80b221fe766ed97bd7bd849ddee092e161653e4ea1f98b662401a8`。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix28_framefreeze_preset.zip`：解压即用。
- 发布包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix28.zip`（与 `_framefreeze_preset` 同内容）。

## 1.0.75-hotfix27 这次改了什么（F7 内置冻结渲染帧：PBZ Pause 式全线程冻结 + 时间补偿）

- 用户参考 `_ref/pbzpause/ANALYSIS.md`（PBZ Pause 1.0.5 逆向分析）提出：把“冻结渲染帧”做成内置功能。hotfix26 的 F7 已经是“挂起全部线程、屏幕停在最后一帧”的雏形；本版按 PBZ Pause 补齐三块关键机制，使它在恢复后不留下时间跳变/音频状态问题：
  1. **线程枚举与死锁防护**：改用 `NtGetNextThread` 枚举（`CreateToolhelp32Snapshot` 回退），挂起前 `GetProcessHeap()+HeapLock()`、挂起后 `HeapUnlock()`，降低线程恰好停在堆操作中间导致的死锁风险；线程句柄用 `DuplicateHandle` 保存，恢复时逐个 `ResumeThread+CloseHandle`。
  2. **Wwise 协调**：若摄影模式本身没有处于 Wwise 整引擎挂起状态，F7 预先在引擎 Tick 线程调用 `AK::SoundEngine::Suspend(true,false)`（render-anyway 只静音、保语音状态），恢复后再 `WakeupFromSuspend(0)`；摄影模式已经挂起的则沿用，不重复挂/唤醒。
  3. **时间 API IAT 补偿（PBZ Pause 同款）**：对 `PBZ.exe`、`MRTCEngine.dll`、`AkSoundEngineDLL.dll`、Media Foundation、`audioclient.dll`、`msvcp140.dll`、`ucrtbase.dll` 的 `QueryPerformanceCounter` / `GetTickCount` / `GetTickCount64` / `timeGetTime` 导入槽换成钩子：定格期间返回定格瞬间值，恢复时把定格时长累加进 offset 并从返回值扣掉，动画/物理/粒子/计时器不会把整段定格当成一次巨大 delta。**该钩子只在 F7“连渲染线程一起停”时启用**；摄影模式引擎暂停仍不冻结 QPC（1.0.64~1.0.66 实机教训：会移动视角卡顿）。模组自身与 UE4SS.dll 不在改写列表里，watcher 的热键/安全阀计时始终是真实时间。
- 新增配置（`[effects]`）：
  - `frame_freeze_time_compensation = true`：F7 时间 API 补偿总开关；`false` 时恢复后可能看到时间跳变。
  - `frame_freeze_audio_suspend = true`：F7 前是否由本模组补挂 Wwise 整引擎（摄影模式已挂则自动沿用）。
  - `frame_freeze_timeout_seconds = 20`：安全阀，5~300 秒；即使 F7 恢复消息收不到也会自动恢复，避免永久卡死。（hotfix28 起默认改为 **60 秒**，给一次截图框选留足时间）
- 行为说明：定格期间**相机/输入不响应**（全线程都停），这是“内置冻结渲染帧”的预期取舍；构图请在实时预览里完成，然后 F7 定格、截图、再按 F7 恢复。日志会打出 `画面定格准备：时间补偿=已安装（模块 N，IAT 槽 M）；Wwise=...` 与 `画面定格已恢复：恢复 N 个线程，定格 X ms（时间轴已扣掉定格时长）`。
- 构建状态：核心层测试 **4398 项全绿**；DLL 编译 exit 0。
- DLL：1,329,664 字节，SHA-256 `7c428c653226f274028bac25cc4a53b0f05f2f58d1dcb3ef5fb4aaadeeb2756e`。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix27_framefreeze_preset.zip`：解压即用（画面优先配置保留 DLSS/TSR/Lumen/胶片颗粒，F7 负责绝对静止）。
- 发布包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix27.zip`（与 `_framefreeze_preset` 同内容）。

## 1.0.75-hotfix26 这次改了什么（画面定格快捷键 F7）

- 用户确认：hotfix25 的闪烁已到可接受程度，但画面与正常游玩差别大（因为关闭了 DLSS/TSR/Lumen/胶片颗粒）；quality 预置保留正常画质时闪烁又回来。两者无法同时满足，所以增加**画面定格**：正常画质预览构图 → F7 定格 → 从容截图 → F7 恢复。
- 新增快捷键 `key_frame_freeze = F7`（可在 `[keys]` 改键）。仅在摄影模式内生效：
  - 按 F7：挂起本进程除热键线程外的**所有线程**（渲染线程、游戏线程、音频线程等），屏幕停在最后一帧；
  - 再按 F7：恢复全部线程，游戏从定格前继续。
- 实现方式：`mod_app.cpp` 的 `frame_freeze_watch_loop()` 独立 Win32 线程 + `RegisterHotKey` 消息队列（`GetAsyncKeyState` 仅作按下沿兜底），用 `CreateToolhelp32Snapshot` 枚举本进程线程并 `SuspendThread/ResumeThread`。定格期间游戏线程不跑，所以不存在“渲染还在动”的残余闪烁；相机也不响应输入，属于预期取舍。hotfix27 起改为 `NtGetNextThread` + `HeapLock` + 时间 API 补偿。
- 注意：定格期间不要切窗口/强退；截图完成后先按 F7 恢复。若 F7 被其他程序占用导致注册失败，会自动改注册 **Ctrl+F7**；如果连按键消息都收不到，安全阀会在 **20 秒**后自动恢复，避免永久卡死。长时间定格（数十秒以上）担心显卡驱动 TDR，拍完尽快恢复即可。
- hotfix26 预置包采用**画面优先**配置（`freeze_aa_off/dlss_off/lumen_off/tonemapper_dither_off` 全 false，保留 DLSS/TSR/Lumen/胶片颗粒），把“画面正常”交给实时预览，把“绝对静止”交给 F7。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix26_framefreeze_preset.zip`：解压即用。加载日志会有一行 `画面定格快捷键已启用：F7` 确认 watcher 线程已启动。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix26.zip`（预置包另加 `_framefreeze_preset`）。

## 1.0.75-hotfix25 这次改了什么（树叶/布料专项：MPC 风场 + 实例化植被 WPO + FXAA）

- hotfix24 实测（10:15–10:17）用户反馈“闪烁似乎减轻，但树叶和布料区域仍有一些闪烁”。日志里 `MPC 变化` 已经从 GrassInteractor 变成：
  `UltraDynamicWeather_Parameters.FieldRefOrigin(向量) | FieldOriginRel(向量) | MPC_Water.PrevTime 25.694→25.721`
  这三个参数正好是树叶/植被风场与水面材质时钟，之前不在热点白名单里，4Hz 全量写回之间仍会漂移。
- hotfix25 三处针对性收口：
  1. MPC 热点前缀再扩展：`FieldRef*`、`FieldOrigin*` 以及 `MPC_Water.PrevTime` 逐帧写回。
  2. WPO 评估遍历补上 `InstancedStaticMeshComponent` / `HierarchicalInstancedStaticMeshComponent` / `FoliageInstancedStaticMeshComponent` / `SplineMeshComponent`——树叶通常是实例化植被组件，之前的 `StaticMeshComponent` 扫描可能漏掉子树。
  3. 冻结时 AA 从“完全关闭（r.AntiAliasingMethod=0）”改为 **FXAA（=1）**：树叶/布料这类细边缘在无 AA 下会因像素走样持续闪，FXAA 是空间抗锯齿、不依赖历史帧，不会产生 TSR 那种重影，只做边缘平滑。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix25_preset.zip`：开关与 hotfix24 相同，解压即用。
- 判读：若树叶/布料边缘明显更干净 → 实例化植被 WPO/FXAA 命中；若纹理内部仍有流动感 → 看日志 `MPC 变化` 是否还有 Field/Water 参数，下一步继续扩白名单。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix25.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix24 这次改了什么（MPC GrassInteractor 前缀写回）

- hotfix23 实测（10:04–10:08）用户反馈“好像没啥变化，不过闪烁确实很少了”。日志显示 `MPC 写回：本轮成功 2 个参数` 确实逐帧在写，但 `MPC 变化` 从 `GrassInteractor0` 变成了 `GrassInteractor1` / `GrassInteractor2`：原白名单只写了 `GrassInteractor0` 和 `CameraFadeStartLocation`，游戏实际有多个草地交互子（角色/NPC 各自的 interactor），编号不是 0。
- hotfix24 把白名单改成前缀匹配：`GrassInteractor*`（0/1/2/…）和 `CameraFade*` 全部逐帧写回；其余参数仍 4Hz 全量写回。
- 另外 hotfix23 日志首次打印出首个体积原值：`AA覆盖=0 方法=0；MB覆盖=0 量=0.50；GI覆盖=0 方法=1；反射覆盖=0 方法=1；颗粒覆盖=0 强度=0.00`。这说明首个 PPV 本身没有 override（bOverride 全 0），hotfix22 的改善来自我们把 GI/反射方法强制写成 0 后，某些体积/视图不再回退 Lumen；与 hotfix23 的 MPC 修正无关——所以这版重点仍是把剩余 MPC 热点钉住。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix24_preset.zip`：开关与 hotfix23 相同，解压即用。
- 判读：若残余闪烁再减少 → MPC 多 interactor 窗口就是最后一条；若完全不变，把新日志发回来（重点看 `MPC 变化` 是否还出现 GrassInteractor 编号）。下一步只剩阴影/VSM 或自动曝光锁。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix24.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix23 这次改了什么（MPC 热点参数逐帧写回）

- hotfix22 实测（09:53:18–09:55:25）闪烁**得到改善**，日志确认：`后处理覆盖：体积 39 个（命中 39 个；AA 0 / 动态模糊 40 / GI 40 / 反射 40 / 胶片颗粒 40）`。AA 计数为 0 说明 UE5.7 的 `FPostProcessSettings` 里没有 AA override 字段（AA 只能走 `r.AntiAliasingMethod` cvar）；改善来自动态模糊 / GI / 反射 / 胶片颗粒四项 PPV/POV 覆盖，方向确认。
- 剩余日志里最扎眼的是 `MPC 变化:UltraDynamicWeather_Parameters.CameraFadeStartLocation(向量) | UltraDynamicWeather_Parameters.GrassInteractor0(向量)` 几乎每 10 秒都出现。原因是 `mpc_hold_snapshot()` 为降低 ProcessEvent 开销限频在 **4Hz**，而游戏每帧都在改写这两个参数——两次写回之间最多有 250ms 的窗口，足够让草地/天气材质继续微动，表现为残余闪烁。
- hotfix23 只改这一点：`UltraDynamicWeather_Parameters` 集合里的 `CameraFadeStartLocation`、`GrassInteractor0` 两个热点参数**逐帧写回**（2 次 ProcessEvent/帧），其余 345 个参数仍按 4Hz 全量写回，开销可控。
- 顺手修正诊断：`后处理覆盖首个体积原值` 不再要求 AA 字段存在，会打印真实命中的动态模糊/GI/反射/颗粒原始覆盖值，便于确认 PPV 到底强制了什么。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix23_preset.zip`：开关与 hotfix22 相同（`freeze_aa_off=true`、`freeze_lumen_off=true`、`freeze_wpo=true` 等），解压即用。
- 判读：若残余闪烁再减轻/消失 → MPC 4Hz 窗口就是最后一条；若完全不变，把新日志发回来（重点看 `MPC 变化` 是否还频繁、`后处理覆盖首个体积原值`），下一步考虑自动曝光锁或帧定格。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix23.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix22 这次改了什么（PostProcessVolume / 相机 POV 覆盖）

- hotfix21 实测日志（09:35:18–09:37:xx）确认：`毛发 0 个`（本作没有 Groom 组件）、`Niagara 462 个（本次暂停 462）`、`其他图元 5671 个（本次关闭 WPO 5061 个）`，但用户反馈闪烁仍存在。→ **内容层（骨骼/静态 WPO/粒子）已经全部压住，剩下的只能是渲染管线里“cvar 改不到”的覆盖层。**
- 关键判断：`r.AntiAliasingMethod`、`r.DynamicGlobalIlluminationMethod`、`r.ReflectionMethod`、`r.MotionBlurQuality` 这些 cvar 只是“项目默认值”；如果游戏用 `PostProcessVolume`（本作有 `PBZPostProcessVolume`）或相机 `CameraCache.POV.PostProcessSettings` 显式 override，最终 view 仍按 override 渲染。此前 Lumen 六档 / 阴影八档 A/B “全都一样”也符合这个解释——cvar 全改了，但 PPV override 没变。
- hotfix22：当 `freeze_aa_off=true` 时，除原有 cvar 外，还直接改 PPV 与相机 POV 的 `FPostProcessSettings`：
  - `bOverride_AntiAliasingMethod=1`、`AntiAliasingMethod=0`（真正关 TSR/TAA，而不是只改默认值）
  - `bOverride_MotionBlurAmount/Max=1`、`MotionBlurAmount/Max=0`
  - `bOverride_DynamicGlobalIlluminationMethod=1`、`DynamicGlobalIlluminationMethod=0`（真正关 Lumen GI）
  - `bOverride_ReflectionMethod=1`、`ReflectionMethod=0`（真正关 Lumen/SSR 反射）
  解冻/退出按每个体积的原值逐项恢复；相机 POV 的 override 在 GameViewportClient Tick Post 每帧重申。
- 日志新增：
  - `后处理覆盖：体积 N 个（命中 M 个；AA x / 动态模糊 y / GI z / 反射 w / 胶片颗粒 v），相机POV AA=已覆盖/不可用`
  - `后处理覆盖首个体积原值：AA覆盖=… 方法=…；MB覆盖=… 量=…；GI覆盖=… 方法=…；反射覆盖=… 方法=…；颗粒覆盖=… 强度=…`
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix22_preset.zip`：在 hotfix21 基础上额外把 `freeze_lumen_off=true`（cvar 与 PPV override 双路关 Lumen GI/反射），解压即用。
- 判读：
  - 若闪烁消失、但画面明显变平/变暗（GI/反射被真关）→ 证明 PPV override 是根因；下一版只保留 AA/动态模糊的 override，把 GI/反射恢复成游戏设置，兼顾画质与稳定。
  - 若闪烁仍在 → 看 `后处理覆盖首个体积原值`：若 `AA覆盖=1 方法=4` 之类，说明 PPV 确实在强制 TSR；若 `体积 0 个` 或全部 `方法=0`，说明 PPV 不是变量，只剩最后的“冻结渲染帧”方案（见下）。
  - 关于冻结渲染帧：本作 `FreezeRendering` 只是冻结可见性剔除（DebugCamera 的功能），不是保留最后一帧。**1.0.75-hotfix27 起已内置为 F7 画面定格**：参考 PBZ Pause 用全线程挂起 + 时间 API 补偿，屏幕停在最后一帧，代价是定格期间相机不再实时取景（构图在实时预览完成，见 hotfix27 说明）。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix22.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix21 这次改了什么（全图元 WPO / 毛发模拟 / Niagara 粒子）

- hotfix20 实测日志（09:09:35–09:12:38）确认 `r.Nanite.SkeletalMesh.AllowWPO=0`、`r.MorphTarget.Enable=0`、`r.MorphTarget.ForceUpdate=0`、`r.XeSS.Enabled=0`、`r.XeFG.Enabled=0` 全程为 0，`MPC 变化:无` 占绝大多数，`视角微动:位移0.000 旋转0.00°`，但用户反馈闪烁仍在。→ **可反射的骨骼渲染路径已经关尽，剩余变化只能来自“非骨骼”的顶点/粒子/毛发路径。**
- hotfix21 补最后三条内容层路径，全部并入现有开关（不新增配置项，预置包保持原开关）：
  1. **静态网格/植被/地形 WPO**：`freeze_wpo` 现在除 SkeletalMesh 的 `SetEvaluateWorldPositionOffset(false)` 外，还遍历 `StaticMeshComponent` / `LandscapeComponent`，把 `bEvaluateWorldPositionOffset` 写 false 并记录原值。覆盖草、树叶、灌木、水面、旗帜等非骨骼材质顶点动画——这些在户外场景（PBZ_Xigu_WP）里最容易表现为细边缘持续闪烁。
  2. **Groom 毛发模拟**：`freeze_custom_cloth` 现在遍历 `GroomComponent`，把 `bSimulationEnabled` 写 false（保留当前发丝姿态），解冻恢复。发丝模拟走独立更新，暂停骨骼动画不会停它。
  3. **Niagara 粒子**：同一函数遍历 `NiagaraComponent`，调用 `SetPaused(true)`（保留当前粒子位置；没有 SetPaused 的构建退化为停组件 tick），解冻恢复。雨/雪/余烬/尾迹等 GPU 粒子按真实时间或固定步长演化，TimeDilation 拦不住。
  4. 另外把 `r.HairStrands.Simulation=0` 并入 `freeze_wpo_velocity_off` 目标组，作为毛发模拟的全局兜底。
- 日志新增：
  - `WPO 评估冻结：SkeletalMesh N 个（本次关闭 X；无函数 0 个）；其他图元 N 个（本次关闭 WPO M 个）`
  - `自定义布料/摆动冻结：... 毛发 N 个（本次停模拟 M）；Niagara N 个（本次暂停 M）；...`
  - 冻结诊断末尾多出 `毛发=N暂停 粒子=N暂停 WPO图元=N关`
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix21_preset.zip`：开关与 hotfix20 完全相同（`freeze_wpo=true`、`freeze_custom_cloth=true`、`freeze_wpo_velocity_off=true` 等），解压即用。
- 判读：进摄影模式冻结后看三行日志。若 `其他图元` / `毛发` / `Niagara` 都有非零计数且闪烁消失 → 根因就是这些路径；若计数为 0，说明本场景没有这类对象，把日志发回来。若三者都非零且闪烁仍在 → 内容层已无残留，下一版只能做“冻结渲染帧”（保留最后一帧，相机不再实时取景），届时会先征求你的意见。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix21.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix20 这次改了什么（Nanite Skeletal WPO / MorphTarget / XeSS 兜底）

- hotfix19 日志里 `MPC 变化` 仍偶尔出现 `UltraDynamicWeather_Parameters.CameraFadeStartLocation / GrassInteractor0`，说明即便在 GameViewportClient Tick Post 写回，游戏仍有某条路径在渲染前/渲染中改写 MPC；同时用户反馈闪烁仍在 → MPC 不是唯一源，继续收口。
- 还剩两个此前没覆盖的全局顶点/超分路径：
  - **Nanite 骨骼网格的 WPO**：`r.Nanite.SkeletalMesh.AllowWPO` 是 Nanite 专用开关，`SetEvaluateWorldPositionOffset(false)` 管不到它；
  - **MorphTarget**：`r.MorphTarget.Enable` / `r.MorphTarget.ForceUpdate` 可能每帧强制更新形态目标；
  - **XeSS/XeFG**：此前只关了 DLSS/FSR，若游戏实际选的是 XeSS 时域超分，`r.AntiAliasingMethod=0` 也管不到。
- hotfix20 把 `r.Nanite.SkeletalMesh.AllowWPO=0`、`r.MorphTarget.Enable=0`、`r.MorphTarget.ForceUpdate=0` 并入 `freeze_wpo_velocity_off` 的目标组；把 `r.XeSS.Enabled=0`、`r.XeFG.Enabled=0` 并入 `freeze_dlss_off` 的目标组（两个开关在预置包里本来都是 true）。日志 `渲染状态` 会多出这几项。
- 预置包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix20_preset.zip`：开关与 hotfix19 相同，解压即用。
- 判读：若闪烁消失 → Nanite WPO / MorphTarget / XeSS 是最后一条路径；若仍一样，且日志显示这些都为 0，则所有可反射的渲染路径已关尽，只能改“冻结渲染帧”（不再逐帧重渲染）。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix20.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix19 这次改了什么（MPC 快照改到渲染前写回）

- hotfix18 日志确认：
  - `渲染=` 每 10 秒都是 `cr.PBZ.FrameGeneration.Backend=0，r.PBZ.SuppressFrameGen=1，r.Streamline.DLSSG.Enable=0，r.NGX.DLSS.Enable=0，r.AntiAliasingMethod=0，r.Tonemapper.Quality=2` → **FG / DLSS / AA 确实全程关闭**；
  - 同时出现 `渲染对照重申：游戏把 2 个 CVar 改写，已压回目标值`，说明游戏确实会周期性改回某些渲染 CVar，hotfix18 的每帧重申已经在兜底，仍闪 → 不是这 2 个 CVar。
- 日志里还有一条长期被忽略的线索：`冻结诊断 ... MPC=34集/347参 变化:UltraDynamicWeather_Parameters.CameraFadeStartLocation(向量) | GrassInteractor0(向量)`。这说明 **MPC 快照写回发生在 EngineTickPre（World Tick 之前）**，游戏在 World Tick 里又把参数改回去；所以 `freeze_mpc` 实际没有赢回写入权，MPC 驱动的材质仍在动。
- hotfix19 修复：在 `GameViewportClient Tick Post`（World Tick 之后、RedrawViewports 之前）再执行一次 `mpc_hold_snapshot()`，确保渲染用的是我们快照的值；pre-tick 的写回保留作为兜底。日志中 `MPC 变化` 应稳定为 `无`。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix19_preset.zip`：开关与 hotfix18 相同（`freeze_mpc=true`、`freeze_wpo=true`、`freeze_custom_cloth=true` 等），解压即用。
- 判读：如果闪烁消失/明显减轻 → 根因是 MPC 写入时序（材质参数被游戏在 World Tick 覆盖），下一版把 `mpc_hold` 固定到渲染前；若 `MPC 变化` 仍不是“无”，把日志发我，继续查具体集合。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix19.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix18 这次改了什么（渲染 CVar 每帧重申 + Tonemapper 档位保护）

- hotfix17 日志暴露了一个关键事实：本作 `r.Tonemapper.Quality` 原始值是 **2**（低档，本来就没有 GrainJitter），我们却把它 2→4，反而可能打开了 Grain；同时 `r.BackbufferQuantizationDitheringOverride=0` 早就已经是 0。Tonemapper 这条路排除，并修正为“只有原值 >=5 才降到 4，绝不反向拉高”。
- 另一个此前漏掉的可能：游戏可能在每个 World Tick 把 `r.Streamline.DLSSG.Enable` / `r.PBZ.SuppressFrameGen` / `r.NGX.DLSS.Enable` / `r.AntiAliasingMethod` 改回去，导致冻结时写的“关 FG/DLSS/AA”只在第一帧有效。hotfix18 在 `GameViewportClient Tick Post`（渲染前）每帧重申 `render_cvar_snapshots_` 里所有目标值，若游戏改写就立刻压回，并在日志输出：
  `渲染对照重申：游戏把 N 个 CVar 改写，已压回目标值`。
- `freeze_diag` 增加运行时渲染值（每 10 秒一条）：
  `渲染=cr.PBZ.FrameGeneration.Backend=... r.PBZ.SuppressFrameGen=... r.Streamline.DLSSG.Enable=... r.NGX.DLSS.Enable=... r.AntiAliasingMethod=... r.Tonemapper.Quality=...`
  这样能直接看出 FG/DLSS/AA 是否被游戏重置。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix18_preset.zip`：`freeze_tonemapper_dither_off=true`（现在不会再拉高 tonemapper）、`freeze_wpo=true`、`freeze_custom_cloth=true`，其余保持 hotfix10 值。
- 判读：如果闪烁消失，说明是游戏每帧重置了 FG/DLSS/AA，重申修复；如果仍一样，看 `渲染=` 这行——若 `r.PBZ.SuppressFrameGen=1` 且 `r.Streamline.DLSSG.Enable=0` 仍是 0，则 FG 确实已关，下一步只剩“冻结渲染帧”方案。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix18.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix17 这次改了什么（Tonemapper GrainJitter 逐帧抖动关闭）

- hotfix16 实测（22:47:24–22:48:42）确认 WPO 这次真正生效：`WPO 评估冻结：SkeletalMesh 150 个（本次关闭 150；无函数 0 个）`，第二次进入 168/168；解冻也按 150/168 恢复。说明参数名兼容修复成功，WPO 评估可以完整关闭。
- 如果这版仍有闪烁，剩下的最大嫌疑是 **Tonemapper 的 GrainJitter**：UE5 的 `r.Tonemapper.Quality` 档位说明里明确写着 `5: + GrainJitter = full quality (default)`——默认 5 会每帧给 tonemapper 加随机抖动（不是 FilmGrain，`r.FilmGrain=0` 管不到它），在布料细纹理上就是高频闪烁。
- hotfix17 新增 `freeze_tonemapper_dither_off`（默认 false，测试包预置 true）：冻结时把 `r.Tonemapper.Quality` 从 5 降到 **4**（去掉 GrainJitter，保留 Grain 及以下），同时 `r.BackbufferQuantizationDitheringOverride=0`、`r.FilmGrain=0`；解冻/退出按原值还原。日志：
  `渲染对照已应用：r.Tonemapper.Quality 5→4；r.BackbufferQuantizationDitheringOverride ...→0；r.FilmGrain ...→0`。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix17_preset.zip`：`freeze_tonemapper_dither_off=true` + `freeze_wpo=true` + `freeze_custom_cloth=true`，其余开关保持 hotfix10 的值。
- 判读：如果闪烁消失/明显减轻 → 根因就是 Tonemapper GrainJitter，下一版把 `r.Tonemapper.Quality=4` 做成摄影模式默认；如果仍一样，才说明是更底层的逐帧材质着色，需要走“冻结渲染帧”。
- 核心层测试 **4392 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix17.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix16 这次改了什么（WPO setter 参数名兼容）

- hotfix15 实测（22:40:39–22:43:28）：`WPO 评估冻结：SkeletalMesh 150 个（本次关闭 0；无 SetEvaluateWorldPositionOffset 的函数 0 个）`。函数存在，但参数名没命中，导致 0 个真正关闭——这版等于没生效。
- hotfix16 修复：`SetEvaluateWorldPositionOffset` 的参数名在不同引擎分支可能是 `bNewValue` / `NewValue` / `Value` / `bEvaluateWorldPositionOffset` 等；名字全部不中时，退化为“写函数第一个 bool 参数”，仍由反射类型校验，不按内存偏移硬写。
- 如果还是 0，日志会输出 `WPO setter 参数名探测：...`，把真实参数名带出来，下一版直接按名字写。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix16_preset.zip`：`freeze_wpo=true` + `freeze_custom_cloth=true`，其余冻结/渲染开关保持 hotfix10 的值，解压即用。
- 判读：看 `WPO 评估冻结：SkeletalMesh 150 个（本次关闭 M...）`。M=150 表示这次真正关了 WPO；若这时闪烁明显减轻/消失，WPO 就是根因。若 M 仍为 0，把同一条 `WPO setter 参数名探测` 发我。
- 核心层测试 **4390 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix16.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix15 这次改了什么（WPO 材质顶点动画冻结）

- hotfix14 实测（22:33:10–22:34:47）：`自定义布料候选类` 一行都没有——全量 UClass 里没有任何类名含 sway/wind/cloth 等关键词；`材质参数候选：DMI=251，标量匹配=0，向量匹配=0`，也没有 Time/Wind/Sway 这类 DMI 参数。说明 PBZ 的 Sway 不是 UClass 组件（更可能是 C++ 内部动画节点），DMI 参数也不是驱动源。
- hotfix13/14 里唯一实际生效的是 `r.Cloth.Enable=0` 与 `pbz.Sway.SkipSkeletalKinematicBoneUpdate=1`，且用户反馈“有些缓解”。这符合“布料模拟被压住一部分，但材质顶点动画（WPO）还在每帧求值”的分层现象。
- hotfix15 新增 `freeze_wpo`（默认 false，测试包预置 true）：冻结时遍历全部 `SkeletalMeshComponent`，调用 `SetEvaluateWorldPositionOffset(false)` 关闭材质 WorldPositionOffset 评估（布料/披风/裙摆的材质顶点摆动），解冻再调用 true 恢复。日志：
  `WPO 评估冻结：SkeletalMesh N 个（本次关闭 M；无 SetEvaluateWorldPositionOffset 的函数 K 个）`；
  `WPO 评估恢复：恢复 N 个 SkeletalMesh`。
- 这是内容层最后一条大路径：即使布料模拟已挂起、Cloth 全局关闭、Sway 骨骼开关打开，材质的 `Time/WPO` 节点仍会在渲染时每帧求值（尤其是 `bIgnorePause` 走 RealTime 的材质），WPO 关闭后会直接停止顶点位移。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix15_preset.zip`：`freeze_wpo=true` + `freeze_custom_cloth=true`，其余冻结/渲染开关保持 hotfix10 的值，解压即用。
- 判读：进摄影模式冻结后等 10–20 秒。若闪烁消失/明显减轻 → WPO 是最后的内容层源；若仍完全一样 → 只剩渲染器的逐帧材质着色/后处理，需要改用“冻结渲染帧”方案（不再逐帧重渲染）。
- 核心层测试 **4390 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix15.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix14 这次改了什么（自定义 Sway 类名枚举 + 材质参数诊断）

- hotfix13 实测（22:26:22–22:27:34）：`Sway/Wind 组件 0 个（无命中），SwayManager=0`，但 `r.Cloth.Enable=已关`、`pbz.Sway.SkipSkeletalKinematicBoneUpdate=已开` 都真实生效；用户反馈**闪烁仍在，但貌似有些缓解**。说明：① 硬编码的 Sway 类名（SwayComponent/BP_SwayComponent_C/...）在本作不是实际类名，0 命中；② 全局 `r.Cloth.Enable=0` 或 Sway 骨骼开关至少压掉了一部分布料路径。
- hotfix14 改进 `freeze_custom_cloth`：
  - 冻结时先枚举所有已加载 `UClass`，收集类名中含 **sway / windsystem / windtarget / windcomponent / clothcomponent / drape / ribbon / skirt / cape** 的候选类，日志输出 `自定义布料候选类：...`；
  - 对候选类逐个 `FindAllOf` 并停用组件 tick（原本就停用的不重复处理），日志 `自定义布料/摆动冻结：Sway/Wind 组件 N 个（本次停用 M；类名=计数）`；
  - 保留 `r.Cloth.Enable=0` 与 `pbz.Sway.SkipSkeletalKinematicBoneUpdate=1`。
- 同时新增只读诊断 `材质参数候选：DMI=N，标量匹配=..., 向量匹配=...`：扫描所有 `MaterialInstanceDynamic` 的 `ScalarParameterValues`/`VectorParameterValues`，列出名字含 time/wind/sway/flow/wave/phase/swing/offset/noise/strength 的参数（带当前标量值）。如果下一轮闪烁仍在，这一行可以直接告诉我们是否要转到 DMI 参数钉住（Time/Wind/Sway 参数）。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix14_preset.zip`：`freeze_custom_cloth=true`，其余冻结/渲染开关保持 hotfix10 的值，解压即用。
- 判读：冻结后等 10–20 秒。日志出现 `自定义布料候选类：...` 时看里面有没有疑似类名；`Sway/Wind 组件 N 个（本次停用 M）` 若 M>0，说明这次真正停到了 Sway 组件。若闪烁消失 → 锁定自定义 Sway；若仍存在，看 `材质参数候选` 的匹配项，下一轮直接钉住对应 DMI 参数。
- 核心层测试 **4383 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix14.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix13 这次改了什么（PBZ 自定义 Sway / 全局 Cloth 冻结）

- hotfix12 实测（22:15:38–22:17:29）确认 8 档全部真实执行：阶段 1/2/3/4/5/6/7 分别改动 2/1/1/1/2/2/3 个 CVar，无“未生效”告警；用户反馈**8 档全都存在闪烁**。初始状态：`r.ShadowQuality=1`、`r.Shadow.Virtual.Enable=1`、`r.Shadow.Virtual.SMRT.RayCountDirectional=3`、`r.Shadow.Virtual.Cache.DeformableMeshesInvalidate=1`、`r.Shadow.Denoiser=2`、`r.ContactShadows=1`、`r.VolumetricFog=1`、`r.FilmGrain=1`、`r.PostProcessing.DisableMaterials=0`、`r.DepthOfFieldQuality=1`。把这些逐项关掉（包括 `r.ShadowQuality=0` 全部阴影关、`r.FilmGrain=0` + `r.PostProcessing.DisableMaterials=1` + `r.DepthOfFieldQuality=0`）仍闪 → **VSM/SMRT/接触阴影/体积雾/胶片颗粒/后处理材质/景深全部排除**。
- 剩下最大的嫌疑是 **PBZ 自己的 Sway/Wind/Cloth 系统**：游戏内有 `SwayComponent` / `SwayManager` / `WindSystem` / `WindTarget`，以及 `r.Cloth.Enable`（默认 1）和 `pbz.Sway.SkipSkeletalKinematicBoneUpdate`（默认 0）这两个开关；这套系统在引擎 Chaos Cloth 之外驱动衣摆/披风，世界暂停与 `SuspendClothingSimulation` 都拦不住。
- hotfix13 新增 `freeze_custom_cloth`（默认 false，测试包预置 true）：
  - 扫描并停用 `SwayComponent` / `SwayComponent_C` / `BP_SwayComponent_C` / `WindSystemComponent` / `WindSystem` / `WindTargetComponent` 的组件 tick（记录原状态，解冻恢复）；
  - `r.Cloth.Enable 1→0`（全局布料开关，补上引擎组件挂起漏掉的路径）；
  - `pbz.Sway.SkipSkeletalKinematicBoneUpdate 0→1`（跳过 Sway 的骨骼运动学更新）。
  - 日志：`自定义布料/摆动冻结：Sway/Wind 组件 N 个（本次停用 M；SwayComponent=...），SwayManager=N；r.Cloth.Enable=已关；pbz.Sway.SkipSkeletalKinematicBoneUpdate=已开`；解冻 `自定义布料/摆动恢复：...`。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix13_preset.zip`：`freeze_custom_cloth=true`，`freeze_lighting_ab_cycle=false`、`freeze_shadow_ab_cycle=false`，其余冻结/渲染开关保持 hotfix10 的值，解压后无需改 ini。
- 判读：进摄影模式冻结后等 10–20 秒。若闪烁消失 → 自定义 Sway/Cloth 是主因，下一版把它做成默认冻结项并细调（只停 Sway、不停全局 Cloth）；若仍完全一样 → 下一层转材质/顶点动画（材质 `Time` 的 `bIgnorePause` RealTime、DMI 的 Time/Wind 参数、WPO）。
- 核心层测试 **4383 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix13.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix12 这次改了什么（阴影/后处理 A/B：Lumen 六档全灭后的下一层）

- hotfix11 实测（22:05:44–22:07:01）确认六档全部真实执行：阶段 1/2/3/4/5 分别改动 2/2/1/3/3 个 CVar，日志无“光照对照部分项目未生效”；用户反馈**六档都存在闪烁**。同时初始状态显示 `r.Lumen.Reflections.Allow=0`、`r.Lumen.ScreenProbeGather.ShortRangeAO=0`（游戏本来就关了这两项），阶段 5 又把 `r.Lumen.DiffuseIndirect.Allow`、`r.DynamicGlobalIlluminationMethod`、`r.ReflectionMethod` 写成 0。**Lumen GI / Lumen 反射 / SSAO 全部排除。**
- 下一层最可疑：**VSM 软阴影的 SMRT 随机采样**（`r.Shadow.Virtual.SMRT.RayCountDirectional/Local`，逐帧随机抖动）、**VSM 布料/变形体缓存逐帧失效**（`r.Shadow.Virtual.Cache.DeformableMeshesInvalidate`，布料每帧重渲 shadow page → SMRT 噪音无法收敛）、接触阴影、体积雾时域重投影、以及胶片颗粒/后处理材质/景深等后处理。
- hotfix12 新增 `freeze_shadow_ab_cycle`（默认 false）+ `freeze_shadow_ab_phase_seconds`（默认 8 秒）：冻结后自动轮换 8 档（含原始基线），日志前缀 `阴影/后处理A/B：阶段 N/8 ...`：
  1. Shadow SMRT 关（`RayCountDirectional/Local=0`）
  2. VSM 布料缓存逐帧失效关（`Cache.DeformableMeshesInvalidate=0`）
  3. VSM 关（回退 CSM）
  4. 全部阴影关（`r.ShadowQuality=0`，决定性判断）
  5. 接触阴影关（`r.ContactShadows=0` + Denoise）
  6. 体积雾关（`r.VolumetricFog=0` + TemporalReprojection=0）
  7. 后处理组合关（`r.FilmGrain=0` + `r.PostProcessing.DisableMaterials=1` + `r.DepthOfFieldQuality=0`）
  - 阶段 0 = 原始（阴影/后处理全开），之后循环。
- 新增 `阴影/后处理状态：...` 冻结瞬间日志，先记录 VSM/SMRT/VSM 缓存/接触阴影/体积雾/胶片颗粒/后处理材质/景深的当前值，方便直接看哪些本来就没开。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix12_preset.zip`：`freeze_shadow_ab_cycle=true`、每阶段 8 秒、`freeze_lighting_ab_cycle=false`，其余冻结/渲染开关保持 hotfix10/11 的预置值，解压后无需改 ini。
- 判读：进摄影模式后相机不动等约 64 秒（8 档×8 秒）。哪一档闪烁明显减轻/消失就是对应层；若只有“全部阴影关”消失，下一轮在 VSM/SMRT/接触阴影里继续二分；若 8 档全都不变，再转材质/顶点动画层（Time 节点 RealTime / DMI 参数）。
- 核心层测试 **4376 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix12.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix11 这次改了什么（Lumen / 屏幕空间光照 A/B）

- 最新实测日志（21:48:22–21:49:19）确认 hotfix10 的 1e-9 已真实生效：`时间设置目标=0.0000 生效=0.0000`（4 位小数显示，实际 1e-9）、`WorldSettings Min=0.0000`；同时 `布料冻结：SkeletalMesh 带布料 17 个（本次挂起 17）；ChaosCloth 18 个（本次挂起 4，原本已暂停 14）`、`骨骼动画冻结：150 个（播放中 17，本次暂停 150）`、`动态模糊已关闭：r.MotionBlurQuality 2→0`，四个渲染开关也全部应用。用户反馈闪烁**几乎完全一样**。
- 这条证据把“时间驱动”这一层基本排除：材质 Time / MPC / 风 / 布料模拟 / 骨骼动画 / 动态模糊 / TSR-TAA / DLSS / 帧生成 / WPO 假速度全部压住后仍闪，剩下的只能是**渲染器每帧重新采样的光照环节**——Lumen 的屏幕空间追踪、短距屏幕空间 AO、屏幕探针辐射缓存，以及 Lumen 的逐帧随机抖动。
- hotfix11 新增 5 个独立光照开关（默认全关，不影响正常画质；冻结时临时改 CVar，解冻/退出按快照还原）：
  - `freeze_lumen_screen_traces_off`：`r.Lumen.ScreenProbeGather.ScreenTraces=0` + `r.Lumen.Reflections.ScreenTraces=0`（保留 Lumen 距离场追踪）
  - `freeze_lumen_short_range_ao_off`：`r.Lumen.ScreenProbeGather.ShortRangeAO=0` + `r.Lumen.DiffuseIndirect.SSAO=0` + `r.AmbientOcclusionLevels=0`
  - `freeze_lumen_radiance_cache_off`：`r.Lumen.ScreenProbeGather.RadianceCache=0`
  - `freeze_lumen_fixed_jitter`：`r.Lumen.ScreenProbeGather.FixedJitterIndex=0` + `FixedStateFrameIndex=0` + `r.Lumen.Reflections.FixedStateFrameIndex=0`（把逐帧随机采样固定成同一组，若闪烁消失即 Lumen 时域噪声）
  - `freeze_lumen_off`：`r.Lumen.DiffuseIndirect.Allow=0` + `r.Lumen.Reflections.Allow=0`（方法 CVar 能写就写，只读时日志会列为未生效）
- 新增自动轮换：`freeze_lighting_ab_cycle=true` 时，冻结后每 `freeze_lighting_ab_phase_seconds` 秒自动切换一档并打日志：
  `阶段 0 原始 → 1 屏幕空间追踪关 → 2 短距屏幕空间 AO 关 → 3 Lumen 辐射缓存关 → 4 Lumen 抖动固定 → 5 Lumen GI/反射全关 → 回到 0`。
  日志关键字：`灯光状态：...`（冻结瞬间）、`灯光A/B：阶段 N/6 ...（CVar 改动 N 项）`（每次切换）、`光照对照已应用/已还原/部分项目未生效`。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix11_preset.zip`：`config.ini` 已开启 `freeze_lighting_ab_cycle=true`、每阶段 8 秒，其余冻结/渲染开关保持 hotfix10 的预置值，解压后无需再改 ini。
- 判读：冻结后等约 48 秒（6 档×8 秒）看哪一档闪烁明显减轻/消失。若阶段 1/2/3 某一档消失 → 对应屏幕空间环节是主因；若阶段 4 消失 → Lumen 逐帧随机采样/时域积累；若阶段 5 才消失 → Lumen GI 整体；若 6 档都不变 → 转查 SSAO/阴影/体积雾等非 Lumen 后处理（下一轮）。
- 核心层测试 **4358 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix11.zip`（预置包另加 `_preset`）。

## 1.0.75-hotfix10 这次改了什么（dilation 下限放到 1e-9）

- 用户实测 `freeze_mode=dilation, freeze_dilation=0.0001`：日志显示 `时间设置目标=0.0001 生效=0.0001`，闪烁**有一些缓解但仍在**；同时 `冻结诊断` 里 `MPC_Water.Time 16.950→16.952`、`PrevTime` 仍缓慢前进，说明 0.0001 的时间缩放还没有把材质时间完全压死。
- hotfix10 只做一件事：把 `freeze_dilation` 的允许下限从 `0.0001` 放宽到 `1e-9`，让 dilation 模式可以设到 `0.000000001`，把世界/材质时间压到实际不可见。
- 建议测试配置：
  `freeze_mode = dilation`、`freeze_dilation = 0.000000001`，并保留 `freeze_cloth` / `freeze_skeletal_animation` / `freeze_motion_blur_off` 与四个渲染开关全部为 true。
- 如果 1e-9 下闪烁消失或基本消失，根因就锁定为**材质时间/材质 RealTime**；下一版会把“暂停模式也强制极小时间缩放”做成默认，不再要求用户切 dilation。如果 1e-9 下仍存在，下一步转向 Lumen/屏幕空间光照。
- 本次提供**预置包** `PBZPhotoModeCpp_1.0.75-cpp_hotfix10_preset.zip`：`config.ini` 已把 `freeze_mode=dilation`、`freeze_dilation=0.000000001`、布料/骨骼/动态模糊/四渲染开关/诊断全部设好，解压后无需再改 ini。
- 核心层测试 **4327 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix10.zip`。

## 1.0.75-hotfix9 这次改了什么（骨骼动画 + 动态模糊）

- 最新日志（21:18:58–21:19:47）确认**四开关 + 布料冻结同时成立**：`r.AntiAliasingMethod=0`、`r.Velocity.EnableVertexDeformation=0`、`r.Velocity.EnableLandscapeGrass=0`、`r.NGX.DLSS.Enable=0`、`r.Streamline.DLSSG.Enable=0`、`r.PBZ.SuppressFrameGen=1`；布料 `17+18` 与 `18+24` 个组件、`冻结诊断 ... 布料=35/42挂起`，用户仍反馈存在流动式闪烁。说明剩余问题不在布料模拟、TSR/TAA、WPO 假速度、DLSS 或 FG。
- 下一层最可疑的两个来源：
  - **骨骼动画**：世界暂停不一定停住 `SkeletalMeshComponent` 的 AnimBP/骨骼 pose；角色/敌人的衣摆、披风即使布料模拟已挂起，仍可能被骨骼驱动继续“流动”。
  - **动态模糊**：`r.MotionBlurQuality=2` 仍开着，残留速度缓冲会被 motion blur 抹成“流动式”拖影；静止拍照不需要动态模糊。
- hotfix9 新增：
  - `freeze_skeletal_animation = true`：冻结时对全部 `SkeletalMeshComponent` 写 `bPauseAnims=true`、`bNoSkeletonUpdate=true`，记录原值，解冻/退出恢复；日志输出 `骨骼动画冻结：SkeletalMesh N 个（播放中 M，本次暂停 K）`。
  - `freeze_motion_blur_off = true`：冻结时 `r.MotionBlurQuality=0`，解冻/退出按原值还原；日志输出 `动态模糊已关闭：r.MotionBlurQuality 2→0`。
  - `freeze_diag` 增加 `动画=N暂停`。
- 如果 hotfix9 后仍有闪烁，下一步按材质层处理：只对布料组件的 `MaterialInstanceDynamic` 参数做快照/写回，并排查材质里的 `RealTime`/Panner 节点。
- 核心层测试 **4327 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix9.zip`。

## 1.0.75-hotfix8 这次改了什么（布料模拟冻结：裙摆/披风）

- 用户最新日志（2026-11-03 20:33–20:34）确认四个渲染开关全部真实生效：`r.AntiAliasingMethod=0`、`r.Velocity.EnableVertexDeformation=0`、`r.Velocity.EnableLandscapeGrass=0`、`r.Streamline.DLSSG.Enable=0`、`r.NGX.DLSS.Enable=0`、`r.PBZ.SuppressFrameGen=1`；闪烁仍在，且具体对象是**角色与敌人的衣摆、披风等布料**。这基本排除了 TSR/TAA、WPO 假速度、DLSS SR 与帧生成。
- 关键现象：进入相机模式会卡顿一下，**卡顿期间不闪，卡顿结束后才开始闪**。卡顿期间游戏线程被占满、没有推进模拟步；恢复逐帧后布料立刻开始变化——典型是布料模拟由独立 tick/真实时间驱动，世界暂停与 TimeDilation 都拦不住。
- 新增 `freeze_cloth = true`（默认开启）：
  - 扫描带 `MeshClothingAssets` 的 `SkeletalMeshComponent`，调用 `SuspendClothingSimulation()`（停止模拟但保留最后一次模拟姿态，不会弹回参考姿势）；解冻调用 `ResumeClothingSimulation()`。
  - 扫描 `ChaosClothComponent`（裙摆/披风也可能走 ChaosClothAsset），调用 `SuspendSimulation()` / `ResumeSimulation()`。
  - 原本就处于暂停态的组件不重复处理，也不在解冻时误恢复；日志输出 `布料冻结：SkeletalMesh 带布料 N 个（本次挂起 M，原本已暂停 K）；ChaosCloth ...`，退出/解冻输出 `布料恢复：恢复 N 个`。
  - `freeze_diag` 的 `冻结诊断` 增加 `布料=N挂起`，方便直接确认。
- 如果日志出现 `布料冻结：未发现带布料的 SkeletalMesh / ChaosCloth 组件`，说明这套布料路径没被反射扫到（可能是自定义布料组件），把日志发回来，下一步按类名定向处理。
- 核心层测试 **4168 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix8.zip`。

## 1.0.75-hotfix7 这次改了什么（FG 应用顺序 + 渲染后状态）

- hotfix6 的 `freeze_dlss_off=true` 实机日志（20:20–20:22）：`r.NGX.DLSS.Enable 1→0` 已应用，`视角微动:位移0.000`、`风源=0`，但闪烁仍存在；说明 DLSS 超分已关掉，不能再把锅甩给 DLSS。
- hotfix6 日志中 `r.Streamline.DLSSG.Enable` 没有出现在“已应用”列表：因为 `r.PBZ.SuppressFrameGen=1` 可能先被设置、游戏随即把 DLSSG 改成 0，导致后续目标原值==目标值而被跳过。hotfix7 调整顺序：**先设 `r.Streamline.DLSSG.Enable=0` / `r.FidelityFX.FI.Enabled=0`，再设 `r.PBZ.SuppressFrameGen=1`**，并在应用后输出 `渲染对照后状态：...`，保证日志能看到 FG 是否真的关掉。
- 注意：当前测到的 `MPC=34集/347参` 里偶发 `UltraDynamicWeather_Parameters.CameraFadeStartLocation` 变化，说明 UltraDynamicWeather 仍有参数在动；如果渲染开关全关后仍闪烁，下一步要查天气/粒子或材质 Time，而不是 DLSS/TSR。
- 组合测试：`freeze_dlss_off=true` + `freeze_aa_off=true` + `freeze_wpo_velocity_off=true` + `freeze_framegen_off=true`，四个全开后再试一次；仍有闪烁基本可判定为内容层（Niagara/天气粒子/Material Time/Chaos Cloth），不再是 DLSS/TSR/WPO 速度。
- 核心层测试 **4082 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix7.zip`。

## 1.0.75-hotfix6 这次改了什么（DLSS/帧生成显式关闭 + 渲染状态判读）

- 实机日志（2026-11-03 19:58）暴露了关键事实：`渲染状态：... r.NGX.DLSS.Enable=1，r.Streamline.DLSSG.Enable=1，r.FidelityFX.FSR.Enabled=0，r.FidelityFX.FI.Enabled=0，r.PBZ.SuppressFrameGen=0`——当前实际跑的是 **DLSS 超分 + DLSS 帧生成**，不是 TSR。`freeze_aa_off` 改 `r.AntiAliasingMethod` 不会关 DLSS，所以此前“关 AA 无效”不能说明 TSR 没问题。
- WPO 单独测试已按预期应用（`r.Velocity.EnableVertexDeformation 1→0`、`EnableLandscapeGrass 1→0`），闪烁仍无缓解 → WPO 顶点速度不是主因。
- `freeze_mode=dilation` 在本作不可用作真冻结：日志显示游戏每秒把 `WorldSettings.TimeDilation` 写回 1.0，模组只能每秒重申一次，期间人物正常速度移动；这是 dilation 模式本身的限制，不是冻结成功。
- hotfix6 改动：
  - `freeze_framegen_off` 不再只设 `r.PBZ.SuppressFrameGen=1`，还显式设 `r.Streamline.DLSSG.Enable=0`、`r.FidelityFX.FI.Enabled=0`。
  - 新增 `freeze_dlss_off = false`：设为 true 时冻结期临时 `r.NGX.DLSS.Enable=0`（关 DLSS 超分），解冻/退出还原；这是下一轮判断 DLSS SR 是否主因的开关。
  - 测试矩阵：只验 FG → `freeze_framegen_off=true, freeze_dlss_off=false`；只验 DLSS SR → `freeze_framegen_off=false, freeze_dlss_off=true`；两者都验 → 都 true。
- 修复 `freeze_mode=dilation` 的人物不立即冻结：在 `GameViewportClient Tick` 之后（渲染前）每帧重申 `TimeDilation=0.0001`，不再只靠每秒自检；游戏仍会周期性写回 1.0，但下一帧渲染前会被压回。
- 核心层测试 **4082 项全绿**；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix6.zip`。

## 1.0.75-hotfix5 这次改了什么（帧生成抑制 + FG 状态日志）

- hotfix4 日志显示：`r.AntiAliasingMethod 4→0` 已成功应用，但用户反馈闪烁没有减轻；当前 config.ini 中 `freeze_wpo_velocity_off` 为 false，日志里没有出现 WPO 速度组的应用记录（该项还需单独复测）。
- 下一步最可疑的是**帧生成（Frame Generation）**：本作历史环境长期开着 FSR3/DLSS 帧生成，FG 会在真实帧之间插入生成帧，静止画面也可能出现闪烁/边缘 halo，且 `r.AntiAliasingMethod=0` 不会关掉 FG。
- 新增 `freeze_framegen_off = true`（默认开启）：冻结时把游戏自带的 `r.PBZ.SuppressFrameGen` 设为 1（运行时抑制 FG，不改玩家保存设置），解冻/退出按原值还原。静止拍照不需要插帧，关闭 FG 是低风险做法；想保留插帧可设 false。
- `渲染状态` 追加 `r.NGX.DLSS.Enable`、`r.Streamline.DLSSG.Enable`、`r.FidelityFX.FSR.Enabled`、`r.FidelityFX.FI.Enabled`、`r.PBZ.SuppressFrameGen`，可直接从日志判断当前超分/FG 后端。
- 核心层断言全部通过（当前断言总数见 [test_count.txt](test_count.txt)，由脚本生成）；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix5.zip`。

## 1.0.75-hotfix4 这次改了什么（相机静止仍闪烁：渲染对照 A/B）

- 实机日志确认 hotfix3 的 A/B 与 MPC 都已生效（`相机可移动标志已写入/已还原`、`MPC 写回：本轮成功 344~347 个参数`），但用户反馈**相机静止时仍有部分物体闪烁**——这属于渲染侧：TSR/TAA 的时域历史 + WPO/Nanite WPO/Groom/HairCards/GPU Skin/Niagara 的 pause-unaware 假运动向量（Epic 论坛 2025-09 已确认 JIRA）。
- 新增每次冻结自动输出的 `渲染状态：r.AntiAliasingMethod=...，r.Velocity.EnableVertexDeformation=...，r.Velocity.EnableLandscapeGrass=...，r.HairStrands.ForceVelocityOutput=...，r.MotionBlurQuality=...`（读不到时只 Warn 一次）。
- 新增两个 A/B 开关（默认 `false`，不改正常画质）：
  - `freeze_aa_off = true`：冻结时临时 `r.AntiAliasingMethod 0`（关 TSR/TAA），解冻/退出还原原值；若闪烁消失，说明来自时域抗锯齿历史。
  - `freeze_wpo_velocity_off = true`：冻结时临时把 `r.Velocity.EnableVertexDeformation`、`r.Velocity.EnableLandscapeGrass`、`r.HairStrands.ForceVelocityOutput` 归零，解冻/退出还原；若闪烁消失，说明来自 WPO/植被/毛发的假运动向量。
- 两项都失败/不可读只 Warn，不阻断冻结；解冻或退出时按快照原值还原，还原失败保留标记并在退出时重试。
- 核心层断言全部通过（当前断言总数见 [test_count.txt](test_count.txt)，由脚本生成）；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix4.zip`。

## 1.0.75-hotfix3 这次改了什么（暂停相机 A/B + MPC 写回可见性）

- **低风险 A/B**：`set_paused(true)` 在原有 `APlayerController.bShouldPerformFullTickWhenPaused=true` 之外，增加 `UWorld.bIsCameraMoveableWhenPaused=true`（进入前快照原值，解冻/退出/暂停失败时还原）。两者是 UE 暂停相机可移动的标准成对设置；日志新增 `相机可移动标志已写入/已还原`，暂停成功行带 `UWorld.bIsCameraMoveableWhenPaused=...`，读不到/写失败只 Warn 一次且不阻断暂停。用于 A/B 验证“暂停时相机移动 → TAA/TSR/MotionBlur 旧视角矩阵拖影”。
- **MPC 写回修复**：沿用工作树 hotfix 的 `Value → ParameterValue → 第 4 个参数` 兜底（1.0.75 里 `SetScalarParameterValue/SetVectorParameterValue` 因参数名 `Value` 不匹配而全部静默失败）；`mpc_hold_snapshot()` 每 10 秒输出 `MPC 写回：本轮成功 N 个参数`，不再用“MPC 快照参数数”冒充写回成功数。
- 核心层断言全部通过（当前断言总数见 [test_count.txt](test_count.txt)，由脚本生成）；DLL 编译 exit 0；dist 打包 `PBZPhotoModeCpp_1.0.75-cpp_hotfix3.zip`。

## 1.0.75-hotfix1 这次改了什么（修复“完全无法进入摄影模式”）

- 现象：1.0.75 实机按 F2 后每次进入都在 `SetViewTargetWithBlend` 之后立刻回滚，日志 `相机接管校验第 1 次失败：ViewTarget 当前目标=null，不是摄影相机` + `设置 ViewTarget 失败`。
- 根因：M-1 新增的 ViewTarget 回读确认被当成进入的硬性前置检查。真机上 `SetViewTargetWithBlend` 成功的同一帧里 `ViewTarget.Target` 读出 `null`，要等相机管理器本帧 `UpdateViewTarget` 之后才有值——属于“回读尚未生效”，不是接管失败。
- 修复：`attach_view()` 恢复为“调用成功即返回 true”，回读结果降级为告警；只有读到明确的其他 Actor（视角被抢）或摄影相机位姿被搬走才算确凿失效，且仍需连续 3 次才 `leave`。回读 `null`/不可读不累计失效、不阻断进入。
- 单测新增“回读暂未确认不阻断进入/不触发退出”的回归断言；核心层断言全部通过（当前断言总数见 [test_count.txt](test_count.txt)）。

## 1.0.75 这次改了什么（Q/E 滚转键可在 config.ini 配置）

- 问题：用户反馈 Q/E 旋转视角（滚转）快捷键在 `config.ini` 里改不了——检查发现 1.0.74 及以前 Q/E 硬编码在 `input_logic.cpp` 的 `build()` 里（`kQ`/`kE` 常量），`[keys]` 段只有 `key_roll_reset`（滚转归零），没有滚转左右键配置项。
- 修复：新增 `[keys] key_roll_left` / `key_roll_right`（默认 `Q` / `E`，左=逆时针、右=顺时针），解析进 `KeyMap` 并由 `InputLogic` 使用；`config_ini_text` / `config.ini.example` / PBZFM 菜单「快捷键（键盘）」页同步，PBZFM 改键后写回 `config.ini`。
- 兼容：不写新键名时默认值仍是 Q/E，旧配置无需改动；`key_roll_reset`（默认 Z，滚转归零）保持不变。
- 其他：控制台诊断面板的滚转提示改为显示当前实际键位；四语言词典补 `#key_roll_left/right`。
- 核心层测试 2400 → **2529 项全绿**；DLL 编译 exit 0。

## 1.0.74 这次改了什么（过场对话 Flow 冻结/诊断：保留相机，不挂线程）

- 背景：用户提出「全线程冻结」；经 ANALYSIS.md / 线程模型核对，直接挂起所有线程会连渲染线程和 game thread 一起停，摄影相机与 F2 都会失效，只能得到静帧、无法取景；因此采用方案 A：保留相机线程与渲染，只冻结对话 Flow。
- 1.0.73 实机日志（19:02）：`语音位置校验：可读 41 个，暂停期位置未变 41 个，前进 0 个（最大 +0 ms）`——Wwise 播放头已冻结，剩余疑点在对话 Flow 继续 Tick（`WBP_FlowDialog_C` 仍为 Hidden、Slate 仍 Tick）。
- 新增配置 `freeze_dialogue_flow`（默认 `true`，仅 `hide_subtitles=true` 时生效）：冻结时把 `WBP_FlowDialog_C` 从 `Hidden` 改为 `Collapsed`（ESlateVisibility=1，停止 Slate Tick），解冻恢复 `Hidden`（2，仍隐藏但继续 Tick）；`leave()` 退出时按原可见性恢复。
- 新增 `PlayAudio_C.ForceFinishAudio` / `StopAudio_C.ExecuteUbergraph_StopAudio` 只读诊断钩子：冻结期被调用时打日志（前 8 次），只确认对话 Flow 是否提前结束语音，不改播放行为。
- 不挂线程、不碰 QPC/IAT、不改变 Wwise/相机逻辑；无对话/普通场景 FlowDialog=0 不产生新日志，配置关闭或 `hide_subtitles=false` 时行为回退 1.0.73。
- 核心层测试 2306 → **2400 项全绿**；DLL 编译 exit 0。

## 1.0.73 这次改了什么（过场/对话中进入摄影模式后语句仍不续播）

- 实机反馈：1.0.72 装后，过场/对话中按 F2 进入摄影模式再退出，**被截断的语句仍不能续播**。
- 根因（1.0.72 遗留）：`AK::SoundEngine::Suspend(true,false)` 的 render-anyway 只是“不输出”，音频线程和语音时间轴照常推进；即使逐 PlayingID `PBZPausePlayingID` 执行成功（日志 `对话语音暂停（PlayingID）：截获 47，执行 47`），当前句也会在冻结期间无声播完，解冻后无法从原位置续播。1.0.36 只用逐 PlayingID + 组件音量时实机验证可续播。
- 修复①（整引擎 Suspend 降级为无 ID 兜底）：有逐语音 PlayingID 时**跳过整引擎 Suspend**，逐 PlayingID 真暂停（`PBZPausePlayingID`/`PBZResumePlayingID`）恢复为唯一常规路径；整引擎 `Suspend(true,false)` 仅在 `pause_wwise_voices` 返回 0（没有截获到 ID）时作为静音兜底（`wwise_engine_suspend` 仍默认 true，但只在无 ID 时生效）。
- 修复②：新增 `GetPlayingIDPositionMs` 语音位置校验（只读诊断）：暂停时记录每个 PlayingID 的位置，恢复前再读一次，日志 `语音位置校验：可读 N 个，暂停期位置未变 N 个，前进 N 个（最大 +X ms）`；前进超过 100ms 会 Warn `语音暂停未冻住：当前句在冻结期继续前进`。
- 修复③：`pause_wwise_voices` 返回值只统计实际执行的 PlayingID 数（组件音量兜底不计入），避免 core 误判「逐语音已生效」；补 `对话类函数：…` 一次性诊断。
- 验证：有 ID 时日志应出现 `逐语音真暂停已执行 N 个 PlayingID，跳过整引擎 Suspend`，且**不再出现** `Wwise 音频引擎已静音挂起`；无 ID 时才出现 `无逐语音 PlayingID，整引擎 Suspend 作为静音兜底`。
- 核心层测试 2301 → **2306 项全绿**；DLL 重编通过。

## 1.0.72 这次改了什么（过场动画进入/退出摄影模式后语句断句不续播）

- 实机反馈：过场动画（剧情演出）中按 F2 进入摄影模式、再退出后，**正在播的语句被截断，不能从原位置续播**。
- 根因①（音频）：1.0.66 起冻结走 `AK::SoundEngine::Suspend(false,false)` 整引擎硬挂起。Wwise 2024.1 的签名是 `Suspend(bool in_bRenderAnyway, bool in_bFadeOut)`，`(false,false)` 表示音频完全不处理（连 `RenderAudio` 也不处理），音频线程被硬停、事件队列与语音流状态丢失，解冻后播到一半的语句无法续播；同时绕过了 1.0.36 实机验证过的逐 PlayingID `PBZPausePlayingID`/`PBZResumePlayingID` 路径。
- 根因②（镜头）：退出摄影模式时 `restore_view()` 固定还原到玩家 Pawn，过场动画期间的 ViewTarget（过场相机）被切走，正在播的对话/语音被打断。
- 根因③（对话 UI）：字幕文本画在 `WBP_FlowDialog_C` 等对话控件上；`Collapsed` 会停止控件 Tick、让对话 Flow/语句推进停摆；进入时关闭 `bEnableSubtitles` 也会截断当前语句。
- 修复①：整引擎改 `AK::SoundEngine::Suspend(true,false)`（render-anyway：音频继续处理但不输出，保住语音流状态）；冻结顺序恢复为「先逐 PlayingID 真暂停 → 再整引擎 render-anyway 静音」，解冻逆序「先 `WakeupFromSuspend` 恢复输出 → 再 `PBZResumePlayingID` 恢复语音」。
- 修复②：进入时保存相机管理器的原 `ViewTarget`，退出优先还原它（日志 `进入前 ViewTarget 已记录` / `已还原进入前 ViewTarget`），记录不可用时才回退玩家 Pawn。
- 修复③：FlowDialog 家族控件用 `ESlateVisibility=2`（Hidden，保留 Tick），其他字幕控件仍用 `Collapsed`；扫描到 FlowDialog 时保留 `bEnableSubtitles` 原值、只隐藏控件（日志 `检测到对话 UI（FlowDialog），保留字幕设置开关不写…`）。
- 配置说明：`stop_wwise_on_freeze` 仍默认 `true`；`wwise_engine_suspend` 仍默认 `true`，但现在是**第二层 render-anyway 静音**（音频继续处理、不输出），逐 PlayingID 暂停始终先行，不再被整引擎替代。
- 核心层测试 2294 → **2301 项全绿**；DLL 重编通过。

## 1.0.67 这次改了什么（清理两处旧日志提示）

- **PostProcess 快照不再误报失败**：本版不修改后期处理设置，`save_postfx()` 没有需要保存的内容；此前返回 `false` 导致每次进摄影模式都打 `PostProcess 快照失败`。现在按“无需还原”处理并返回成功，退出时 `restore_postfx()` 为空操作，不再刷警告。（1.0.75 收口批次：改为如实返回 `false` 并日志「PostFX 快照暂不支持」，不再宣称“已保存、退出会还原”；后续版本已把该死接口整体移除）
- **相机修饰器列表改用 UE5 正确字段名**：`APlayerCameraManager` 的运行时列表在 UE5 叫 `ModifierList`，旧代码只查 `CameraModifiers`（旧名/部分分支）导致找不到数组、冻结时跳过修饰器治理。现在按 `ModifierList` → `CameraModifiers` → `CameraModifierList` 顺序尝试；找到后正常停用/恢复，确实不存在时才打一次信息日志（恢复阶段也不再无条件警告）。

## 1.0.66 这次改了什么（修复移动视角卡顿）

- **现象**：1.0.65 相比 1.0.64 没有明显改善，反而在冻结状态下移动视角时频繁卡顿。
- **根因（时间偏移补偿）**：1.0.65 的 IAT Hook 把 `PBZ.exe` 和 `MRTCEngine.dll` 的 `QueryPerformanceCounter` 也换成了“冻结期返回常量”的钩子。PBZPause 能这么做，是因为它把进程内所有线程（包括渲染线程）都挂起了；摄影模式必须保留渲染线程和相机 Tick，QPC 被冻住后渲染线程的帧计时/引擎 delta 变成 0，画面就会一顿一顿。这个前提在本项目不成立，套用后只有副作用。
- **修复**：整段移除时间偏移补偿（IAT Hook、`time_offset_begin/end`、配置项、菜单项与四语言标签全部删除）；旧 `config.ini` 若保留 `time_offset_compensation = true` 会收到“已移除并忽略”的 warning，不影响启动。摄影模式仍用 `SetGamePaused` / 时间缩放冻结，不依赖 QPC 钩子。
- **同时修复 Wwise 整引擎 Suspend 的静默失效**：当前 PBZ 把 Wwise 静态链接进 `PBZ.exe`，`AkSoundEngineDLL.dll` 并不会被加载；1.0.65 只查 DLL，导致 `AK::SoundEngine::Suspend` 从未真正调用、每次都回落逐语音。1.0.66 优先从主程序导出表解析 `Suspend` / `WakeupFromSuspend` / `IsInitialized`，失败才回落 DLL 与逐语音，并在日志中写明接口来源；同时修正 `AKRESULT` 成功值判断（`AK_Success = 1`，旧代码误把 1 当失败）。
- 核心层测试 **2294 项全绿**（移除时间偏移用例，保留 Wwise 整引擎优先/回落/关闭用例）。

## 1.0.65 这次改了什么（修复 1.0.64 冻结崩溃）

- **根因**：1.0.64 的 `PatchModuleImports` 在解析导入函数名时把 `IMAGE_THUNK_DATA.AddressOfData`（RVA）直接当指针传给 `strcmp`，没有加模块基址。PBZ.exe 的 RVA 是 `0xD9CEABE`，`strcmp` 访问 `0xD9CEAC0` 触发 `0xC0000005`，所以一按 F2 自动冻结就崩。
- **修复**：按 PBZPause 原实现改为 `module_base + AddressOfData + sizeof(WORD)`，并对 `FirstThunk` / `OriginalFirstThunk` / 名称 RVA 增加模块 `SizeOfImage` 边界校验，坏模块/怪 import 表直接跳过而不是让 `strcmp` 读飞。
- 本次崩溃 dump：`crash_2026_11_02_13_17_56.3389705.dmp`，异常地址 `ucrtbase.dll+0xED8B0`，栈顶回到 `PBZPhotoModeCpp.dll!patch_module_time_imports`。

## 1.0.64 这次改了什么（Wwise 整引擎 Suspend + 时间偏移补偿）

- **Wwise 整引擎挂起**：冻结时优先调用 `AkSoundEngineDLL.dll` 的 `AK::SoundEngine::Suspend(false,false)`，解冻 `WakeupFromSuspend(0)`——一次覆盖环境/音乐/语音/UI 全部声音，解冻从冻结位置继续；引擎未初始化或调用失败自动回落原有逐 `PlayingID` + 组件音量兜底（`stop_wwise_on_freeze` 仍总开关，新配置 `wwise_engine_suspend` 默认 true）。
- **时间偏移补偿**：借鉴 PBZPause 的 IAT Hook 方案，冻结前把主程序与游戏侧 DLL（MRTCEngine/AkSoundEngineDLL/Media Foundation/audioclient）的 `QueryPerformanceCounter`、`GetTickCount`、`GetTickCount64`、`timeGetTime` 换成钩子；冻结期间返回暂停瞬间值，解冻时把本次暂停时长累加进偏移——动画/物理/粒子/计时器不会在解冻瞬间跳时间。新配置 `time_offset_compensation` 默认 true，Hook 失败自动降级并告警；不 patch 本模组自身的 CRT，摄影相机 dt 不受影响。
- **安全边界**：只改 IAT 不挂线程；钩子只用原子变量、不分配/不加锁；`Ue4ssPort` 析构时恢复所有 IAT 槽，避免 DLL 卸载后悬空。
- 核心层测试 **2372 项全绿**（新增 Wwise 整引擎优先/回落、时间偏移时序/失败降级/关闭用例）。
- **后续（1.0.66）**：实测确认时间偏移补偿会冻结渲染线程 QPC、导致移动视角卡顿，已整段移除；Wwise 整引擎 Suspend 保留并从 PBZ.exe 静态导出解析。

## 1.0.63 这次改了什么（升降键可配置）

- **需求**：右键/左键是升降键，但 config.ini 里没有键位项，无法改成其它鼠标键或键盘键。
- **实现**：新增 `[keys] key_vertical_up` / `key_vertical_down`（默认 `MOUSE_LEFT` / `MOUSE_RIGHT`）。键名表新增 `MOUSE_LEFT` / `MOUSE_RIGHT` / `MOUSE_MIDDLE` / `MOUSE_X1` / `MOUSE_X2`（别名 `LMB` / `RMB` / `MMB` / `MOUSE_BACK` / `MOUSE_FORWARD`）；输入层把 5 个鼠标键并入 `GetAsyncKeyState` 轮询，`translate`（上下平移）与 `look`（俯仰）两种模式都按新键工作。
- PBZFM 菜单「快捷键（键盘）」页同步显示两项，改键捕获支持鼠标键；四语言词典新增对应标签。
- 核心层测试 **2216 项全绿**（新增鼠标键名解析、默认值、交换键位、键盘键替代与 PBZFM 字段往返用例）。

## 1.0.71 这次改了什么（字幕/交互提示反复进出后仍消失）

- 实机反馈：1.0.70 不再崩溃，但进入相机模式再退出几次后字幕和 E 交互图标一直消失。
- 日志证据：只有 `UI 设置修复：等待玩家进入游戏世界…`，20 次后 `UI 设置修复多次重试未成功…跳过`（修复从未执行）；每次进出都是 `字幕设置开关原值=关（读取=GetSubtitlesEnabled()）`、`场景交互提示开关原值=关（读取=GetSceneInteractPromptVisible()）`。
- 根因：① `find_live_player_pawn()` 要求 Player 属性 + Pawn + `bBegunPlay` 全部命中，实机 Player 属性/世界属性读不到导致永远失败；② 设置原值 getter 优先读到了未同步旧副本，退出把「本来开着」还原成关。1.0.34 实机证明 `bEnableSubtitles` / `bSceneInteractPromptVisible` 属性才是生效值。
- 修复：`find_live_player_pawn()` 只要求 Pawn 可读、`bBegunPlay` 仅在明确 false 时拦截；修复只在非摄影模式的 Tick 执行。`read_bool_setting()` 改为属性优先、getter 兜底，回读校验按属性；写仍走 setter + Pending + 属性三路。
- 核心层 2294 项测试全绿；DLL 重编通过。

## 1.0.70 这次改了什么（进入游戏时崩溃）

- 实机反馈：安装 1.0.69 后进入游戏即崩溃（`crash_2026_11_02_15_36_45.7956922.dmp`）。
- minidump 取证：异常 `0xC0000005` 读地址 `0xFFFFFFFFFFFFFFFF`，RIP=`PBZPhotoModeCpp.dll+0x198A3`（`pbzpm::find_property`，ue4ss_port.cpp:117）；调用链 `recover_ui_settings_once` → `player_pawn`（ue4ss_port.cpp:939）→ `find_property(UObject*)`（:126）→ `find_property(UStruct*)`（:117）。
- 根因：1.0.69 的 UI 一次性修复启动 3 秒就调用 `player_pawn()`；`controller()` 缓存的 PlayerController 在换图/读档时已被销毁，`GetClassPrivate()` 读成 `0xFFFFFFFFFFFFFFFF`，旧 `is_ok` 只判非空，`find_property` 把 -1 当 `UStruct*` 构造 `TFieldIterator` 读地址 -1 崩溃。
- 修复：`is_ok()` 增加指针范围/对齐 + SEH 保护的 `GetClassPrivate` + `FUObjectItem` 对象数组反查；`find_property`/`find_function` 同样前置校验。UI 修复改用新增的 `find_live_player_pawn()`——每次重新枚举 PlayerController，要求 Player+Pawn 都在且 Pawn 所在 `UWorld.bBegunPlay=true`。
- 核心层 2294 项测试全绿；DLL 重编通过。

## 1.0.69 这次改了什么（字幕/交互提示退出后一直消失）

- 实机反馈：1.0.68 白点/血条正常了，但退出摄影模式后**字幕与交互按钮一直消失**，重进重出也不行。日志证据：首次进入即「字幕设置开关已关闭（原值=关，回读=关）」——两个开关在测试前就已是关闭态（旧版本 1.0.30 时代遗留：还原静默失败被游戏存盘）。
- **一次性修复**：新配置 `repair_ui_settings_once`（默认开）——引擎 Tick 起每 3 秒试一次（上限 20 次/约 1 分钟），把字幕/场景交互提示两个设置强制打开、回读校验、调用 `PBZSettingsShared::SaveSettings()` 立即存盘，成功后写 `ui_settings_repair_done.flag` 只执行一次。若你本来就关闭字幕，把它设为 false。
- **设置读写加固**：字幕/提示开关改为 getter 优先读（`GetSubtitlesEnabled`/`AreSubtitlesEnabled`/`GetSceneInteractPromptVisible`）、setter + Pending + 属性三路写、回读校验，日志记录原值与回读，不再静默写坏玩家设置。

## 1.0.68 这次改了什么（锁定白点与锁定血条一起残留）

- 实机反馈：锁定敌人后进摄影模式，**血条与白点一起残留**。日志证据：原血条模式=OnlyLockedTarget（切档无效——该模式只为当前锁定目标显示血条，锁定目标不清就永远在）；关键词扫描零命中（白点不是普通 UMG/可视化组件）。
- exe 反射字符串取证找到游戏自己的锁定系统 `TargetingComponent`（PBZ 里锁定 = Focus 家族）：`LockWidget` / `SecondaryLockIndicators`（锁定/副目标指示器控件）、`SetShowLockIndicator(bool)`、`HasLockTarget` / `GetLockTarget` / `SetLockTarget` / `StopLockTarget`、`TargetingHandlers`。
- 本版（`hide_lockon_marker` 默认开）：运行时定位锁定系统对象 → `LockWidget`/`SecondaryLockIndicators` 逐个 Collapsed（宿主 WidgetComponent 一并隐藏）→ `SetShowLockIndicator(false)` → **保存 `GetLockTarget` 后 `StopLockTarget` 清掉锁定目标**（血条根因）；退出时 `SetLockTarget(保存目标)` + `SetShowLockIndicator(true)` 成对恢复。
- 关键词补网：`focus`/`indicator`/`monsterhp`/`enemyhp`/`hpbar`/`healthbar`（实机可见清单 `WG_PBZFocus_C` 即疑似白点控件）；全名带 Monster/Enemy 的 WidgetComponent（敌人头顶血条/名牌）一并隐藏兜底。
- 诊断④：锁定系统对象清单（类/对象/有锁定/目标/成员表）；诊断①②上限 40→120。所有动作逐条记日志、退出按原值还原。

## 1.0.44 这次改了什么（隐藏锁定标记）

- 需求：锁定敌人后进摄影模式，敌人身上的**锁定白点**仍在——`SetHideUIForPV` 订阅者、HUD 布局、血条设置（`MonsterHealthBarMode` 只管血条）都不覆盖它。
- 实现（新配置 `hide_lockon_marker`，默认开，PBZFM 菜单「隐藏」页同名开关）：进入摄影模式全量扫描对象数组，类名/资产名命中锁定·目标·标记关键词（`_`/`-` 归一、`block` 排除）的 UMG 控件 Collapsed、可视组件（Widget/Niagara/Particle/Decal/Sprite/Mesh）bVisible=false，退出全部按原值还原；隐藏清单逐条记日志。
- 白点的真实类名离线证不了：零命中时直接确认完成（进模式后无法再锁定，重扫无益），并打一次性诊断（可见 UserWidget 清单 / 可视组件清单 / 玩家 pawn 名下锁定·目标组件清单）——白点仍在时把日志发回来收敛关键词。
- 隐藏失败（有目标藏不住）才按秒重试，上限 10 次（同 HUD/提示/血条）。

## 1.0.3 这次改了什么

1. **鼠标增量改从引擎取**：hook `PlayerController::InputAxis`，直接用引擎算好的 `MouseX`/`MouseY`/`MouseWheelAxis` 增量。
   系统层 Raw Input 只是兜底 —— 上一版日志显示 Raw Input 在游戏里会「装上但收不到事件」，这条路不可靠。
2. **进模式时打一份引擎对象清单**：PC / Pawn / HUD / MyHUD / 相机 Actor / 相机组件 / ViewTarget.Actor / PC 上和输入有关的函数名。
3. **每秒诊断行加了相机状态**：管理器视角坐标、我们相机的坐标与朝向、组件 FOV、ViewTarget 是不是我们的相机。
4. **ViewTarget 没接管就每秒重试**一次，并把 SpawnActor / SetViewTargetWithBlend 的失败明确写进日志（以前是静默返回）。
5. 菜单文本同时写进 UE 日志（bPrintToLog），用来判断 PrintString 到底有没有执行。


## 1.0.4 这次改了什么（按你的日志逐条修）

日志证据 → 修法：

| 日志里的现象 | 真正原因 | 1.0.4 的改法 |
| --- | --- | --- |
| `PC 输入相关函数` 里没有 `InputAxis`/`InputKey` | 本作是 Enhanced Input，PlayerController 上没有这两个 UFUNCTION | 改用 `PlayerController.GetInputMouseDelta(DeltaX&, DeltaY&)`，直接读引擎这一帧的鼠标增量 |
| `raw=1 事件计数=0`、`raw失效=1` | Raw Input 注册成功但一个事件都收不到 | 滚轮改走 `WH_MOUSE_LL` 低级鼠标钩子（装在跑消息循环的那条线程上，只放行不拦截） |
| `HUD 上找不到 DrawHUD` | `AHUD::DrawHUD` 是 C++ 虚函数、不是 UFUNCTION | 改钩蓝图事件 `ReceiveDrawHUD(SizeX, SizeY)`，用 `AHUD::DrawText` + `GetTextSize` 画右下角菜单 |
| 我们的相机=(4972,280454) 朝向=(0,0)，管理器视角=(4795,280138) | 相机被生成在**角色原点**、朝向 0，等于卡在角色身体里 | 初始位姿改用相机管理器的真实视角 `GetCameraLocation`/`GetCameraRotation` |
| `读不到 PlayerController.ViewTarget.Actor` | 这个属性在本作读不出来 | 改读 `PlayerCameraManager.ViewTarget.Target`，能读到就打印当前视角目标是谁 |
| 按 W 不是向前走 | 视角没被摄影相机接管，相机朝向是 0，WASD 的参考方向就错了 | 上面两条修好后，移动基准跟着相机朝向走 |

另外：进模式时会打印「已屏蔽游戏键鼠输入」，退出时也会逐项还原并打日志。

## 1.0.9 这次改了什么（按 13:02 真机日志）

- **修 Alt + 鼠标上下反向**：引擎 `GetInputMouseDelta` / `InputAxis` 的 `MouseY` 是「向上为正」，
  而 core 的 `Intent.look_dy` 和 Win32 原始坐标是「向下为正」。旧代码直接把引擎 Y 喂给 core，
  所以鼠标上移被当成下移。现在引擎/轴输入统一取反 Y，`invert_y` 仍可再反转。
- **修滚轮 FOV 方向**：`Intent.wheel` 正值定义为滚轮向上；现在向上 `FOV -= wheel * fov_step`（拉近），
  向下则放大。旧代码是加的，方向相反。
- **新增 `R` 重置相机视角**：回到进入摄影模式时快照的位置/朝向/FOV（`key_reset` 可改键）。
- 顺手修了 `handle_intent` 和 `PhotoMode::tick` 重复调用 `toggle_freeze` 导致的“按 F 没反应/双翻转”。
- 核心层测试 236 → **252 项全绿**。


## 1.0.8 这次改了什么（按 12:25 真机日志）

- **修相机朝向永远 (0,0)**：根因不是输入，也不是 Mobility，而是 UE4SS 的 UEPseudo 里
  `AActor::K2_SetActorRotation` 包装按 float 往 `FRotator`（UE5 是 3 个 double）里写值，等于写 0。
  现在改为按反射属性写 `NewRotation.Pitch/Yaw/Roll`，自动按 FDoubleProperty 处理。
  相机位置能动、鼠标也进了 core，但画面不转的根因就是这一条。
- **修 WASD 与视角方向对不上**：这是上一条的连带现象 —— 移动方向用的是 core 里的 yaw，画面用的却是
  被写坏的相机旋转 (0,0)，两者自然不一致。旋转修好后两者同源。
- **修进入摄影模式没有菜单**：`KismetSystemLibrary::PrintString` 的参数名是小写 `duration`，
  旧代码写成 `Duration` 导致 duration=0，消息瞬间消失。现在 duration=1.0 并每 0.35 秒重推，
  按视口尺寸补空行/空格推到右下角；HUD Canvas 路径只作为命中 `ReceiveDrawHUD` 时的主路径。


## 1.0.10 这次改了什么（按 13:25 真机日志：F 键冻结时间不生效）

- 13:16:59 / 13:17:07 的日志里 F 键确实触发了 `时间已冻结` / `时间已恢复`，说明按键和状态机没问题；
  问题是 `UGameplayStatics::SetGlobalTimeDilation` 在这个作里只是“调用成功”，实际倍率没变。
- UE5 的 `AWorldSettings::SetTimeDilation` 会把目标值夹在 `MinGlobalTimeDilation` / `MaxGlobalTimeDilation` 之间；
  且 `GetEffectiveTimeDilation()` 在 `bAllowTimeDilation=false` 时直接返回 1.0。
- 现在直接写 `AWorldSettings.bAllowTimeDilation / TimeDilation / MinGlobalTimeDilation / MaxGlobalTimeDilation`，
  再调用一次 `SetGlobalTimeDilation` 并回读校验，解冻/退出时恢复原始字段。
- 进入摄影模式时日志会多一行 `时间设置：WorldSettings bAllowTimeDilation=... TimeDilation=... Min=... Max=...`，
  下次如果还没冻结成功，把这行发回来即可直接定位。

## 1.0.11 这次改了什么（15:50 日志：TimeDilation 已生效，但角色/敌人/特效仍在动）

- 15:50:15 的日志：`时间设置目标=0.0001 生效=0.0001`，说明 `AWorldSettings.TimeDilation` 确实写进去了；
  但你在游戏里看到角色/敌人/NPC 的动作和特效继续播放，说明本作的 SGame 玩法模拟并不吃全局时间膨胀。
- 因此 v1.0.11 把默认冻结方式改成 **pause（游戏暂停）**，不再依赖 `TimeDilation`：
  - 暂停时临时把 `APlayerController.bShouldPerformFullTickWhenPaused` 设为 true，让相机管理器继续 Tick，
    摄影相机仍可自由移动/转视角；
  - 如 GameMode 的 `bPauseable` 被关掉，也会临时打开，退出/解冻时原样恢复；
  - 解冻/退出用 `IsGamePaused` 回读确认。
- 想保留原来的时间膨胀行为，可在 `config.ini` 把 `freeze_mode` 改回 `dilation`。
- 版本 `1.0.11-cpp`；核心层测试 252 项全绿；打包为 `dist/PBZPhotoModeCpp_1.0.11-cpp.zip`。

## 1.0.11 实机验证

2026-10-30 16:04 用户日志确认：`游戏暂停成功 | IsGamePaused=true` → `时间已冻结`，暂停期间相机仍可移动；`IsGamePaused=false` → `时间已恢复`。F 键冻结时间已验证通过。

## 1.0.12 这次改了什么（进入摄影模式自动隐藏 HUD）

- 旧版 `hide_game_hud` 只是把 `PlayerController.MyHUD`（AHUD Actor）`SetActorHiddenInGame`，对本作纯 UMG 的 HUD 无效。
- 1.0.12 首先调用游戏自带的 `UPBZUIManagerSubsystem::SetHideUIForPV(true/false)` 总开关（它会广播 `OnHideUIForPVChanged`，
  Boss 血条、教程提示等订阅者一起隐藏/恢复）；同时找 UMG HUD 根控件 `W_FrontEndHUDLayout_C`
  （`UPBZHUDLayout` 的唯一蓝图子类，HP/快捷栏/武器/任务等挂在它下面），通过
  `UWidget::SetVisibility(ESlateVisibility::Collapsed)` 隐藏，退出时按进入前读到的原始 `Visibility` 值恢复。
- 如果进入模式时 HUD 根控件还没建好，每秒重试一次；成功后不再重复遍历对象数组，退出时清理记录。
- `hide_game_hud` 默认值改为 `true`；想保留游戏 HUD 可在 `config.ini` 改回 `false`。
- 进入摄影模式时的引擎对象日志会多出 `HUD控件=... Visibility=...`，用于确认根控件是否找到。
- 版本 `1.0.12-cpp`；核心层测试 258 项全绿；打包为 `dist/PBZPhotoModeCpp_1.0.12-cpp.zip`。

## 1.0.13 这次改了什么（曾移除 HighResShot 截图；1.0.75 收口批次恢复 F9 机制，但本作实测不可用）

- 1.0.13 当版曾移除实验性的 `HighResShot` 截图：本作 `BP_PlayerController_C` 没有 `ConsoleCommand` UFunction；直接调用 `UGameViewportClient` 虚函数存在 ABI/崩溃风险，且 `HighResShot` 仅 Editor/standalone 可用。
- **当前状态（1.0.75 收口批次）**：F9 机制已恢复（`GetProcAddress` 解析 `FScreenshotRequest::RequestScreenshot(bShowUI=false)`），但本作 Shipping 导出表无该符号，实机日志为「截图能力不可用」，因此本作 F9 不会出图；摄影模式 UI 已隐藏，请用 NVIDIA App `Alt+F1` / Steam `F12` / Xbox Game Bar。机制本身对导出该符号的其它 UE 游戏仍有效，详见「F9 无 UI 截图」。
- 若符号被裁剪，外部工具替代：NVIDIA App / GeForce Experience（默认 `Alt + F1`）、Steam（`F12`）、Xbox Game Bar（`Win + Alt + Pr`）。
- 版本 `1.0.13-cpp`；核心层测试当时为 257 项（历史快照，当前断言总数见 [test_count.txt](test_count.txt)）。

## 1.0.14 这次改了什么（进入摄影模式自动暂停时间）

- 需求：打开相机模式时自动暂停时间，用户可在 `config.ini` 中修改开关，默认开启。
- `[effects]` 新增 `auto_pause_time = true`：进入摄影模式成功后，自动按 `freeze_mode` 的方式冻结时间
  （`pause` = 游戏暂停，`dilation` = 全局时间变慢）；退出摄影模式时照常恢复。
- 关闭后（`auto_pause_time = false`）进入摄影模式时间照常流动，仍可按 `F` 手动冻结/恢复。
- 游戏内菜单新增「进入时暂停」状态行，UE4SS 的 ImGui 面板新增对应开关。
- 自动暂停失败只写 warning，不会阻止进入摄影模式。
- 版本 `1.0.14-cpp`；核心层测试 298 项全绿。

## 1.5.0 这次改了什么（超宽屏支持，消除两侧黑边）

- 需求：21:9 / 32:9 超宽屏用户反映，进入摄影模式后画面两侧仍有黑边。游戏官方支持超宽屏
  （超过 3:1 的比例，21:9/32:9 均在范围内），黑边应只出在摄影模式路径——拍照/演出常主动约束画幅：
  UE 里这把锁挂在 `APlayerCameraManager.bDefaultConstrainAspectRatio / DefaultAspectRatio`
  （对任何 ViewTarget 生效，换成摄影相机也锁），部分相机还会在组件上开 `bConstrainAspectRatio`；
  真机日志 `画幅锁=开/关(比例)` 会确认锁的位置。
- `[camera]` 新增 `ultrawide_unlock = true`（默认开启）：进入摄影模式时保存管理器原值并解除锁定，
  同时把摄影相机组件的 `bConstrainAspectRatio` 显式关掉；退出摄影模式时按进入前的值还原。
- 解锁结果写日志：`画幅解锁：已解除 16:9 锁定（原比例=1.778）`；管理器没锁会打
  `画幅：相机管理器未锁定画幅，无需解锁`；属性读不到/写不进去会打 warning（黑边若来自别的机制，
  把这条日志发回来）。解锁失败不阻断进入摄影模式。
- 每秒诊断行新增 `画幅锁=开/关(比例)`，真机验证解锁是否生效一眼可查。
- 想保留 16:9 构图（比如要裁成 16:9 输出）的用户可在 `config.ini` 里把 `ultrawide_unlock` 改为 `false`。
- 版本 `1.5.0-cpp`；核心层测试 503 → **527 项全绿**（新增画幅解锁进退/失败/热重载用例）。

## 1.0.30 这次改了什么（对话语音：游戏自有停止接口）

- 真机观察：冻结时对话推进已随世界冻结（说过的句子不会触发下一句），残留的是「正在播的当前句」。
- 普查在 `PBZSoundFXSubsystem` 找到游戏自有的 `PBZStopAkComponent`/`PBZStopAllSoundsOnActor`/`PBZPausePlayingID`/`PBZResumePlayingID` 等接口。冻结静音改为优先调用 `PBZStopAkComponent`（参数签名校验，失败回落组件 `Stop`）。
- `stop_wwise_on_freeze` 默认改为 **true**：冻结即静音（环境/对话/音乐一并停，解冻后由游戏自行重播）；不想静音改回 `false`。

## 1.0.35 这次改了什么（字幕压制扩到对话 UI 本体；对话语音按真实签名填参）

- **字幕**：1.0.34 实机确认设置开关已真关（`bEnableSubtitles` 回读=关）、字幕子控件已 Collapsed，但文本仍显示——文本画在对话 UI 本体 `WBP_FlowDialog_C` 上。压制范围扩到对话框家族（类名含 FlowDialog）：摄影模式里对话框+字幕一起隐藏，退出还原。
- 每 10 次重申做一次全量扫描：对话中途新出现的控件类也会被收编。
- 一次性「可见 UserWidget 清单」诊断：若压制后字幕仍显示，日志会列出场上所有可见控件类——把那行发回来即可精确指认。
- **对话语音**：`PBZPausePlayingID`/`PBZResumePlayingID` 按真实签名 `(int32 PlayingID, float FadeTime)` 填参（FadeTime=0 立即生效），冻结当前句真静音、解冻从停住位置续播。

## 1.0.33 这次改了什么（字幕隐藏改持续压制；对话语音见 PROGRESS）

- 1.0.32 反馈：进模式后字幕不消失。根因：对话 UI 只在创建时读一次字幕设置，运行中的对话由游戏逐条把控件重新显示——进入时藏一次会被下一条字幕盖回去。
- 修复：进入后**每秒重申**——已管控件被游戏改回可见就再压回 Collapsed，新实例（对话 UI 重建）自动收编并记录原值，退出统一还原；设置开关只在进入时写一次（避免重复写的未知副作用）。
- 进入/退出各做一次全量扫描并缓存字幕控件类名（`WBP_FlowSubTitle_C`），每秒重申只按类名查找，微秒级不拖帧。
- 已知边界：字幕被 1.0.30 写成关闭的游戏存档，仍需在游戏设置里手动开回一次。

## 1.0.31 这次改了什么（修复：字幕退出后永久消失）

- 1.0.30 实机 bug：进模式字幕消失正常，退出后字幕永久消失。根因：`PBZSettingsShared` 只有 `SetSubtitlesEnabled` 函数、没有 `bSubtitlesEnabled` 属性（属性在引擎 GlobalUserParam 上），旧代码只在属性读成功时记录原值，导致开关翻转后退出还原分支被整个跳过（且静默无日志）。
- 修复：原值改走 `AreSubtitlesEnabled` getter（读不到按开兜底）；还原条件改为「只要动过开关」，还原失败打 Warn；字幕控件收集扩到游戏自制控件 `GAA_Subtitles_C`（场上没有 USubtitleDisplay 插件控件）。
- **曾被子 1.0.30 卡掉字幕的，先在游戏设置里把字幕手动开回来，再装本包。**

## 1.0.30 这次改了什么（对话语音停住 + 隐藏对话字幕）

- **对话语音**：冻结静音升级走游戏自有接口（`PBZSoundFXSubsystem.PBZStopAkComponent`，签名校验通过才调，失败回落组件 `Stop`）；`stop_wwise_on_freeze` 默认改为 **true**（实测对话流程随世界冻结，停掉当前句不丢内容，不想静音可在 config.ini 改回 false）。
- **进入摄影模式自动隐藏对话字幕**（`hide_subtitles` 默认 `true`）：双路径——① 游戏字幕设置 `bSubtitlesEnabled` 关掉（管住之后新出的字幕，退出还原原状态）；② 字幕显示控件（GameSubtitles 插件 `USubtitleDisplay`，含 BP 子类）直接 Collapsed（管住此刻已经画出来的；暂停下控件不再刷新，光改设置对已显示的字幕不生效，退出按原可见性恢复）。失败每秒重试，不阻断进模式。
- 字幕控件一个都没找到时会打一次性 Warn 并列出场上类名带 Subtitle 的控件类——游戏若用自制字幕控件，把那行日志发回来定下一版。
## 1.0.25 这次改了什么（按真机日志：命中 1 执行 0 + Wwise 语音）

- 真机日志结论：动作冻结已稳定；但日志里多次「剧情播放器：命中 1，执行 0」——Sequencer 的 `Pause`/`Play` 是无参函数，参数块为空导致调用在 ProcessEvent 之前就放弃。已修复（缓冲区至少留 1 字节），剧情与场景后台播放器现在真正能暂停/恢复。
- 对话语音「有时拦得住有时拦不住」：序列音轨驱动的语音随上面的修复一起停；Wwise 直接 post 的语音由新增的 `stop_wwise_on_freeze`（默认 false，停止不可恢复）处理。
- 每会话探测一次并打日志：`Wwise 组件诊断：AkComponent=N，可用函数=…`——下一版据此决定是否做可恢复的音频暂停。
- **进入摄影模式自动隐藏敌人（怪物）血条**（`hide_enemy_health_bars` 默认 `true`）：游戏侧总开关是设置项 `MonsterHealthBarMode`（总是显示/仅锁定目标/满血隐藏），摄影模式没有锁定目标，切到"仅锁定目标"即全部隐藏，退出还原原模式；设置没就绪时每秒重试，失败只告警不阻断。

## 1.0.24 这次改了什么（剧情中冻结失效修复）

- 现象：游戏处于剧情时进摄影模式，自动冻结"看起来"生效（画面像冻住），但角色动作和声音还在继续。
- 修复分三层：① 冻结时同时**暂停 Sequencer 剧情播放器**（`FindAllOf(MovieSceneSequencePlayer)` 沿继承链匹配，`LevelSequenceActor.SequencePlayer` 兜底；暂停用 `Pause()`，解冻只恢复处于暂停态的 `Play()`；`freeze_sequences` 默认 `true`）；② **暂停被游戏拒绝时自动回退时间缩放**，不再只告警；③ **冻结期每秒回读自检**，被剧情/游戏改写就重新冻结，连续 3 次压不住停止对抗并告警。
- 已知边界：剧情语音若走 Wwise/独立音频组件，播放器暂停拦不住（日志里有 `Wwise=有/无` 探测），实机确认后单独处理。
- 每次冻结/解冻日志会打 `剧情播放器[暂停/恢复]：扫描 N，命中 M，执行 K`——剧情中冻结仍无效时把这行发回来即可定位。

## 1.6.0 这次改了什么（进入摄影模式隐藏交互提示）

- 需求：进入摄影模式后，对准场景互动物（铃铛/拾取点）时的“E”键提示图标不会消失。
- 根因：该提示是 `PBZInteractComponent` 挂的**世界空间 UMG**（组件属性 `InteractWidgetComponent`），
  不在 `SetHideUIForPV` / HUD 根控件的覆盖范围内；且 `freeze_mode=pause` 暂停下游戏 UI 不再刷新，
  已画出来的提示必须隐藏控件组件本身才会消失。
- 实现（双路径，退出都还原）：① 游戏自带设置开关 `bSceneInteractPromptVisible`（运行时按候选类探测，
  优先调 `SetSceneInteractPromptVisible`，参数名对不上直写属性）——管住之后新出现的提示；
  ② 枚举 `PBZInteractComponent` 的 `InteractWidgetComponent` 并 `SetVisibility(false)`——管住此刻已画出的。
- `[effects]` 新增 `hide_interact_prompt = true`（想保留提示可改 `false`）。
- 失败不阻断进入摄影模式：两路都失败只打一次性 Warn（真机排障把日志发回来）。
- 版本 `1.6.0-cpp`；核心层测试 527 → **544 项全绿**（新增交互提示隐藏用例）。
