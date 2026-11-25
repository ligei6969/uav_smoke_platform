#include "smoke/video_report.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace smoke {
namespace video {

void SummaryAccumulator::Add(std::uint64_t frame_id, double timestamp,
                             const std::vector<smoke::BoundingBox> &smoke_boxes)
{
    if ((frame_count_ == 0 && frame_id != 0) ||
        (frame_count_ != 0 && frame_id <= previous_frame_)) {
        throw std::invalid_argument("summary_frame_ids_must_start_at_zero_and_increase");
    }
    if (!std::isfinite(timestamp) || timestamp < 0.0 ||
        (frame_count_ != 0 && timestamp < previous_time_)) {
        throw std::invalid_argument("summary_time_must_be_finite_nonnegative_and_nondecreasing");
    }

    double frame_confidence_sum = 0.0;
    double frame_max_confidence = 0.0;
    for (const auto &box : smoke_boxes) {
        if (!std::isfinite(box.x1) || !std::isfinite(box.y1) ||
            !std::isfinite(box.x2) || !std::isfinite(box.y2) ||
            !std::isfinite(box.confidence) || box.x1 < 0.0 || box.y1 < 0.0 ||
            box.x2 <= box.x1 || box.y2 <= box.y1 ||
            box.confidence < 0.0 || box.confidence > 1.0) {
            throw std::invalid_argument("summary_invalid_smoke_box");
        }
        frame_confidence_sum += box.confidence;
        frame_max_confidence = std::max(frame_max_confidence, box.confidence);
    }
    if (frame_count_ == std::numeric_limits<std::uint64_t>::max() ||
        smoke_boxes.size() > std::numeric_limits<std::uint64_t>::max() - box_count_) {
        throw std::overflow_error("summary_count_overflow");
    }

    // Commit only after all validation so a rejected frame cannot partly alter
    // counts, confidence statistics, keyframe choices or ordering cursors.
    if (!smoke_boxes.empty()) {
        if (positive_frame_count_ == 0) {
            first_frame_ = frame_id;
            first_time_ = timestamp;
        }
        if (positive_frame_count_ == 0 || frame_max_confidence > max_confidence_) {
            max_frame_ = frame_id;
            max_confidence_ = frame_max_confidence;
        }
        last_frame_ = frame_id;
        last_time_ = timestamp;
        ++positive_frame_count_;
        box_count_ += static_cast<std::uint64_t>(smoke_boxes.size());
        confidence_sum_ += frame_confidence_sum;
    }
    ++frame_count_;
    previous_frame_ = frame_id;
    previous_time_ = timestamp;
}

std::vector<std::pair<std::string, std::uint64_t>>
SummaryAccumulator::KeyframeSelections() const
{
    if (positive_frame_count_ == 0) { return {}; }
    std::vector<std::pair<std::string, std::uint64_t>> result;
    result.emplace_back("first_detection", first_frame_);
    if (max_frame_ != first_frame_) { result.emplace_back("max_confidence", max_frame_); }
    if (last_frame_ != first_frame_ && last_frame_ != max_frame_) {
        result.emplace_back("last_detection", last_frame_);
    }
    return result;
}

} // namespace video
} // namespace smoke
