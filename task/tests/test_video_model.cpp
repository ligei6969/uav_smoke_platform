#include "smoke/video_model.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition " failed\n"; \
    ++failures; } } while (false)

bool Near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

void ExpectThrow(const std::function<void()> &function)
{
    bool rejected = false;
    try { function(); }
    catch (const std::exception &) { rejected = true; }
    CHECK(rejected);
}

cv::Mat EmptyOutput()
{
    const int dimensions[] = {1, 25200, 7};
    cv::Mat output(3, dimensions, CV_32F, cv::Scalar(0));
    for (int row = 0; row < 25200; ++row) {
        output.ptr<float>(0, row)[2] = 1.0f;
        output.ptr<float>(0, row)[3] = 1.0f;
    }
    return output;
}

void SetRow(cv::Mat &output, int index, float x, float y, float width, float height,
            float objectness, float smoke, float cloud)
{
    float *row = output.ptr<float>(0, index);
    row[0] = x;
    row[1] = y;
    row[2] = width;
    row[3] = height;
    row[4] = objectness;
    row[5] = smoke;
    row[6] = cloud;
}

smoke::video::LetterboxTransform Identity()
{
    return smoke::video::PrepareInput(cv::Mat(640, 640, CV_8UC3, cv::Scalar(0, 0, 0))).transform;
}

void TestColorAndLetterbox()
{
    const cv::Mat image(641, 1280, CV_8UC3, cv::Scalar(10, 20, 30));
    const auto prepared = smoke::video::PrepareInput(image);
    const auto &transform = prepared.transform;
    CHECK(transform.original_width == 1280 && transform.original_height == 641);
    CHECK(transform.resized_width == 640 && transform.resized_height == 320);
    CHECK(transform.pad_left == 0 && transform.pad_top == 160);
    CHECK(transform.scale == 0.5);
    CHECK(prepared.blob.type() == CV_32FC1 && prepared.blob.dims == 4);
    CHECK(prepared.blob.size[0] == 1 && prepared.blob.size[1] == 3);
    CHECK(prepared.blob.size[2] == 640 && prepared.blob.size[3] == 640);
    CHECK(prepared.blob.isContinuous());
    const float *red = prepared.blob.ptr<float>(0, 0);
    const float *green = prepared.blob.ptr<float>(0, 1);
    const float *blue = prepared.blob.ptr<float>(0, 2);
    CHECK(Near(red[0], 114.0 / 255.0));
    CHECK(Near(red[160 * 640], 30.0 / 255.0));
    CHECK(Near(green[160 * 640], 20.0 / 255.0));
    CHECK(Near(blue[160 * 640], 10.0 / 255.0));
    CHECK(Near(red[480 * 640], 114.0 / 255.0));

    const auto odd = smoke::video::PrepareInput(cv::Mat(638, 1280, CV_8UC3, cv::Scalar(0)));
    CHECK(odd.transform.resized_height == 319);
    CHECK(odd.transform.pad_top == 160); // Top=160, bottom=161, not a half-pixel offset.
    const auto enlarged = smoke::video::PrepareInput(cv::Mat(10, 20, CV_8UC3, cv::Scalar(0)));
    CHECK(enlarged.transform.scale == 32.0);
    CHECK(enlarged.transform.resized_width == 640 && enlarged.transform.resized_height == 320);
    CHECK(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(10, 20, 30));
}

void TestTwoClassSevenColumnDecoding()
{
    cv::Mat output = EmptyOutput();
    SetRow(output, 0, 100, 100, 20, 20, 0.8f, 0.9f, 0.1f);
    SetRow(output, 1, 300, 300, 40, 40, 0.9f, 0.1f, 0.9f);
    SetRow(output, 2, 500, 500, 20, 20, 0.8f, 0.8f, 0.8f);
    const auto decoded = smoke::video::DecodeOutput(output, Identity());
    CHECK(decoded.size() == 3);
    if (decoded.size() == 3) {
        CHECK(decoded[0].class_id == 1);
        CHECK(Near(decoded[0].box.confidence, 0.81));
        CHECK(decoded[1].class_id == 0);
        CHECK(Near(decoded[1].box.x1, 90));
        CHECK(Near(decoded[1].box.confidence, 0.72));
        CHECK(decoded[2].class_id == 0); // Equal class probabilities choose the earlier class.
    }
    CHECK(smoke::video::DecodeOutput(EmptyOutput(), Identity()).empty());
}

void TestOddPaddingInverseAndIntersection()
{
    cv::Mat output = EmptyOutput();
    const auto transform = smoke::video::PrepareInput(
        cv::Mat(638, 1280, CV_8UC3, cv::Scalar(0))).transform;
    SetRow(output, 0, 50, 210, 100, 100, 1, 0.9f, 0.1f);
    auto decoded = smoke::video::DecodeOutput(output, transform);
    CHECK(decoded.size() == 1);
    if (!decoded.empty()) {
        CHECK(Near(decoded[0].box.x1, 0));
        CHECK(Near(decoded[0].box.y1, 0));
        CHECK(Near(decoded[0].box.x2, 200));
        CHECK(Near(decoded[0].box.y2, 200));
    }
    output = EmptyOutput();
    SetRow(output, 0, 0, 0, 20, 40, 1, 0.9f, 0.1f);
    SetRow(output, 1, -100, -100, 20, 40, 1, 0.9f, 0.1f);
    decoded = smoke::video::DecodeOutput(output, Identity());
    CHECK(decoded.size() == 1);
    if (!decoded.empty()) {
        CHECK(Near(decoded[0].box.x1, 0));
        CHECK(Near(decoded[0].box.y1, 0));
        CHECK(Near(decoded[0].box.x2, 10));
        CHECK(Near(decoded[0].box.y2, 20));
    }
}

void TestStableClassAwareNmsAndLimit()
{
    cv::Mat output = EmptyOutput();
    SetRow(output, 0, 100, 100, 100, 100, 1, 0.9f, 0.1f);
    SetRow(output, 1, 101, 101, 100, 100, 1, 0.9f, 0.1f);
    SetRow(output, 2, 100, 100, 100, 100, 1, 0.1f, 0.8f);
    const auto decoded = smoke::video::DecodeOutput(output, Identity());
    CHECK(decoded.size() == 2);
    if (decoded.size() == 2) {
        CHECK(decoded[0].class_id == 0 && decoded[1].class_id == 1);
        CHECK(Near(decoded[0].box.x1, 50)); // Tied later row must not replace row zero.
    }
    output = EmptyOutput();
    for (int index = 0; index < 400; ++index) {
        SetRow(output, index, static_cast<float>(10 + index % 20 * 30),
                static_cast<float>(10 + index / 20 * 30), 10, 10, 1, 1, 0);
    }
    const auto capped = smoke::video::DecodeOutput(output, Identity());
    CHECK(capped.size() == 300);
    if (capped.size() == 300) {
        CHECK(Near(capped.front().box.x1, 5));
        CHECK(Near(capped.back().box.x1, 575));
        CHECK(Near(capped.back().box.y1, 425));
    }
}

void TestWrongShapesAndInputs()
{
    for (const auto &dimensions : {std::vector<int>{1, 25200, 6},
                                   std::vector<int>{1, 29400, 6},
                                   std::vector<int>{1, 7, 25200},
                                   std::vector<int>{2, 25200, 7}}) {
        const cv::Mat wrong(3, dimensions.data(), CV_32F, cv::Scalar(0));
        ExpectThrow([&] { smoke::video::DecodeOutput(wrong, Identity()); });
    }
    ExpectThrow([&] { smoke::video::DecodeOutput(cv::Mat(25200, 7, CV_32F), Identity()); });
    const int dimensions[] = {1, 25200, 7};
    ExpectThrow([&] {
        smoke::video::DecodeOutput(cv::Mat(3, dimensions, CV_64F), Identity());
    });
    ExpectThrow([] { smoke::video::PrepareInput(cv::Mat()); });
    ExpectThrow([] { smoke::video::PrepareInput(cv::Mat(10, 10, CV_8UC1)); });
    ExpectThrow([] { smoke::video::PrepareInput(cv::Mat(10, 10, CV_32FC3)); });
    ExpectThrow([] { smoke::video::PrepareInput(cv::Mat(1, 16385, CV_8UC3)); });
    ExpectThrow([] { smoke::video::YoloModel model(""); });
}

void TestInvalidNumbersAndTransform()
{
    const auto identity = Identity();
    for (int column = 0; column < 7; ++column) {
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()}) {
            auto output = EmptyOutput();
            output.ptr<float>(0, 25199)[column] = invalid;
            ExpectThrow([&] { smoke::video::DecodeOutput(output, identity); });
        }
    }
    for (int column : {4, 5, 6}) {
        for (float invalid : {-0.1f, 1.1f}) {
            auto output = EmptyOutput();
            output.ptr<float>(0, 25199)[column] = invalid;
            ExpectThrow([&] { smoke::video::DecodeOutput(output, identity); });
        }
    }
    for (int column : {2, 3}) {
        for (float invalid : {0.0f, -1.0f}) {
            auto output = EmptyOutput();
            output.ptr<float>(0, 25199)[column] = invalid;
            ExpectThrow([&] { smoke::video::DecodeOutput(output, identity); });
        }
    }
    for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
        ExpectThrow([&] { smoke::video::DecodeOutput(EmptyOutput(), identity, invalid); });
        ExpectThrow([&] { smoke::video::DecodeOutput(EmptyOutput(), identity, 0.5f, invalid); });
    }
    auto bad_transform = identity;
    bad_transform.scale = 0;
    ExpectThrow([&] { smoke::video::DecodeOutput(EmptyOutput(), bad_transform); });
    bad_transform = identity;
    bad_transform.pad_top = 1;
    ExpectThrow([&] { smoke::video::DecodeOutput(EmptyOutput(), bad_transform); });
}

} // namespace

int main()
{
    TestColorAndLetterbox();
    TestTwoClassSevenColumnDecoding();
    TestOddPaddingInverseAndIntersection();
    TestStableClassAwareNmsAndLimit();
    TestWrongShapesAndInputs();
    TestInvalidNumbersAndTransform();
    if (failures != 0) { return 1; }
    std::cout << "video model contract checks passed\n";
    return 0;
}
