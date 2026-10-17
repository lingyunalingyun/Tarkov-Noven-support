#pragma once

#include "data/GameMode.h"

#include <algorithm>
#include <chrono>
#include <deque>

namespace noven::data {

// 使用单调时钟，休眠后只补一轮，不累积漏掉的刷新。
// Use a monotonic clock and coalesce missed intervals into one refresh round.
class EconomyRefreshSchedule final {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto Interval = std::chrono::minutes(10);

    explicit EconomyRefreshSchedule(Clock::time_point now) : next_(now + Interval) {}

    [[nodiscard]] Clock::time_point Next() const noexcept { return next_; }

    static void Enqueue(std::deque<GameMode>& queue, GameMode mode) {
        if (std::find(queue.begin(), queue.end(), mode) == queue.end()) {
            queue.push_back(mode);
        }
    }

    void EnqueueDue(std::deque<GameMode>& queue, Clock::time_point now) {
        if (now < next_) {
            return;
        }
        for (const auto mode : AllGameModes()) {
            Enqueue(queue, mode);
        }
        next_ = now + Interval;
    }

private:
    Clock::time_point next_;
};

} // namespace noven::data
