#ifndef SMOKE_STABLE_EVENT_HPP
#define SMOKE_STABLE_EVENT_HPP

#include "smoke/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace smoke {

// A confirmed smoke target: consecutive positive frames from one
// source/epoch/lens stream. This is a detection-stability decision over
// recorded METADATA, not proof of fire and not a tracked real-world object.
struct StableEvent {
    std::uint64_t event_id = 0;      // Sequence across this tracker's lifetime.
    std::string source;
    std::uint64_t stream_epoch = 0;
    Lens lens = Lens::Unknown;
    std::uint64_t first_frame = 0;
    std::uint64_t last_frame = 0;
    double first_time_sec = 0.0;
    double last_time_sec = 0.0;
    std::uint64_t frame_count = 0;
    double max_confidence = 0.0;
    double mean_confidence = 0.0;
    // Mean box center over contributing frames, in pixels.
    double center_x_px = 0.0;
    double center_y_px = 0.0;
    // Mean bottom-center row: the point ground projection uses.
    double bottom_center_y_px = 0.0;
};

struct StableEventConfig {
    // Frames required before a run of positives becomes an event.
    std::uint64_t min_frames = 3;
    // A gap larger than this between positive frames breaks the run.
    double max_frame_gap_sec = 1.0;
    double min_confidence = 0.5;
    // Bounded tracker: older closed events beyond this count are dropped.
    std::uint64_t max_stored_events = 64;
};

// Single-owner, single-stream tracker. Frames must arrive in replay order
// (increasing frame_id, nondecreasing time) exactly as the replay CLI
// validates them; violations are rejected like the gimbal module rejects
// stale frames. Bounded memory: only per-stream run state plus closed
// events up to max_stored_events.
class StableEventTracker {
public:
    explicit StableEventTracker(const StableEventConfig &config = StableEventConfig());

    // Feed one frame; positive means a smoke box passed confidence gating.
    // Returns false with a reason on order violations.
    bool Observe(const FrameStamp &stamp, bool positive, double confidence,
                 double center_x_px, double center_y_px, double bottom_center_y_px,
                 std::string *reason = nullptr);

    // Closed events, oldest first. Bounded to max_stored_events.
    const std::vector<StableEvent> &Events() const { return events_; }

    // Force-close any open run (session end). Returns true if an event closed.
    bool Flush();

    std::uint64_t OpenRunCount() const { return open_ ? 1 : 0; }
    std::uint64_t DroppedEvents() const { return dropped_; }

private:
    void CloseRun();
    bool SameStream(const FrameStamp &stamp) const;

    StableEventConfig config_;
    std::vector<StableEvent> events_;
    std::uint64_t next_event_id_ = 1;
    std::uint64_t dropped_ = 0;
    // Open run state (at most one run at a time in a single ordered stream).
    bool open_ = false;
    bool has_stream_ = false;
    FrameStamp stream_;
    std::uint64_t first_frame_ = 0;
    std::uint64_t last_frame_ = 0;
    double first_time_ = 0.0;
    double last_time_ = 0.0;
    double confidence_sum_ = 0.0;
    double max_confidence_ = 0.0;
    double center_x_sum_ = 0.0;
    double center_y_sum_ = 0.0;
    double bottom_center_y_sum_ = 0.0;
    std::uint64_t run_frames_ = 0;
};

} // namespace smoke
#endif
