#include "uav/smoke_event_builder.hpp"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
    const target_geolocation::CameraModel camera = {82.0};
    const uav::SmokeEventBuilder builder(camera);

    uav::TelemetryPacket telemetry = {};
    telemetry.timestampUs = 100000;
    telemetry.latitudeDeg = 30.0;
    telemetry.longitudeDeg = 120.0;
    telemetry.altitudeM = 100.0;
    telemetry.heightAboveGroundM = 100.0;
    telemetry.gimbalPitchDeg = -90.0;
    telemetry.gimbalRollDeg = 0.0;
    telemetry.gimbalYawDeg = 0.0;
    telemetry.gpsValid = true;

    uav::DetectionResult detection = {};
    detection.timestampUs = 100000;
    detection.className = "smoke";
    detection.confidence = 0.91;
    detection.centerXPx = 960.0;
    detection.centerYPx = 540.0;
    detection.widthPx = 160.0;
    detection.heightPx = 120.0;
    detection.imageWidthPx = 1920;
    detection.imageHeightPx = 1080;

    uav::SmokeTargetEvent event = {};
    std::string error;
    assert(builder.Build(telemetry, detection, &event, &error));
    assert(error.empty());
    assert(event.timestamp_ms == 100);
    assert(std::fabs(event.confidence - detection.confidence) < 1.0e-9);
    assert(std::fabs(event.target_latitude_deg - telemetry.latitudeDeg) < 1.0e-9);
    assert(std::fabs(event.target_longitude_deg - telemetry.longitudeDeg) < 1.0e-9);
    assert(std::fabs(event.target_altitude_m) < 1.0e-9);
    assert(std::fabs(event.north_offset_m) < 1.0e-9);
    assert(std::fabs(event.east_offset_m) < 1.0e-9);
    assert(std::fabs(event.horizontal_distance_m) < 1.0e-9);

    telemetry.gimbalPitchDeg = 0.0;
    assert(!builder.Build(telemetry, detection, &event, &error));
    assert(!error.empty());
    return 0;
}
