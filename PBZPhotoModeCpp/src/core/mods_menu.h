// Mods Menu 149（SLEEP「Mods Menu - In-Game Mod Settings」）UE4SS Bridge 的纯逻辑层。
//
// 路线约束（见 Mods Menu 149 的《guide for mod authors》section 2）：
//   - 玩家安装 Mods Menu 0.9.x + 可选 PBZModsMenuBridge；
//   - 我们的 UE4SS 模组在 <Mods>/PBZPhotoModeCpp/ 下提供
//       PBZPhotoModeCpp.modsmenu.json   （随包分发，声明设置行）
//       PBZPhotoModeCpp.config.json     （本模组首次运行创建，此后 Bridge 负责写）
//   - Bridge 每次保存后写 PBZModsMenuBridge/events-PBZPhotoModeCpp.txt，本模组按 250ms 轮询。
//
// 本文件只做“纯核心”部分：扁平 JSON 解析/序列化、字段映射、Config -> JSON。
// 文件路径、轮询、写盘在 src/mods_menu_client.cpp（仅 DLL 构建）。
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/config.h"

namespace pbzpm {

enum class ModsMenuValueType { Bool, Number, String, Null, Other };

struct ModsMenuValue {
    ModsMenuValueType type{ModsMenuValueType::Null};
    bool boolean{false};
    double number{0.0};
    std::string text;
};

using ModsMenuValues = std::map<std::string, ModsMenuValue>;

// 解析一个“扁平 JSON 对象”（对象的值只能是 bool / number / string / null）。
// 嵌套对象/数组会被跳过并标记为 Other（协议本身要求扁平；这样未来多出来的未知键也不会
// 让整份配置解析失败）。BOM、空行、CRLF 都接受。
bool parse_mods_menu_json(const std::string& text, ModsMenuValues& out, std::string* error = nullptr);

// 序列化为稳定、可读的扁平 JSON（2 空格缩进，键按字典序）。文件末尾带换行。
std::string write_mods_menu_json(const ModsMenuValues& values);

enum class ModsMenuSettingKind { Bool, Number, Choice, Key };

struct ModsMenuSettingSpec {
    const char* key;
    ModsMenuSettingKind kind;
};

// 我们暴露到 Mods Menu 149 的设置：只保留 2.2.0 的 41 项基础设置（行为/隐藏/冻结/
// 相机与移动/景深总开关/手柄）+ 末尾 27 个键盘键位的自定义 choice 行
// （值是我们自己的键名，如 F2 / MOUSE_LEFT），共 68 项设置、7 个 heading。
// 景深详细参数、画面效果/调色不进入 Mods Menu：它们在摄影模式取景时才有意义，而游戏
// 设置里的 Mods Menu 在摄影模式中打不开；请用 PBZFM（F8）或直接编辑 settings.json。
// 手柄组合键仍留在 settings.json 的 pad_* 字段。
const std::vector<ModsMenuSettingSpec>& mods_menu_settings();
const ModsMenuSettingSpec* mods_menu_find_setting(const std::string& key);

// 把扁平 JSON 值覆盖到 Config 上。越界值由 pbzfm_apply_field 统一夹紧；
// 未知键、类型不匹配、解析失败都只进 warnings，不改变 Config。
// 返回成功应用的字段数。
int mods_menu_apply_values(const ModsMenuValues& values, Config& cfg, std::vector<std::string>* warnings = nullptr);

// 从 Config 生成我们暴露字段的扁平 JSON（不保留未知键）。
std::string mods_menu_config_json(const Config& cfg);

// 在现有 JSON 文本上合并 Config（保留未知键；现有文本解析失败时退化为重新生成）。
std::string mods_menu_merge_config_json(const std::string& existing, const Config& cfg);

// 解析整份 Config JSON 并覆盖到 cfg；空文本/坏 JSON 返回 false。
bool mods_menu_apply_json_text(const std::string& text, Config& cfg, std::vector<std::string>* warnings = nullptr);

}  // namespace pbzpm
