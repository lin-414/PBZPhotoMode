#pragma once

#include <cstdint>

namespace pbzpm {

// ---- 世界身份令牌 ----
// UWorld / AWorldSettings 的指针就是「这一局/这一张图」的身份。任一非空指针变化即换代，
// 上一代的引擎对象缓存与「本 Mod 写入过的原始值」全部作废——旧对象随关卡卸载被 GC，
// 继续按旧裸指针回写会解引用已释放内存。
//
// 空指针（关卡卸载/切换的窗口期）不换代：否则每次读不到世界都会误作废快照，
// 把同一世界的原始值丢掉，退出时就再也还原不回去。
//
// 换代收尾（2.2.1 修复）：UWorld 变化时 WorldSettings 往往还没跟上（PersistentLevel
// 尚未赋值/尚未被读），基线里留着上一代的 settings 指针；几十到几百毫秒后第一次读
// 到新世界的 settings，指针追平。这次追平是**同一次换代的收尾**，不是第二次换代——
// 2.2.0 在这里误触发 invalidate_world_scoped_state()：实机 2026-11-09 21:09 进图
// 180ms 后进摄影模式，冻结刚登记的 14 个布料 + 176 个骨骼动画记账被整批清空，
// 退出时无可恢复，布料留在挂起态，解冻即拉条。追平用 settings_pending_ 闩锁吸收；
// 追平之后世界没变而 settings 再变（分区流送/世界设置对象重建），仍按换代保护。
class WorldIdentity {
public:
    // 返回 true 表示本次观察发生了换代（调用方可据此清缓存/打日志）。
    // catchup 出参（可空）：本次是换代收尾（settings 首次追平）时置 true，调用方可打一条
    // 区别于换代的日志，实机日志里能与「误第二次换代」直接区分开。
    bool observe(const void* world, const void* settings, bool* catchup = nullptr) {
        if (catchup != nullptr) *catchup = false;
        bool changed = false;
        bool world_changed = false;
        bool settings_changed = false;
        if (world != nullptr && last_world_ != nullptr && world != last_world_) world_changed = true;
        if (settings != nullptr && last_settings_ != nullptr && settings != last_settings_) settings_changed = true;
        if (world_changed) {
            changed = true;
            // settings 在同一次观测里也变了：新 settings 基线当帧已记下，无需等追平；
            // 只在世界换了、settings 还没读到时挂起，等收尾。
            settings_pending_ = !settings_changed;
        } else if (settings_changed) {
            if (settings_pending_) {
                settings_pending_ = false;  // 换代收尾：补齐本代基线，不算换代
                if (catchup != nullptr) *catchup = true;
            } else {
                changed = true;  // 世界没变而 settings 变了：仍按换代处理
            }
        }
        if (world != nullptr) last_world_ = world;
        if (settings != nullptr) last_settings_ = settings;
        if (changed) ++generation_;
        return changed;
    }

    void invalidate() { ++generation_; }
    std::uint64_t generation() const { return generation_; }

private:
    const void* last_world_{nullptr};
    const void* last_settings_{nullptr};
    // UWorld 刚换代、WorldSettings 还没追平时的闩锁：下一次 settings 变化视为收尾。
    bool settings_pending_{false};
    std::uint64_t generation_{0};
};

// ---- 时间字段位图 ----
// 用「本 Mod 到底写过哪个字段」记账，取代旧版靠 |dilation - original| <= 0.001*(1+|original|)
// 的数值启发式：数值接近不代表我们写过（游戏自己也可能把倍率写成接近原值）。
enum class TimeField : std::uint8_t {
    Allow = 1u << 0,     // bAllowTimeDilation
    Dilation = 1u << 1,  // TimeDilation
    Min = 1u << 2,       // MinGlobalTimeDilation
    Max = 1u << 3,       // MaxGlobalTimeDilation
};

inline auto time_field_bit(TimeField field) -> std::uint8_t {
    return static_cast<std::uint8_t>(field);
}

inline auto operator|(TimeField a, TimeField b) -> std::uint8_t {
    return time_field_bit(a) | time_field_bit(b);
}

// WorldSettings 时间字段原始值快照。generation 记录采样时的世界代次：
// 与当前代次不一致即视为无效快照，既不写回也不参与判定。
struct TimeSettingsSnapshot {
    std::uint64_t generation{0};
    std::uint8_t read_mask{0};     // 成功读到的字段
    std::uint8_t written_mask{0};  // 本 Mod 实际写入过的字段（恢复只处理这些）
    bool allow{true};
    double dilation{1.0};
    double min_dilation{0.0001};
    double max_dilation{20.0};
    double effective{1.0};

    bool belongs_to(std::uint64_t world_generation) const { return generation == world_generation; }
    void adopt(std::uint64_t world_generation) { generation = world_generation; }
    void mark_read(TimeField field) { read_mask |= time_field_bit(field); }
    void mark_written(TimeField field) { written_mask |= time_field_bit(field); }
    bool was_read(TimeField field) const { return (read_mask & time_field_bit(field)) != 0; }
    bool was_written(TimeField field) const { return (written_mask & time_field_bit(field)) != 0; }
    bool any_written() const { return written_mask != 0; }

    void reset() {
        generation = 0;
        read_mask = 0;
        written_mask = 0;
        allow = true;
        dilation = 1.0;
        min_dilation = 0.0001;
        max_dilation = 20.0;
        effective = 1.0;
    }
};

// 冻结时间前决定「要写哪几个字段」：读取原始值失败就不写（不猜默认值）。
// 核心字段 TimeDilation 读不到 → 返回 0，整个冻结放弃；
// Allow 读不到 → 不解锁（引擎会按 false 夹回 1.0，回读校验会暴露失败）；
// Min/Max 读不到 → 不越界写，交给回读校验判断是否真的生效。
inline auto plan_time_freeze_fields(const TimeSettingsSnapshot& snapshot, double target) -> std::uint8_t {
    if (!snapshot.was_read(TimeField::Dilation)) return 0;
    std::uint8_t plan = time_field_bit(TimeField::Dilation);
    if (snapshot.was_read(TimeField::Allow)) plan |= time_field_bit(TimeField::Allow);
    if (target < snapshot.min_dilation && snapshot.was_read(TimeField::Min)) plan |= time_field_bit(TimeField::Min);
    if (target > snapshot.max_dilation && snapshot.was_read(TimeField::Max)) plan |= time_field_bit(TimeField::Max);
    return plan;
}

// ---- 暂停字段快照 ----
// 三项分开记账：bShouldPerformFullTickWhenPaused 在 PlayerController 上，
// bPauseable 在 GameMode 上，bIsCameraMoveableWhenPaused 在 UWorld 上；
// 读不到原值就完全不写，恢复也只写确实写过的项。
struct PauseSnapshot {
    std::uint64_t generation{0};
    bool full_tick_read{false};
    bool full_tick_original{false};
    bool full_tick_written{false};
    bool game_mode_read{false};
    bool game_mode_original{true};
    bool game_mode_written{false};
    // UWorld.bIsCameraMoveableWhenPaused：暂停时相机仍可动的第二个标准 flag。
    // 只把它当 A/B 实验项：读不到/写失败不影响暂停本身，日志会明确标出。
    bool camera_moveable_read{false};
    bool camera_moveable_original{false};
    bool camera_moveable_written{false};

    bool belongs_to(std::uint64_t world_generation) const { return generation == world_generation; }
    void adopt(std::uint64_t world_generation) { generation = world_generation; }
    bool any_written() const { return full_tick_written || game_mode_written || camera_moveable_written; }

    void reset() {
        generation = 0;
        full_tick_read = false;
        full_tick_original = false;
        full_tick_written = false;
        game_mode_read = false;
        game_mode_original = true;
        game_mode_written = false;
        camera_moveable_read = false;
        camera_moveable_original = false;
        camera_moveable_written = false;
    }
};

}  // namespace pbzpm
