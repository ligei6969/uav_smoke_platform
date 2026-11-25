#include "smoke/localization.hpp"

#include <algorithm>
#include <cmath>

namespace smoke {
namespace {

const double kPi = 3.14159265358979323846;
const double kSemiMajorM = 6378137.0;
const double kEccentricitySquared = 6.69437999014e-3;
// Explicit limits of this flat-earth engineering approximation, not universal
// accuracy guarantees. Longer range or polar work requires another model.
const double kMaximumHorizontalM = 10000.0;

double Radians(double degrees) { return degrees * kPi / 180.0; }
double Degrees(double radians) { return radians * 180.0 / kPi; }

GeoResult Invalid(const std::string &reason)
{
    GeoResult result;
    result.reason = reason;
    return result;
}

} // namespace

GeoResult Locate(const Detection &detection, const TelemetryMatch &telemetry,
                 const CameraCalibration &camera)
{
    std::string reason;
    if (!ValidateDetection(detection, &reason)) { return Invalid(reason); }
    if (!telemetry.valid) {
        return Invalid(telemetry.reason.empty() ? "telemetry_unmatched" : telemetry.reason);
    }
    const auto &sample = telemetry.sample;
    if (!ValidateTelemetry(sample, &reason)) { return Invalid(reason); }
    if (detection.stamp.time_sec > 1.0e12 || sample.time_sec > 1.0e12) {
        return Invalid("invalid_time_range");
    }
    if ((sample.time_base != TimeBase::Replay && sample.time_base != TimeBase::HostMonotonic) ||
        (detection.stamp.time_base != TimeBase::Replay &&
         detection.stamp.time_base != TimeBase::HostMonotonic)) {
        return Invalid("invalid_time_base");
    }
    if (sample.altitude_m < -1000.0 || sample.altitude_m > 100000.0) {
        return Invalid("invalid_gps");
    }
    if (std::fabs(sample.pitch_deg) > 180.0 || std::fabs(sample.roll_deg) > 180.0 ||
        std::fabs(sample.yaw_deg) > 360.0) {
        return Invalid("invalid_attitude");
    }
    if (sample.source != detection.stamp.source) { return Invalid("source_mismatch"); }
    if (sample.time_base != detection.stamp.time_base) { return Invalid("time_base_mismatch"); }
    if (!std::isfinite(telemetry.delta_sec) || telemetry.delta_sec < 0.0 ||
        !std::isfinite(sample.time_sec) || !std::isfinite(detection.stamp.time_sec) ||
        std::fabs(std::fabs(sample.time_sec - detection.stamp.time_sec) -
                  telemetry.delta_sec) > 1.0e-8) {
        return Invalid("telemetry_match_inconsistent");
    }
    if (!camera.calibrated) { return Invalid("camera_uncalibrated"); }
    if (camera.lens == Lens::Unknown || camera.lens == Lens::Fpv ||
        detection.stamp.lens == Lens::Unknown || detection.stamp.lens == Lens::Fpv) {
        return Invalid("unsupported_lens");
    }
    if (camera.lens != detection.stamp.lens) { return Invalid("lens_mismatch"); }
    if (!std::isfinite(camera.horizontal_fov_deg) || camera.horizontal_fov_deg <= 1.0 ||
        camera.horizontal_fov_deg >= 179.0) {
        return Invalid("invalid_calibration_fov");
    }
    if (std::fabs(sample.latitude_deg) >= 89.9) { return Invalid("polar_location_unsupported"); }

    const double focal = detection.width /
        (2.0 * std::tan(Radians(camera.horizontal_fov_deg) * 0.5));
    const double x = (detection.box.x1 + detection.box.x2) * 0.5;
    const double y = detection.box.y2;
    const double camera_right = (x - detection.width * 0.5) / focal;
    const double camera_down = (y - detection.height * 0.5) / focal;
    const double roll = Radians(sample.roll_deg);
    const double pitch = Radians(sample.pitch_deg);
    const double yaw = Radians(sample.yaw_deg);
    // Camera FRD -> global NED, Rz(yaw) * Ry(pitch) * Rx(roll).
    const double roll_right = std::cos(roll) * camera_right - std::sin(roll) * camera_down;
    const double roll_down = std::sin(roll) * camera_right + std::cos(roll) * camera_down;
    const double pitch_forward = std::cos(pitch) + std::sin(pitch) * roll_down;
    const double ray_down = -std::sin(pitch) + std::cos(pitch) * roll_down;
    const double ray_north = std::cos(yaw) * pitch_forward - std::sin(yaw) * roll_right;
    const double ray_east = std::sin(yaw) * pitch_forward + std::cos(yaw) * roll_right;
    const double ray_length = std::sqrt(1.0 + camera_right * camera_right +
                                       camera_down * camera_down);
    if (!std::isfinite(ray_down) || ray_down / ray_length <= 0.01) {
        return Invalid("ray_not_downward");
    }
    const double scale = sample.height_agl_m / ray_down;
    const double north = ray_north * scale;
    const double east = ray_east * scale;
    if (!std::isfinite(north) || !std::isfinite(east) ||
        std::hypot(north, east) > kMaximumHorizontalM) {
        return Invalid("projection_too_far");
    }

    const double latitude = Radians(sample.latitude_deg);
    const double sin_latitude = std::sin(latitude);
    const double curvature = std::sqrt(1.0 - kEccentricitySquared *
                                      sin_latitude * sin_latitude);
    const double meridian = kSemiMajorM * (1.0 - kEccentricitySquared) /
        (curvature * curvature * curvature);
    const double prime_vertical = kSemiMajorM / curvature;
    GeoResult result;
    result.north_m = north;
    result.east_m = east;
    result.altitude_m = sample.altitude_m - sample.height_agl_m;
    result.latitude_deg = sample.latitude_deg + Degrees(north / (meridian + sample.altitude_m));
    result.longitude_deg = std::remainder(sample.longitude_deg +
        Degrees(east / ((prime_vertical + sample.altitude_m) * std::cos(latitude))), 360.0);
    if (!std::isfinite(result.latitude_deg) || !std::isfinite(result.longitude_deg) ||
        !std::isfinite(result.altitude_m) || result.latitude_deg < -90.0 ||
        result.latitude_deg > 90.0) {
        return Invalid("invalid_projection");
    }
    result.valid = true;
    return result;
}

} // namespace smoke
