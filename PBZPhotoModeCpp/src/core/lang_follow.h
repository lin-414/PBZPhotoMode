// 菜单语言跟随策略（纯逻辑，不依赖 UE4SS / 文件系统）。
//
// 背景（H-6 语言不跟随修复）：模组菜单有两份语言来源——
//   1) settings.json 的 menu_language（玩家在本模组语言页的显式选择，auto=跟随）；
//   2) PBZFM.ini 的 [framework] language（框架自己在用的语言）。
// 生效语言 = 显式选择（且词典存在）优先，否则跟随 PBZFM，否则用兜底语言，
// 都没有就返回空串让调用方保持现状（绝不因为读不到文件就把语言重置成 0 号）。
//
// 放在 core 侧是为了可单测：解析与决策都是纯字符串/集合运算，真实读文件与
// SetLanguageById 留在 pbzfm_client。
#pragma once

#include <string>
#include <vector>

namespace pbzpm {

// 从 PBZFM.ini 文本里取 [framework] 段的 language 值（去首尾空白、去成对引号）。
// 只看 [framework] 段；文件里完全没有该段时退回"第一个 language 键"（兼容手写/旧版）。
// 注释行（; / #）忽略；找不到返回空串。
std::string parse_pbzfm_language(const std::string& ini_text);

// 决定菜单生效语言 id（空串 = 保持当前语言，调用方不得回退到 0 号语言）：
//   settings_value 是具体 id 且在 available 里 → 用它（玩家显式选择）
//   否则 ini_language 在 available 里          → 用它（跟随 PBZFM）
//   否则 fallback_id 在 available 里           → 用它（首次启动的默认语言）
//   否则                                       → 空串
std::string decide_menu_language(const std::string& settings_value, const std::string& ini_language,
                                 const std::vector<std::string>& available, const std::string& fallback_id);

}  // namespace pbzpm
