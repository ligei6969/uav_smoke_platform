#include "uav/smoke_event_builder.hpp"

namespace uav {
namespace {

bool Fail(const char *message, std::string *errorMessage)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace

SmokeEventBuilder::SmokeEventBuilder(const target_geolocation::CameraModel &camera)
    : camera_(camera)
{
}

bool SmokeEventBuilder::Build(const TelemetryPacket &telemetry,
                              const DetectionResult &detection,
                              SmokeTargetEvent *event,
                              std::string *errorMessage) const
{
    if (event == nullptr) {
        return Fail("smoke target event output is null", errorMessage);
    }
    if (!telemetry.gpsValid) {
        return Fail("telemetry GPS is invalid", errorMessage);
    }

    target_geolocation::AircraftPose aircraft = {};
    aircraft.latitudeDeg = telemetry.latitudeDeg;
    aircraft.longitudeDeg = telemetry.longitudeDeg;
    aircraft.altitudeM = telemetry.altitudeM;
    aircraft.heightAboveGroundM = telemetry.heightAboveGroundM;
    aircraft.gimbalPitchDeg = telemetry.gimbalPitchDeg;
    aircraft.gimbalRollDeg = telemetry.gimbalRollDeg;
    aircraft.gimbalYawDeg = telemetry.gimbalYawDeg;

    target_geolocation::ImagePoint point = {};
    point.xPx = detection.centerXPx;
    point.yPx = detection.centerYPx;
    point.imageWidthPx = detection.imageWidthPx;
    point.imageHeightPx = detection.imageHeightPx;

    target_geolocation::TargetLocation target = {};
    if (!target_geolocation::CalculateGroundTarget(camera_, aircraft, point, &target, errorMessage)) {
        return false;
    }

    event->timestamp_ms = detection.timestampUs / 1000;
    event->confidence = detection.confidence;
    event->target_latitude_deg = target.latitudeDeg;
    event->target_longitude_deg = target.longitudeDeg;
    event->target_altitude_m = target.altitudeM;
    event->north_offset_m = target.northOffsetM;
    event->east_offset_m = target.eastOffsetM;
    event->horizontal_distance_m = target.horizontalDistanceM;
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

}  // namespace uav
