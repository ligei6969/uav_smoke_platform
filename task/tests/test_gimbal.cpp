#include "smoke/gimbal.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition " failed\n"; \
    ++failures; } } while (false)

smoke::Detection Target(std::uint64_t id, double time, double x = 800.0,
                        double y = 200.0)
{
    smoke::Detection d;
    d.stamp.source = "main-camera";
    d.stamp.lens = smoke::Lens::Wide;
    d.stamp.time_base = smoke::TimeBase::Replay;
    d.stamp.frame_id = id;
    d.stamp.stream_epoch = 1;
    d.stamp.time_sec = time;
    d.width = 1000;
    d.height = 1000;
    d.box = smoke::BoundingBox{x - 25.0, y - 25.0, x + 25.0, y + 25.0, 0.9};
    return d;
}

smoke::GimbalConfig Enabled()
{
    smoke::GimbalConfig config;
    config.enabled = true;
    return config;
}

void TestDefaultAndInvalidConfiguration()
{
    smoke::GimbalController disabled;
    CHECK(disabled.Update(Target(1, 1.0), 1.0).reason == "disabled");
    smoke::GimbalController host(Enabled());
    CHECK(host.Update(Target(1, 1.0), 1.0).reason == "timebase_mismatch");
    auto bad = Enabled();
    bad.deadband = std::numeric_limits<double>::quiet_NaN();
    bool threw = false;
    try { smoke::GimbalController controller(bad); }
    catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
    bad = Enabled();
    bad.min_interval_sec = -1.0;
    threw = false;
    try { smoke::GimbalController controller(bad); }
    catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
}

void TestDeadlineAndAntiReplay()
{
    smoke::GimbalController controller(Enabled(), smoke::TimeBase::Replay);
    CHECK(controller.Update(Target(1, 1.0), 1.0).reason == "source_changed");
    const auto boundary = controller.Update(Target(2, 1.0), 1.5);
    CHECK(boundary.permitted); // Exactly max_target_age_sec remains eligible.
    CHECK(std::abs(boundary.yaw_step_deg) <= 5.0);
    CHECK(std::abs(boundary.pitch_step_deg) <= 3.0);
    CHECK(controller.Update(Target(2, 1.0), 1.5).reason == "stale_frame");
    CHECK(controller.Update(Target(1, 1.0), 1.6).reason == "stale_frame");
    CHECK(controller.Update(Target(3, 1.0), 1.600001).reason == "target_expired");
    CHECK(controller.Update(Target(3, 1.0), 1.7).reason == "stale_frame");
    CHECK(controller.Update(Target(4, 2.0), 1.7).reason == "future_target");
}

void TestDeadbandRateLimitAndAxisLimits()
{
    smoke::GimbalController controller(Enabled(), smoke::TimeBase::Replay);
    CHECK(!controller.Update(Target(1, 1.0), 1.0).permitted);
    CHECK(controller.Update(Target(2, 1.0, 500.0, 500.0), 1.0).reason == "deadband");
    const auto first = controller.Update(Target(3, 1.0), 1.0);
    CHECK(first.permitted);
    CHECK(first.yaw_step_deg > 0.0);
    CHECK(first.pitch_step_deg > 0.0);
    CHECK(controller.Update(Target(4, 1.1), 1.1).reason == "rate_limited");
    CHECK(controller.Update(Target(4, 1.1), 1.3).reason == "stale_frame");
    const auto yaw_blocked = controller.Update(Target(5, 1.3), 1.3, false, true, false);
    CHECK(yaw_blocked.permitted);
    CHECK(yaw_blocked.yaw_step_deg == 0.0);
    CHECK(yaw_blocked.pitch_step_deg > 0.0);
    const auto pitch_blocked = controller.Update(Target(6, 1.6), 1.6, false, false, true);
    CHECK(pitch_blocked.permitted);
    CHECK(pitch_blocked.yaw_step_deg > 0.0);
    CHECK(pitch_blocked.pitch_step_deg == 0.0);
    const auto both = controller.Update(Target(7, 1.9), 1.9, false, true, true);
    CHECK(!both.permitted);
    CHECK(both.reason == "axis_limited");
    CHECK(both.yaw_step_deg == 0.0 && both.pitch_step_deg == 0.0);
}

void TestExactBoundariesAndFrameTimeOrdering()
{
    smoke::GimbalController controller(Enabled(), smoke::TimeBase::Replay);
    CHECK(controller.Update(Target(1, 1.0), 1.0).reason == "source_changed");
    CHECK(controller.Update(Target(2, 1.0, 420.0, 580.0), 1.0).reason == "deadband");
    CHECK(controller.Update(Target(3, 1.0), 1.0).permitted);
    CHECK(controller.Update(Target(4, 1.25), 1.25).permitted);
    CHECK(controller.Update(Target(5, 1.2), 1.3).reason == "out_of_order_time");
    CHECK(controller.Update(Target(6, 1.3),
        std::numeric_limits<double>::quiet_NaN()).reason == "invalid_now");

    smoke::GimbalController host(Enabled(), smoke::TimeBase::HostMonotonic);
    auto d = Target(1, 4.0);
    d.stamp.time_base = smoke::TimeBase::HostMonotonic;
    CHECK(host.Update(d, 4.0).reason == "source_changed");
    d.stamp.frame_id = 2;
    CHECK(host.Update(d, 4.0).permitted);
}

void TestInvalidAndLostTargets()
{
    smoke::GimbalController controller(Enabled(), smoke::TimeBase::Replay);
    CHECK(!controller.Update(Target(1, 1.0), 1.0).permitted);
    auto d = Target(2, 1.0);
    d.box.confidence = std::numeric_limits<double>::quiet_NaN();
    CHECK(controller.Update(d, 1.0).reason == "invalid_box");
    d = Target(2, 1.0);
    CHECK(controller.Update(d, std::numeric_limits<double>::infinity()).reason == "invalid_now");
    d.stamp.time_sec = std::numeric_limits<double>::quiet_NaN();
    CHECK(controller.Update(d, 1.0).reason == "invalid_frame_time");
    d = Target(2, 1.0);
    d.stamp.lens = smoke::Lens::Fpv;
    CHECK(controller.Update(d, 1.0).reason == "unsupported_lens");
    d.stamp.lens = smoke::Lens::Unknown;
    CHECK(controller.Update(d, 1.0).reason == "unsupported_lens");
    d = Target(2, 1.0);
    d.box.confidence = 0.49;
    CHECK(controller.Update(d, 1.0).reason == "low_confidence");
    CHECK(controller.Update(Target(2, 1.0), 1.0).reason == "stale_frame");
    CHECK(controller.Update(Target(3, 1.1), 1.1).reason == "source_changed");
    CHECK(controller.Update(Target(4, 1.2), 1.2).permitted);
    CHECK(controller.Update(Target(5, 1.3), 1.1).reason == "clock_reversed");
}

void TestStreamChangesAndTimebase()
{
    smoke::GimbalController controller(Enabled(), smoke::TimeBase::Replay);
    CHECK(controller.Update(Target(1, 1.0), 1.0).reason == "source_changed");
    CHECK(controller.Update(Target(2, 1.0), 1.0).permitted);
    auto zoom = Target(3, 1.3);
    zoom.stamp.lens = smoke::Lens::Zoom;
    CHECK(controller.Update(zoom, 1.3).reason == "source_changed");
    zoom.stamp.frame_id = 4;
    zoom.stamp.time_sec = 1.4;
    CHECK(controller.Update(zoom, 1.4).permitted);
    zoom.stamp.frame_id = 0;
    zoom.stamp.stream_epoch = 2;
    zoom.stamp.time_sec = 1.7;
    CHECK(controller.Update(zoom, 1.7).reason == "source_changed");
    CHECK(controller.Update(Target(20, 1.8), 1.8).reason == "stale_epoch");
    zoom.stamp.frame_id = 1;
    zoom.stamp.time_sec = 1.8;
    CHECK(controller.Update(zoom, 1.8).permitted);
    zoom.stamp.frame_id = 2;
    zoom.stamp.time_sec = 2.1;
    zoom.stamp.time_base = smoke::TimeBase::HostMonotonic;
    CHECK(controller.Update(zoom, 2.1).reason == "timebase_mismatch");
    controller.Reset();
    CHECK(controller.Update(zoom, 2.2).reason == "timebase_mismatch");

    auto other = Target(1, 2.3);
    other.stamp.source = "second-camera";
    CHECK(controller.Update(other, 2.3).reason == "source_changed");
    CHECK(controller.Update(Target(2, 2.4), 2.4).reason == "stale_epoch");
}

void TestManualResetAndInstanceIsolation()
{
    smoke::GimbalController first(Enabled(), smoke::TimeBase::Replay);
    smoke::GimbalController second(Enabled(), smoke::TimeBase::Replay);
    CHECK(first.Update(Target(1, 1.0), 1.0).reason == "source_changed");
    CHECK(first.Update(Target(2, 1.0), 1.0).permitted);
    CHECK(second.Update(Target(1, 1.0), 1.0).reason == "source_changed");
    CHECK(second.Update(Target(2, 1.0), 1.0).permitted);
    CHECK(first.Update(Target(3, 1.1), 1.1, true).reason == "manual_override");
    CHECK(first.Update(Target(3, 1.1), 1.1).reason == "stale_frame");
    CHECK(first.Update(Target(4, 1.2), 1.2).reason == "source_changed");
    CHECK(first.Update(Target(5, 1.3), 1.3).permitted);
    first.Reset();
    CHECK(first.Update(Target(5, 1.3), 1.4).reason == "stale_frame");
    CHECK(second.Update(Target(3, 1.3), 1.3).permitted);
}

} // namespace

int main()
{
    TestDefaultAndInvalidConfiguration();
    TestDeadlineAndAntiReplay();
    TestDeadbandRateLimitAndAxisLimits();
    TestExactBoundariesAndFrameTimeOrdering();
    TestInvalidAndLostTargets();
    TestStreamChangesAndTimebase();
    TestManualResetAndInstanceIsolation();
    if (failures != 0) { return 1; }
    std::cout << "gimbal dry-run checks passed\n";
    return 0;
}
