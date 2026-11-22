// 全量 JSON 配置（core/config_json）的往返与类型测试。
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/config_json.h"
#include "core/mods_menu.h"
#include "tests/test_harness.h"

namespace {

using namespace pbzpm;
using namespace pbzpm_test;

void test_json_config_roundtrip() {
    section("JSON 配置往返");
    Config original;
    original.key_toggle = "F6";
    original.move_speed = 777.0;
    original.auto_pause_time = false;
    original.hide_player = true;
    original.freeze_mode = FreezeMode::Dilation;
    original.mouse_vertical_mode = MouseVerticalMode::Look;
    original.vignette = 2.5;
    original.color_gamma = 1.25;
    original.menu_language = "zh-CN";
    original.pad_deadzone = 0.07;

    const std::string text = config_json_text(original);
    ModsMenuValues values;
    CHECK(parse_mods_menu_json(text, values, nullptr));
    CHECK(values["key_toggle"].type == ModsMenuValueType::String);
    CHECK_STR(values["key_toggle"].text, "F6");
    CHECK(values["move_speed"].type == ModsMenuValueType::Number);
    CHECK(values["auto_pause_time"].type == ModsMenuValueType::Bool);
    CHECK(values["freeze_mode"].type == ModsMenuValueType::String);
    CHECK_STR(values["freeze_mode"].text, "dilation");
    CHECK_STR(values["menu_language"].text, "zh-CN");

    const ConfigLoadResult loaded = parse_config_json(text, "test.json");
    CHECK(loaded.ok);
    CHECK(!loaded.read_error);
    CHECK(loaded.warnings.empty());
    CHECK_STR(loaded.cfg.key_toggle, "F6");
    CHECK_NEAR(loaded.cfg.move_speed, 777.0, 1e-9);
    CHECK(!loaded.cfg.auto_pause_time);
    CHECK(loaded.cfg.hide_player);
    CHECK(loaded.cfg.freeze_mode == FreezeMode::Dilation);
    CHECK(loaded.cfg.mouse_vertical_mode == MouseVerticalMode::Look);
    CHECK_NEAR(loaded.cfg.vignette, 2.5, 1e-9);
    CHECK_NEAR(loaded.cfg.color_gamma, 1.25, 1e-9);
    CHECK_STR(loaded.cfg.menu_language, "zh-CN");
    CHECK_NEAR(loaded.cfg.pad_deadzone, 0.07, 1e-9);

    // 全字段往返：规范 INI 文本应与原配置完全一致（保证 JSON 没漏字段/丢类型）。
    Config all;
    all.key_toggle = "F6";
    all.key_vertical_up = "MOUSE_MIDDLE";
    all.move_speed = 777.25;
    all.freeze_mode = FreezeMode::Dilation;
    all.mouse_vertical_mode = MouseVerticalMode::Look;
    all.vignette = 2.5;
    all.chromatic_aberration = 1.25;
    all.color_gain = 1.3;
    all.menu_language = "zh#CN";
    all.log_level = LogLevel::Debug;
    all.dof_focal_distance = 12345.6789;
    all.pad_deadzone = 0.123456789;
    const ConfigLoadResult all_loaded = parse_config_json(config_json_text(all), "all.json");
    CHECK(all_loaded.ok);
    CHECK_STR(all_loaded.cfg.menu_language, "zh#CN");
    CHECK_NEAR(all_loaded.cfg.dof_focal_distance, 12345.6789, 1e-9);
    CHECK_NEAR(all_loaded.cfg.pad_deadzone, 0.123456789, 1e-9);
    CHECK_STR(config_ini_text(all_loaded.cfg), config_ini_text(all));
}

void test_json_config_defaults_and_bad() {
    section("JSON 配置默认值与坏输入");
    const ConfigLoadResult defaults = parse_config_json("{}", "test.json");
    CHECK(defaults.ok);
    CHECK_STR(defaults.cfg.key_toggle, "F2");
    CHECK_NEAR(defaults.cfg.move_speed, 400.0, 1e-9);
    CHECK(defaults.cfg.auto_pause_time);

    const ConfigLoadResult bad = parse_config_json("{\"move_speed\":", "test.json");
    CHECK(!bad.ok);
    CHECK(bad.read_error);
    CHECK(!bad.warnings.empty());

    // 未知键只告警；非法值保留默认值
    const ConfigLoadResult odd = parse_config_json(
        R"({"unknown_key": true, "move_speed": "not-a-number", "hide_player": "yes"})", "test.json");
    CHECK(odd.ok);
    CHECK(!odd.warnings.empty());
    CHECK_NEAR(odd.cfg.move_speed, 400.0, 1e-9);
    CHECK(odd.cfg.hide_player);  // parse_bool 接受 yes（true/false/1/0/yes/no/on/off）
}

void test_json_empty_file_and_cross_validation() {
    section("JSON 空文件与 fov 交叉校验");

    // 空 / 全空白文本 = 全默认（与旧 config.ini「空文件=默认值」同口径，不是读错误）
    const ConfigLoadResult empty = parse_config_json("", "empty.json");
    CHECK(empty.ok);
    CHECK(!empty.read_error);
    CHECK(empty.warnings.empty());
    CHECK_NEAR(empty.cfg.move_speed, 400.0, 1e-9);
    const ConfigLoadResult blank = parse_config_json(" \r\n\t ", "blank.json");
    CHECK(blank.ok);
    CHECK(!blank.read_error);

    // 0 字节 settings.json 端到端：读层的流式拷贝对空文件按标准置 failbit，
    // 修复后按 Ok 交空文本、按默认值解析（手清空的配置文件不再把 Mods Menu 整线禁用）
    {
        const std::string path = "pbzpm_test_empty_settings.json";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
        }
        const ConfigLoadResult loaded = load_config_json_file(path);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        CHECK(loaded.ok);
        CHECK(!loaded.read_error);
        CHECK(!loaded.file_missing);
        CHECK_NEAR(loaded.cfg.move_speed, 400.0, 1e-9);
    }

    // fov_min/fov_max 倒挂：单字段 clamp 管不到的键间约束，与 INI 路径同款交叉校验
    //（回退 15/150 + 告警）。没有这层时下游 std::clamp(v, 150, 90) debug 断言崩、release 钉死。
    const ConfigLoadResult inverted = parse_config_json(R"({"fov_min":150,"fov_max":90})", "fov.json");
    CHECK(inverted.ok);
    CHECK(!inverted.warnings.empty());
    CHECK_NEAR(inverted.cfg.fov_min, 15.0, 1e-9);
    CHECK_NEAR(inverted.cfg.fov_max, 150.0, 1e-9);
    CHECK_NEAR(inverted.cfg.fov_default, 90.0, 1e-9);  // 默认 90 在回退后的区间内，不受影响

    // fov_default 越出 [fov_min, fov_max]：夹紧
    const ConfigLoadResult pinned = parse_config_json(R"({"fov_min":30,"fov_max":60,"fov_default":100})", "fov2.json");
    CHECK(pinned.ok);
    CHECK(!pinned.warnings.empty());
    CHECK_NEAR(pinned.cfg.fov_default, 60.0, 1e-9);

    // Bool 只收 0/1：0.5 告警并保留默认（外部工具把 bool 重序列化成小数不该静默翻转语义）
    const ConfigLoadResult frac_bool = parse_config_json(R"({"hide_player":0.5})", "bool.json");
    CHECK(frac_bool.ok);
    CHECK(!frac_bool.cfg.hide_player);
    CHECK(!frac_bool.warnings.empty());
    // 0/1 数值写法仍接受（Bridge 的数字风格）
    const ConfigLoadResult numeric_bool = parse_config_json(R"({"hide_player":1})", "bool2.json");
    CHECK(numeric_bool.ok);
    CHECK(numeric_bool.cfg.hide_player);
    CHECK(numeric_bool.warnings.empty());

    // Enum 只收 0/1：1e20（修复前 static_cast<int> 是 UB）与 0.5 都告警并保留默认（Pause）
    const ConfigLoadResult huge_enum = parse_config_json(R"({"freeze_mode":1e20})", "enum.json");
    CHECK(huge_enum.ok);
    CHECK(!huge_enum.warnings.empty());
    CHECK(huge_enum.cfg.freeze_mode == FreezeMode::Pause);
    const ConfigLoadResult frac_enum = parse_config_json(R"({"freeze_mode":0.5})", "enum3.json");
    CHECK(frac_enum.ok);
    CHECK(!frac_enum.warnings.empty());
    CHECK(frac_enum.cfg.freeze_mode == FreezeMode::Pause);
    // 0/1 数值写法仍接受
    const ConfigLoadResult numeric_enum = parse_config_json(R"({"freeze_mode":1})", "enum2.json");
    CHECK(numeric_enum.ok);
    CHECK(numeric_enum.warnings.empty());
    CHECK(numeric_enum.cfg.freeze_mode == FreezeMode::Pause);
}

void test_ini_migration_roundtrip() {
    section("config.ini → settings.json 迁移端到端");
    // mod_app::reload_config 的迁移数据路径 = load_config_file(旧 ini) →
    // save_config_json_file → load_config_json_file。这里在 core 层把同一条链
    // 钉进测试：数值/枚举/键位全部保真，弃用键告警并物理消失（settings.json 不携带）。
    const std::string ini_path = "pbzpm_test_legacy.ini";
    {
        std::ofstream out(ini_path, std::ios::binary);
        out << "[keys]\n"
            << "key_toggle = F6\n"
            << "key_freeze = MOUSE_MIDDLE\n"
            << "\n[gamepad]\n"
            << "pad_look_speed = 555\n"
            << "pad_deadzone = 0.2\n"
            << "\n[camera]\n"
            << "fov_default = 110\n"
            << "move_speed = 333\n"
            << "\n[dof]\n"
            << "dof_nlp_enable = true\n"
            << "\n[ui]\n"
            << "menu_language = zh-CN\n";
    }
    const ConfigLoadResult legacy = load_config_file(ini_path);
    std::error_code ignored;
    std::filesystem::remove(ini_path, ignored);
    CHECK(legacy.ok);
    CHECK(!legacy.read_error);
    CHECK_STR(legacy.cfg.key_toggle, "F6");
    CHECK_STR(legacy.cfg.key_freeze, "MOUSE_MIDDLE");
    CHECK_NEAR(legacy.cfg.pad_look_speed, 555.0, 1e-9);
    CHECK_NEAR(legacy.cfg.pad_deadzone, 0.2, 1e-9);
    CHECK_NEAR(legacy.cfg.fov_default, 110.0, 1e-9);
    CHECK_NEAR(legacy.cfg.move_speed, 333.0, 1e-9);
    CHECK_STR(legacy.cfg.menu_language, "zh-CN");
    // 弃用键（dof_nlp_enable）：告警、值不进 cfg
    CHECK(!legacy.warnings.empty());

    const std::string json_path = "pbzpm_test_migrated.json";
    std::string save_error;
    CHECK(save_config_json_file(json_path, legacy.cfg, &save_error));
    const ConfigLoadResult loaded = load_config_json_file(json_path);
    std::filesystem::remove(json_path, ignored);
    CHECK(loaded.ok);
    CHECK(!loaded.read_error);
    CHECK(loaded.warnings.empty());
    // 全字段保真（config_ini_text 含全部 schema 字段，两侧逐字节相等 = 迁移没丢任何字段）
    CHECK_STR(config_ini_text(loaded.cfg), config_ini_text(legacy.cfg));
    CHECK_STR(loaded.cfg.key_toggle, "F6");
    CHECK_NEAR(loaded.cfg.fov_default, 110.0, 1e-9);
}

}  // namespace

void run_config_json_tests() {
    test_json_config_roundtrip();
    test_json_config_defaults_and_bad();
    test_json_empty_file_and_cross_validation();
    test_ini_migration_roundtrip();
}
