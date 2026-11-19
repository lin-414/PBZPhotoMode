#pragma once

// 配置字段表（历史文件名 pbzfm_schema_map）。
// 这张表是「PBZFM 菜单 / Mods Menu 子集 / Config JSON 字段 ↔ Config 成员」的唯一事实来源：
//   - pbzfm_client.cpp 按它注册/播种/回读 PBZFM 自绘页；
//   - mods_menu.cpp 按它生成/应用菜单子集 JSON；
//   - 范围/枚举/默认值与 config.cpp 解析共用同一套定义；
//   - 核心层测试按它做往返校验。
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
    // 取景角度滑条（pitch/yaw/roll）：-1 = 普通配置字段；0/1/2 = 该轴的实时相机姿态。
    // 带这个标记的 Float 字段不读写 Config，菜单直接从 Host 读实时角度、拖动直接写实时姿态，
    // 这样鼠标/手柄转过之后滑条也是最新值，不会出现「滑条与鼠标各改一份、互相掰回去」。
    int pose_axis{-1};
    // 只在摄影模式激活时可用（未激活时客户端把滑块与重置按钮置灰）。
    bool requires_photo_mode{false};
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
