#include "smoke/localization.hpp"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    throw std::runtime_error(std::string("check failed at line ") + \
    std::to_string(__LINE__) + ": " #condition); } } while (false)

smoke::TelemetrySample Sample(double time = 10.0)
{
    smoke::TelemetrySample s;
    s.source = "camera_1";
    s.time_sec = time;
    s.latitude_deg = 30.0;
    s.longitude_deg = 120.0;
    s.altitude_m = 150.0;
    s.height_agl_m = 100.0;
    s.pitch_deg = -90.0;
    s.gps_valid = s.height_valid = s.attitude_valid = true;
    return s;
}

smoke::Detection Detection(double time = 10.0)
{
    smoke::Detection d;
    d.stamp.source = "camera_1";
    d.stamp.time_sec = time;
    d.stamp.lens = smoke::Lens::Wide;
    d.width = 1920;
    d.height = 1080;
    // Bottom-center is the optical image center.
    d.box.x1 = 900.0;
    d.box.x2 = 1020.0;
    d.box.y1 = 400.0;
    d.box.y2 = 540.0;
    d.box.confidence = 0.9;
    return d;
}

smoke::CameraCalibration Camera()
{
    smoke::CameraCalibration c;
    c.lens = smoke::Lens::Wide;
    c.horizontal_fov_deg = 82.0;
    c.calibrated = true;
    return c;
}

smoke::TelemetryMatch Matched(const smoke::TelemetrySample &sample,
                              const smoke::Detection &d)
{
    smoke::TelemetryBuffer buffer;
    CHECK(buffer.Push(sample));
    return buffer.Match(d.stamp, 0.25);
}

void TestGeometry()
{
    auto d = Detection();
    auto c = Camera();
    auto s = Sample();
    auto location = smoke::Locate(d, Matched(s, d), c);
    CHECK(location.valid);
    CHECK(std::fabs(location.latitude_deg - 30.0) < 1.0e-10);
    CHECK(std::fabs(location.longitude_deg - 120.0) < 1.0e-10);
    CHECK(std::fabs(location.altitude_m - 50.0) < 1.0e-10);
    CHECK(std::fabs(location.north_m) < 1.0e-10);
    CHECK(std::fabs(location.east_m) < 1.0e-10);

    d.box.y2 = 640.0;
    location = smoke::Locate(d, Matched(s, d), c);
    const double focal = 1920.0 / (2.0 * std::tan(82.0 * 3.14159265358979323846 / 360.0));
    CHECK(location.valid);
    CHECK(std::fabs(location.north_m + 100.0 * 100.0 / focal) < 1.0e-9);
    d = Detection();

    s.pitch_deg = -45.0;
    s.yaw_deg = 90.0;
    location = smoke::Locate(d, Matched(s, d), c);
    CHECK(location.valid);
    CHECK(std::fabs(location.north_m) < 1.0e-9);
    CHECK(std::fabs(location.east_m - 100.0) < 1.0e-9);
    CHECK(location.longitude_deg > 120.0);

    s.pitch_deg = 0.0;
    CHECK(smoke::Locate(d, Matched(s, d), c).reason == "ray_not_downward");
    s = Sample();
    s.latitude_deg = 89.99;
    CHECK(smoke::Locate(d, Matched(s, d), c).reason == "polar_location_unsupported");
    s = Sample();
    s.pitch_deg = -1.0;
    s.height_agl_m = 10000.0;
    CHECK(smoke::Locate(d, Matched(s, d), c).reason == "projection_too_far");
}

void TestMatching()
{
    smoke::TelemetryBuffer buffer;
    auto d = Detection(10.0);
    CHECK(buffer.Match(d.stamp, 0.1).reason == "no_telemetry_for_source");
    CHECK(buffer.Push(Sample(10.1)));
    CHECK(buffer.Push(Sample(9.95)));
    auto m = buffer.Match(d.stamp, 0.2);
    CHECK(m.valid);
    CHECK(m.sample.time_sec == 9.95);
    CHECK(std::fabs(m.delta_sec - 0.05) < 1.0e-10);
    d.stamp.time_sec = 10.09;
    m = buffer.Match(d.stamp, 0.02);
    CHECK(m.valid && m.sample.time_sec == 10.1);
    d.stamp.time_sec = 8.0;
    CHECK(buffer.Match(d.stamp, 0.2).reason == "telemetry_stale");
    CHECK(smoke::Locate(d, buffer.Match(d.stamp, 0.2), Camera()).reason == "telemetry_stale");
    d = Detection();
    d.stamp.source = "other";
    CHECK(buffer.Match(d.stamp, 0.2).reason == "no_telemetry_for_source");
    d = Detection();
    d.stamp.time_base = smoke::TimeBase::HostMonotonic;
    CHECK(buffer.Match(d.stamp, 0.2).reason == "time_base_mismatch");
    d = Detection();
    CHECK(buffer.Match(d.stamp, -0.1).reason == "invalid_max_delta");
    CHECK(buffer.Match(d.stamp, std::numeric_limits<double>::infinity()).reason == "invalid_max_delta");
    d.stamp.time_sec = std::numeric_limits<double>::quiet_NaN();
    CHECK(buffer.Match(d.stamp, 0.1).reason == "invalid_frame_time");

    smoke::TelemetryBuffer ties;
    CHECK(ties.Push(Sample(11.0)));
    CHECK(ties.Push(Sample(9.0)));
    CHECK(ties.Match(Detection().stamp, 1.0).sample.time_sec == 9.0);
    smoke::TelemetryBuffer boundary;
    CHECK(boundary.Push(Sample(10.25)));
    CHECK(boundary.Match(Detection().stamp, 0.25).valid);
}

void TestEpochIsolation()
{
    // Reconnect protection: telemetry from a previous session epoch must
    // never be matched against frames of the current epoch, even when the
    // times overlap exactly.
    smoke::TelemetryBuffer buffer;
    auto old = Sample(10.0);
    old.stream_epoch = 0;
    CHECK(buffer.Push(old));
    auto frame = Detection(10.0).stamp;
    frame.stream_epoch = 1; // Frame arrives after a reconnect.
    const auto mismatch = buffer.Match(frame, 0.2);
    CHECK(!mismatch.valid);
    CHECK(mismatch.reason == "telemetry_epoch_mismatch");
    CHECK(smoke::Locate(Detection(), mismatch, Camera()).reason == "telemetry_epoch_mismatch");
    // The new epoch's own samples match normally; the old ones stay excluded.
    auto fresh = Sample(10.0);
    fresh.stream_epoch = 1;
    CHECK(buffer.Push(fresh));
    const auto matched = buffer.Match(frame, 0.2);
    CHECK(matched.valid && matched.sample.stream_epoch == 1);
    // Frame from an epoch with no samples at all, source otherwise known.
    frame.stream_epoch = 2;
    CHECK(buffer.Match(frame, 0.2).reason == "telemetry_epoch_mismatch");
}

void TestCapacityAndQuality()
{
    smoke::TelemetryBuffer buffer(2);
    CHECK(buffer.Push(Sample(20.0)));
    CHECK(buffer.Push(Sample(10.0)));
    CHECK(buffer.Push(Sample(15.0)));
    CHECK(buffer.Size() == 2);
    CHECK(buffer.Match(Detection(10.0).stamp, 0.1).reason == "telemetry_stale");
    CHECK(buffer.Match(Detection(15.0).stamp, 0.0).valid);
    CHECK(buffer.Push(Sample(5.0)));
    CHECK(buffer.Match(Detection(15.0).stamp, 0.0).valid);
    CHECK(buffer.Size() == 2);

    auto invalid = Sample(20.0);
    invalid.gps_valid = false;
    CHECK(buffer.Push(invalid));
    CHECK(buffer.Match(Detection(20.0).stamp, 0.1).reason == "gps_invalid");
    invalid.gps_valid = true;
    invalid.height_valid = false;
    CHECK(buffer.Push(invalid));
    CHECK(buffer.Match(Detection(20.0).stamp, 0.1).reason == "height_invalid");
    invalid.height_valid = true;
    invalid.attitude_valid = false;
    CHECK(buffer.Push(invalid));
    CHECK(buffer.Match(Detection(20.0).stamp, 0.1).reason == "attitude_invalid");
    bool threw = false;
    try { smoke::TelemetryBuffer bad(4097); } catch (const std::invalid_argument &) { threw = true; }
    CHECK(threw);
}

void TestNumericalRejection()
{
    smoke::TelemetryBuffer buffer;
    std::string reason;
    auto sample = Sample();
    sample.yaw_deg = std::numeric_limits<double>::quiet_NaN();
    CHECK(!buffer.Push(sample, &reason) && reason == "invalid_attitude");
    sample = Sample();
    sample.height_agl_m = std::numeric_limits<double>::infinity();
    CHECK(!buffer.Push(sample, &reason) && reason == "invalid_height");
    sample = Sample();
    sample.time_sec = 1.0e13;
    CHECK(!buffer.Push(sample, &reason) && reason == "invalid_telemetry_time");
    sample = Sample();
    sample.latitude_deg = 91.0;
    CHECK(!buffer.Push(sample, &reason) && reason == "invalid_gps");
    sample = Sample();
    sample.gps_valid = false;
    sample.longitude_deg = std::numeric_limits<double>::infinity();
    CHECK(!buffer.Push(sample));
    sample = Sample();
    sample.pitch_deg = 181.0;
    CHECK(!buffer.Push(sample));
    CHECK(buffer.Size() == 0);

    auto d = Detection();
    auto match = Matched(Sample(), d);
    auto c = Camera();
    match.sample.yaw_deg = std::numeric_limits<double>::quiet_NaN();
    CHECK(smoke::Locate(d, match, c).reason == "invalid_attitude");
    match = Matched(Sample(), d);
    match.sample.source = "another";
    CHECK(smoke::Locate(d, match, c).reason == "source_mismatch");
    match = Matched(Sample(), d);
    match.sample.time_base = smoke::TimeBase::HostMonotonic;
    CHECK(smoke::Locate(d, match, c).reason == "time_base_mismatch");
    match = Matched(Sample(), d);
    match.sample.time_sec = 11.0;
    CHECK(smoke::Locate(d, match, c).reason == "telemetry_match_inconsistent");
    match = Matched(Sample(), d);
    c.calibrated = false;
    CHECK(smoke::Locate(d, match, c).reason == "camera_uncalibrated");
    c = Camera();
    c.lens = smoke::Lens::Zoom;
    CHECK(smoke::Locate(d, match, c).reason == "lens_mismatch");
    c.lens = smoke::Lens::Fpv;
    CHECK(smoke::Locate(d, match, c).reason == "unsupported_lens");
    c.lens = smoke::Lens::Unknown;
    CHECK(smoke::Locate(d, match, c).reason == "unsupported_lens");
    c = Camera();
    c.horizontal_fov_deg = std::numeric_limits<double>::infinity();
    CHECK(smoke::Locate(d, match, c).reason == "invalid_calibration_fov");
    c = Camera();
    d.box.x2 = std::numeric_limits<double>::quiet_NaN();
    CHECK(smoke::Locate(d, match, c).reason == "invalid_box");
}

void TestConcurrentAccess()
{
    smoke::TelemetryBuffer buffer(64);
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int producer = 0; producer < 4; ++producer) {
        threads.emplace_back([&buffer, &failed, producer] {
            for (int i = 0; i < 500; ++i) {
                auto s = Sample(i);
                s.source = "camera_" + std::to_string(producer);
                if (!buffer.Push(s)) { failed = true; }
                if (buffer.Size() > 64) { failed = true; }
                auto d = Detection(i);
                d.stamp.source = s.source;
                auto m = buffer.Match(d.stamp, 1.0);
                if (m.valid && m.sample.source != s.source) { failed = true; }
            }
        });
    }
    for (auto &thread : threads) { thread.join(); }
    CHECK(!failed.load());
    CHECK(buffer.Size() == 64);
}

} // namespace

int main()
{
    try {
        TestGeometry();
        TestMatching();
        TestEpochIsolation();
        TestCapacityAndQuality();
        TestNumericalRejection();
        TestConcurrentAccess();
        std::cout << "localization tests: 5 cases, " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
