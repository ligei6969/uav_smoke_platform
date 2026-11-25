#include "smoke/gimbal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smoke {
namespace {

GimbalDecision Reject(const std::string &reason)
{
    GimbalDecision decision;
    decision.reason = reason;
    return decision;
}

bool ValidLens(Lens lens)
{
    return lens == Lens::Wide || lens == Lens::Zoom || lens == Lens::Infrared;
}

} // namespace

GimbalController::GimbalController(const GimbalConfig &config, TimeBase time_base)
    : config_(config), time_base_(time_base)
{
    if (!std::isfinite(config.deadband) || config.deadband < 0.0 || config.deadband >= 0.5 ||
        !std::isfinite(config.max_yaw_step_deg) || config.max_yaw_step_deg <= 0.0 ||
        !std::isfinite(config.max_pitch_step_deg) || config.max_pitch_step_deg <= 0.0 ||
        !std::isfinite(config.min_interval_sec) || config.min_interval_sec < 0.0 ||
        !std::isfinite(config.max_target_age_sec) || config.max_target_age_sec < 0.0 ||
        !std::isfinite(config.min_confidence) || config.min_confidence < 0.0 ||
        config.min_confidence > 1.0 ||
        (time_base != TimeBase::HostMonotonic && time_base != TimeBase::Replay)) {
        throw std::invalid_argument("invalid gimbal configuration or timebase");
    }
}

void GimbalController::Reset()
{
    has_stream_ = false;
    active_stream_ = FrameStamp();
}

bool GimbalController::SameStream(const FrameStamp &stamp) const
{
    return has_stream_ && active_stream_.source == stamp.source &&
           active_stream_.lens == stamp.lens &&
           active_stream_.stream_epoch == stamp.stream_epoch &&
           active_stream_.time_base == stamp.time_base;
}

std::string GimbalController::ObserveFrame(const FrameStamp &stamp)
{
    StreamCursor *matching = nullptr;
    for (auto &cursor : cursors_) {
        if (cursor.stamp.source != stamp.source) { continue; }
        if (stamp.stream_epoch < cursor.stamp.stream_epoch) { return "stale_epoch"; }
        if (cursor.stamp.lens == stamp.lens) { matching = &cursor; }
    }
    if (matching && matching->stamp.stream_epoch == stamp.stream_epoch) {
        if (stamp.frame_id <= matching->stamp.frame_id) { return "stale_frame"; }
        if (stamp.time_sec < matching->stamp.time_sec) { return "out_of_order_time"; }
    }
    if (matching) {
        matching->stamp = stamp;
    } else {
        if (cursors_.size() >= 64) { return "stream_limit"; }
        cursors_.push_back(StreamCursor{stamp});
    }
    return std::string();
}

GimbalDecision GimbalController::Update(const Detection &d, double now_sec,
                                      bool manual_override, bool yaw_limited,
                                      bool pitch_limited)
{
    if (manual_override) {
        Reset();
        // Do not allow this manual-control frame to be retried as an automatic target.
        if (ValidateDetection(d) && ValidLens(d.stamp.lens) &&
            d.stamp.time_base == time_base_) {
            ObserveFrame(d.stamp);
        }
        return Reject("manual_override");
    }
    if (!config_.enabled) { return Reject("disabled"); }
    if (!std::isfinite(now_sec) || now_sec < 0.0) {
        Reset();
        return Reject("invalid_now");
    }
    if (has_now_ && now_sec < last_now_sec_) {
        Reset();
        return Reject("clock_reversed");
    }
    has_now_ = true;
    last_now_sec_ = now_sec;

    std::string reason;
    if (!ValidateDetection(d, &reason)) {
        Reset();
        return Reject(reason);
    }
    if (!ValidLens(d.stamp.lens)) {
        Reset();
        return Reject("unsupported_lens");
    }
    if (d.stamp.time_base != time_base_) {
        Reset();
        return Reject("timebase_mismatch");
    }
    reason = ObserveFrame(d.stamp);
    if (!reason.empty()) { return Reject(reason); }

    // A frame is consumed even if it is expired, weak, inside the deadband, or limited.
    if (d.stamp.time_sec > now_sec) {
        Reset();
        return Reject("future_target");
    }
    if (now_sec - d.stamp.time_sec > config_.max_target_age_sec) {
        Reset();
        return Reject("target_expired");
    }
    if (d.box.confidence < config_.min_confidence) {
        Reset();
        return Reject("low_confidence");
    }
    if (!SameStream(d.stamp)) {
        active_stream_ = d.stamp;
        has_stream_ = true;
        return Reject("source_changed");
    }
    if (has_command_ && now_sec - last_command_sec_ < config_.min_interval_sec) {
        return Reject("rate_limited");
    }

    const double offset_x = (d.box.x1 + d.box.x2 - d.width) / 2.0;
    const double offset_y = (d.box.y1 + d.box.y2 - d.height) / 2.0;
    const double error_x = offset_x / d.width;
    const double error_y = offset_y / d.height;
    double yaw = std::abs(offset_x) <= config_.deadband * d.width ? 0.0 :
        std::max(-1.0, std::min(1.0, 2.0 * error_x)) * config_.max_yaw_step_deg;
    double pitch = std::abs(offset_y) <= config_.deadband * d.height ? 0.0 :
        -std::max(-1.0, std::min(1.0, 2.0 * error_y)) * config_.max_pitch_step_deg;
    if (yaw_limited) { yaw = 0.0; }
    if (pitch_limited) { pitch = 0.0; }
    if (yaw == 0.0 && pitch == 0.0) {
        return Reject(yaw_limited || pitch_limited ? "axis_limited" : "deadband");
    }

    GimbalDecision decision;
    decision.permitted = true;
    decision.yaw_step_deg = yaw;
    decision.pitch_step_deg = pitch;
    decision.reason = "dry_run";
    has_command_ = true;
    last_command_sec_ = now_sec;
    return decision;
}

} // namespace smoke
