#ifndef SMOKE_VIDEO_PIPELINE_HPP
#define SMOKE_VIDEO_PIPELINE_HPP

#include "smoke/types.hpp"
#include <cstdint>
#include <opencv2/core.hpp>
#include <string>

namespace smoke {
namespace video {

struct VideoFrame {
    FrameStamp stamp;
    cv::Mat bgr; // Owns its storage; the producer never reuses this buffer.
};

struct VideoOptions {
    std::string video;
    std::string model;
    std::string output_dir;
    std::string source = "video-11";
    Lens lens = Lens::Unknown;
    // Session epoch recorded into the summary so downstream replay metadata
    // can carry reconnect boundaries end to end. Zero for single recordings.
    std::uint64_t stream_epoch = 0;
    float confidence = .5f;
    float iou = .45f;
    std::uint64_t max_output_bytes = 1073741824;
};

// Returns the complete summary JSON. Throws on error; never actuates hardware.
std::string AnalyzeVideo(const VideoOptions &options);

} // namespace video
} // namespace smoke
#endif
