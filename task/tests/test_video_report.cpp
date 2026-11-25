#include "smoke/video_report.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    throw std::runtime_error(std::string("check failed at line ") + \
    std::to_string(__LINE__) + ": " #condition); } } while (false)

smoke::BoundingBox Box(double confidence)
{
    return {10.0, 20.0, 50.0, 60.0, confidence};
}

template <typename Operation>
void Rejects(Operation operation)
{
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument &) { rejected = true; }
    CHECK(rejected);
}

void TestNoDetection()
{
    smoke::video::SummaryAccumulator summary;
    CHECK(summary.FrameCount() == 0);
    CHECK(summary.PositiveFrameCount() == 0);
    CHECK(summary.BoxCount() == 0);
    CHECK(summary.ConfidenceSum() == 0.0);
    CHECK(summary.MaxConfidence() == 0.0);
    CHECK(summary.FirstFrame() == 0 && summary.LastFrame() == 0 && summary.MaxFrame() == 0);
    CHECK(summary.FirstTime() == 0.0 && summary.LastTime() == 0.0);
    CHECK(summary.KeyframeSelections().empty());
    summary.Add(0, 0.0, {});
    summary.Add(2, 0.1, {});
    CHECK(summary.FrameCount() == 2);
    CHECK(summary.PositiveFrameCount() == 0);
    CHECK(summary.BoxCount() == 0);
    CHECK(summary.KeyframeSelections().empty());
}

void TestConfidenceAndRatio()
{
    smoke::video::SummaryAccumulator summary;
    summary.Add(0, 0.0, {});
    summary.Add(1, 0.1, {Box(0.2), Box(0.8)});
    summary.Add(4, 0.4, {Box(0.6)});
    summary.Add(5, 0.5, {});
    CHECK(summary.FrameCount() == 4);
    CHECK(summary.PositiveFrameCount() == 2);
    CHECK(summary.BoxCount() == 3);
    CHECK(std::fabs(summary.ConfidenceSum() - 1.6) < 1.0e-12);
    CHECK(std::fabs(summary.ConfidenceSum() / summary.BoxCount() - 1.6 / 3.0) < 1.0e-12);
    CHECK(std::fabs(summary.MaxConfidence() - 0.8) < 1.0e-12);
    CHECK(static_cast<double>(summary.PositiveFrameCount()) / summary.FrameCount() == 0.5);
    CHECK(summary.FirstFrame() == 1 && summary.LastFrame() == 4 && summary.MaxFrame() == 1);
    CHECK(summary.FirstTime() == 0.1 && summary.LastTime() == 0.4);
}

void TestKeyframeDeduplicationAndTies()
{
    smoke::video::SummaryAccumulator one;
    one.Add(0, 0.0, {Box(0.9)});
    auto selected = one.KeyframeSelections();
    CHECK(selected.size() == 1);
    CHECK(selected[0].first == "first_detection" && selected[0].second == 0);

    one.Add(3, 0.3, {Box(0.9)});
    selected = one.KeyframeSelections();
    CHECK(one.MaxFrame() == 0);
    CHECK(selected.size() == 2);
    CHECK(selected[0].first == "first_detection" && selected[0].second == 0);
    CHECK(selected[1].first == "last_detection" && selected[1].second == 3);

    smoke::video::SummaryAccumulator three;
    three.Add(0, 0.0, {Box(0.2)});
    three.Add(5, 0.5, {Box(0.9)});
    three.Add(8, 0.8, {Box(0.3)});
    selected = three.KeyframeSelections();
    CHECK(selected.size() == 3);
    CHECK(selected[0].first == "first_detection" && selected[0].second == 0);
    CHECK(selected[1].first == "max_confidence" && selected[1].second == 5);
    CHECK(selected[2].first == "last_detection" && selected[2].second == 8);

    smoke::video::SummaryAccumulator strongest_last;
    strongest_last.Add(0, 0.0, {Box(0.2)});
    strongest_last.Add(1, 0.1, {Box(0.9)});
    selected = strongest_last.KeyframeSelections();
    CHECK(selected.size() == 2);
    CHECK(selected[1].first == "max_confidence" && selected[1].second == 1);

    smoke::video::SummaryAccumulator zero_confidence;
    zero_confidence.Add(0, 0.0, {});
    zero_confidence.Add(5, 0.5, {Box(0.0)});
    CHECK(zero_confidence.PositiveFrameCount() == 1);
    CHECK(zero_confidence.MaxFrame() == 5);
    CHECK(zero_confidence.KeyframeSelections().size() == 1);
}

void TestInvalidInput()
{
    smoke::video::SummaryAccumulator summary;
    Rejects([&] { summary.Add(1, 0.0, {}); });
    Rejects([&] { summary.Add(0, -1.0, {}); });
    Rejects([&] { summary.Add(0, std::numeric_limits<double>::quiet_NaN(), {}); });
    Rejects([&] { summary.Add(0, std::numeric_limits<double>::infinity(), {}); });
    auto bad = Box(0.5);
    bad.x1 = -1.0;
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(0.5); bad.y2 = bad.y1;
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(0.5); bad.x2 = bad.x1 - 1.0;
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(0.5); bad.x2 = std::numeric_limits<double>::infinity();
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(std::numeric_limits<double>::quiet_NaN());
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(-0.01);
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    bad = Box(1.01);
    Rejects([&] { summary.Add(0, 0.0, {bad}); });
    CHECK(summary.FrameCount() == 0);
    CHECK(summary.KeyframeSelections().empty());

    summary.Add(0, 1.0, {});
    Rejects([&] { summary.Add(0, 1.0, {}); });
    Rejects([&] { summary.Add(1, 0.9, {}); });
    CHECK(summary.FrameCount() == 1);
    summary.Add(1, 1.0, {}); // Equal time is allowed, duplicate frame is not.
    summary.Add(9, 1.1, {}); // Frames need not be contiguous.
    Rejects([&] { summary.Add(8, 1.2, {}); });
    CHECK(summary.FrameCount() == 3);
}

void TestRejectedAddIsAtomic()
{
    smoke::video::SummaryAccumulator summary;
    Rejects([&] { summary.Add(0, 0.0, {Box(0.9), Box(-0.1)}); });
    CHECK(summary.FrameCount() == 0 && summary.PositiveFrameCount() == 0);
    CHECK(summary.BoxCount() == 0 && summary.ConfidenceSum() == 0.0);
    summary.Add(0, 0.0, {Box(0.5)});
    Rejects([&] { summary.Add(1, 1.0, {Box(0.9), Box(-0.1)}); });
    CHECK(summary.FrameCount() == 1 && summary.PositiveFrameCount() == 1);
    CHECK(summary.BoxCount() == 1 && summary.ConfidenceSum() == 0.5);
    CHECK(summary.MaxConfidence() == 0.5 && summary.LastFrame() == 0);
    summary.Add(1, 1.0, {Box(0.6)});
    CHECK(summary.FrameCount() == 2 && summary.MaxFrame() == 1);
}

void TestFrameIdUpperBound()
{
    smoke::video::SummaryAccumulator summary;
    summary.Add(0, 0.0, {});
    summary.Add(std::numeric_limits<std::uint64_t>::max(), 1.0, {Box(0.8)});
    CHECK(summary.FrameCount() == 2);
    CHECK(summary.FirstFrame() == std::numeric_limits<std::uint64_t>::max());
    Rejects([&] { summary.Add(std::numeric_limits<std::uint64_t>::max() - 1, 2.0, {}); });
}

} // namespace

int main()
{
    try {
        TestNoDetection();
        TestConfidenceAndRatio();
        TestKeyframeDeduplicationAndTies();
        TestInvalidInput();
        TestRejectedAddIsAtomic();
        TestFrameIdUpperBound();
        std::cout << "video report tests: 6 cases, " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
