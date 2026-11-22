// Mods Menu 149（SLEEP）UE4SS Bridge 纯逻辑测试：扁平 JSON、字段映射、Config 往返。
#include <cmath>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/mods_menu.h"
#include "pbzfm_schema_map.h"
#include "tests/test_harness.h"

namespace {

using namespace pbzpm;
using namespace pbzpm_test;

const PbzfmField* find_field(const std::string& key) {
    for (const PbzfmField& field : pbzfm_fields()) {
        if (field.key != nullptr && key == field.key) return &field;
    }
    return nullptr;
}

void test_json_parser() {
    section("Mods Menu 扁平 JSON 解析");
    const std::string json = R"({
        "b": true,
        "f": false,
        "n": -2.5e3,
        "s": "a\"b\\c\nd",
        "u": "\u4e2d\u6587",
        "z": null,
        "nested": {"x": [1, 2, {"y": "z"}]},
        "arr": [true, null, "x"]
    })";
    ModsMenuValues values;
    std::string error;
    CHECK(parse_mods_menu_json(json, values, &error));
    CHECK_STR(error, "");
    CHECK_INT(static_cast<int>(values.size()), 8);
    CHECK(values["b"].type == ModsMenuValueType::Bool);
    CHECK(values["b"].boolean);
    CHECK(values["f"].type == ModsMenuValueType::Bool);
    CHECK(!values["f"].boolean);
    CHECK_NEAR(values["n"].number, -2500.0, 1e-9);
    CHECK_STR(values["s"].text, "a\"b\\c\nd");
    CHECK_STR(values["u"].text, "中文");
    CHECK(values["z"].type == ModsMenuValueType::Null);
    CHECK(values["nested"].type == ModsMenuValueType::Other);
    CHECK(values["arr"].type == ModsMenuValueType::Other);

    // BOM + CRLF
    const std::string bom = "\xEF\xBB\xBF{\r\n  \"Enabled\": true\r\n}\r\n";
    CHECK(parse_mods_menu_json(bom, values, &error));
    CHECK_INT(static_cast<int>(values.size()), 1);

    // 失败路径：顶层不是对象 / 坏转义 / 未闭合 / 非 JSON 数字
    CHECK(!parse_mods_menu_json("[]", values, &error));
    CHECK(!parse_mods_menu_json("{\"a\": \"\\q\"}", values, &error));
    CHECK(!parse_mods_menu_json("{\"a\": true", values, &error));
    CHECK(!parse_mods_menu_json("{\"a\": 1,}", values, &error));
    CHECK(!parse_mods_menu_json("{\"a\": 1.}", values, &error));  // from_chars 全消费：拒绝非 JSON 的 "1."

    // 空文件（0 字节流式拷贝按标准置 failbit，读层交上来的是 Ok 空文本）：
    // 空 / 全空白 / 只有 BOM 一律按空对象成功——Bridge 写一半崩溃、用户手清空
    // config.json / settings.json 时调用方把它当普通空值重新生成自愈，而不是
    // 当解析失败永久卡死往返（与旧 config.ini「空文件=默认值」同口径）。
    CHECK(parse_mods_menu_json("", values, &error));
    CHECK_STR(error, "");
    CHECK_INT(static_cast<int>(values.size()), 0);
    CHECK(parse_mods_menu_json("  \r\n\t ", values, &error));
    CHECK_INT(static_cast<int>(values.size()), 0);
    CHECK(parse_mods_menu_json("\xEF\xBB\xBF", values, &error));
    CHECK_INT(static_cast<int>(values.size()), 0);
}

void test_json_roundtrip() {
    section("Mods Menu JSON 往返");
    ModsMenuValues values;
    values["Enabled"] = ModsMenuValue{ModsMenuValueType::Bool, true, 0.0, ""};
    values["Speed"] = ModsMenuValue{ModsMenuValueType::Number, false, 1.25, ""};
    values["Name"] = ModsMenuValue{ModsMenuValueType::String, false, 0.0, "A\"B\\C"};
    const std::string text = write_mods_menu_json(values);
    CHECK(text.find("\"Enabled\": true") != std::string::npos);
    CHECK(text.find("\"Speed\": 1.25") != std::string::npos);
    ModsMenuValues parsed;
    CHECK(parse_mods_menu_json(text, parsed, nullptr));
    CHECK_INT(static_cast<int>(parsed.size()), 3);
    CHECK(parsed["Enabled"].boolean);
    CHECK_NEAR(parsed["Speed"].number, 1.25, 1e-12);
    CHECK_STR(parsed["Name"].text, "A\"B\\C");
}

void test_field_table_consistency() {
    section("Mods Menu 字段表与 PBZFM 字段表一致");
    CHECK(!mods_menu_settings().empty());
    for (const ModsMenuSettingSpec& spec : mods_menu_settings()) {
        const PbzfmField* field = find_field(spec.key);
        CHECK(field != nullptr);
        if (field == nullptr) continue;
        if (spec.kind == ModsMenuSettingKind::Bool) CHECK(field->kind == PbzfmFieldKind::Bool);
        if (spec.kind == ModsMenuSettingKind::Number) CHECK(field->kind == PbzfmFieldKind::Float);
        if (spec.kind == ModsMenuSettingKind::Choice) CHECK(field->kind == PbzfmFieldKind::Enum);
        if (spec.kind == ModsMenuSettingKind::Key) CHECK(field->kind == PbzfmFieldKind::Key);
    }
    CHECK_INT(static_cast<int>(mods_menu_settings().size()), 68);
    CHECK(mods_menu_find_setting("dof_enabled") != nullptr);
    CHECK(mods_menu_find_setting("dof_focal_distance") == nullptr);
    CHECK(mods_menu_find_setting("dof_fstop") == nullptr);
    CHECK(mods_menu_find_setting("vignette") == nullptr);
    CHECK(mods_menu_find_setting("vignette_enabled") == nullptr);
    CHECK(mods_menu_find_setting("freeze_diag") == nullptr);
    CHECK(mods_menu_find_setting("move_speed") != nullptr);
    CHECK(mods_menu_find_setting("key_toggle") != nullptr);
    CHECK(mods_menu_find_setting("no_such_key") == nullptr);
}

void test_key_choice_apply() {
    section("Mods Menu 键位 choice");
    Config cfg;
    ModsMenuValues values;
    values["key_toggle"] = ModsMenuValue{ModsMenuValueType::String, false, 0.0, "MOUSE_MIDDLE"};
    values["key_freeze"] = ModsMenuValue{ModsMenuValueType::String, false, 0.0, "NOT_A_KEY"};
    std::vector<std::string> warnings;
    const int applied = mods_menu_apply_values(values, cfg, &warnings);
    CHECK_INT(applied, 1);
    CHECK_STR(cfg.key_toggle, "MOUSE_MIDDLE");
    CHECK_INT(static_cast<int>(warnings.size()), 1);  // NOT_A_KEY 被拒绝

    Config original;
    original.key_toggle = "MOUSE_X1";
    const std::string text = mods_menu_config_json(original);
    ModsMenuValues parsed;
    CHECK(parse_mods_menu_json(text, parsed, nullptr));
    CHECK(parsed["key_toggle"].type == ModsMenuValueType::String);
    CHECK_STR(parsed["key_toggle"].text, "MOUSE_X1");
}

void test_apply_values() {
    section("Mods Menu 值覆盖 Config");
    Config cfg;
    ModsMenuValues values;
    values["auto_pause_time"] = ModsMenuValue{ModsMenuValueType::Bool, false, 0.0, ""};
    values["hide_player"] = ModsMenuValue{ModsMenuValueType::Bool, true, 0.0, ""};
    values["move_speed"] = ModsMenuValue{ModsMenuValueType::Number, false, 1e9, ""};
    values["freeze_mode"] = ModsMenuValue{ModsMenuValueType::Number, false, 99.0, ""};
    values["unknown_key"] = ModsMenuValue{ModsMenuValueType::Bool, true, 0.0, ""};
    values["look_sensitivity"] = ModsMenuValue{ModsMenuValueType::String, false, 0.0, "not-a-number"};

    std::vector<std::string> warnings;
    const int applied = mods_menu_apply_values(values, cfg, &warnings);
    CHECK_INT(applied, 4);                       // auto_pause_time / hide_player / move_speed / freeze_mode
    CHECK(!cfg.auto_pause_time);
    CHECK(cfg.hide_player);
    CHECK_NEAR(cfg.move_speed, 2000.0, 1e-9);    // pbzfm_apply_field 夹到上限
    CHECK(cfg.freeze_mode == FreezeMode::Pause); // 99 夹到最后一个选项（pause）
    CHECK_NEAR(cfg.look_sensitivity, 0.10, 1e-9);
    CHECK_INT(static_cast<int>(warnings.size()), 1);  // look_sensitivity 类型不合法
}

void test_config_json_roundtrip() {
    section("Mods Menu Config JSON 往返");
    Config original;
    original.auto_pause_time = false;
    original.hide_player = true;
    original.move_speed = 777.0;
    original.freeze_mode = FreezeMode::Dilation;
    original.mouse_vertical_mode = MouseVerticalMode::Look;
    original.pad_deadzone = 0.07;

    const std::string text = mods_menu_config_json(original);
    ModsMenuValues values;
    CHECK(parse_mods_menu_json(text, values, nullptr));
    CHECK(values.find("auto_pause_time") != values.end());
    CHECK(values.find("move_speed") != values.end());

    Config loaded;
    std::vector<std::string> warnings;
    CHECK(mods_menu_apply_json_text(text, loaded, &warnings));
    CHECK(warnings.empty());
    CHECK(!loaded.auto_pause_time);
    CHECK(loaded.hide_player);
    CHECK_NEAR(loaded.move_speed, 777.0, 1e-9);
    CHECK(loaded.freeze_mode == FreezeMode::Dilation);
    CHECK(loaded.mouse_vertical_mode == MouseVerticalMode::Look);
    CHECK_NEAR(loaded.pad_deadzone, 0.07, 1e-9);

    // 所有暴露字段逐一往返
    for (const ModsMenuSettingSpec& spec : mods_menu_settings()) {
        const PbzfmField* field = find_field(spec.key);
        CHECK(field != nullptr);
        if (field == nullptr) continue;
        const double before = pbzfm_read_field(*field, original, KeyMap{});
        const double after = pbzfm_read_field(*field, loaded, KeyMap{});
        CHECK_NEAR(after, before, 1e-9);
    }
}

void test_key_name_preserved_outside_vk_table() {
    section("Mods Menu 键位生成保留 vk 表外手改名");
    // NUMPAD5 不在 vk 名表里（小键盘叫 NUM_FIVE；单字符兜底只认 A-Z/0-9）：
    // MODS_MENU_149.md 指引玩家对表外键名直接手改 settings.json，生成/镜像侧必须
    // 原样保留，不能经 KeyMap{} 兜底换成默认键（否则下次菜单保存会静默洗掉手改）。
    Config cfg;
    cfg.key_toggle = "NUMPAD5";
    const std::string text = mods_menu_config_json(cfg);
    ModsMenuValues parsed;
    CHECK(parse_mods_menu_json(text, parsed, nullptr));
    CHECK_STR(parsed["key_toggle"].text, "NUMPAD5");

    // 合并侧同口径：现有文本里的值即使被重新生成也保持原名
    const std::string merged = mods_menu_merge_config_json(text, cfg);
    ModsMenuValues merged_values;
    CHECK(parse_mods_menu_json(merged, merged_values, nullptr));
    CHECK_STR(merged_values["key_toggle"].text, "NUMPAD5");

    // 应用侧对未知名拒识+告警：cfg 保留默认键，不会被兜底值覆盖（防 settings.json 被洗）
    Config target;
    std::vector<std::string> warnings;
    ModsMenuValues apply_values;
    apply_values["key_toggle"] = parsed["key_toggle"];
    CHECK_INT(mods_menu_apply_values(apply_values, target, &warnings), 0);
    CHECK_STR(target.key_toggle, "F2");
    CHECK_INT(static_cast<int>(warnings.size()), 1);

    // 可解析的名字仍归一成规范名（Bridge 下拉才能对上选项）
    Config canonical;
    canonical.key_toggle = "f2";
    const std::string canonical_text = mods_menu_config_json(canonical);
    ModsMenuValues canonical_values;
    CHECK(parse_mods_menu_json(canonical_text, canonical_values, nullptr));
    CHECK_STR(canonical_values["key_toggle"].text, "F2");
}

void test_merge_preserves_unknown() {
    section("Mods Menu JSON 合并保留未知键");
    const std::string existing = R"({
        "Unknown_Key": "keep me",
        "move_speed": 123
    })";
    Config cfg;
    cfg.move_speed = 456.0;
    const std::string merged = mods_menu_merge_config_json(existing, cfg);
    ModsMenuValues values;
    CHECK(parse_mods_menu_json(merged, values, nullptr));
    CHECK(values.find("Unknown_Key") != values.end());
    CHECK_STR(values["Unknown_Key"].text, "keep me");
    CHECK_NEAR(values["move_speed"].number, 456.0, 1e-9);
}

}  // namespace

void run_mods_menu_tests() {
    test_json_parser();
    test_json_roundtrip();
    test_field_table_consistency();
    test_apply_values();
    test_key_choice_apply();
    test_key_name_preserved_outside_vk_table();
    test_config_json_roundtrip();
    test_merge_preserves_unknown();
}
