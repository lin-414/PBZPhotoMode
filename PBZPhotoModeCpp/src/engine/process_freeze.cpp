// 进程级画面定格实现：参考 _ref/pbzpause/ANALYSIS.md（PBZ Pause 1.0.5）。
//
// 设计边界：
//   1. 只给 F7“画面定格”用：连渲染线程一起挂起，画面才是真正的最后一帧。
//   2. 时间 IAT 补偿只在这种“全线程都停”的场景成立；摄影模式的引擎暂停
//      保留渲染/相机线程，QPC 冻结会导致移动视角卡顿（1.0.64~1.0.66 实机教训）。
//   3. Wwise Suspend/Wakeup 不在这里做：它需要和摄影模式已有的音频状态协调，
//      由 mod_app 在引擎 Tick 线程上通过 Ue4ssPort 统一管理。
#include "engine/process_freeze.h"

#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace pbzpm {
namespace {

using QueryPerformanceCounterFn = BOOL(WINAPI*)(LARGE_INTEGER*);
using GetTickCountFn = DWORD(WINAPI*)();
using GetTickCount64Fn = ULONGLONG(WINAPI*)();
using TimeGetTimeFn = DWORD(WINAPI*)();
using NtGetNextThreadFn = LONG(NTAPI*)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
using NtQueryInformationThreadFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
constexpr ULONG kThreadQuerySetWin32StartAddress = 9;

std::atomic<bool> g_time_frozen{false};
QueryPerformanceCounterFn g_orig_qpc{nullptr};
GetTickCountFn g_orig_get_tick_count{nullptr};
GetTickCount64Fn g_orig_get_tick_count64{nullptr};
TimeGetTimeFn g_orig_time_get_time{nullptr};
NtGetNextThreadFn g_nt_get_next_thread{nullptr};
NtQueryInformationThreadFn g_nt_query_information_thread{nullptr};

std::int64_t g_qpc_pause_base{0};
std::int64_t g_qpc_offset{0};
std::int64_t g_qpc_last{0};
DWORD g_tick_pause_base{0};
std::uint64_t g_tick_offset{0};
ULONGLONG g_tick64_pause_base{0};
std::uint64_t g_tick64_offset{0};
DWORD g_time_pause_base{0};
std::uint64_t g_time_offset{0};

// Hook_QueryPerformanceCounter：与 PBZ Pause 0x1000 同语义。
BOOL WINAPI hook_query_performance_counter(LARGE_INTEGER* out) {
    if (g_orig_qpc == nullptr || out == nullptr) return FALSE;
    if (!g_orig_qpc(out)) return FALSE;

    std::int64_t value;
    if (g_time_frozen.load(std::memory_order_relaxed) && g_qpc_pause_base > 0) {
        value = g_qpc_pause_base;  // 定格期间：返回定格瞬间的计数
    } else {
        value = out->QuadPart;
    }
    value -= g_qpc_offset;  // 扣掉历史累计的定格时长
    if (value < g_qpc_last) {
        value = g_qpc_last;  // 单调性保护：绝不回退
    } else if (value > g_qpc_last) {
        g_qpc_last = value;
    }
    out->QuadPart = value;
    return TRUE;
}

// Hook_GetTickCount（32 位毫秒）：与 PBZ Pause 0x1080 同语义。
DWORD WINAPI hook_get_tick_count() {
    if (g_orig_get_tick_count == nullptr) return 0;
    const std::int64_t now = static_cast<std::int64_t>(g_orig_get_tick_count());
    std::int64_t value = g_time_frozen.load(std::memory_order_relaxed) ? static_cast<std::int64_t>(g_tick_pause_base) : now;
    value -= static_cast<std::int64_t>(g_tick_offset);
    return static_cast<DWORD>(value);
}

// Hook_GetTickCount64（64 位毫秒）。
ULONGLONG WINAPI hook_get_tick_count64() {
    if (g_orig_get_tick_count64 == nullptr) return 0;
    const std::uint64_t now = static_cast<std::uint64_t>(g_orig_get_tick_count64());
    std::uint64_t value = g_time_frozen.load(std::memory_order_relaxed) ? static_cast<std::uint64_t>(g_tick64_pause_base) : now;
    value -= g_tick64_offset;
    return static_cast<ULONGLONG>(value);
}

// Hook_timeGetTime（winmm 毫秒）。
DWORD WINAPI hook_time_get_time() {
    if (g_orig_time_get_time == nullptr) return 0;
    const std::int64_t now = static_cast<std::int64_t>(g_orig_time_get_time());
    std::int64_t value = g_time_frozen.load(std::memory_order_relaxed) ? static_cast<std::int64_t>(g_time_pause_base) : now;
    value -= static_cast<std::int64_t>(g_time_offset);
    return static_cast<DWORD>(value);
}

bool is_our_hook(void* address) {
    return address == reinterpret_cast<void*>(&hook_query_performance_counter) ||
           address == reinterpret_cast<void*>(&hook_get_tick_count) ||
           address == reinterpret_cast<void*>(&hook_get_tick_count64) ||
           address == reinterpret_cast<void*>(&hook_time_get_time);
}

}  // namespace

ProcessFreezer::~ProcessFreezer() {
    shutdown();
}

ProcessFreezer::TimeHookReport ProcessFreezer::install_time_hooks() {
    if (time_hooks_installed_) return time_report_;

    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel32 != nullptr) {
        g_orig_qpc = reinterpret_cast<QueryPerformanceCounterFn>(
            ::GetProcAddress(kernel32, "QueryPerformanceCounter"));
        g_orig_get_tick_count = reinterpret_cast<GetTickCountFn>(
            ::GetProcAddress(kernel32, "GetTickCount"));
        g_orig_get_tick_count64 = reinterpret_cast<GetTickCount64Fn>(
            ::GetProcAddress(kernel32, "GetTickCount64"));
    }
    HMODULE winmm = ::GetModuleHandleW(L"winmm.dll");
    if (winmm == nullptr) winmm = ::LoadLibraryW(L"winmm.dll");
    if (winmm != nullptr) {
        g_orig_time_get_time = reinterpret_cast<TimeGetTimeFn>(
            ::GetProcAddress(winmm, "timeGetTime"));
    }

    TimeHookReport report{};
    report.qpc = g_orig_qpc != nullptr;
    report.get_tick_count = g_orig_get_tick_count != nullptr;
    report.get_tick_count64 = g_orig_get_tick_count64 != nullptr;
    report.time_get_time = g_orig_time_get_time != nullptr;
    if (!report.qpc && !report.get_tick_count && !report.get_tick_count64 && !report.time_get_time) {
        return report;
    }

    int slots = 0;
    HMODULE main_module = ::GetModuleHandleW(nullptr);
    if (patch_module_imports(main_module, slots)) ++report.modules_patched;

    // 与 PBZ Pause 的 PatchModuleImports 目标一致；不 patch 本模组自身（PBZPhotoModeCpp.dll）
    // 与 UE4SS.dll，watcher 的热键/安全阀计时必须是真实时间。
    static const wchar_t* const kModules[] = {
        L"MRTCEngine.dll",
        L"AkSoundEngineDLL.dll",
        L"mfplat.dll",
        L"mf.dll",
        L"mfcore.dll",
        L"RTWorkQ.dll",
        L"mfreadwrite.dll",
        L"evr.dll",
        L"audioclient.dll",
        L"msvcp140.dll",
        L"ucrtbase.dll",
    };
    for (const wchar_t* name : kModules) {
        HMODULE module = ::GetModuleHandleW(name);
        if (module != nullptr && patch_module_imports(module, slots)) ++report.modules_patched;
    }

    report.slots_patched = slots;
    report.installed = slots > 0;
    report.partial = report.installed &&
                     !(report.qpc && report.get_tick_count && report.get_tick_count64 && report.time_get_time);
    time_hooks_installed_ = report.installed;
    time_report_ = report;
    return report;
}

bool ProcessFreezer::patch_module_imports(void* module_pointer, int& slots_patched) {
    auto* module = static_cast<HMODULE>(module_pointer);
    if (module == nullptr) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<std::uint8_t*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;

    auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0) return false;

    int patched_here = 0;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        reinterpret_cast<std::uint8_t*>(module) + directory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor) {
        auto* original_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            reinterpret_cast<std::uint8_t*>(module) +
            (descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk : descriptor->FirstThunk));
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(
            reinterpret_cast<std::uint8_t*>(module) + descriptor->FirstThunk);

        for (; iat->u1.Function != 0; ++iat, ++original_thunk) {
            void* current = reinterpret_cast<void*>(iat->u1.Function);
            if (is_our_hook(current)) continue;

            const char* name = nullptr;
            if (descriptor->OriginalFirstThunk != 0 &&
                (original_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) == 0) {
                // AddressOfData 是 RVA，必须加上模块基址；直接当指针会访问 0x1234 之类的地址。
                auto* hint = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    reinterpret_cast<std::uint8_t*>(module) + original_thunk->u1.AddressOfData);
                name = reinterpret_cast<const char*>(hint->Name);
            }

            void* target = nullptr;
            if (g_orig_qpc != nullptr &&
                (current == reinterpret_cast<void*>(g_orig_qpc) ||
                 (name != nullptr && std::strcmp(name, "QueryPerformanceCounter") == 0))) {
                target = reinterpret_cast<void*>(&hook_query_performance_counter);
            } else if (g_orig_get_tick_count != nullptr &&
                       (current == reinterpret_cast<void*>(g_orig_get_tick_count) ||
                        (name != nullptr && std::strcmp(name, "GetTickCount") == 0))) {
                target = reinterpret_cast<void*>(&hook_get_tick_count);
            } else if (g_orig_get_tick_count64 != nullptr &&
                       (current == reinterpret_cast<void*>(g_orig_get_tick_count64) ||
                        (name != nullptr && std::strcmp(name, "GetTickCount64") == 0))) {
                target = reinterpret_cast<void*>(&hook_get_tick_count64);
            } else if (g_orig_time_get_time != nullptr &&
                       (current == reinterpret_cast<void*>(g_orig_time_get_time) ||
                        (name != nullptr && std::strcmp(name, "timeGetTime") == 0))) {
                target = reinterpret_cast<void*>(&hook_time_get_time);
            }

            if (target == nullptr || patched_count_ >= kMaxPatchedSlots) continue;
            DWORD old_protect = 0;
            if (!::VirtualProtect(iat, sizeof(void*), PAGE_READWRITE, &old_protect)) continue;
            patched_slot_[patched_count_] = reinterpret_cast<void**>(iat);
            patched_original_[patched_count_] = current;
            ++patched_count_;
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(target);
            DWORD ignored = 0;
            ::VirtualProtect(iat, sizeof(void*), old_protect, &ignored);
            ++patched_here;
        }
    }
    slots_patched += patched_here;
    return patched_here > 0;
}

void ProcessFreezer::clear_skip_targets() {
    skip_thread_count_ = 0;
    skip_module_count_ = 0;
}

void ProcessFreezer::add_skip_thread(unsigned long thread_id) {
    if (thread_id == 0 || skip_thread_count_ >= kMaxSkipTargets) return;
    skip_thread_ids_[skip_thread_count_++] = thread_id;
}

void ProcessFreezer::add_skip_module(void* module_base) {
    if (module_base == nullptr || skip_module_count_ >= kMaxSkipTargets) return;
    skip_modules_[skip_module_count_++] = module_base;
}

void ProcessFreezer::clear_suspend_modules() {
    suspend_module_count_ = 0;
}

void ProcessFreezer::add_suspend_module(void* module_base) {
    if (module_base == nullptr || suspend_module_count_ >= kMaxSkipTargets) return;
    suspend_modules_[suspend_module_count_++] = module_base;
}

// 判断一个线程是否要跳过挂起：输入钩子（WH_KEYBOARD_LL/WH_MOUSE_LL）的回调
// 派发到安装它的线程；把该线程挂起会让整个桌面的键鼠输入等待 LowLevelHooksTimeout，
// 表现为 F7 定格期间按 Ctrl+P/移动鼠标卡顿、F7 自己也进不来。
bool ProcessFreezer::should_skip_thread(void* thread_handle) {
    HANDLE thread = static_cast<HANDLE>(thread_handle);
    if (thread == nullptr) return false;
    const DWORD thread_id = ::GetThreadId(thread);
    for (int i = 0; i < skip_thread_count_; ++i) {
        if (skip_thread_ids_[i] == thread_id) return true;
    }
    if ((skip_module_count_ == 0 && suspend_module_count_ == 0) ||
        g_nt_query_information_thread == nullptr) {
        return false;
    }

    void* start_address = nullptr;
    LONG query_status = g_nt_query_information_thread(thread, kThreadQuerySetWin32StartAddress,
                                                      &start_address, sizeof(start_address), nullptr);
    if (query_status < 0 || start_address == nullptr) {
        // NtGetNextThread 返回的句柄只有 LIMITED_INFORMATION；ThreadQuerySetWin32StartAddress
        // 在部分 Windows 版本上要求 THREAD_QUERY_INFORMATION。用临时提权句柄重试一次。
        HANDLE query_handle = ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (query_handle != nullptr) {
            query_status = g_nt_query_information_thread(query_handle, kThreadQuerySetWin32StartAddress,
                                                         &start_address, sizeof(start_address), nullptr);
            ::CloseHandle(query_handle);
        }
    }
    if (query_status < 0 || start_address == nullptr) {
        return false;
    }
    HMODULE module = nullptr;
    MEMORY_BASIC_INFORMATION memory{};
    if (::VirtualQuery(start_address, &memory, sizeof(memory)) != 0 && memory.Type == MEM_IMAGE) {
        module = static_cast<HMODULE>(memory.AllocationBase);
    } else if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     reinterpret_cast<LPCWSTR>(start_address), &module)) {
        return false;
    }

    // 显式跳过（PBZFM/本模组等输入钩子模块）优先于白名单。
    for (int i = 0; i < skip_module_count_; ++i) {
        if (skip_modules_[i] == module) return true;
    }
    if (skip_module_count_ > 0) {
        // GetModuleHandleW 偶尔取不到模组基址时的兜底：按模块文件名识别。
        wchar_t module_path[MAX_PATH]{};
        if (::GetModuleFileNameW(module, module_path, MAX_PATH) > 0 &&
            (std::wcsstr(module_path, L"PBZFM") != nullptr ||
             std::wcsstr(module_path, L"PBZPhotoModeCpp") != nullptr)) {
            return true;
        }
    }

    // 白名单：只挂起游戏引擎自身的模块（PBZ.exe/MRTCEngine 等），
    // 其余第三方覆盖层/输入钩子/驱动辅助线程全部保留，避免全桌面键鼠卡顿。
    if (suspend_module_count_ > 0) {
        for (int i = 0; i < suspend_module_count_; ++i) {
            if (suspend_modules_[i] == module) return false;
        }
        return true;
    }
    return false;
}

int ProcessFreezer::suspend_all_threads() {
    if (suspended_count_ > 0) return suspended_count_;
    skipped_count_ = 0;

    const DWORD self_thread_id = ::GetCurrentThreadId();
    if (g_nt_get_next_thread == nullptr || g_nt_query_information_thread == nullptr) {
        HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr) {
            if (g_nt_get_next_thread == nullptr) {
                g_nt_get_next_thread = reinterpret_cast<NtGetNextThreadFn>(
                    ::GetProcAddress(ntdll, "NtGetNextThread"));
            }
            if (g_nt_query_information_thread == nullptr) {
                g_nt_query_information_thread = reinterpret_cast<NtQueryInformationThreadFn>(
                    ::GetProcAddress(ntdll, "NtQueryInformationThread"));
            }
        }
    }

    // Toolhelp 回退：先在 HeapLock 之外把线程 ID 枚举到栈数组；HeapLock 期间不能分配堆内存。
    std::array<DWORD, kMaxSuspendedThreads> fallback_thread_ids{};
    int fallback_count = 0;
    if (g_nt_get_next_thread == nullptr) {
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (::Thread32First(snapshot, &entry)) {
                do {
                    if (entry.th32OwnerProcessID != ::GetCurrentProcessId()) continue;
                    if (entry.th32ThreadID == self_thread_id) continue;
                    if (fallback_count < kMaxSuspendedThreads) {
                        fallback_thread_ids[static_cast<std::size_t>(fallback_count++)] = entry.th32ThreadID;
                    }
                } while (::Thread32Next(snapshot, &entry));
            }
            ::CloseHandle(snapshot);
        }
    }

    HANDLE process_heap = ::GetProcessHeap();
    const bool heap_locked = process_heap != nullptr && ::HeapLock(process_heap) != FALSE;
    int count = 0;

    if (g_nt_get_next_thread != nullptr) {
        HANDLE previous = nullptr;
        for (;;) {
            HANDLE next = nullptr;
            const LONG status = g_nt_get_next_thread(
                ::GetCurrentProcess(), previous,
                THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &next);
            if (previous != nullptr) {
                ::CloseHandle(previous);
                previous = nullptr;
            }
            if (status < 0 || next == nullptr) {
                if (next != nullptr) ::CloseHandle(next);
                break;
            }
            previous = next;
            if (::GetThreadId(next) == self_thread_id) continue;
            if (should_skip_thread(next)) {
                ++skipped_count_;
                continue;
            }

            if (count >= kMaxSuspendedThreads) continue;
            HANDLE duplicate = nullptr;
            if (::DuplicateHandle(::GetCurrentProcess(), next, ::GetCurrentProcess(), &duplicate,
                                  THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
                if (::SuspendThread(duplicate) != static_cast<DWORD>(-1)) {
                    suspended_[count++] = duplicate;
                } else {
                    ::CloseHandle(duplicate);
                }
            }
        }
    } else {
        for (int i = 0; i < fallback_count; ++i) {
            if (count >= kMaxSuspendedThreads) break;
            HANDLE thread = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                                         FALSE, fallback_thread_ids[static_cast<std::size_t>(i)]);
            if (thread == nullptr) continue;
            if (should_skip_thread(thread)) {
                ++skipped_count_;
                ::CloseHandle(thread);
                continue;
            }
            if (::SuspendThread(thread) != static_cast<DWORD>(-1)) {
                suspended_[count++] = thread;
            } else {
                ::CloseHandle(thread);
            }
        }
    }

    if (heap_locked) ::HeapUnlock(process_heap);
    suspended_count_ = count;
    return count;
}

void ProcessFreezer::resume_all_threads() {
    for (int i = 0; i < suspended_count_; ++i) {
        HANDLE thread = static_cast<HANDLE>(suspended_[i]);
        if (thread != nullptr) {
            ::ResumeThread(thread);
            ::CloseHandle(thread);
            suspended_[i] = nullptr;
        }
    }
    suspended_count_ = 0;
}

void ProcessFreezer::begin_time_freeze() {
    if (!time_hooks_installed_ || g_time_frozen.load(std::memory_order_acquire)) return;

    if (g_orig_qpc != nullptr) {
        LARGE_INTEGER now{};
        if (g_orig_qpc(&now)) {
            g_qpc_pause_base = now.QuadPart;
            g_qpc_last = g_qpc_pause_base - g_qpc_offset;
        }
    }
    if (g_orig_get_tick_count != nullptr) g_tick_pause_base = g_orig_get_tick_count();
    if (g_orig_get_tick_count64 != nullptr) g_tick64_pause_base = g_orig_get_tick_count64();
    if (g_orig_time_get_time != nullptr) g_time_pause_base = g_orig_time_get_time();
    g_time_frozen.store(true, std::memory_order_release);
}

void ProcessFreezer::end_time_freeze() {
    if (!g_time_frozen.load(std::memory_order_acquire)) return;

    if (g_orig_qpc != nullptr) {
        LARGE_INTEGER now{};
        if (g_orig_qpc(&now) && now.QuadPart > g_qpc_pause_base) {
            g_qpc_offset += now.QuadPart - g_qpc_pause_base;
        }
    }
    if (g_orig_get_tick_count != nullptr) {
        const DWORD now = g_orig_get_tick_count();
        const std::uint32_t elapsed = static_cast<std::uint32_t>(now - g_tick_pause_base);
        g_tick_offset += elapsed;
    }
    if (g_orig_get_tick_count64 != nullptr) {
        const ULONGLONG now = g_orig_get_tick_count64();
        if (now > g_tick64_pause_base) g_tick64_offset += static_cast<std::uint64_t>(now - g_tick64_pause_base);
    }
    if (g_orig_time_get_time != nullptr) {
        const DWORD now = g_orig_time_get_time();
        const std::uint32_t elapsed = static_cast<std::uint32_t>(now - g_time_pause_base);
        g_time_offset += elapsed;
    }
    g_time_frozen.store(false, std::memory_order_release);
}

void ProcessFreezer::restore_time_hooks() {
    for (int i = 0; i < patched_count_; ++i) {
        auto** slot = reinterpret_cast<void**>(patched_slot_[i]);
        if (slot == nullptr) continue;
        MEMORY_BASIC_INFORMATION info{};
        if (::VirtualQuery(slot, &info, sizeof(info)) == 0) continue;
        if (info.State != MEM_COMMIT || info.Protect == PAGE_NOACCESS || (info.Protect & PAGE_GUARD) != 0) continue;
        void* current = *slot;
        if (!is_our_hook(current)) continue;
        DWORD old_protect = 0;
        if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) continue;
        *slot = patched_original_[i];
        DWORD ignored = 0;
        ::VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
    }
    patched_count_ = 0;
    time_hooks_installed_ = false;
    time_report_ = TimeHookReport{};
}

void ProcessFreezer::shutdown() {
    end_time_freeze();
    resume_all_threads();
    restore_time_hooks();
}

}  // namespace pbzpm
