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
    const char* const* options{nullptr};  // Enum：选项文本（存储值为下标）
    int option_count{0};
    // Bool / Float / Enum 的取值与写值
    bool (*get_bool)(const Config&){nullptr};
    void (*set_bool)(Config&, bool){nullptr};
    double (*get_num)(const Config&){nullptr};
    void (*set_num)(Config&, double){nullptr};
    int (*get_enum)(const Config&){nullptr};
    void (*set_enum)(Config&, int){nullptr};
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

}  // namespace pbzpm
