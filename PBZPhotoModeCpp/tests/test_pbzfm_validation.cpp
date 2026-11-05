// PBZFM 菜单 ↔ Config 的纯映射校验（pbzfm_schema_map）。
// 重点覆盖失败路径：未知/不可命名的键、越界浮点、枚举下标越界、缺少回调的畸形字段、
// 键名解析失败时回退 KeyMap —— 即菜单「提交被拒 → 显示值回滚」所依赖的判定。
#include <string>
#include <vector>

#include "core/config.h"
#include "core/input_logic.h"
#include "pbzfm_schema_map.h"
#include "tests/test_harness.h"

namespace {

using namespace pbzpm;
using namespace pbzpm_test;

std::vector<const PbzfmField*> fields_of(PbzfmFieldKind kind) {
    std::vector<const PbzfmField*> out;
    for (const PbzfmField& f : pbzfm_fields()) {
        if (f.kind == kind) out.push_back(&f);
    }
    return out;
}

const PbzfmField* find_key_field(const std::string& name) {
    for (const PbzfmField& f : pbzfm_fields()) {
        if (f.kind == PbzfmFieldKind::Key && std::string(f.key) == name) return &f;
    }
    return nullptr;
}

void test_float_clamp() {
    section("PBZFM 浮点越界夹紧");
    Config cfg;
    for (const PbzfmField* f : fields_of(PbzfmFieldKind::Float)) {
        CHECK(pbzfm_apply_field(*f, 1e9, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), f->mx, 1e-9);
        CHECK(pbzfm_apply_field(*f, -1e9, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), f->mn, 1e-9);
        // 回环：合法值写进去能原样读回
        const double mid = (f->mn + f->mx) * 0.5;
        CHECK(pbzfm_apply_field(*f, mid, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), mid, 1e-9);
    }
}

void test_enum_clamp() {
    section("PBZFM 枚举下标夹紧");
    Config cfg;
    const auto enums = fields_of(PbzfmFieldKind::Enum);
    for (const PbzfmField* f : enums) {
        CHECK(f->option_count >= 1);
        CHECK(pbzfm_apply_field(*f, -5, cfg));
        CHECK_INT(static_cast<int>(pbzfm_read_field(*f, cfg, KeyMap{})), 0);
        CHECK(pbzfm_apply_field(*f, 1e6, cfg));
        CHECK_INT(static_cast<int>(pbzfm_read_field(*f, cfg, KeyMap{})), f->option_count - 1);
        CHECK(pbzfm_apply_field(*f, 1, cfg));
        CHECK_INT(static_cast<int>(pbzfm_read_field(*f, cfg, KeyMap{})), 1);
    }
}

void test_bool_normalize() {
    section("PBZFM 布尔归一化");
    Config cfg;
    for (const PbzfmField* f : fields_of(PbzfmFieldKind::Bool)) {
        CHECK(pbzfm_apply_field(*f, 0.0, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), 0.0, 1e-12);
        CHECK(pbzfm_apply_field(*f, -1.0, cfg));  // 任何非零都算 true
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), 1.0, 1e-12);
        CHECK(pbzfm_apply_field(*f, 2.0, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), 1.0, 1e-12);
    }
}

void test_key_roundtrip_rejects() {
    section("PBZFM 键位往返校验与拒绝");
    Config cfg;
    KeyMap keys = build_keymap(cfg, nullptr);
    int checked = 0;
    for (const PbzfmField* f : fields_of(PbzfmFieldKind::Key)) {
        const std::string before = cfg.*(f->key_name);
        CHECK(!pbzfm_apply_field(*f, 0.0, cfg));       // 0 = 无按键，拒绝
        CHECK(!pbzfm_apply_field(*f, 0xFF, cfg));      // 键名表表达不了，拒绝
        CHECK_STR(cfg.*(f->key_name), before);         // 拒绝后原值不变
        CHECK(pbzfm_apply_field(*f, 0x74, cfg));       // F5：可命名且能解析回来
        CHECK_STR(cfg.*(f->key_name), "F5");
        CHECK_INT(static_cast<int>(pbzfm_read_field(*f, cfg, keys)), 0x74);
        ++checked;
    }
    CHECK(checked > 0);

    // 配置里的键名损坏时，读取必须回退到 KeyMap 默认值而不是返回 0
    const PbzfmField* toggle = find_key_field("key_toggle");
    CHECK(toggle != nullptr);
    if (toggle != nullptr) {
        cfg.*(toggle->key_name) = "NOT_A_KEY";
        CHECK_INT(static_cast<int>(pbzfm_read_field(*toggle, cfg, keys)), keys.toggle);
    }
}

void test_dof_group_isolated() {
    section("PBZFM 景深配置独立节点");
    int dof_fields = 0;
    int misplaced = 0;
    for (const PbzfmField& f : pbzfm_fields()) {
        if (f.key == nullptr) continue;
        const bool is_dof = std::string(f.key).rfind("dof_", 0) == 0;
        if (is_dof) {
            ++dof_fields;
            CHECK_STR(f.group, "#group_dof");
        } else if (f.group != nullptr && std::string(f.group) == "#group_dof") {
            ++misplaced;
        }
    }
    // 景深功能已整体移除：菜单里不应再有任何 dof_* 字段，也没有 #group_dof 分组。
    CHECK_INT(dof_fields, 0);
    CHECK_INT(misplaced, 0);
}

void test_malformed_fields_rejected() {
    section("PBZFM 畸形字段拒绝");
    Config cfg;

    PbzfmField f{};
    f.kind = PbzfmFieldKind::Bool;
    CHECK(!pbzfm_apply_field(f, 1.0, cfg));
    CHECK_NEAR(pbzfm_read_field(f, cfg, KeyMap{}), 0.0, 1e-12);

    f = PbzfmField{};
    f.kind = PbzfmFieldKind::Float;
    CHECK(!pbzfm_apply_field(f, 1.0, cfg));
    CHECK_NEAR(pbzfm_read_field(f, cfg, KeyMap{}), 0.0, 1e-12);

    f = PbzfmField{};
    f.kind = PbzfmFieldKind::Enum;
    CHECK(!pbzfm_apply_field(f, 1.0, cfg));  // option_count == 0
    CHECK_NEAR(pbzfm_read_field(f, cfg, KeyMap{}), 0.0, 1e-12);

    f = PbzfmField{};
    f.kind = PbzfmFieldKind::Key;
    CHECK(!pbzfm_apply_field(f, 0x74, cfg));  // key_name 为空
    CHECK_NEAR(pbzfm_read_field(f, cfg, KeyMap{}), 0.0, 1e-12);
}

}  // namespace

void run_pbzfm_validation_tests() {
    test_float_clamp();
    test_enum_clamp();
    test_bool_normalize();
    test_key_roundtrip_rejects();
    test_dof_group_isolated();
    test_malformed_fields_rejected();
}
