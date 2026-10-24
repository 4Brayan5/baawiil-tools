#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <chrono>

namespace monitor {
inline std::int64_t monotonicMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct CpuTimes {
    std::uint64_t idle, kernel, user;
};
inline std::optional<double> cpuUsage(CpuTimes previous, CpuTimes current) {
    if (current.idle < previous.idle || current.kernel < previous.kernel ||
        current.user < previous.user)
        return std::nullopt;
    const auto kernel = current.kernel - previous.kernel;
    const auto user = current.user - previous.user;
    const auto idle = current.idle - previous.idle;
    const long double total = static_cast<long double>(kernel) + user;
    if (total <= 0 || idle > total)
        return std::nullopt;
    return static_cast<double>(100 * (total - idle) / total);
}
inline std::optional<double> processUsage(std::uint64_t before, std::uint64_t after, double seconds,
                                          unsigned processors) {
    if (after < before || !std::isfinite(seconds) || seconds <= 0 || !processors)
        return std::nullopt;
    return std::clamp(100.0 * static_cast<double>(after - before) / 1e7 / seconds / processors, 0.0,
                      100.0);
}
template <class T> class History {
    std::deque<T> data_;
    std::size_t capacity_;

  public:
    explicit History(std::size_t capacity) : capacity_(capacity) {}
    void push(T value) {
        if (!capacity_)
            return;
        data_.push_back(value);
        while (data_.size() > capacity_)
            data_.pop_front();
    }
    const std::deque<T> &data() const {
        return data_;
    }
};
} // namespace monitor
