#include "smoke/target_selector.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smoke {
namespace {
bool Unit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
double IoU(const BoundingBox &a, const BoundingBox &b)
{
    const double intersection = std::max(0.0, std::min(a.x2,b.x2)-std::max(a.x1,b.x1)) *
                                std::max(0.0, std::min(a.y2,b.y2)-std::max(a.y1,b.y1));
    return intersection / ((a.x2-a.x1)*(a.y2-a.y1)+(b.x2-b.x1)*(b.y2-b.y1)-intersection);
}
bool SameSession(const FrameStamp &a, const FrameStamp &b)
{
    return a.source==b.source && a.lens==b.lens && a.stream_epoch==b.stream_epoch && a.time_base==b.time_base;
}
bool ValidEnums(const FrameStamp &s)
{
    return (s.lens==Lens::Unknown || s.lens==Lens::Wide || s.lens==Lens::Zoom ||
            s.lens==Lens::Infrared || s.lens==Lens::Fpv) &&
           (s.time_base==TimeBase::Replay || s.time_base==TimeBase::HostMonotonic);
}
TargetSelection Result(const char *reason, int index=-1, bool changed=false)
{
    TargetSelection result;
    result.reason=reason; result.selected_index=index; result.target_changed=changed;
    return result;
}
}

TargetSelector::TargetSelector(const TargetSelectorConfig &c) : config_(c)
{
    if (!Unit(c.acquire_confidence) || !Unit(c.keep_confidence) ||
        c.keep_confidence>c.acquire_confidence || !Unit(c.association_iou) ||
        c.association_iou==0 || !Unit(c.switch_margin) ||
        c.confirmation_frames<1 || c.confirmation_frames>120 ||
        c.lost_release_frames<1 || c.lost_release_frames>120 ||
        !std::isfinite(c.max_target_age_sec) || c.max_target_age_sec<0 || c.max_target_age_sec>10 ||
        !std::isfinite(c.max_lost_sec) || c.max_lost_sec<=0 || c.max_lost_sec>30 ||
        c.max_candidates<1 || c.max_candidates>300) {
        throw std::invalid_argument("invalid_target_selector_config");
    }
}

void TargetSelector::ClearTarget()
{
    locked_=false; pending_=false; pending_count_=0; missed_=0;
}
void TargetSelector::Reset() { ClearTarget(); }

bool TargetSelector::Confirm(const BoundingBox &box, bool consecutive)
{
    pending_count_ = pending_ && consecutive && IoU(pending_box_,box)>=config_.association_iou
        ? pending_count_+1 : 1;
    pending_=true; pending_box_=box;
    return pending_count_>=config_.confirmation_frames;
}

TargetSelection TargetSelector::Update(const FrameStamp &stamp, int width, int height,
                                      const std::vector<BoundingBox> &boxes, double now)
{
    const bool was_locked=locked_;
    auto invalidate=[&](const char *reason) { ClearTarget(); return Result(reason,-1,was_locked); };
    if (!std::isfinite(now) || now<0) { return invalidate("invalid_now"); }
    Detection validation;
    validation.stamp=stamp; validation.width=width; validation.height=height;
    validation.box={0,0,1,1,1};
    if (!ValidEnums(stamp) || !ValidateDetection(validation)) { return invalidate("invalid_frame"); }
    if (boxes.size()>config_.max_candidates) { return invalidate("candidate_limit"); }
    for (const auto &box : boxes) {
        validation.box=box;
        if (!ValidateDetection(validation)) { return invalidate("invalid_candidate"); }
    }
    const bool same=has_cursor_ && SameSession(cursor_,stamp);
    if (has_cursor_ && cursor_.source==stamp.source && stamp.stream_epoch<cursor_.stream_epoch) {
        return Result("stale_epoch");
    }
    if (same && has_now_ && now<last_now_) { return invalidate("clock_reversed"); }
    if (same && stamp.frame_id<=cursor_.frame_id) { return Result("stale_frame"); }
    if (same && stamp.time_sec<cursor_.time_sec) { return Result("out_of_order_time"); }
    const bool consecutive=same && stamp.frame_id-cursor_.frame_id==1 &&
                           stamp.time_sec-cursor_.time_sec<=config_.max_lost_sec;
    bool changed=false;
    if (!same || width!=width_ || height!=height_) { changed=locked_; ClearTarget(); }
    cursor_=stamp; has_cursor_=true; width_=width; height_=height;
    last_now_=now; has_now_=true;
    if (stamp.time_sec>now) { return invalidate("future_frame"); }
    if (now-stamp.time_sec>config_.max_target_age_sec) { return invalidate("target_expired"); }

    int strongest=-1, match=-1;
    double best_overlap=-1;
    for (std::size_t i=0; i<boxes.size(); ++i) {
        const auto &box=boxes[i];
        if (box.confidence>=config_.acquire_confidence &&
            (strongest<0 || box.confidence>boxes[static_cast<std::size_t>(strongest)].confidence)) {
            strongest=static_cast<int>(i);
        }
        if (locked_ && box.confidence>=config_.keep_confidence) {
            const double overlap=IoU(locked_box_,box);
            if (overlap>=config_.association_iou && overlap>best_overlap) {
                best_overlap=overlap; match=static_cast<int>(i);
            }
        }
    }
    if (locked_) {
        if (match<0) {
            pending_=false; pending_count_=0; ++missed_;
            if (missed_>=config_.lost_release_frames || now-last_seen_>=config_.max_lost_sec) {
                ClearTarget(); return Result("target_released",-1,true);
            }
            return Result("target_missing"); // Never continue commanding the last box.
        }
        if (now-last_seen_>config_.max_lost_sec) {
            ClearTarget(); return Result("target_released",-1,true);
        }
        const auto &incumbent=boxes[static_cast<std::size_t>(match)];
        locked_box_=incumbent; missed_=0; last_seen_=now;
        if (strongest>=0 && strongest!=match) {
            const auto &challenger=boxes[static_cast<std::size_t>(strongest)];
            if (IoU(incumbent,challenger)<config_.association_iou &&
                challenger.confidence>=incumbent.confidence+config_.switch_margin) {
                if (Confirm(challenger,consecutive)) {
                    locked_box_=challenger; pending_=false; pending_count_=0;
                    return Result("target_switched",strongest,true);
                }
                return Result("switch_pending",match);
            }
        }
        pending_=false; pending_count_=0;
        return Result("target_locked",match);
    }
    if (strongest<0) { pending_=false; pending_count_=0; return Result("no_candidate",-1,changed); }
    if (!Confirm(boxes[static_cast<std::size_t>(strongest)],consecutive)) {
        return Result("acquiring",-1,changed);
    }
    locked_=true; locked_box_=boxes[static_cast<std::size_t>(strongest)];
    last_seen_=now; missed_=0; pending_=false; pending_count_=0;
    return Result("target_acquired",strongest,true);
}
} // namespace smoke
