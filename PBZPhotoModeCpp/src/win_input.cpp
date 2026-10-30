#ifdef PBZPM_WITH_UE4SS

#include "win_input.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <xinput.h>

// 部分 Windows SDK 的 xinput.h 略去了 GUIDE 键宏（原生值 0x0400）
#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

#include "core/input_logic.h"

namespace pbzpm {
namespace {

// 专用线程与引擎 Tick 线程之间只通过原子量交接数据，不用锁
std::atomic<long> g_dx{0};
std::atomic<long> g_dy{0};
std::atomic<long> g_wheel_delta{0};
std::atomic<bool> g_raw_ready{false};
std::atomic<unsigned long> g_mouse_events{0};
std::atomic<DWORD> g_raw_thread_id{0};
std::atomic<bool> g_ll_ready{false};
std::atomic<bool> g_wheel_hook_enabled{true};
std::atomic<unsigned long> g_ll_wheel_events{0};
HHOOK g_mouse_hook = nullptr;

constexpr wchar_t kRawWindowClass[] = L"PBZPM_RawInputSink";

// ---------------- XInput（手柄） ----------------
// 动态加载而不是链 xinput.lib：不给构建加依赖，1_4 缺失时退回 1_3 / 9_1_0 也照常工作。
using PFN_XInputGetState = DWORD(WINAPI*)(DWORD dwUserIndex, XINPUT_STATE* pState);
PFN_XInputGetState g_xinput_get_state = nullptr;
std::wstring g_xinput_module_path;

void load_xinput() {
    if (g_xinput_get_state != nullptr) return;

    // 先按绝对路径加载系统目录的 DLL：游戏目录/第三方（Steam Input、代理 DLL）常往
    // 游戏进程目录塞同名 xinput，那种代理可能"加载成功但永远报无手柄"，必须绕开。
    wchar_t sys_dir[MAX_PATH]{};
    const UINT sys_len = ::GetSystemDirectoryW(sys_dir, MAX_PATH);

    for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        const auto try_load = [&](const std::wstring& path) {
            if (g_xinput_get_state != nullptr) return;
            HMODULE mod = ::LoadLibraryW(path.c_str());
            if (mod == nullptr) return;
            auto* fn = reinterpret_cast<PFN_XInputGetState>(::GetProcAddress(mod, "XInputGetState"));
            if (fn != nullptr) {
                g_xinput_get_state = fn;
                wchar_t file[MAX_PATH]{};
                g_xinput_module_path = ::GetModuleFileNameW(mod, file, MAX_PATH) > 0 ? file : path;
                return;
            }
            ::FreeLibrary(mod);
        };
        if (sys_len > 0) try_load(std::wstring(sys_dir) + L"\\" + name);
        try_load(name);  // 系统路径失败再按搜索顺序兜底
        if (g_xinput_get_state != nullptr) return;
    }
}

double thumb_to_axis(SHORT value) {
    double v = static_cast<double>(value) / 32767.0;
    if (v > 1.0) v = 1.0;
    if (v < -1.0) v = -1.0;
    return v;
}

PadState xinput_to_pad(const XINPUT_STATE& st) {
    PadState p;
    const WORD b = st.Gamepad.wButtons;
    p.held[static_cast<int>(PadButton::A)] = (b & XINPUT_GAMEPAD_A) != 0;
    p.held[static_cast<int>(PadButton::B)] = (b & XINPUT_GAMEPAD_B) != 0;
    p.held[static_cast<int>(PadButton::X)] = (b & XINPUT_GAMEPAD_X) != 0;
    p.held[static_cast<int>(PadButton::Y)] = (b & XINPUT_GAMEPAD_Y) != 0;
    p.held[static_cast<int>(PadButton::LB)] = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    p.held[static_cast<int>(PadButton::RB)] = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    p.held[static_cast<int>(PadButton::LS)] = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    p.held[static_cast<int>(PadButton::RS)] = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    p.held[static_cast<int>(PadButton::Back)] = (b & XINPUT_GAMEPAD_BACK) != 0;
    p.held[static_cast<int>(PadButton::Start)] = (b & XINPUT_GAMEPAD_START) != 0;
    p.held[static_cast<int>(PadButton::Guide)] = (b & XINPUT_GAMEPAD_GUIDE) != 0;
    p.held[static_cast<int>(PadButton::Up)] = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
    p.held[static_cast<int>(PadButton::Down)] = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    p.held[static_cast<int>(PadButton::Left)] = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    p.held[static_cast<int>(PadButton::Right)] = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    p.lt = static_cast<double>(st.Gamepad.bLeftTrigger) / 255.0;
    p.rt = static_cast<double>(st.Gamepad.bRightTrigger) / 255.0;
    p.lx = thumb_to_axis(st.Gamepad.sThumbLX);
    p.ly = thumb_to_axis(st.Gamepad.sThumbLY);
    p.rx = thumb_to_axis(st.Gamepad.sThumbRX);
    p.ry = thumb_to_axis(st.Gamepad.sThumbRY);
    return p;
}

// 需要轮询的键：功能键 + 摄影模式里用到的所有键
constexpr int kPolledKeys[] = {
    VK_F1,  VK_F2,  VK_F3,  VK_F4,  VK_F5,  VK_F6,  VK_F7,  VK_F8,  VK_F9,  VK_F10, VK_F11, VK_F12,
    'A',    'B',    'C',    'D',    'E',    'F',    'G',    'H',    'I',    'J',    'K',    'L',
    'M',    'N',    'O',    'P',    'Q',    'R',    'S',    'T',    'U',    'V',    'W',    'X',
    'Y',    'Z',
    '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',
    VK_ADD, VK_SUBTRACT, VK_MULTIPLY, VK_DIVIDE, VK_OEM_PLUS, VK_OEM_MINUS, VK_OEM_3,
    VK_PRIOR, VK_NEXT, VK_HOME, VK_END, VK_INSERT, VK_DELETE,
    VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN,
    VK_SPACE, VK_TAB, VK_RETURN, VK_ESCAPE, VK_BACK,
    VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU,
};

// 只在游戏窗口在前台时上报输入，避免切出去看攻略时相机跟着乱动
bool foreground_is_ours() {
    const HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr) return false;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(foreground, &pid);
    return pid == ::GetCurrentProcessId();
}

void handle_raw_input(HRAWINPUT handle) {
    UINT size = 0;
    if (::GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0) return;
    if (size == 0 || size > 256) return;

    BYTE buffer[256]{};
    if (::GetRawInputData(handle, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) != size) return;

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
    if (raw->header.dwType != RIM_TYPEMOUSE) return;

    const RAWMOUSE& mouse = raw->data.mouse;
    // 绝对模式（远程桌面 / 某些平板驱动）给出的不是增量，直接丢弃，交给 GetCursorPos 兜底
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
        const long dx = static_cast<long>(mouse.lLastX);
        const long dy = static_cast<long>(mouse.lLastY);
        if (dx != 0 || dy != 0) {
            g_dx.fetch_add(dx);
            g_dy.fetch_add(dy);
            g_mouse_events.fetch_add(1);
        }
    }

    if ((mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
        g_wheel_delta.fetch_add(static_cast<long>(static_cast<short>(mouse.usButtonData)));
    }
}

// 低级鼠标钩子：Raw Input 在这作里会「注册成功但一个事件都不来」（真机日志：鼠标事件=0），
// 而 WH_MOUSE_LL 只要安装它的线程跑消息循环就能稳定收到滚轮，所以滚轮专门走这条路。
LRESULT CALLBACK ll_mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && lparam != 0) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        if (wparam == WM_MOUSEWHEEL) {
            const short delta = static_cast<short>(HIWORD(info->mouseData));
            if (delta != 0) {
                g_wheel_delta.fetch_add(delta);
                g_ll_wheel_events.fetch_add(1);
            }
        }
    }
    return ::CallNextHookEx(g_mouse_hook, code, wparam, lparam);
}

LRESULT CALLBACK sink_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT) {
        handle_raw_input(reinterpret_cast<HRAWINPUT>(lparam));
        // 交给 DefWindowProc 做 RIM 清理，避免句柄堆积
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

// 专用线程：建隐藏窗口 + 装 Raw Input + 跑自己的消息循环
void raw_thread_main() {
    g_raw_thread_id.store(::GetCurrentThreadId());

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &sink_proc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kRawWindowClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    // 普通的隐藏窗口（不显示、不进任务栏）；消息专用窗口对 Raw Input 支持不稳，这里不用
    HWND hwnd = ::CreateWindowExW(0, kRawWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    if (hwnd == nullptr) return;

    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;  // Generic Desktop
    device.usUsage = 0x02;      // Mouse
    device.dwFlags = RIDEV_INPUTSINK;  // 即使窗口不在前台也收（前台过滤在 poll 里做）
    device.hwndTarget = hwnd;
    g_raw_ready.store(::RegisterRawInputDevices(&device, 1, sizeof(device)) == TRUE);

    // 低级钩子的回调派发到安装它的线程，所以必须在这个跑消息循环的线程里装
    if (g_wheel_hook_enabled.load()) {
        g_mouse_hook = ::SetWindowsHookExW(WH_MOUSE_LL, &ll_mouse_proc, ::GetModuleHandleW(nullptr), 0);
        g_ll_ready.store(g_mouse_hook != nullptr);
    }

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (g_mouse_hook != nullptr) {
        ::UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
        g_ll_ready.store(false);
    }
}

}  // namespace

Win32InputSource& Win32InputSource::instance() {
    static Win32InputSource source;
    return source;
}

bool Win32InputSource::raw_ready() const {
    return g_raw_ready.load();
}

unsigned long Win32InputSource::mouse_event_count() const {
    return g_mouse_events.load();
}

bool Win32InputSource::pad_connected() const {
    return pad_slot_active_ >= 0;
}

int Win32InputSource::pad_slot() const {
    return pad_slot_active_;
}

bool Win32InputSource::pad_api_ready() const {
    return g_xinput_get_state != nullptr;
}

std::string Win32InputSource::pad_api_module() const {
    if (g_xinput_module_path.empty()) return "(未加载)";
    return std::string(g_xinput_module_path.begin(), g_xinput_module_path.end());
}

bool Win32InputSource::install(bool enable_wheel_hook) {
    if (thread_ != nullptr) return true;
    g_wheel_hook_enabled.store(enable_wheel_hook);

    load_xinput();

    ::SetLastError(0);
    auto* thread = new std::thread(&raw_thread_main);
    thread_ = thread;

    // 等线程把窗口和 Raw Input 装好（最多 300ms），拿不到也继续跑：
    // 那时 poll 会退化成 GetCursorPos 差分，鼠标依然能动
    for (int i = 0; i < 30; ++i) {
        if (g_raw_ready.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

void Win32InputSource::uninstall() {
    auto* thread = static_cast<std::thread*>(thread_);
    if (thread == nullptr) return;
    thread_ = nullptr;

    const DWORD thread_id = g_raw_thread_id.load();
    if (thread_id != 0) {
        ::PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    }
    if (thread->joinable()) {
        thread->join();
    }
    delete thread;
    g_raw_ready.store(false);
}

bool Win32InputSource::ll_ready() const {
    return g_ll_ready.load();
}

unsigned long Win32InputSource::ll_wheel_events() const {
    return g_ll_wheel_events.load();
}

void Win32InputSource::poll(InputLogic& logic, bool gamepad_enabled) {
    foreground_ = foreground_is_ours();
    const bool active = foreground_;

    // 1) Raw Input 的累计值
    const long dx = g_dx.exchange(0);
    const long dy = g_dy.exchange(0);
    const long wheel_delta = g_wheel_delta.exchange(0);

    const unsigned long events_now = g_mouse_events.load();
    const bool raw_this_frame = events_now != last_mouse_events_;
    if (raw_this_frame) {
        last_mouse_events_ = events_now;
        raw_seen_ = true;
        quiet_frames_ = 0;
    } else if (quiet_frames_ < 1000) {
        ++quiet_frames_;
    }

    // 2) 光标差分：既用来给 Raw Input 兜底，也用来判定 Raw Input 是不是“装上了但收不到事件”。
    //    注意游戏用鼠标捕获时会把光标锁在窗口里，差分仍然有效。
    long cdx = 0;
    long cdy = 0;
    if (::GetCursorPos(&cursor_) != 0) {
        if (!have_last_cursor_) {
            last_cursor_ = cursor_;
            have_last_cursor_ = true;
        }
        cdx = static_cast<long>(cursor_.x) - static_cast<long>(last_cursor_.x);
        cdy = static_cast<long>(cursor_.y) - static_cast<long>(last_cursor_.y);
        last_cursor_ = cursor_;
    }

    if (!raw_dead_ && !raw_seen_ && (cdx != 0 || cdy != 0)) {
        // 光标连续几十帧在动，Raw Input 却一个事件都没来过 -> 判定 raw 不可用，永久切到光标差分
        if (++cursor_move_frames_ > 30) {
            raw_dead_ = true;
        }
    } else if (raw_seen_) {
        cursor_move_frames_ = 0;
    }

    const bool use_cursor = active && (raw_dead_ || (!raw_this_frame && quiet_frames_ >= 5));

    if (active && !mouse_muted_) {
        if (use_cursor) {
            if (cdx != 0 || cdy != 0) logic.on_mouse_move(static_cast<double>(cdx), static_cast<double>(cdy));
        } else if (dx != 0 || dy != 0) {
            logic.on_mouse_move(static_cast<double>(dx), static_cast<double>(dy));
        }
    }
    if (active) {
        if (wheel_delta != 0) {
            logic.on_wheel(static_cast<double>(wheel_delta) / 120.0);
        }
    }

    // 3) 键盘 + 鼠标左右键：GetAsyncKeyState 轮询，只在状态边沿上报
    for (const int vk : kPolledKeys) {
        const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool next = active ? down : false;
        if (next == key_state_[vk]) continue;
        key_state_[vk] = next;
        logic.on_key(vk, next);
    }

    // 4) 左右键
    const bool left = active && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right = active && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (left != left_down_) {
        left_down_ = left;
        logic.on_mouse_button(MouseButton::Left, left);
    }
    if (right != right_down_) {
        right_down_ = right;
        logic.on_mouse_button(MouseButton::Right, right);
    }

    // 5) 手柄（XInput）：每帧喂一份快照。没接手柄/被禁用时喂空状态，组合键视为全部松开。
    //    没锁定槽位时每帧只扫一个槽位（最坏 4 帧轮完），避免每帧 4 次 XInput 调用的卡顿。
    if (gamepad_enabled && g_xinput_get_state != nullptr) {
        XINPUT_STATE st{};
        if (pad_slot_active_ >= 0) {
            if (g_xinput_get_state(static_cast<DWORD>(pad_slot_active_), &st) == ERROR_SUCCESS) {
                pad_last_ = xinput_to_pad(st);
                pad_fail_frames_ = 0;
            } else if (++pad_fail_frames_ >= 3) {
                // 连续三帧读不到：判定拔出，回到槽位扫描并松开所有键
                pad_slot_active_ = -1;
                pad_fail_frames_ = 0;
                pad_last_ = PadState{};
            }
        } else {
            const int slot = pad_scan_pos_;
            pad_scan_pos_ = (pad_scan_pos_ + 1) % 4;
            if (g_xinput_get_state(static_cast<DWORD>(slot), &st) == ERROR_SUCCESS) {
                pad_slot_active_ = slot;
                pad_last_ = xinput_to_pad(st);
                pad_fail_frames_ = 0;
            }
        }
        if (!active) pad_last_ = PadState{};  // 切后台时强制松开，避免组合键卡住
        logic.on_gamepad(pad_last_);
    } else {
        logic.on_gamepad(PadState{});
    }
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
