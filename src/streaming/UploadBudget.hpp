#pragma once

#include "streaming/PingPongStaging.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <utility>

namespace betacraft::streaming {

// Injectable monotonic time source (nanoseconds since an arbitrary epoch). Production uses
// steadyClockNow(); tests inject a fake clock so budget behaviour is fully deterministic.
using MonotonicClock = std::function<std::chrono::nanoseconds()>;

[[nodiscard]] inline std::chrono::nanoseconds steadyClockNow() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

struct DrainStats {
    size_t uploaded{0};
    size_t discarded{0};
    size_t remaining{0};
    bool budgetExhausted{false};
    std::chrono::nanoseconds elapsed{0};
};

// Main-thread GPU upload pump bounded by a per-frame time budget (e.g. 4 ms).
//
// Rules, chosen so a frame never stalls yet streaming never starves:
//  * the budget is checked BEFORE each upload, against the injected monotonic clock;
//  * at least one current item is uploaded per call, even with a zero budget or an
//    upload slower than the whole budget (guaranteed forward progress);
//  * items rejected by `isCurrent` (stale version / unloaded section) are dropped
//    without being uploaded and do not count as progress;
//  * whatever is left stays queued, in order, for the next frame.
// The item is consumed before `upload` runs, so an uploader that throws loses that one
// item instead of retrying it forever; the exception propagates to the caller.
// `upload` receives a mutable reference so it may move the vertex data out.
template <typename T, typename IsCurrent, typename Upload>
[[nodiscard]] DrainStats drainWithBudget(PingPongStaging<T>& staging, std::chrono::nanoseconds budget,
                                         const MonotonicClock& clock, IsCurrent&& isCurrent, Upload&& upload) {
    DrainStats stats;
    const std::chrono::nanoseconds start = clock();

    while (T* next = staging.peek()) {
        if (!isCurrent(static_cast<const T&>(*next))) {
            staging.pop();
            ++stats.discarded;
            continue;
        }
        if (stats.uploaded > 0 && clock() - start >= budget) {
            stats.budgetExhausted = true;
            break;
        }
        T item = std::move(*next);
        staging.pop();
        ++stats.uploaded;
        upload(item);
    }

    // A single over-long upload also exhausts the budget, even if the queue is now empty.
    stats.elapsed = clock() - start;
    if (stats.uploaded > 0 && stats.elapsed >= budget) {
        stats.budgetExhausted = true;
    }
    stats.remaining = staging.frontRemaining() + staging.backSize();
    return stats;
}

} // namespace betacraft::streaming
