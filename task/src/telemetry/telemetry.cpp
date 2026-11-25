#include "smoke/telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace smoke {
namespace {

const double kMaximumTimeSec = 1.0e12;

bool Fail(const char *message, std::string *reason)
{
    if (reason) { *reason = message; }
    return false;
}

bool ValidSource(const std::string &source)
{
    if (source.empty() || source.size() > 64) { return false; }
    for (unsigned char c : source) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool ValidTimeBase(TimeBase base)
{
    return base == TimeBase::Replay || base == TimeBase::HostMonotonic;
}

// Unavailable measurements may carry finite zero placeholders. Validity flags
// are evaluated separately by Match, but NaN/Inf and numerical outliers never
// enter the history, even when their quality flag is false.
bool ValidInput(const TelemetrySample &sample, std::string *reason)
{
    if (!ValidSource(sample.source)) { return Fail("invalid_source", reason); }
    if (!ValidTimeBase(sample.time_base)) { return Fail("invalid_time_base", reason); }
    if (!std::isfinite(sample.time_sec) || sample.time_sec < 0.0 ||
        sample.time_sec > kMaximumTimeSec) {
        return Fail("invalid_telemetry_time", reason);
    }
    if (!std::isfinite(sample.latitude_deg) ||
        !std::isfinite(sample.longitude_deg) || !std::isfinite(sample.altitude_m) ||
        sample.latitude_deg < -90.0 || sample.latitude_deg > 90.0 ||
        sample.longitude_deg < -180.0 || sample.longitude_deg > 180.0 ||
        sample.altitude_m < -1000.0 || sample.altitude_m > 100000.0) {
        return Fail("invalid_gps", reason);
    }
    if (!std::isfinite(sample.height_agl_m) || sample.height_agl_m < 0.0 ||
        sample.height_agl_m > 10000.0 ||
        (sample.height_valid && sample.height_agl_m <= 0.5)) {
        return Fail("invalid_height", reason);
    }
    if (!std::isfinite(sample.pitch_deg) || !std::isfinite(sample.roll_deg) ||
        !std::isfinite(sample.yaw_deg) || std::fabs(sample.pitch_deg) > 180.0 ||
        std::fabs(sample.roll_deg) > 180.0 || std::fabs(sample.yaw_deg) > 360.0) {
        return Fail("invalid_attitude", reason);
    }
    if (reason) { reason->clear(); }
    return true;
}

} // namespace

TelemetryBuffer::TelemetryBuffer(std::size_t capacity) : capacity_(capacity)
{
    if (capacity == 0 || capacity > 4096) {
        throw std::invalid_argument("telemetry capacity must be in [1,4096]");
    }
    samples_.reserve(capacity + 1);
}

bool TelemetryBuffer::Push(const TelemetrySample &sample, std::string *reason)
{
    if (!ValidInput(sample, reason)) { return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &stored : samples_) {
        if (stored.source == sample.source && stored.time_base == sample.time_base &&
            stored.time_sec == sample.time_sec) {
            stored = sample; // A new quality assessment replaces the same timestamp.
            return true;
        }
    }
    const auto position = std::upper_bound(
        samples_.begin(), samples_.end(), sample.time_sec,
        [](double time, const TelemetrySample &stored) { return time < stored.time_sec; });
    samples_.insert(position, sample);
    if (samples_.size() > capacity_) { samples_.erase(samples_.begin()); }
    return true;
}

TelemetryMatch TelemetryBuffer::Match(const FrameStamp &frame, double max_delta_sec) const
{
    TelemetryMatch result;
    if (!ValidSource(frame.source)) { result.reason = "invalid_source"; return result; }
    if (!ValidTimeBase(frame.time_base)) { result.reason = "invalid_time_base"; return result; }
    if (!std::isfinite(frame.time_sec) || frame.time_sec < 0.0 ||
        frame.time_sec > kMaximumTimeSec) {
        result.reason = "invalid_frame_time";
        return result;
    }
    if (!std::isfinite(max_delta_sec) || max_delta_sec < 0.0 ||
        max_delta_sec > kMaximumTimeSec) {
        result.reason = "invalid_max_delta";
        return result;
    }

    bool saw_source = false;
    bool saw_time_base = false;
    bool found = false;
    double best_delta = std::numeric_limits<double>::infinity();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &sample : samples_) {
            if (sample.source != frame.source) { continue; }
            saw_source = true;
            if (sample.time_base != frame.time_base) { continue; }
            saw_time_base = true;
            if (sample.stream_epoch != frame.stream_epoch) { continue; }
            const double delta = std::fabs(sample.time_sec - frame.time_sec);
            // Samples are time ordered: equal-distance ties retain the earlier one.
            if (!found || delta < best_delta) {
                found = true;
                best_delta = delta;
                result.sample = sample;
            }
        }
    }
    if (!found) {
        // Diagnose in contract order: no source, wrong time base, then the
        // reconnect case — same source and time base but another session epoch.
        result.reason = !saw_source ? "no_telemetry_for_source"
                                    : (!saw_time_base ? "time_base_mismatch"
                                                      : "telemetry_epoch_mismatch");
        return result;
    }
    result.delta_sec = best_delta;
    if (best_delta > max_delta_sec) { result.reason = "telemetry_stale"; return result; }
    if (!ValidateTelemetry(result.sample, &result.reason)) { return result; }
    result.valid = true;
    return result;
}

std::size_t TelemetryBuffer::Size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.size();
}

} // namespace smoke
