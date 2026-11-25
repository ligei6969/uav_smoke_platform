#include "smoke/stable_event.hpp"

#include <cmath>
#include <stdexcept>

namespace smoke {
namespace {

bool ValidLens(Lens lens)
{
    return lens == Lens::Wide || lens == Lens::Zoom || lens == Lens::Infrared;
}

bool Fail(const char *message, std::string *reason)
{
    if (reason) { *reason = message; }
    return false;
}

} // namespace

StableEventTracker::StableEventTracker(const StableEventConfig &config)
    : config_(config)
{
    if (config.min_frames == 0 || config.min_frames > 4096 ||
        !std::isfinite(config.max_frame_gap_sec) || config.max_frame_gap_sec <= 0.0 ||
        config.max_frame_gap_sec > 3600.0 ||
        !std::isfinite(config.min_confidence) || config.min_confidence < 0.0 ||
        config.min_confidence > 1.0 ||
        config.max_stored_events == 0 || config.max_stored_events > 4096) {
        throw std::invalid_argument("invalid stable event configuration");
    }
}

bool StableEventTracker::SameStream(const FrameStamp &stamp) const
{
    return has_stream_ && stream_.source == stamp.source &&
           stream_.stream_epoch == stamp.stream_epoch &&
           stream_.lens == stamp.lens &&
           stream_.time_base == stamp.time_base;
}

void StableEventTracker::CloseRun()
{
    if (!open_) { return; }
    open_ = false;
    if (run_frames_ < config_.min_frames) { return; }
    StableEvent event;
    event.event_id = next_event_id_++;
    event.source = stream_.source;
    event.stream_epoch = stream_.stream_epoch;
    event.lens = stream_.lens;
    event.first_frame = first_frame_;
    event.last_frame = last_frame_;
    event.first_time_sec = first_time_;
    event.last_time_sec = last_time_;
    event.frame_count = run_frames_;
    event.max_confidence = max_confidence_;
    event.mean_confidence = confidence_sum_ / static_cast<double>(run_frames_);
    event.center_x_px = center_x_sum_ / static_cast<double>(run_frames_);
    event.center_y_px = center_y_sum_ / static_cast<double>(run_frames_);
    event.bottom_center_y_px = bottom_center_y_sum_ / static_cast<double>(run_frames_);
    if (events_.size() >= config_.max_stored_events) {
        events_.erase(events_.begin());
        ++dropped_;
    }
    events_.push_back(event);
}

bool StableEventTracker::Observe(const FrameStamp &stamp, bool positive,
                                 double confidence, double center_x_px,
                                 double center_y_px, double bottom_center_y_px,
                                 std::string *reason)
{
    if (!ValidLens(stamp.lens)) {
        return Fail("unsupported_lens", reason);
    }
    if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0) {
        return Fail("invalid_confidence", reason);
    }
    if (!std::isfinite(center_x_px) || !std::isfinite(center_y_px) ||
        !std::isfinite(bottom_center_y_px)) {
        return Fail("invalid_center", reason);
    }
    if (!std::isfinite(stamp.time_sec) || stamp.time_sec < 0.0) {
        return Fail("invalid_frame_time", reason);
    }
    if (has_stream_ && !SameStream(stamp)) {
        // A source/epoch/lens change closes any open run; callers treat it as
        // a session boundary. Times may restart under a new epoch.
        CloseRun();
        has_stream_ = false;
    }
    if (has_stream_) {
        if (stamp.frame_id <= last_frame_ && !open_ && stamp.frame_id < first_frame_) {
            return Fail("stale_frame", reason);
        }
        if (stamp.frame_id <= stream_.frame_id) {
            return Fail("stale_frame", reason);
        }
        if (stamp.time_sec < stream_.time_sec) {
            return Fail("out_of_order_time", reason);
        }
    }
    stream_ = stamp;
    has_stream_ = true;

    const bool strong = positive && confidence >= config_.min_confidence;
    if (strong) {
        if (open_ && stamp.time_sec - last_time_ > config_.max_frame_gap_sec) {
            CloseRun();
        }
        if (!open_) {
            open_ = true;
            first_frame_ = stamp.frame_id;
            first_time_ = stamp.time_sec;
            confidence_sum_ = 0.0;
            max_confidence_ = 0.0;
            center_x_sum_ = 0.0;
            center_y_sum_ = 0.0;
            bottom_center_y_sum_ = 0.0;
            run_frames_ = 0;
        }
        ++run_frames_;
        last_frame_ = stamp.frame_id;
        last_time_ = stamp.time_sec;
        confidence_sum_ += confidence;
        if (confidence > max_confidence_) { max_confidence_ = confidence; }
        center_x_sum_ += center_x_px;
        center_y_sum_ += center_y_px;
        bottom_center_y_sum_ += bottom_center_y_px;
    } else {
        CloseRun();
    }
    if (reason) { reason->clear(); }
    return true;
}

bool StableEventTracker::Flush()
{
    const bool had_open = open_;
    CloseRun();
    return had_open && run_frames_ >= config_.min_frames;
}

} // namespace smoke
