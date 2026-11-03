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
// 并发模型（H-4 修复）：
//   - 语言表是不可变快照 Snapshot：写侧（RescanLanguages / SetLanguage*）持
//     write_mutex_ 构造新快照，再原子发布 shared_ptr<const Snapshot>；
//   - 读侧（T / Languages / FindLanguage / ...）只做一次原子读取拿到快照，整个调用
//     周期都持有它。重扫替换快照后旧快照仍存活到最后一个读者释放，因此不会悬垂；
//   - T 返回 std::string 拷贝：调用方脱离锁/快照之后依然有效。
// 线程归属：所有方法可被多个线程并发调用；仅写侧串行化，读侧无锁。
// 本文件不依赖 UE4SS/ImGui，编译进纯核心层，单元测试可直接覆盖。

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pbzpm {

class PbzmI18n {
public:
    // 一次性拷贝的语言列表项：UI 遍历期间不受重扫影响（ID 与显示名始终配套）。
    struct LanguageInfo {
        std::string id;
        std::string display;
    };

    // 扫描 <mod_dir>/languages/*.json 并覆盖/追加语言；force=true 立即执行，
    // 否则内部 2 秒节流（自绘页每帧调用无压力，词典边玩边改可热生效）。
    // 注册线程与框架渲染线程都会进来；写侧由 write_mutex_ 串行化。
    void RescanLanguages(const std::string& mod_dir, bool force);

    int LanguageCount() const;
    std::string LanguageId(int index) const;       // 越界返回空串
    std::string LanguageDisplay(int index) const;  // 越界返回空串
    std::vector<LanguageInfo> Languages() const;   // 同一快照内的一致性列表
    int FindLanguage(const char* id) const;        // 未加载返回 -1（空串/nullptr 同）
    void SetLanguage(int index);                   // 越界归位到 0（无词典时保持 0）
    void SetLanguageById(const std::string& id);    // 未找到归位到 0；重扫重排不会错位
    int CurrentIndex() const;
    std::string CurrentId() const;

    // 查译：当前语言 → en-US → 原样返回键名。
    // 返回 std::string（值语义）：调用方拿到的是独立拷贝，出锁/词典重扫后仍有效。
    std::string T(const char* key) const;

private:
    struct Lang {
        std::string id;
        std::string display;
        std::map<std::string, std::string> strings;
    };

    // 发布后不再修改；current 与 langs 必须同属一个快照，避免重排后下标错位。
    struct Snapshot {
        std::vector<Lang> langs;  // 展示顺序见 SortAndClamp
        int current{0};
    };

    using SnapshotPtr = std::shared_ptr<const Snapshot>;

    SnapshotPtr Load() const;  // 读侧：一次原子读取，整个调用周期持有返回值
    void Publish(SnapshotPtr next);

    static void Upsert(std::vector<Lang>& langs, const std::string& id, const std::string& display,
                       std::map<std::string, std::string>&& strings);
    static void SortAndClamp(Snapshot& snap);

    mutable std::mutex write_mutex_;                    // 仅写侧串行化（Rescan / SetLanguage*）
    std::atomic<SnapshotPtr> snapshot_;                 // 读侧无锁取不可变快照
    double next_scan_{0.0};                             // 写在 write_mutex_ 内；steady 秒
};

}  // namespace pbzpm
