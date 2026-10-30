#pragma once

#include <Windows.h>

#ifdef PBZPM_WITH_UE4SS

#include "core/gamepad.h"

namespace pbzpm {

class InputLogic;

// Win32 输入源：把键鼠状态桥接给 core 层的 InputLogic。
//
//  为什么不用 hook PlayerController::InputKey/InputAxis？
//    - UE4SS 的 C++ 侧没有 keyup 回调（只有 register_keydown_event），
//      “按住 Alt/WASD/鼠标左右键”没法用它表达；
//    - InputKey 的参数块布局随引擎版本变化，靠反射猜参数成本很高。
//
//  为什么不用 SetWindowsHookEx(WH_MOUSE_LL)？
//    - 低级钩子的回调只会派发到“安装钩子的那个线程”，而且要求该线程自己在跑消息循环。
//      UE4SS 的初始化线程不 pump message，所以钩子装上（返回非空）却永远收不到事件——
//      上一版就是这个问题：键盘（GetAsyncKeyState 轮询）能用，鼠标完全没反应。
//
//  现在改用 Raw Input：
//    - 自己起一个专用线程，建隐藏窗口 + 自己的消息循环，装 RIDEV_INPUTSINK，
//      收到 WM_INPUT 就有相对位移（lLastX/lLastY）、左右键、滚轮；
//    - 与 UE4SS 的线程模型完全解耦，不需要它帮忙 pump 任何消息；
//    - 键盘仍然用 GetAsyncKeyState 轮询（raw keyboard 会给重复键，边沿检测反而麻烦）。
//
// 只在“前台窗口属于本进程”时上报，避免切出去看攻略时相机乱转。
class Win32InputSource {
public:
    static Win32InputSource& instance();

    // enable_wheel_hook=false 时不装 WH_MOUSE_LL（排障用：放一个 no_wheel_hook.txt 在 mod 目录即可）
    bool install(bool enable_wheel_hook = true);
    void uninstall();
    bool installed() const { return thread_ != nullptr; }
    // Raw Input 是否注册成功（失败时 poll 会退化成 GetCursorPos 差分）
    bool raw_ready() const;

    // 每帧调用（引擎 Tick 线程）：把自上次以来累积的鼠标事件 + 键盘状态喂给 logic。
    // gamepad_enabled=false 时不轮询 XInput，并喂一份空手柄快照（组合键视为全部松开）。
    void poll(InputLogic& logic, bool gamepad_enabled);
    // 引擎侧钩子已经在提供鼠标增量时，把系统层增量静音，避免同一段移动被算两次
    void set_mouse_muted(bool muted) { mouse_muted_ = muted; }

    // 诊断用：鼠标位移事件计数（进模式时打一行日志，方便判断输入源是否活着）
    unsigned long mouse_event_count() const;
    // 低级鼠标钩子（滚轮来源）：安装线程在哪个线程，回调就派发到哪个线程
    bool ll_ready() const;
    unsigned long ll_wheel_events() const;
    // 诊断用：Raw Input 是否已被判定为“装上了但收不到事件”
    bool raw_dead() const { return raw_dead_; }
    // 诊断用：当前前台窗口是否属于本进程（为 false 时所有输入都会被忽略）
    bool foreground() const { return foreground_; }
    // 手柄诊断：是否检测到连接（已锁定 XInput 槽位）与槽位号（-1 = 未连接/未启用）
    bool pad_connected() const;
    int pad_slot() const;
    // XInput 是否加载成功（失败 = 手柄功能完全不可用，与有没有插手柄无关）
    bool pad_api_ready() const;
    // 实际加载的 XInput 模块完整路径（诊断：确认没加载到游戏目录里的代理 DLL）
    std::string pad_api_module() const;

private:
    Win32InputSource() = default;
    ~Win32InputSource() = default;
    Win32InputSource(const Win32InputSource&) = delete;
    Win32InputSource& operator=(const Win32InputSource&) = delete;

    void* thread_{nullptr};
    bool key_state_[256]{};
    bool left_down_{false};
    bool right_down_{false};
    POINT cursor_{};
    POINT last_cursor_{};
    bool have_last_cursor_{false};
    unsigned long last_mouse_events_{0};
    int quiet_frames_{0};
    bool raw_seen_{false};
    bool raw_dead_{false};
    bool mouse_muted_{false};
    int cursor_move_frames_{0};
    bool foreground_{false};

    // 手柄（XInput）：只在引擎 Tick 线程的 poll() 里访问，无需加锁
    int pad_slot_active_{-1};  // 已锁定并独占轮询的槽位；-1 = 尚未发现手柄
    int pad_scan_pos_{0};      // 未锁定时每帧扫描一个槽位，最坏 4 帧轮完
    int pad_fail_frames_{0};   // 锁定槽位连续读失败的帧数（满 3 次判定拔出）
    PadState pad_last_{};      // 最近一份有效快照（读失败的帧继续喂它，避免组合键抖动）
};

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
