#ifndef SMOKE_GIMBAL_HPP
#define SMOKE_GIMBAL_HPP

#include "smoke/types.hpp"

#include <string>
#include <vector>

namespace smoke {

struct GimbalConfig {
    bool enabled = false; // Enable dry-run suggestions only, never device control.
    double deadband = 0.08; // Fraction of the full image dimension, per axis.
    double max_yaw_step_deg = 5.0;
    double max_pitch_step_deg = 3.0;
    double min_interval_sec = 0.25;
    double max_target_age_sec = 0.5;
    double min_confidence = 0.5;
};

struct GimbalDecision {
    bool permitted = false; // A dry-run suggestion; not authorization to move hardware.
    double yaw_step_deg = 0.0;
    double pitch_step_deg = 0.0;
    std::string reason;
};

// Single-owner pure decision state. No SDK, worker thread, or actuator calls.
class GimbalController {
public:
    // Replay callers must explicitly select TimeBase::Replay at construction.
    explicit GimbalController(const GimbalConfig &config = GimbalConfig(),
                              TimeBase time_base = TimeBase::HostMonotonic);

    GimbalDecision Update(const Detection &detection, double now_sec,
                          bool manual_override = false,
                          bool yaw_limited = false, bool pitch_limited = false);

    // Clear target lock. Anti-replay watermarks, timebase and rate limit survive.
    void Reset();

private:
    struct StreamCursor {
        FrameStamp stamp;
    };

    std::string ObserveFrame(const FrameStamp &stamp);
    bool SameStream(const FrameStamp &stamp) const;

    GimbalConfig config_;
    TimeBase time_base_;
    bool has_stream_ = false;
    FrameStamp active_stream_;
    bool has_now_ = false;
    double last_now_sec_ = 0.0;
    bool has_command_ = false;
    double last_command_sec_ = 0.0;
    std::vector<StreamCursor> cursors_; // Bounded to 64 source/lens identities.
};

} // namespace smoke
#endif
