#include "core/i18n_logic.h"

namespace pbzpm {
namespace {

// 已知语言的固定顺序；未知语言统一排在已知语言之后。
int LanguageOrderRank(const std::string& id) {
    if (id == "en-US") return 0;
    if (id == "zh-CN") return 1;
    if (id == "ja-JP") return 2;
    if (id == "ko-KR") return 3;
    return 100;
}

}  // namespace

bool language_display_less(const std::string& a, const std::string& b) {
    const int ra = LanguageOrderRank(a);
    const int rb = LanguageOrderRank(b);
    if (ra != rb) return ra < rb;
    if (ra != 100) return false;  // 已知语言保持固定声明顺序
    return a < b;
}

std::string resolve_translation(const std::map<std::string, std::string>& current,
                                const std::map<std::string, std::string>* fallback,
                                const std::string& key) {
    if (auto it = current.find(key); it != current.end()) return it->second;
    if (fallback != nullptr) {
        if (auto it = fallback->find(key); it != fallback->end()) return it->second;
    }
    return key;
}

}  // namespace pbzpm
