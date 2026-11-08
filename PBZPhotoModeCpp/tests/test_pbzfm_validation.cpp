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

// 任意类型字段按名查找（景深分组断言用：dof_* 既有 Bool 也有 Float）
const PbzfmField* find_field(const std::string& name) {
    for (const PbzfmField& f : pbzfm_fields()) {
        if (f.key != nullptr && std::string(f.key) == name) return &f;
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
    // 景深功能复活（2.1）：dof_* 字段必须全部属于 #group_dof 分组，其它字段不得混入。
    // （dof_nlp_enable 已于 2026-11-08 并入 dof_enabled 删除，字段数 7→6。）
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
    CHECK_INT(dof_fields, 6);  // dof_enabled / focal_distance / fstop / sensor_width / focus_step / aperture_step
    CHECK_INT(misplaced, 0);
    // 对数滑条标志：距离/步进/F 值三列必须是对数（hotfix11 的真机结论），传感器宽度线性
    const PbzfmField* focal = find_field("dof_focal_distance");
    const PbzfmField* fstop = find_field("dof_fstop");
    const PbzfmField* focus_step = find_field("dof_focus_step");
    const PbzfmField* sensor = find_field("dof_sensor_width");
    CHECK(focal != nullptr && focal->logarithmic);
    CHECK(fstop != nullptr && fstop->logarithmic);
    CHECK(focus_step != nullptr && focus_step->logarithmic);
    CHECK(sensor != nullptr && !sensor->logarithmic);
    // 色差/颗粒回到 [look] 分组（值走 DofPort 通道，但菜单归属不变）
    const PbzfmField* ca = find_field("chromatic_aberration");
    CHECK(ca != nullptr && ca->group != nullptr && std::string(ca->group) == "#group_look");
}

// 每个数值滑块都能回到编译期默认值；画面效果五项还带独立开关（关掉保留滑块值）。
void test_float_reset_and_switch() {
    section("PBZFM 滑块重置与独立开关");
    Config cfg;
    int switch_fields = 0;
    int looked = 0;
    for (const PbzfmField* f : fields_of(PbzfmFieldKind::Float)) {
        const double def = pbzfm_default_value(*f);
        CHECK_NEAR(pbzfm_read_field(*f, Config{}, KeyMap{}), def, 1e-9);
        // 先改到与默认不同的边界（pitch_limit 的上界恰好等于默认 89），再重置回默认
        const double target = f->mx != def ? f->mx : f->mn;
        CHECK(pbzfm_apply_field(*f, target, cfg));
        CHECK(pbzfm_reset_field(*f, cfg));
        CHECK_NEAR(pbzfm_read_field(*f, cfg, KeyMap{}), def, 1e-9);
        if (f->get_enabled != nullptr || f->set_enabled != nullptr) {
            CHECK(f->get_enabled != nullptr && f->set_enabled != nullptr);
            CHECK(pbzfm_write_enabled(*f, false, cfg));
            CHECK(!pbzfm_read_enabled(*f, cfg));
            CHECK(pbzfm_reset_field(*f, cfg));  // 重置整行会把开关恢复默认（开）
            CHECK(pbzfm_read_enabled(*f, cfg));
            ++switch_fields;
        } else {
            CHECK(!pbzfm_write_enabled(*f, false, cfg));  // 普通滑块没有独立开关
        }
        ++looked;
    }
    CHECK(looked > 0);
    CHECK_INT(switch_fields, 5);  // 画面效果五项：暗角 / 饱和 / 对比 / 泛光 / 冷暖
}

// 调色·进阶分组（2026-11-08 批次；实机探针后由 13 项收缩为三件）：全部属于 #group_color_pro，
// 全部无独立开关（中性值即停用，沿用色差先例）；不得混入其它组，其它字段也不得误挂新组。
void test_color_pro_group_isolated() {
    section("PBZFM 调色·进阶分组隔离");
    static const char* const kColorProKeys[] = {
        "color_gain", "color_gamma", "color_offset",
    };
    int found = 0;
    int misplaced = 0;
    for (const PbzfmField& f : pbzfm_fields()) {
        if (f.key == nullptr) continue;
        const std::string key(f.key);
        bool is_pro = false;
        for (const char* k : kColorProKeys) {
            if (key == k) is_pro = true;
        }
        if (is_pro) {
            ++found;
            CHECK_STR(f.group, "#group_color_pro");
            CHECK(f.kind == PbzfmFieldKind::Float);
            CHECK(f.get_enabled == nullptr && f.set_enabled == nullptr);  // 无独立开关
        } else if (f.group != nullptr && std::string(f.group) == "#group_color_pro") {
            ++misplaced;
        }
    }
    CHECK_INT(found, 3);
    CHECK_INT(misplaced, 0);
    // 范围与中性值的对应关系：滑条默认值必须落在 [mn, mx] 内（渲染时中性值=不覆盖）
    for (const char* k : kColorProKeys) {
        const PbzfmField* f = find_field(k);
        CHECK(f != nullptr);
        if (f == nullptr) continue;
        const double def = pbzfm_default_value(*f);
        CHECK(def >= f->mn - 1e-9 && def <= f->mx + 1e-9);
    }
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
    test_color_pro_group_isolated();
    test_float_reset_and_switch();
    test_malformed_fields_rejected();
}
