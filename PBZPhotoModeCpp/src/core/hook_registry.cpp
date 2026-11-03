#include "core/hook_registry.h"

#include <algorithm>

namespace pbzpm {

HookRecord* HookRegistry::find(const std::string& target) {
    for (HookRecord& record : records_) {
        if (record.target == target) return &record;
    }
    return nullptr;
}

const HookRecord* HookRegistry::find(const std::string& target) const {
    for (const HookRecord& record : records_) {
        if (record.target == target) return &record;
    }
    return nullptr;
}

bool HookRegistry::installed(const std::string& target) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const HookRecord* record = find(target);
    return record != nullptr && record->state == HookState::Installed;
}

bool HookRegistry::record_install(const std::string& target, HookKind kind, void* function, long long pre_id,
                                  long long post_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool ok = pre_id >= 0 || post_id >= 0;
    HookRecord* record = find(target);
    if (record == nullptr) {
        records_.push_back(HookRecord{target, kind, function, pre_id, post_id,
                                      ok ? HookState::Installed : HookState::Failed, 1});
        return ok;
    }
    // 已经挂上就别动：调用方的「先查 installed 再注册」如果漏了，这里也不会把
    // 旧 id 覆盖成新 id（否则旧钩子会永远失去注销线索）。
    if (record->state == HookState::Installed) return false;
    record->kind = kind;
    record->function = ok ? function : nullptr;
    record->pre_id = pre_id;
    record->post_id = post_id;
    record->state = ok ? HookState::Installed : HookState::Failed;
    ++record->attempts;
    return ok;
}

void HookRegistry::record_failure(const std::string& target, HookKind kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    HookRecord* record = find(target);
    if (record == nullptr) {
        records_.push_back(HookRecord{target, kind, nullptr, -1, -1, HookState::Failed, 1});
        return;
    }
    if (record->state == HookState::Installed) return;  // 不把已挂上的记录降级
    record->kind = kind;
    record->state = HookState::Failed;
    ++record->attempts;
}

std::vector<HookRecord> HookRegistry::take_pending_unregister() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HookRecord> pending;
    for (const HookRecord& record : records_) {
        if (record.state == HookState::Installed || record.state == HookState::UnregisterFailed) {
            pending.push_back(record);
        }
    }
    return pending;
}

void HookRegistry::record_unregistered(const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (HookRecord* record = find(target); record != nullptr) record->state = HookState::Unregistered;
}

void HookRegistry::record_unregister_failure(const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (HookRecord* record = find(target); record != nullptr) record->state = HookState::UnregisterFailed;
}

int HookRegistry::installed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(records_.begin(), records_.end(), [](const HookRecord& record) {
        return record.state == HookState::Installed;
    }));
}

int HookRegistry::failed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(records_.begin(), records_.end(), [](const HookRecord& record) {
        return record.state == HookState::Failed;
    }));
}

int HookRegistry::unregister_failed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(std::count_if(records_.begin(), records_.end(), [](const HookRecord& record) {
        return record.state == HookState::UnregisterFailed;
    }));
}

std::vector<HookRecord> HookRegistry::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

}  // namespace pbzpm
