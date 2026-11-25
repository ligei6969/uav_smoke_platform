#ifndef SMOKE_TELEMETRY_HPP
#define SMOKE_TELEMETRY_HPP

#include "smoke/types.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace smoke {

struct TelemetryMatch {
    bool valid = false;
    TelemetrySample sample;
    double delta_sec = 0.0; // Absolute frame/sample time difference.
    std::string reason;
};

// Global, bounded history sorted by timestamp. Oldest timestamps are evicted;
// late arrivals may therefore be immediately evicted when the buffer is full.
// Samples with invalid quality flags are retained, so Match reports the nearest
// sample's actual quality rather than silently falling back to older good data.
class TelemetryBuffer {
public:
    explicit TelemetryBuffer(std::size_t capacity = 256);
    bool Push(const TelemetrySample &sample, std::string *reason = nullptr);
    TelemetryMatch Match(const FrameStamp &frame, double max_delta_sec) const;
    std::size_t Size() const;

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<TelemetrySample> samples_;
};

} // namespace smoke
#endif
