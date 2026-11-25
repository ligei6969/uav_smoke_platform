#ifndef SMOKE_VIDEO_MODEL_HPP
#define SMOKE_VIDEO_MODEL_HPP

#include "smoke/types.hpp"

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>

namespace smoke {
namespace video {

struct LetterboxTransform {
    int original_width = 0;
    int original_height = 0;
    int resized_width = 0;
    int resized_height = 0;
    int pad_left = 0;
    int pad_top = 0;
    double scale = 0.0;
};

struct LabeledBox {
    smoke::BoundingBox box;
    int class_id = 0; // Original model labels: 0=yan (smoke), 1=yun (cloud).
};

struct PreparedInput {
    cv::Mat blob; // FP32 RGB NCHW, [1,3,640,640], normalized by 255.
    LetterboxTransform transform;
};

PreparedInput PrepareInput(const cv::Mat &bgr);
std::vector<LabeledBox> DecodeOutput(const cv::Mat &output,
                                    const LetterboxTransform &transform,
                                    float confidence = 0.5f, float iou = 0.45f);

// Single-owner OpenCV DNN CPU model. No implicit backend or weight fallback.
class YoloModel {
public:
    explicit YoloModel(const std::string &path);
    cv::Mat ForwardRaw(const cv::Mat &bgr);
    std::vector<LabeledBox> Infer(const cv::Mat &bgr,
                                 float confidence = 0.5f, float iou = 0.45f);

private:
    cv::Mat ForwardPrepared(const cv::Mat &blob);
    cv::dnn::Net net_;
};

} // namespace video
} // namespace smoke
#endif
