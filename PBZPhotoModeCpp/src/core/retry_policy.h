#pragma once

#include <algorithm>

namespace pbzpm {

// 失败重试的指数退避（纯函数，便于单测；不依赖 UE4SS / 引擎 / 真实时钟）。
// 语义：第 failures 次「连续失败」（failures 从 1 计）之后，等 delay_seconds(failures) 再试。
// 默认序列：0.5 / 1 / 2 / 4 / 8 / 8 / 8 ...（秒），一次成功后计数清零。
// 计时用调用方传入的单调秒（std::chrono::steady_clock），不再按调用次数累加。
struct RetryBackoff {
    double base_delay_seconds{0.5};
    double max_delay_seconds{8.0};
    // 翻倍次数的上限：只为避免循环次数随 failures 无限增长（封顶早已到达）
    int max_doublings{16};

    auto delay_seconds(int failures) const -> double {
        if (failures <= 1) return base_delay_seconds;
        const int doublings = std::min(failures - 1, max_doublings);
        double delay = base_delay_seconds;
        for (int i = 0; i < doublings; ++i) {
            delay *= 2.0;
            if (delay >= max_delay_seconds) return max_delay_seconds;
        }
        return delay > max_delay_seconds ? max_delay_seconds : delay;
    }
};

// 单调秒语义下的到点判定：now >= next 时可以重试（相等即算到点，避免卡在浮点边界）
inline auto retry_due(double now_seconds, double next_retry_seconds) -> bool {
    return now_seconds >= next_retry_seconds;
}

}  // namespace pbzpm
