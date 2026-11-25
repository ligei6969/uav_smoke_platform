#ifndef SMOKE_VIDEO_REPORT_HPP
#define SMOKE_VIDEO_REPORT_HPP

#include "smoke/types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace smoke {
namespace video {

// Single-owner aggregation of all processed frames, including negatives.
// Frame IDs start at zero and must strictly increase; gaps are permitted.
// Times are zero-based seconds and must be finite, nonnegative and nondecreasing.
// Empty/no-detection getters return zero; PositiveFrameCount distinguishes this
// from a real detection on frame zero. Serialization can then emit JSON null.
class SummaryAccumulator {
public:
    void Add(std::uint64_t frame_id, double timestamp,
             const std::vector<smoke::BoundingBox> &smoke_boxes);

    std::uint64_t FrameCount() const { return frame_count_; }
    std::uint64_t PositiveFrameCount() const { return positive_frame_count_; }
    std::uint64_t BoxCount() const { return box_count_; }
    double ConfidenceSum() const { return confidence_sum_; }
    double MaxConfidence() const { return max_confidence_; }
    std::uint64_t FirstFrame() const { return first_frame_; }
    std::uint64_t LastFrame() const { return last_frame_; }
    std::uint64_t MaxFrame() const { return max_frame_; }
    double FirstTime() const { return first_time_; }
    double LastTime() const { return last_time_; }
    std::vector<std::pair<std::string, std::uint64_t>> KeyframeSelections() const;

private:
    std::uint64_t frame_count_ = 0;
    std::uint64_t positive_frame_count_ = 0;
    std::uint64_t box_count_ = 0;
    double confidence_sum_ = 0.0;
    double max_confidence_ = 0.0;
    std::uint64_t first_frame_ = 0;
    std::uint64_t last_frame_ = 0;
    std::uint64_t max_frame_ = 0;
    double first_time_ = 0.0;
    double last_time_ = 0.0;
    std::uint64_t previous_frame_ = 0;
    double previous_time_ = 0.0;
};

} // namespace video
} // namespace smoke
#endif
