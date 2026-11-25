#include "smoke/video_model.hpp"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smoke {
namespace video {
namespace {

// Python/Ultralytics round() uses ties-to-even, unlike std::round().
int RoundDimension(double value)
{
    const double lower = std::floor(value);
    const double fraction = value - lower;
    const int integer = static_cast<int>(lower);
    return integer + (fraction > 0.5 || (fraction == 0.5 && integer % 2 != 0) ? 1 : 0);
}

LetterboxTransform TransformFor(int width, int height)
{
    if (width < 1 || height < 1 || width > 16384 || height > 16384) {
        throw std::invalid_argument("invalid_image_dimensions");
    }
    LetterboxTransform transform;
    transform.original_width = width;
    transform.original_height = height;
    transform.scale = std::min(640.0 / width, 640.0 / height);
    transform.resized_width = RoundDimension(width * transform.scale);
    transform.resized_height = RoundDimension(height * transform.scale);
    if (transform.resized_width < 1 || transform.resized_height < 1) {
        throw std::invalid_argument("image_aspect_ratio_cannot_be_letterboxed");
    }
    transform.pad_left = (640 - transform.resized_width) / 2;
    transform.pad_top = (640 - transform.resized_height) / 2;
    return transform;
}

void ValidateTransform(const LetterboxTransform &transform)
{
    const auto expected = TransformFor(transform.original_width, transform.original_height);
    if (!std::isfinite(transform.scale) || transform.scale != expected.scale ||
        transform.resized_width != expected.resized_width ||
        transform.resized_height != expected.resized_height ||
        transform.pad_left != expected.pad_left || transform.pad_top != expected.pad_top) {
        throw std::invalid_argument("invalid_letterbox_transform");
    }
}

void ValidateShape(const cv::Mat &output)
{
    if (output.empty() || output.type() != CV_32FC1 || output.dims != 3 ||
        output.size[0] != 1 || output.size[1] != 25200 || output.size[2] != 7) {
        throw std::invalid_argument("expected_float32_output_1x25200x7");
    }
}

double IntersectionOverUnion(const BoundingBox &a, const BoundingBox &b)
{
    const double width = std::max(0.0, std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
    const double height = std::max(0.0, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
    const double intersection = width * height;
    const double total = (a.x2 - a.x1) * (a.y2 - a.y1) +
                         (b.x2 - b.x1) * (b.y2 - b.y1) - intersection;
    return total > 0.0 ? intersection / total : 0.0;
}

double Clip(double value, double maximum)
{
    return std::max(0.0, std::min(maximum, value));
}

} // namespace

PreparedInput PrepareInput(const cv::Mat &bgr)
{
    if (bgr.empty() || bgr.dims != 2 || bgr.type() != CV_8UC3) {
        throw std::invalid_argument("expected_nonempty_bgr_uint8_image");
    }
    PreparedInput prepared;
    prepared.transform = TransformFor(bgr.cols, bgr.rows);
    const auto &transform = prepared.transform;
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(transform.resized_width, transform.resized_height),
               0.0, 0.0, cv::INTER_LINEAR);
    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, transform.pad_top,
                       640 - transform.resized_height - transform.pad_top,
                       transform.pad_left,
                       640 - transform.resized_width - transform.pad_left,
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    cv::dnn::blobFromImage(padded, prepared.blob, 1.0 / 255.0, cv::Size(640, 640),
                           cv::Scalar(), true, false, CV_32F);
    return prepared;
}

std::vector<LabeledBox> DecodeOutput(const cv::Mat &output,
                                    const LetterboxTransform &transform,
                                    float confidence, float iou)
{
    ValidateShape(output);
    ValidateTransform(transform);
    if (!std::isfinite(confidence) || confidence < 0.0f || confidence > 1.0f ||
        !std::isfinite(iou) || iou < 0.0f || iou > 1.0f) {
        throw std::invalid_argument("invalid_detection_threshold");
    }

    std::vector<LabeledBox> candidates;
    for (int index = 0; index < 25200; ++index) {
        const float *row = output.ptr<float>(0, index);
        for (int column = 0; column < 7; ++column) {
            if (!std::isfinite(row[column])) {
                throw std::invalid_argument("nonfinite_model_output");
            }
        }
        if (row[2] <= 0.0f || row[3] <= 0.0f ||
            row[4] < 0.0f || row[4] > 1.0f ||
            row[5] < 0.0f || row[5] > 1.0f ||
            row[6] < 0.0f || row[6] > 1.0f) {
            throw std::invalid_argument("invalid_model_box_or_confidence");
        }
        const int class_id = row[6] > row[5] ? 1 : 0;
        const double score = static_cast<double>(row[4]) * row[5 + class_id];
        if (score < confidence) { continue; }
        const double left = (static_cast<double>(row[0]) - row[2] / 2.0 -
                             transform.pad_left) / transform.scale;
        const double top = (static_cast<double>(row[1]) - row[3] / 2.0 -
                            transform.pad_top) / transform.scale;
        const double right = (static_cast<double>(row[0]) + row[2] / 2.0 -
                              transform.pad_left) / transform.scale;
        const double bottom = (static_cast<double>(row[1]) + row[3] / 2.0 -
                               transform.pad_top) / transform.scale;
        BoundingBox box;
        box.x1 = Clip(left, transform.original_width);
        box.y1 = Clip(top, transform.original_height);
        box.x2 = Clip(right, transform.original_width);
        box.y2 = Clip(bottom, transform.original_height);
        box.confidence = score;
        if (box.x2 <= box.x1 || box.y2 <= box.y1) { continue; }
        candidates.push_back(LabeledBox{box, class_id});
    }

    // Stable ordering preserves the earlier model row for equal confidences.
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const LabeledBox &a, const LabeledBox &b) {
            return a.box.confidence > b.box.confidence;
        });
    std::vector<LabeledBox> selected;
    for (const auto &candidate : candidates) {
        bool suppressed = false;
        for (const auto &previous : selected) {
            if (candidate.class_id == previous.class_id &&
                IntersectionOverUnion(candidate.box, previous.box) > iou) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            selected.push_back(candidate);
            if (selected.size() == 300) { break; }
        }
    }
    return selected;
}

YoloModel::YoloModel(const std::string &path)
{
    if (path.empty()) { throw std::invalid_argument("empty_model_path"); }
    net_ = cv::dnn::readNetFromONNX(path);
    if (net_.empty()) { throw std::runtime_error("cannot_load_onnx_model"); }
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
}

cv::Mat YoloModel::ForwardPrepared(const cv::Mat &blob)
{
    const auto names = net_.getUnconnectedOutLayersNames();
    if (names.size() != 1) { throw std::runtime_error("expected_one_model_output"); }
    net_.setInput(blob);
    cv::Mat output = net_.forward(names[0]);
    ValidateShape(output);
    return output.clone(); // The next forward must not change a caller's result.
}

cv::Mat YoloModel::ForwardRaw(const cv::Mat &bgr)
{
    return ForwardPrepared(PrepareInput(bgr).blob);
}

std::vector<LabeledBox> YoloModel::Infer(const cv::Mat &bgr, float confidence, float iou)
{
    const auto prepared = PrepareInput(bgr);
    return DecodeOutput(ForwardPrepared(prepared.blob), prepared.transform, confidence, iou);
}

} // namespace video
} // namespace smoke
