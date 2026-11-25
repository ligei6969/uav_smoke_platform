#ifndef SMOKE_TARGET_SELECTOR_HPP
#define SMOKE_TARGET_SELECTOR_HPP

#include "smoke/types.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace smoke {

struct TargetSelectorConfig {
    double acquire_confidence = 0.5;
    double keep_confidence = 0.35;
    double association_iou = 0.2;
    double switch_margin = 0.15;
    std::size_t confirmation_frames = 3;
    std::size_t lost_release_frames = 3;
    double max_target_age_sec = 0.5;
    double max_lost_sec = 0.75;
    std::size_t max_candidates = 300;
};

struct TargetSelection {
    int selected_index = -1; // Index into THIS frame's candidates; never an old box.
    bool target_changed = false; // Acquired, switched, or released a lock.
    std::string reason;
};

// Single-consumer geometric selection, not a multi-object tracker or an actuator.
// Call once for EVERY frame, including empty detections. Unknown lenses are
// accepted for image selection only; gimbal/localization permissions are separate.
class TargetSelector {
public:
    explicit TargetSelector(const TargetSelectorConfig &config = TargetSelectorConfig());
    TargetSelection Update(const FrameStamp &stamp, int width, int height,
                           const std::vector<BoundingBox> &candidates, double now_sec);
    // Forget the lock/pending candidate; retain the current session watermark.
    void Reset();

private:
    void ClearTarget();
    bool Confirm(const BoundingBox &box, bool consecutive);
    TargetSelectorConfig config_;
    bool has_cursor_ = false, has_now_ = false;
    FrameStamp cursor_;
    int width_ = 0, height_ = 0;
    double last_now_ = 0.0;
    bool locked_ = false, pending_ = false;
    BoundingBox locked_box_, pending_box_;
    std::size_t pending_count_ = 0, missed_ = 0;
    double last_seen_ = 0.0;
};

} // namespace smoke
#endif
