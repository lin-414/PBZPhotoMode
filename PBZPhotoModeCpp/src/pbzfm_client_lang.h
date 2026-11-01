#pragma once

// PBZFM 自绘设置页的客户端自治 i18n。
//
// 词典全部来自外部 JSON，DLL 不内置任何文本：
//   - 词典文件：<mod 目录>\languages\<语言 id>.json，扁平 UTF-8 JSON：
//       key   = 稳定命名键（# 前缀；设置项用 # + config 键名，如 #pad_enabled），
//       value = 该语言的显示文本；
//       "__display" = 菜单语言列表里的显示名（可选，缺省用文件名）。
//   - en-US 是基准词典（键 → 英文原文）：新增译文直接对照它逐键翻译。
//     发行包自带 en-US / zh-CN / ja-JP / ko-KR 四份，键集完全一致；
//     同名 id 的文件覆盖旧词典，其他文件名即新增语言（排在已知四种之后）。
//   - languages 目录缺失或词典为空：语言列表为空，页面文案显示键名。
//   - 查译链：当前语言 → en-US（基准兜底）→ 原样返回键名。
// 解析器为扁平「"key": "value"」对象（与框架同款语义），不支持注释；坏词典整份跳过。
//
// 仅真实 DLL 构建参与编译（PBZPM_WITH_UE4SS）。
#ifdef PBZPM_WITH_UE4SS

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pbzpm {

class PbzmI18n {
public:
    // 扫描 <mod_dir>/languages/*.json 并覆盖/追加语言；force=true 立即执行，
    // 否则内部 2 秒节流（自绘页每帧调用无压力，词典边玩边改可热生效）。
    // 注册线程与框架渲染线程都会进来。
    void RescanLanguages(const std::string& mod_dir, bool force);

    int LanguageCount();
    const char* LanguageId(int index);
    const char* LanguageDisplay(int index);
    int FindLanguage(const char* id);   // 未加载返回 -1（空串/nullptr 同）
    void SetLanguage(int index);        // 越界归位到 0（无词典时保持 0）
    int CurrentIndex();
    const char* CurrentId();

    // 查译：当前语言 → en-US → 原样返回键名。返回值当帧内有效（词典重扫可能换指针）。
    const char* T(const char* key);

private:
    struct Lang {
        std::string id;
        std::string display;
        std::map<std::string, std::string> strings;
    };

    void Upsert(const std::string& id, const std::string& display,
                std::map<std::string, std::string>&& strings);
    void SortAndClamp();

    std::mutex mutex_;
    std::vector<Lang> langs_;       // 加载自 languages/*.json；展示顺序见 SortAndClamp
    int current_{0};
    double next_scan_{0.0};         // steady 秒
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
