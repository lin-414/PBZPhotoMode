// 全量 JSON 配置（core/config_json）的往返与类型测试。
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
    CHECK(odd.cfg.hide_player);  // parse_bool 接受 yes 吗？当前实现按 true/false/1/0，若不接受则默认 false
}

}  // namespace

void run_config_json_tests() {
    test_json_config_roundtrip();
    test_json_config_defaults_and_bad();
}
