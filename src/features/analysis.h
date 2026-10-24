#pragma once
#include <optional>
#include <cstdint>
#include <deque>
#include <cmath>

namespace features {
class ThresholdRule {
    double threshold_, hysteresis_;
    std::int64_t duration_, cooldown_;
    std::optional<std::int64_t> since_, last_;
    std::optional<std::int64_t> lastSource_;
    bool active_ = false;

  public:
    ThresholdRule(double threshold, double hysteresis, std::int64_t durationMs,
                  std::int64_t cooldownMs)
        : threshold_(threshold), hysteresis_(hysteresis), duration_(durationMs),
          cooldown_(cooldownMs) {}
    bool observe(std::optional<double> value, std::int64_t now) {
        if (!value || !std::isfinite(*value)) {
            since_.reset();
            return false;
        }
        if (active_) {
            if (*value < threshold_ - hysteresis_) {
                active_ = false;
                since_.reset();
            }
            return false;
        }
        if (*value < threshold_) {
            since_.reset();
            return false;
        }
        if (!since_)
            since_ = now;
        if (now - *since_ >= duration_ && (!last_ || now - *last_ >= cooldown_)) {
            last_ = now;
            active_ = true;
            return true;
        }
        return false;
    }
    bool observeSample(std::optional<double> value, std::int64_t sourceTime,
                       std::int64_t observedTime, std::int64_t maxGap) {
        if (sourceTime < 0 || observedTime < sourceTime || observedTime - sourceTime > maxGap) {
            observe(std::nullopt, observedTime);
            return false;
        }
        if (lastSource_ && sourceTime <= *lastSource_)
            return false;
        if (lastSource_ && sourceTime - *lastSource_ > maxGap)
            since_.reset();
        lastSource_ = sourceTime;
        return observe(value, sourceTime);
    }
};
struct Observation {
    std::int64_t time;
    std::optional<double> cpu, ram;
};
class DiagnosticWindow {
    std::deque<Observation> rows_;

  public:
    void append(Observation row) {
        rows_.push_back(row);
        while (!rows_.empty() && row.time - rows_.front().time > 120000)
            rows_.pop_front();
        while (rows_.size() > 2000)
            rows_.pop_front();
    }
    struct Summary {
        double cpu = 0, ram = 0;
        std::size_t cpuCount = 0, ramCount = 0;
        std::int64_t duration = 0;
    };
    Summary summary() const {
        Summary s;
        for (const auto &r : rows_) {
            if (r.cpu) {
                s.cpu += *r.cpu;
                ++s.cpuCount;
            }
            if (r.ram) {
                s.ram += *r.ram;
                ++s.ramCount;
            }
        }
        if (s.cpuCount)
            s.cpu /= s.cpuCount;
        if (s.ramCount)
            s.ram /= s.ramCount;
        if (rows_.size() > 1)
            s.duration = rows_.back().time - rows_.front().time;
        return s;
    }
};
} // namespace features
