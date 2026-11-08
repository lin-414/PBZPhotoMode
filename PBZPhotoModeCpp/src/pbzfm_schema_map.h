#pragma once

// PBZFM（PBZ-Framework-Menu 菜单框架）的设置项映射表。
// 这张表是「菜单 schema 条目 ↔ Config 字段」的唯一事实来源：
//   - pbzfm_client.cpp 按它注册/播种/回读框架侧的 schema 条目；
//   - 菜单改动按它写回 Config（再由 mod_app 落盘 config.ini 并实时应用）；
//   - 核心层测试按它做往返校验，不需要框架 DLL 存在。
// 纯数据 + 纯函数：不含 Windows/UE4SS 依赖，随 pbzpm_core 一起编译与测试。
#include <string>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "core/vk.h"

namespace pbzpm {

enum class PbzfmFieldKind { Bool, Float, Enum, Key };

struct PbzfmField {
    PbzfmFieldKind kind;
    const char* group;   // schema 分组标题（与上一项的 group 不同时客户端补一个分组头）
    const char* key;     // schema 存储键（沿用 config.ini 键名，少一层映射）
    const char* label;   // 菜单里显示的文本
    double mn{0.0}, mx{0.0}, step{1.0};
    const char* fmt{"%.3f"};
    // Float：滑条是否走对数刻度。跨 3 个数量级以上的字段（对焦距离 1..100000cm、
    // 对焦步进 1..5000cm）必须开，否则线性滑条上 1 像素 ≈ 500cm，根本拖不到可用的 300~1500cm。
    bool logarithmic{false};
    const char* const* options{nullptr};  // Enum：选项文本（存储值为下标）
    int option_count{0};
    // Bool / Float / Enum 的取值与写值
    bool (*get_bool)(const Config&){nullptr};
    void (*set_bool)(Config&, bool){nullptr};
    double (*get_num)(const Config&){nullptr};
    void (*set_num)(Config&, double){nullptr};
    int (*get_enum)(const Config&){nullptr};
    void (*set_enum)(Config&, int){nullptr};
    // Float：可选的独立开关（画面效果五项使用）。非空时菜单在该滑块行渲染一个开关按钮；
    // 关掉只停用该项，滑块值保留。与 get_bool/set_bool 分开，避免影响 Bool 条目的读值语义。
    bool (*get_enabled)(const Config&){nullptr};
    void (*set_enabled)(Config&, bool){nullptr};
    // Key：config 里的键名成员 + 键名解析失败时的回退 VK（来自 KeyMap）
    std::string Config::* key_name{nullptr};
    int (*key_fallback)(const KeyMap&){nullptr};
};

// 全部可进菜单的字段（顺序即菜单顺序；group 变化即换组）
const std::vector<PbzfmField>& pbzfm_fields();

// 读取字段当前值：bool→0/1；float/enum→数值；key→VK（键名解析失败回退 KeyMap 默认值）
double pbzfm_read_field(const PbzfmField& f, const Config& cfg, const KeyMap& keys);

// 把菜单提交的值写进 cfg。Key 做 vk→键名→vk 往返校验（捕获到不可命名的键时拒绝），
// float/enum 越界夹紧。返回 false 表示未应用（调用方应把菜单显示值回滚）。
bool pbzfm_apply_field(const PbzfmField& f, double value, Config& cfg);

// Float 的可选独立开关读写；字段没有开关时分别返回 true / false（不写）。
bool pbzfm_read_enabled(const PbzfmField& f, const Config& cfg);
bool pbzfm_write_enabled(const PbzfmField& f, bool enabled, Config& cfg);

// 字段的编译期默认值（由 Config{} 读取，避免菜单再抄一份默认值表）。
double pbzfm_default_value(const PbzfmField& f);

// 把单个滑块重置为默认值（只对 Float 有意义）；返回是否写入了不同值。
bool pbzfm_reset_field(const PbzfmField& f, Config& cfg);

}  // namespace pbzpm
