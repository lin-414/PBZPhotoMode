#pragma once

// 钩子记账（H-5）：UE4SS 脚本钩子与全局回调的 CallbackId 集中登记在这里，
// 卸载路径据此逐条注销。本文件不依赖 UE4SS 头文件，可直接编入核心层并被单测覆盖。
//
// 设计要点：
//   - target 是幂等键（如 "PBZSoundFXSubsystem.PBZPostEventOnActor"）。
//     同一 target 处于 Installed 时，调用方不得再次注册（install 幂等的判定依据）。
//   - Script 钩子记 pre/post 两个 id 与注册时的 UFunction*（注销必须原样传回）；
//     Global 回调只记一个 id（放在 pre_id 字段）。
//   - 安装失败也留记录（state=Failed），后续重试可再次 record_install；
//     「装上了但没记」在这里不可能发生，因为登记与注册在同一调用点完成。
//   - 注销必须回报结果：成功 → Unregistered，失败 → UnregisterFailed；
//     take_pending_unregister 会把上次注销失败的条目重新列出，允许重试。
//
// 线程：安装发生在引擎 Tick 线程，注销发生在卸载路径；所有接口内部加锁。

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace pbzpm {

enum class HookKind {
    Script,  // UObjectGlobals::RegisterHook，返回 pre/post 两个 id
    Global,  // Hook::RegisterXxxCallback，返回单个 GlobalCallbackId
};

enum class HookState {
    Installed,         // 注册成功、尚未注销
    Failed,            // 本次安装失败（可重试）
    Unregistered,      // 已成功注销
    UnregisterFailed,  // 注销失败（钩子仍在 UE4SS 里，必须如实告警）
};

struct HookRecord {
    std::string target;
    HookKind kind{HookKind::Script};
    void* function{nullptr};  // Script：注册时的 UFunction*；Global：nullptr
    long long pre_id{-1};     // Script：pre id；Global：回调 id
    long long post_id{-1};    // Script：post id；Global：-1
    HookState state{HookState::Installed};
    int attempts{0};
};

class HookRegistry {
public:
    // 幂等判定：target 已挂上时返回 true（调用方应跳过注册）。
    bool installed(const std::string& target) const;

    // 登记一次安装结果。ids 全无效（都 < 0）视为失败。
    // 返回 true 表示该 target 当前为 Installed；已 Installed 的 target 不会被覆盖。
    bool record_install(const std::string& target, HookKind kind, void* function, long long pre_id,
                        long long post_id);

    // 安装抛异常/明确失败时登记（不写 id）。
    void record_failure(const std::string& target, HookKind kind);

    // 待注销条目（Installed + 上次注销失败的）。调用方逐条真正注销后必须回报。
    std::vector<HookRecord> take_pending_unregister() const;

    void record_unregistered(const std::string& target);
    void record_unregister_failure(const std::string& target);

    int installed_count() const;
    int failed_count() const;
    int unregister_failed_count() const;
    std::vector<HookRecord> records() const;

private:
    HookRecord* find(const std::string& target);
    const HookRecord* find(const std::string& target) const;

    mutable std::mutex mutex_;
    std::vector<HookRecord> records_;
};

}  // namespace pbzpm
