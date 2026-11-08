# v2.0.4 收口回归计划（三项待回归 + API 探针）

对应 `docs/README.md` 状态行「超宽屏/DInput 手柄/字幕压制各项待回归」。
三项修复均已合入 2.0.2/2.0.4 且核心层断言覆盖，本文件只做**实机复核**；
同场并入 4 个原生 API 探针（见 §5，结果记入 `native_api_candidates.md`，不影响回归判定）。
顺带抽查工作区「景深/对焦 + 色差/胶片颗粒复活」批次（2026-11-07，PROGRESS 顶部）的进出稳定性。

约定：

- 环境记录：游戏构建号、模组 DLL 版本、UE4SS 版本、显示器分辨率/刷新率、手柄型号与连接方式。
- 证据：UE4SS.log 关键行原文 + 截图；每项给出期望日志子串。
- 全部步骤先在普通存档场景做；过场/处决场景单独标注。

## ① 超宽屏暂停画幅（2.0.2②，`reassert_aspect_ratio_if_needed` 每帧重申）

背景：暂停/演出把 `bDefaultConstrainAspectRatio` 锁打开时，摄影模式里画幅被压成 16:9；
2.0.2 起进入即无条件保存原值、每帧在 GameViewportClient Tick 后把锁压回、退出按进入前状态还原。

| # | 步骤 | 预期 |
| --- | --- | --- |
| 1 | 21:9 正常游玩（无锁）按 F2 进入摄影模式 | 全程保持 21:9，无黑边 |
| 2 | 摄影模式内按 F 冻结、再解冻，反复 3 次 | 画幅始终 21:9 |
| 3 | 处决/过场演出进行中按 F2 进入（自动暂停） | 摄影模式内保持 21:9；演出自身的 16:9 锁不被当作永久状态 |
| 4 | 上一步退出摄影模式 | 恢复进入前画幅；处决演出继续正常播放 |
| 5 | 16:9 全屏下进入再退出 | 仍为 16:9（不该被"还原"成其他比例） |
| 6 | 32:9 重复步骤 1-2 | 同 21:9 |

日志关注：无「恢复失败」类重试告警刷屏。

### 探针 T1：原生画幅约束忽略开关

`SetIgnoreCameraAspectRatioConstraint(bool)` 归属 **PBZPlayerCameraManager**（与
`bDefaultConstrainAspectRatio` 同类，ProjectPBZ.d.ts:13200）。UE4SS GUI Lua 控制台：

```lua
local cm = FindFirstOf("PBZPlayerCameraManager")
cm:SetIgnoreCameraAspectRatioConstraint(true)
```

进入摄影模式并触发暂停，观察暂停是否还能把画幅锁成 16:9。
判定：若忽略开关下暂停不再改画幅 → 记入候选表「采用评估」（评估是否可替代每帧重申）；
若无效或行为怪异 → 记「已评估不适用」，现行每帧重申维持不动。探针后记得 `false` 还原。

## ② DInput 手柄（1.4.0 DInput8 轮询后端）

设备矩阵：DualSense / DualShock4 各一次，USB 与蓝牙各测一轮；另接一只 Xbox 手柄确认
XInput 优先、同一设备不在两套后端重复计数。

接入证据（README「接入诊断」约定）：

- 一次性：`DInput 手柄已接入：<名>（VID_…&PID_…）首读：按钮=… 摇杆=… POV=…`
- 每秒诊断行：`手柄=<后端摘要>`

键位矩阵（`[gamepad]`，README 键位表为准；手柄景深组合键为工作区批次复活项，一并验）：

| 操作 | 按键 | 预期 |
| --- | --- | --- |
| 进出摄影模式 | LS+RS | 同 F2；连带按键不误触 |
| 升降 | RT / LT | 平滑、无粘滞 |
| 滚转 | LB / RB；归零 LB+RB 同按 | 与键盘 Q/E/Z 一致；**单按 LB/RB 时景深组合键不抢**（1.0.7x 修复点） |
| 加速/减速 | LS / RS | 同 Shift/Ctrl |
| 景深组合 | LB+BACK 拉近 / LB+START 拉远 / RB+BACK 开光圈 / RB+START 收光圈 | 仅组合键生效，不触发滚转（含反向用例的实机对映） |
| 菜单（PBZFM F8） | 手柄导航 | 设置项改值写回 config.ini 并实时生效 |

判定：全部行功能正确、无重复计数、日志无「手柄=<空>」异常。

## ③ 字幕压制（`hide_subtitles` + `freeze_dialogue_flow`）

背景：字幕文本画在 `WBP_FlowDialog_C` 等对话控件上；FlowDialog 家族用 `Hidden`(2) 保留
Slate Tick（对话推进不停摆），其余字幕控件 `Collapsed`；`bEnableSubtitles` 原值只读不写。

| # | 步骤 | 预期 |
| --- | --- | --- |
| 1 | 找一段有字幕/对话的剧情点，字幕显示中按 F2 进入 | 字幕与交互提示隐藏；日志出现 FlowDialog 检测行（如命中）：`检测到对话 UI（FlowDialog），保留字幕设置开关不写…` |
| 2 | 摄影模式内停留至原对话应推进的时刻，再解冻 | 解冻后显示的是推进后的语句（FlowDialog Tick 未停摆）；无字幕残留叠影 |
| 3 | 退出摄影模式 | 字幕/交互提示按原可见性恢复 |
| 4 | 反复进出 5 次 | 字幕与 E 交互提示始终恢复（1.0.71 回归点：不出现永久消失）；日志无 `UI 设置修复多次重试未成功…跳过` |
| 5 | `freeze_dialogue_flow=false` 重复步骤 1-2 | 行为回退 1.0.73 语义（对话 UI 只隐藏不干预推进），不报错 |
| 6 | 无对话的普通场景进出 | 无 FlowDialog 相关新日志（`WBP_FlowDialog_C` 不存在时不产生噪音） |

## ④ 顺带抽查：工作区景深复活批次的进出稳定性

只验生命周期，不验画质：F2 进出 ×3、F 冻结/解冻 ×2、F7 帧定格安全阀一次。
关注：进入时「景深诊断：…」第 8 帧回读一次性出现；退出逆序恢复（拾取态光标 → 胶片 tag →
景深 tag → NLP 关档）无「恢复失败」重试；PBZFM F8 菜单 `#group_dof` 节点正常渲染。

## ⑤ API 探针汇总（结果回填 `native_api_candidates.md` 状态列）

| 编号 | 探针 | UE4SS Lua 控制台 | 判定 |
| --- | --- | --- | --- |
| T1 | 画幅约束忽略 | 见 §① | 原生开关可替代每帧重申 → 采用评估；否则不适用 |
| T2 | 暂停中相机可动 | `local pc=FindFirstOf("PBZPlayerController") pc:SetCameraMoveableWhenPaused(true)` 后 F2 进入（自动暂停） | 相机仍可自由移动/暂停状态成立 → 记候选（评估能否简化视角守护）；异常即放弃 |
| T4 | 全局声音淡出 | `local lib=FindFirstOf("PBZBlueprintFunctionLibrary") lib:FadeOutAllSound(lib, 0.8)` / `FadeInAllSound(lib, 0.8)` | 与 Wwise Suspend 的进出过渡兼容性观察；仅记录，不改变现行双保险 |
| T5 | 冻结微动治理 | 冻结状态下对 `FindFirstOf("SkeletalMeshComponent")` 试 `ClearSkeletalMeshMotionVector(mesh, true)` 与 `EnableClothTick(false, mesh)` | 微动/布料残留是否减少；仅记录 |

注意：Lua 直调要求函数可从反射调用；个别被蓝图标志拦住的调用改走 ue4ss_port 临时诊断通道，
不要为此改核心层。T3（HUD ShowModeTag）因需先枚举有效 Tag 集合，本轮不做，见候选表。

## 完成动作

1. 逐项把结果（通过/失败 + 证据行）回填到本文档各表；失败项按 `freeze_ghosting_*.md` 风格追加取证段。
2. 全部通过后更新 `docs/README.md` 状态行（版本号与发布节奏由你自管）。
3. 探针结论写入 `native_api_candidates.md`；探针用的临时代码（如有）不留入正式提交。
