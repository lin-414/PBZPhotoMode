// 全量 JSON 配置（PBZPhotoModeCpp.settings.json）的读写。
//
// JSON 是唯一配置真源：Config <-> 扁平 JSON 对象。实现上复用现有 INI 解析器：
//   - 加载：扁平 JSON 按键查 section，拼成 parse_config() 能吃的 INI 文本；
//   - 保存：config_ini_text() 的规范文本逐行转成带类型推断的 JSON。
// 这样所有既有字段、范围夹紧、弃用键处理都只有一份实现。
#pragma once

#include <string>

#include "core/config.h"

namespace pbzpm {

// 解析 JSON 文本（扁平对象：bool / number / string）。
ConfigLoadResult parse_config_json(const std::string& text, const char* source = "config.json");

// 从磁盘读取；文件不存在时返回默认 Config 并置 file_missing=true。
ConfigLoadResult load_config_json_file(const std::string& path);

// 全量 JSON 文本（键按字典序，值带 bool/number/string 类型）。
std::string config_json_text(const Config& cfg);

// 原子写（先写 <path>.tmp 再替换）；失败时 error 非空则填原因。
bool save_config_json_file(const std::string& path, const Config& cfg, std::string* error = nullptr);

}  // namespace pbzpm
