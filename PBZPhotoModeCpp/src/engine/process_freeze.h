#pragma once

#include <cstdint>

namespace pbzpm {

// 进程级“画面定格”（参考 _ref/pbzpause/ANALYSIS.md，PBZ Pause 1.0.5）。
//
// 只用于 F7 这种“连渲染线程一起停”的定格：所有线程挂起后，屏幕保留最后一帧；
// 时间 API 的 IAT 钩子在定格期间返回定格瞬间值、恢复时扣掉定格时长，避免游戏
// 把整段定格时间当成一次巨大的 delta（动画/物理/计时器暴走）。
//
// 注意：摄影模式的引擎暂停（SetGamePaused）绝不能使用这里的时间钩子——渲染线程
// 和相机 Tick 仍在运行，QPC 被冻结会让移动视角规律性卡顿（1.0.64~1.0.66 实机教训）。
class ProcessFreezer {
public:
    struct TimeHookReport {
        bool installed{false};
        bool partial{false};
        int modules_patched{0};
        int slots_patched{0};
        bool qpc{false};
        bool get_tick_count{false};
        bool get_tick_count64{false};
        bool time_get_time{false};
    };

    ProcessFreezer() = default;
    ~ProcessFreezer();

    ProcessFreezer(const ProcessFreezer&) = delete;
    ProcessFreezer& operator=(const ProcessFreezer&) = delete;

    // 解析四个原始时间 API，并把游戏侧模块的 IAT 槽换成钩子（幂等）。
    // 必须在 suspend_all_threads() 之前调用。
    TimeHookReport install_time_hooks();

    // 挂起本进程除当前线程外的所有线程；返回实际挂起数（0 = 失败）。
    // 内部顺序：必要时先枚举线程 -> HeapLock(进程堆) -> SuspendThread -> HeapUnlock。
    // 注意：输入钩子线程必须先 add_skip_thread()/add_skip_module() 排除，
    // 否则低级键鼠钩子回调所在的线程被挂起，整个系统的键鼠输入会卡住（PBZFM/Hotkey 场景）。
    int suspend_all_threads();

    // 排除不参与挂起的线程/模块（每次 F7 前重新配置；模块用 HMODULE 基址）。
    void clear_skip_targets();
    void add_skip_thread(unsigned long thread_id);
    void add_skip_module(void* module_base);

    // 只挂起这些模块内的线程（白名单；为空时退化为“挂起除跳过名单外的全部线程”）。
    // F7 只用 PBZ.exe/MRTCEngine/AkSoundEngineDLL 等游戏引擎模块，避免把 PBZFM、
    // Steam/NVIDIA 覆盖层、输入钩子线程一起停掉造成全桌面键鼠卡顿。
    void clear_suspend_modules();
    void add_suspend_module(void* module_base);

    // 恢复由 suspend_all_threads() 挂起的线程。
    void resume_all_threads();

    // 时间补偿：所有目标线程挂起后 begin；恢复线程之前 end（PBZ Pause 同款语义）。
    void begin_time_freeze();
    void end_time_freeze();

    bool time_hooks_installed() const { return time_hooks_installed_; }
    int suspended_count() const { return suspended_count_; }
    int skipped_count() const { return skipped_count_; }

    // 卸载安全：恢复线程、结算时间补偿、还原所有 IAT 槽。
    void shutdown();

private:
    static constexpr int kMaxSuspendedThreads = 2048;
    static constexpr int kMaxPatchedSlots = 8192;

    bool patch_module_imports(void* module, int& slots_patched);
    void restore_time_hooks();
    bool should_skip_thread(void* thread_handle);

    static constexpr int kMaxSkipTargets = 16;
    unsigned long skip_thread_ids_[kMaxSkipTargets]{};
    int skip_thread_count_{0};
    void* skip_modules_[kMaxSkipTargets]{};
    int skip_module_count_{0};
    void* suspend_modules_[kMaxSkipTargets]{};
    int suspend_module_count_{0};

    void* suspended_[kMaxSuspendedThreads]{};
    int suspended_count_{0};
    int skipped_count_{0};
    bool time_hooks_installed_{false};
    TimeHookReport time_report_{};
    void* patched_slot_[kMaxPatchedSlots]{};
    void* patched_original_[kMaxPatchedSlots]{};
    int patched_count_{0};
};

}  // namespace pbzpm
