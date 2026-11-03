#pragma once

// 词典的纯判定：不依赖 UE4SS / ImGui / 文件系统，可被核心层测试直接覆盖。

#include <map>
#include <string>

namespace pbzpm {

// 语言展示顺序：en-US < zh-CN < ja-JP < ko-KR < 其余语言按 id 字典序。
bool language_display_less(const std::string& a, const std::string& b);

// 查译链：当前语言词典 → 基准词典（en-US，可为空）→ 原样返回 key。
// 返回值是独立拷贝，脱离任何快照/锁之后仍然有效。
std::string resolve_translation(const std::map<std::string, std::string>& current,
                                const std::map<std::string, std::string>* fallback,
                                const std::string& key);

}  // namespace pbzpm
