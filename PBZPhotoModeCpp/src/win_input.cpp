#ifdef PBZPM_WITH_UE4SS

#include "win_input.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>

// INITGUID 让 dinput.h 里的 IID/GUID 在本 TU 实体化（否则要链 dinput.lib，不给构建加依赖）
#include <initguid.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>

// 较新的 Windows SDK 把 DIDFT_OPTIONAL 从 dinput.h 删了，补回老 SDK 的值（0x80000000）
#ifndef DIDFT_OPTIONAL
#define DIDFT_OPTIONAL 0x80000000
#endif

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

// ---------------- DirectInput（PS 手柄等非 XInput 设备） ----------------
// DualSense/DualShock4 在 Windows 上不是 XInput 设备，XInputGetState 四个槽位永远读不到；
// 它们被系统枚举成 DirectInput 手柄，走 DInput8 轮询（GetDeviceState 主动拉状态，
// 不会像 Raw Input 事件那样被吞——本作鼠标 raw input 出过“注册成功但一个事件都不来”）。
// 动态加载 dinput8.dll 而不是链 dinput.lib：不给构建加依赖。设备对象全在专用 DInput 线程里
// 创建/获取/轮询（见 dinput_thread_main），快照经互斥量交给引擎 Tick 线程。
using PFN_DirectInput8Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPVOID);
PFN_DirectInput8Create g_dinput_create = nullptr;
std::wstring g_dinput_module_path;

void load_dinput() {
    if (g_dinput_create != nullptr) return;
    // 与 XInput 同样优先 System32 绝对路径，绕开游戏目录里的同名代理 DLL
    wchar_t sys_dir[MAX_PATH]{};
    const UINT sys_len = ::GetSystemDirectoryW(sys_dir, MAX_PATH);
    const auto try_load = [&](const std::wstring& path) {
        if (g_dinput_create != nullptr) return;
        HMODULE mod = ::LoadLibraryW(path.c_str());
        if (mod == nullptr) return;
        auto* fn = reinterpret_cast<PFN_DirectInput8Create>(::GetProcAddress(mod, "DirectInput8Create"));
        if (fn != nullptr) {
            g_dinput_create = fn;
            wchar_t file[MAX_PATH]{};
            g_dinput_module_path = ::GetModuleFileNameW(mod, file, MAX_PATH) > 0 ? file : path;
            return;
        }
        ::FreeLibrary(mod);
    };
    if (sys_len > 0) try_load(std::wstring(sys_dir) + L"\\dinput8.dll");
    try_load(L"dinput8.dll");  // System32 拿不到再按搜索顺序兜底
}

// 手绘 DIJOYSTATE2 的数据格式表：c_dfDIJoystick2 实体在 dinput.lib 里，这里照抄语义重建。
// 偏移用 offsetof 自算——SDK 的 DIJOFS_* 宏基于 FIELD_OFFSET（空指针算术），进不了 constexpr。
// 按钮实例号固定 0..13（HID 按钮用法 1..14 依序对应），保证 rgbButtons 下标与映射表一致；
// 摇杆/扳机轴按 GUID 匹配，滑条和多余 POV 标 OPTIONAL（DualSense 没有）。
constexpr DWORD kDIOfsAxis = static_cast<DWORD>(offsetof(DIJOYSTATE2, lX));  // lX..lRz 连续 6 个 LONG
constexpr DWORD kDIOfsSlider = static_cast<DWORD>(offsetof(DIJOYSTATE2, rglSlider));
constexpr DWORD kDIOfsPov = static_cast<DWORD>(offsetof(DIJOYSTATE2, rgdwPOV));
constexpr DWORD kDIOfsButtons = static_cast<DWORD>(offsetof(DIJOYSTATE2, rgbButtons));

// DIDATAFORMAT.rgodf 的声明是 LPDIOBJECTDATAFORMAT（无 const），数组不能加 const
DIOBJECTDATAFORMAT kJoy2Objects[] = {
    {&GUID_XAxis, kDIOfsAxis, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_YAxis, kDIOfsAxis + 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_ZAxis, kDIOfsAxis + 8, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_RxAxis, kDIOfsAxis + 12, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_RyAxis, kDIOfsAxis + 16, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_RzAxis, kDIOfsAxis + 20, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
    {&GUID_Slider, kDIOfsSlider, DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    {&GUID_Slider, kDIOfsSlider + 4, DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    {&GUID_POV, kDIOfsPov, DIDFT_POV | DIDFT_ANYINSTANCE, 0},
    {&GUID_POV, kDIOfsPov + 4, DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    {&GUID_POV, kDIOfsPov + 8, DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    {&GUID_POV, kDIOfsPov + 12, DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0},
    {&GUID_Button, kDIOfsButtons + 0, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(0), 0},
    {&GUID_Button, kDIOfsButtons + 1, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(1), 0},
    {&GUID_Button, kDIOfsButtons + 2, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(2), 0},
    {&GUID_Button, kDIOfsButtons + 3, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(3), 0},
    {&GUID_Button, kDIOfsButtons + 4, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(4), 0},
    {&GUID_Button, kDIOfsButtons + 5, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(5), 0},
    {&GUID_Button, kDIOfsButtons + 6, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(6), 0},
    {&GUID_Button, kDIOfsButtons + 7, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(7), 0},
    {&GUID_Button, kDIOfsButtons + 8, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(8), 0},
    {&GUID_Button, kDIOfsButtons + 9, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(9), 0},
    {&GUID_Button, kDIOfsButtons + 10, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(10), 0},
    {&GUID_Button, kDIOfsButtons + 11, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(11), 0},
    {&GUID_Button, kDIOfsButtons + 12, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(12), 0},
    {&GUID_Button, kDIOfsButtons + 13, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(13), 0},
};
const DIDATAFORMAT kJoy2Format = {
    sizeof(DIDATAFORMAT),
    sizeof(DIOBJECTDATAFORMAT),
    DIDF_ABSAXIS,
    sizeof(DIJOYSTATE2),
    static_cast<DWORD>(sizeof(kJoy2Objects) / sizeof(kJoy2Objects[0])),
    kJoy2Objects,
};

std::string narrow(const std::wstring& w) {
    // 设备名 / VID_PID 都是 ASCII，逐字符截断收窄足够
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) out.push_back(static_cast<char>(c));
    return out;
}

std::string hex8(unsigned long value) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "0x%08lX", value);
    return buf;
}

// 枚举候选：多设备时优先 Sony（VID 054C），避免 Xbox 手柄（同时挂在 XInput 上）抢先
struct DInputCandidate {
    GUID guid{};  // guidInstance
    std::wstring name;
    std::wstring vidpid;
    bool sony{false};
};

BOOL CALLBACK dinput_enum_cb(LPCDIDEVICEINSTANCEW inst, LPVOID ctx) {
    auto* c = static_cast<DInputCandidate*>(ctx);
    if (c->guid != GUID{} && c->sony) return DIENUM_CONTINUE;  // 已有 Sony 设备
    // HID 设备的 guidProduct.Data1：低字 = VID，次字 = PID
    const unsigned long vid = static_cast<unsigned long>(inst->guidProduct.Data1) & 0xFFFFu;
    const bool sony = vid == 0x054C;
    if (c->guid != GUID{} && !sony) return DIENUM_CONTINUE;  // 已有候选且这个不是更优
    wchar_t vidpid[32]{};
    std::swprintf(vidpid, 32, L"VID_%04lX&PID_%04lX", vid,
                  (static_cast<unsigned long>(inst->guidProduct.Data1) >> 16) & 0xFFFFu);
    c->guid = inst->guidInstance;
    c->name = inst->tszProductName != nullptr ? inst->tszProductName : L"";
    c->vidpid = vidpid;
    c->sony = sony;
    return DIENUM_CONTINUE;
}

PadState dinput_to_pad(const DIJOYSTATE2& js) {
    PadState p;
    for (int i = 0; i < kDInputButtonCount; ++i) {
        if ((js.rgbButtons[i] & 0x80) == 0) continue;
        PadButton b{};
        if (!dinput_button_to_pad(i, b)) continue;  // 触摸板等无对应的键
        if (b == PadButton::LT) {
            p.lt = 1.0;  // L2 数字镜像：pad_held(LT) 按 lt 阈值判
        } else if (b == PadButton::RT) {
            p.rt = 1.0;
        } else {
            p.held[static_cast<int>(b)] = true;
        }
    }
    bool pov[4]{};
    dinput_pov_to_buttons(js.rgdwPOV[0], pov);
    p.held[static_cast<int>(PadButton::Up)] = pov[0];
    p.held[static_cast<int>(PadButton::Right)] = pov[1];
    p.held[static_cast<int>(PadButton::Down)] = pov[2];
    p.held[static_cast<int>(PadButton::Left)] = pov[3];
    p.lx = dinput_axis_to_state(DInputAxis::LX, js.lX);
    p.ly = dinput_axis_to_state(DInputAxis::LY, js.lY);
    p.rx = dinput_axis_to_state(DInputAxis::LZ, js.lZ);
    p.ry = dinput_axis_to_state(DInputAxis::LRz, js.lRz);
    // L2/R2 的模拟量与数字镜像取较大者
    const double lt = dinput_axis_to_state(DInputAxis::LRx, js.lRx);
    const double rt = dinput_axis_to_state(DInputAxis::LRy, js.lRy);
    if (lt > p.lt) p.lt = lt;
    if (rt > p.rt) p.rt = rt;
    return p;
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

bool Win32InputSource::pad_connected() const {
    return pad_slot_active_ >= 0 || dinput_present_;
}

bool Win32InputSource::pad_dinput_ready() const {
    return g_dinput_create != nullptr;
}

std::string Win32InputSource::pad_backend() const {
    if (pad_slot_active_ >= 0) return "XInput 槽位 " + std::to_string(pad_slot_active_);
    if (dinput_present_.load(std::memory_order_relaxed)) {
        std::wstring name;
        std::wstring vidpid;
        {
            std::lock_guard<std::mutex> lock(dinput_mutex_);
            name = dinput_name_;
            vidpid = dinput_vidpid_;
        }
        return "DInput: " + narrow(name) + "（" + narrow(vidpid) + "）";
    }
    return "无";
}

bool Win32InputSource::take_pad_dinput_diag(std::string& out) {
    std::lock_guard<std::mutex> lock(dinput_mutex_);
    if (dinput_diag_.empty()) return false;
    out = std::move(dinput_diag_);
    dinput_diag_.clear();
    return true;
}

// 诊断串只在 DInput 线程里写、游戏线程里取，跨线程走互斥量。
// 相同内容的失败诊断只发布一次：探测每 2 秒重试，重复发布会让游戏线程反复写
// UE4SS.log（1.0.38 实机：Xbox 手柄在场时 SetDataFormat 每两秒败一次，日志刷了 68 行）
void Win32InputSource::dinput_set_diag(std::string text) {
    if (text == dinput_last_diag_) return;
    dinput_last_diag_ = text;
    std::lock_guard<std::mutex> lock(dinput_mutex_);
    dinput_diag_ = std::move(text);
}

void Win32InputSource::dinput_append_diag(std::string text) {
    std::lock_guard<std::mutex> lock(dinput_mutex_);
    if (!dinput_diag_.empty()) dinput_diag_ += "；";
    dinput_diag_ += std::move(text);
}

bool Win32InputSource::ensure_dinput_runtime() {
    if (dinput_api_ != nullptr) return true;
    if (dinput_api_failed_) return false;
    // COM 单元由 dinput_thread_main 在线程入口初始化；本函数只创建 IDirectInput8 与窗口
    auto* api = static_cast<IDirectInput8W*>(dinput_api_);
    const HRESULT hr = g_dinput_create(::GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                       reinterpret_cast<void**>(&api), nullptr);
    if (FAILED(hr) || api == nullptr) {
        dinput_api_failed_ = true;
        dinput_set_diag("DInput 初始化失败：DirectInput8Create hr=" + hex8(static_cast<unsigned long>(hr)));
        return false;
    }
    dinput_api_ = api;

    // 合作级窗口：DInput 线程自建隐藏窗口（设备对象与窗口必须同线程；raw sink 窗口
    // 在另一个线程，不跨线程用）
    constexpr wchar_t kDInputWindowClass[] = L"PBZPM_DInputSink";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &::DefWindowProcW;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kDInputWindowClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        dinput_api_failed_ = true;
        dinput_set_diag("DInput 初始化失败：窗口类注册失败");
        return false;
    }
    HWND hwnd = ::CreateWindowExW(0, kDInputWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        dinput_api_failed_ = true;
        dinput_set_diag("DInput 初始化失败：合作级窗口创建失败");
        return false;
    }
    dinput_hwnd_ = hwnd;
    return true;
}

bool Win32InputSource::dinput_probe() {
    const unsigned long now = ::GetTickCount();
    if (dinput_last_probe_ != 0 && now - dinput_last_probe_ < 2000) return false;  // 限流：2 秒试一次
    dinput_last_probe_ = now;

    auto* api = static_cast<IDirectInput8W*>(dinput_api_);
    DInputCandidate candidate;
    api->EnumDevices(DI8DEVCLASS_GAMECTRL, &dinput_enum_cb, &candidate, DIEDFL_ATTACHEDONLY);
    if (candidate.guid == GUID{}) return false;  // 没有 DInput 手柄在场（或只有被 XInput 接管的）

    const auto fail = [&](const char* reason, IDirectInputDevice8W* dev, HRESULT hr) {
        if (dev != nullptr) dev->Release();
        dinput_set_diag(std::string("DInput 设备初始化失败：") + reason + " hr=" + hex8(static_cast<unsigned long>(hr)));
        return false;
    };

    IDirectInputDevice8W* dev = nullptr;
    HRESULT hr = api->CreateDevice(candidate.guid, &dev, nullptr);
    if (FAILED(hr) || dev == nullptr) return fail("CreateDevice", dev, hr);
    hr = dev->SetDataFormat(&kJoy2Format);
    if (FAILED(hr)) return fail("SetDataFormat", dev, hr);
    hr = dev->SetCooperativeLevel(static_cast<HWND>(dinput_hwnd_), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) return fail("SetCooperativeLevel", dev, hr);

    // 量程统一拉到 0..65535：这是 dinput_axis_to_state 的归一化基准（摇杆中心 32767.5、扳机满量 65535）。
    // 逐轴设置（DIPH_DEVICE 会波及按钮/POV）；个别轴被驱动拒绝时记诊断但继续。
    static const DWORD axis_offsets[] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ};
    bool range_ok = true;
    for (const DWORD offset : axis_offsets) {
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(range);
        range.diph.dwHeaderSize = sizeof(range.diph);
        range.diph.dwObj = offset;
        range.diph.dwHow = DIPH_BYOFFSET;
        range.lMin = 0;
        range.lMax = 65535;
        if (FAILED(dev->SetProperty(DIPROP_RANGE, &range.diph))) range_ok = false;
    }
    hr = dev->Acquire();
    if (FAILED(hr)) return fail("Acquire", dev, hr);

    dinput_dev_ = dev;
    {
        std::lock_guard<std::mutex> lock(dinput_mutex_);
        dinput_name_ = candidate.name;
        dinput_vidpid_ = candidate.vidpid;
    }
    dinput_fail_frames_ = 0;
    dinput_dumped_ = false;
    if (!range_ok) {
        dinput_set_diag("DInput 设备已接入但量程设置部分失败：" + narrow(candidate.name) + "（" +
                        narrow(candidate.vidpid) + "），摇杆幅度可能异常");
    }
    return true;
}

void Win32InputSource::dinput_release_device() {
    auto* dev = static_cast<IDirectInputDevice8W*>(dinput_dev_);
    if (dev != nullptr) {
        dev->Unacquire();
        dev->Release();
    }
    dinput_dev_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(dinput_mutex_);
        dinput_name_.clear();
        dinput_vidpid_.clear();
    }
    dinput_present_.store(false, std::memory_order_relaxed);
    dinput_fail_frames_ = 0;
}

// 睡眠顺带 pump 本线程的消息：DInput 的背景协作级别不依赖窗口消息，pump 只是保险
auto dinput_thread_wait(unsigned long milliseconds) -> void {
    const unsigned long long deadline = ::GetTickCount64() + milliseconds;
    MSG msg{};
    for (;;) {
        const unsigned long long now = ::GetTickCount64();
        if (now >= deadline) return;
        const DWORD wait = static_cast<DWORD>(deadline - now);
        const DWORD result = ::MsgWaitForMultipleObjects(0, nullptr, FALSE, wait, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) {
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        } else if (result == WAIT_FAILED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
            return;
        }
    }
}

// DInput 专用线程入口：COM 单元/IDirectInput8/合作级窗口/设备对象全在本线程创建与使用
//（DInput 的 COM 对象不能跨线程调用），重量级的 EnumDevices 探测从此不再占用游戏主线程。
void Win32InputSource::dinput_thread_main() {
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    dinput_com_ = true;

    while (!dinput_quit_.load(std::memory_order_relaxed)) {
        if (!dinput_active_.load(std::memory_order_relaxed)) {
            // 手柄功能关着或 XInput 已锁定槽位（Xbox 手柄不该被 DInput 再数一遍）：
            // 释放设备并清快照，条件重新满足后再探测
            dinput_release_device();
            {
                std::lock_guard<std::mutex> lock(dinput_mutex_);
                dinput_snapshot_ = PadState{};
                dinput_present_.store(false, std::memory_order_relaxed);
            }
            dinput_thread_wait(100);
            continue;
        }
        dinput_thread_tick();
        dinput_thread_wait(10);
    }

    dinput_release_device();
    if (dinput_hwnd_ != nullptr) ::DestroyWindow(static_cast<HWND>(dinput_hwnd_));
    dinput_hwnd_ = nullptr;
    if (dinput_com_) ::CoUninitialize();
    dinput_com_ = false;
}

// 一拍：设备在手就拉一次状态（Poll+GetDeviceState，微秒级）；没设备就按 2 秒限流做一次探测
void Win32InputSource::dinput_thread_tick() {
    auto* dev = static_cast<IDirectInputDevice8W*>(dinput_dev_);
    if (dev == nullptr) {
        if (!ensure_dinput_runtime()) return;
        dinput_probe();  // 枚举到候选设备会一路 CreateDevice/SetProperty/Acquire
        return;
    }

    dev->Poll();  // HID 设备无需轮询（返回 DI_NOEFFECT），失败由 GetDeviceState 兜底
    DIJOYSTATE2 js{};
    const HRESULT hr = dev->GetDeviceState(sizeof(js), &js);
    if (FAILED(hr)) {
        // 设备丢失/被抢：先补一次 Acquire；连续 3 拍仍读不到就释放重枚举（对齐 XInput 拔出判定）
        if (++dinput_fail_frames_ >= 3) {
            dinput_release_device();
            std::lock_guard<std::mutex> lock(dinput_mutex_);
            dinput_snapshot_ = PadState{};
            dinput_present_.store(false, std::memory_order_relaxed);
        } else {
            dev->Acquire();
        }
        return;
    }
    dinput_fail_frames_ = 0;
    const PadState pad = dinput_to_pad(js);

    std::lock_guard<std::mutex> lock(dinput_mutex_);
    dinput_snapshot_ = pad;
    dinput_present_.store(true, std::memory_order_relaxed);

    if (!dinput_dumped_) {
        // 一次性诊断：映射与真机有出入时，这行能直接看出按钮位/轴/POV 的原始值
        dinput_dumped_ = true;
        unsigned long buttons = 0;
        for (int i = 0; i < kDInputButtonCount; ++i) {
            if ((js.rgbButtons[i] & 0x80) != 0) buttons |= (1UL << i);
        }
        char buf[256]{};
        std::snprintf(buf, sizeof(buf),
                      "DInput 手柄已接入：%ls（%ls）首读：按钮=0x%04lX 摇杆L=(%ld,%ld) R=(%ld,%ld) 扳机=(%ld,%ld) POV=%04lX",
                      dinput_name_.c_str(), dinput_vidpid_.c_str(), buttons, js.lX, js.lY, js.lZ, js.lRz,
                      js.lRx, js.lRy, static_cast<unsigned long>(js.rgdwPOV[0] & 0xFFFFu));
        if (!dinput_diag_.empty()) dinput_diag_ += "；";
        dinput_diag_ += buf;
    }
}

bool Win32InputSource::install(bool enable_wheel_hook) {
    if (thread_ != nullptr) return true;
    g_wheel_hook_enabled.store(enable_wheel_hook);

    load_xinput();
    load_dinput();  // PS 手柄等非 XInput 设备的后端（启动日志会报告可用性）

    ::SetLastError(0);
    auto* thread = new std::thread(&raw_thread_main);
    thread_ = thread;

    // 等线程把窗口和 Raw Input 装好（最多 300ms），拿不到也继续跑：
    // 那时 poll 会退化成 GetCursorPos 差分，鼠标依然能动
    for (int i = 0; i < 30; ++i) {
        if (g_raw_ready.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // DInput 探测/轮询独立成线程：EnumDevices 一次几十毫秒起步，留在游戏线程就是
    // “每 2 秒卡一次”的周期性卡顿（1.0.37 用户反馈的根因）。dinput8 没加载成功就不起线程。
    if (g_dinput_create != nullptr) {
        auto* dinput_thread = new std::thread(&Win32InputSource::dinput_thread_main, this);
        dinput_thread_ = dinput_thread;
    }
    return true;
}

void Win32InputSource::uninstall() {
    // DInput 线程先收：置退出标志后线程会在一拍内自行释放设备并结束
    dinput_quit_.store(true);
    dinput_active_.store(false);
    auto* dinput_thread = static_cast<std::thread*>(dinput_thread_);
    if (dinput_thread != nullptr) {
        if (dinput_thread->joinable()) {
            dinput_thread->join();
        }
        delete dinput_thread;
        dinput_thread_ = nullptr;
    }

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

    // 5) 手柄：每帧喂一份快照。XInput 优先——Xbox 手柄在 XInput 和 DInput 里各出现一次，
    //    锁定槽位即视为 Xbox，忽略 DInput；DualSense/DS4 不是 XInput 设备（槽位永远扫不到），
    //    由 DInput 后端接手。没接手柄/被禁用时喂空状态，组合键视为全部松开。
    //    没锁定槽位时每帧只扫一个槽位（最坏 4 帧轮完），避免每帧 4 次 XInput 调用的卡顿。
    if (gamepad_enabled) {
        if (g_xinput_get_state != nullptr) {
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
        }
        // XInput 没锁到槽位时才用 DInput 快照，避免 Xbox 手柄被两套后端同时计数。
        // DInput 的探测/轮询在专用线程里做（EnumDevices 要几十毫秒，不能占游戏线程），
        // 这里只取最近一份快照
        if (pad_slot_active_ < 0 && g_dinput_create != nullptr) {
            std::lock_guard<std::mutex> lock(dinput_mutex_);
            pad_last_ = dinput_present_.load(std::memory_order_relaxed) ? dinput_snapshot_ : PadState{};
        }
    } else {
        pad_last_ = PadState{};
    }
    // DInput 线程的活跃条件：手柄开着且 XInput 没锁定槽位（本帧扫描后的状态）
    dinput_active_.store(gamepad_enabled && pad_slot_active_ < 0, std::memory_order_relaxed);
    if (!active) pad_last_ = PadState{};  // 切后台时强制松开，避免组合键卡住
    logic.on_gamepad(pad_last_);
}

}  // namespace pbzpm

#endif  // PBZPM_WITH_UE4SS
