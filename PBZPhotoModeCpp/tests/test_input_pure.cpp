// win_input 的纯翻译逻辑（XInput 状态 → PadState、短轴归一化、扳机阈值、死区边界）。
// 这些函数原本只在 UE4SS DLL 里编译，无法离线覆盖；抽到 core/gamepad 后与 Win32 解耦。
#include <cmath>

#include "core/gamepad.h"
#include "tests/test_harness.h"

namespace {

using namespace pbzpm;
using namespace pbzpm_test;

int held_count(const PadState& s) {
    int n = 0;
    for (int i = 0; i < kPadButtonCount; ++i) {
        if (s.held[i]) ++n;
    }
    return n;
}

void test_thumb_to_axis() {
    section("XInput 短轴归一化");
    CHECK_NEAR(thumb_to_axis(0), 0.0, 1e-12);
    CHECK_NEAR(thumb_to_axis(32767), 1.0, 1e-12);
    CHECK_NEAR(thumb_to_axis(-32768), -1.0, 1e-12);  // 负最小值会被夹紧
    CHECK_NEAR(thumb_to_axis(-16384), -16384.0 / 32767.0, 1e-9);
    CHECK_NEAR(thumb_to_axis(16384), 16384.0 / 32767.0, 1e-9);
}

void test_xinput_button_map() {
    section("XInput 按钮映射");
    struct Case {
        unsigned short bit;
        PadButton button;
    };
    const Case cases[] = {
        {kXInputPadA, PadButton::A},
        {kXInputPadB, PadButton::B},
        {kXInputPadX, PadButton::X},
        {kXInputPadY, PadButton::Y},
        {kXInputPadLeftShoulder, PadButton::LB},
        {kXInputPadRightShoulder, PadButton::RB},
        {kXInputPadLeftThumb, PadButton::LS},
        {kXInputPadRightThumb, PadButton::RS},
        {kXInputPadBack, PadButton::Back},
        {kXInputPadStart, PadButton::Start},
        {kXInputPadGuide, PadButton::Guide},
        {kXInputPadDpadUp, PadButton::Up},
        {kXInputPadDpadDown, PadButton::Down},
        {kXInputPadDpadLeft, PadButton::Left},
        {kXInputPadDpadRight, PadButton::Right},
    };
    for (const Case& c : cases) {
        PadState p = xinput_state_to_pad(c.bit, 0, 0, 0, 0, 0, 0);
        CHECK(p.held[static_cast<int>(c.button)]);
        CHECK_INT(held_count(p), 1);  // 一个位只应命中一个按钮，不得串位
    }

    // 全部按下
    const unsigned short all = kXInputPadA | kXInputPadB | kXInputPadX | kXInputPadY | kXInputPadLeftShoulder |
                               kXInputPadRightShoulder | kXInputPadLeftThumb | kXInputPadRightThumb |
                               kXInputPadBack | kXInputPadStart | kXInputPadGuide | kXInputPadDpadUp |
                               kXInputPadDpadDown | kXInputPadDpadLeft | kXInputPadDpadRight;
    PadState p = xinput_state_to_pad(all, 0, 0, 0, 0, 0, 0);
    CHECK_INT(held_count(p), 15);
    CHECK(!p.held[static_cast<int>(PadButton::LT)]);
    CHECK(!p.held[static_cast<int>(PadButton::RT)]);  // 扳机只由模拟量/阈值决定
}

void test_xinput_triggers_and_sticks() {
    section("XInput 扳机与摇杆");
    PadState p = xinput_state_to_pad(0, 0, 0, 0, 0, 0, 0);
    CHECK_NEAR(p.lt, 0.0, 1e-12);
    CHECK_NEAR(p.rt, 0.0, 1e-12);
    CHECK_NEAR(p.lx, 0.0, 1e-12);

    p = xinput_state_to_pad(0, 255, 255, 32767, -32768, 32767, -32768);
    CHECK_NEAR(p.lt, 1.0, 1e-12);
    CHECK_NEAR(p.rt, 1.0, 1e-12);
    CHECK_NEAR(p.lx, 1.0, 1e-12);
    CHECK_NEAR(p.ly, -1.0, 1e-12);
    CHECK_NEAR(p.rx, 1.0, 1e-12);
    CHECK_NEAR(p.ry, -1.0, 1e-12);

    p = xinput_state_to_pad(0, 128, 51, 0, 0, 0, 0);
    CHECK_NEAR(p.lt, 128.0 / 255.0, 1e-12);
    CHECK_NEAR(p.rt, 51.0 / 255.0, 1e-12);
}

void test_pad_held_threshold() {
    section("pad_held 扳机阈值边界");
    PadState s;
    s.lt = 0.29;
    s.rt = 0.29;
    CHECK(!pad_held(s, PadButton::LT));
    CHECK(!pad_held(s, PadButton::RT));

    s.lt = 0.30;  // kPadTriggerHeld = 0.3，等于阈值视为按下
    s.rt = 0.31;
    CHECK(pad_held(s, PadButton::LT));
    CHECK(pad_held(s, PadButton::RT));

    s.held[static_cast<int>(PadButton::A)] = true;
    CHECK(pad_held(s, PadButton::A));
    CHECK(!pad_held(s, PadButton::B));
}

void test_deadzone_boundary() {
    section("摇杆死区边界");
    double x = 0.15;  // 幅度恰好等于死区 → 归零
    double y = 0.0;
    apply_stick_deadzone(x, y, 0.15);
    CHECK_NEAR(x, 0.0, 1e-12);
    CHECK_NEAR(y, 0.0, 1e-12);

    x = 0.2;  // 刚好越过死区 → 很小的非零输出
    y = 0.0;
    apply_stick_deadzone(x, y, 0.15);
    CHECK(x > 0.0);
    CHECK(x < 0.06);
    CHECK_NEAR(y, 0.0, 1e-12);

    x = 1.2;  // 分量先夹紧到 1
    y = -1.3;
    apply_stick_deadzone(x, y, 0.0);
    CHECK_NEAR(x, 1.0, 1e-12);
    CHECK_NEAR(y, -1.0, 1e-12);
}

void test_dinput_axis_bounds() {
    section("DInput 轴端点");
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LX, 0), -1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LX, 65535), 1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LY, 0), 1.0, 1e-9);      // Y 取反：0=最低 → +1
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LY, 65535), -1.0, 1e-9);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRx, 0), 0.0, 1e-12);    // 扳机 0=松开
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRx, 65535), 1.0, 1e-12);
    CHECK_NEAR(dinput_axis_to_state(DInputAxis::LRy, 32767), 32767.0 / 65535.0, 1e-9);
}

}  // namespace

void run_input_pure_tests() {
    test_thumb_to_axis();
    test_xinput_button_map();
    test_xinput_triggers_and_sticks();
    test_pad_held_threshold();
    test_deadzone_boundary();
    test_dinput_axis_bounds();
}
