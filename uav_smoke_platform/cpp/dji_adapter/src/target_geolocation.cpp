#include "uav/target_geolocation.hpp"

#include <cmath>

namespace target_geolocation {
namespace {

const double kPi = 3.14159265358979323846;
const double kWgs84SemiMajorAxisM = 6378137.0;
const double kWgs84EccentricitySquared = 6.69437999014e-3;
const double kMinimumDownRayComponent = 0.01;

double DegreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double RadiansToDegrees(double radians)
{
    return radians * 180.0 / kPi;
}

bool IsFinite(double value)
{
    return std::isfinite(value);
}

bool Fail(const char *message, std::string *errorMessage)
{
    if (errorMessage != NULL) {
        *errorMessage = message;
    }
    return false;
}

}  // namespace

bool CalculateGroundTarget(const CameraModel &camera,
                           const AircraftPose &aircraft,
                           const ImagePoint &point,
                           TargetLocation *target,
                           std::string *errorMessage)
{
    if (target == NULL) {
        return Fail("target output is null", errorMessage);
    }
    if (point.imageWidthPx <= 1 || point.imageHeightPx <= 1) {
        return Fail("invalid image dimensions", errorMessage);
    }
    if (!IsFinite(camera.horizontalFovDeg) || camera.horizontalFovDeg <= 1.0 ||
        camera.horizontalFovDeg >= 179.0) {
        return Fail("invalid camera horizontal field of view", errorMessage);
    }
    if (!IsFinite(aircraft.latitudeDeg) || !IsFinite(aircraft.longitudeDeg) ||
        !IsFinite(aircraft.altitudeM) || !IsFinite(aircraft.heightAboveGroundM) ||
        aircraft.latitudeDeg < -90.0 || aircraft.latitudeDeg > 90.0 ||
        aircraft.longitudeDeg < -180.0 || aircraft.longitudeDeg > 180.0) {
        return Fail("invalid aircraft GPS position", errorMessage);
    }
    if (aircraft.heightAboveGroundM <= 0.5 || aircraft.heightAboveGroundM > 10000.0) {
        return Fail("invalid aircraft height above ground", errorMessage);
    }
    if (!IsFinite(point.xPx) || !IsFinite(point.yPx) || point.xPx < 0.0 || point.yPx < 0.0 ||
        point.xPx > point.imageWidthPx || point.yPx > point.imageHeightPx) {
        return Fail("target pixel is outside the image", errorMessage);
    }

    const double halfFovRad = DegreesToRadians(camera.horizontalFovDeg) * 0.5;
    const double focalLengthPx = point.imageWidthPx / (2.0 * std::tan(halfFovRad));
    const double centerX = point.imageWidthPx * 0.5;
    const double centerY = point.imageHeightPx * 0.5;

    // Camera FRD ray: X is optical forward, Y is image-right, Z is image-down.
    const double rayX = 1.0;
    const double rayY = (point.xPx - centerX) / focalLengthPx;
    const double rayZ = (point.yPx - centerY) / focalLengthPx;

    const double roll = DegreesToRadians(aircraft.gimbalRollDeg);
    const double pitch = DegreesToRadians(aircraft.gimbalPitchDeg);
    const double yaw = DegreesToRadians(aircraft.gimbalYawDeg);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);

    // R_ned_camera = Rz(yaw) * Ry(pitch) * Rx(roll).
    const double rollX = rayX;
    const double rollY = cr * rayY - sr * rayZ;
    const double rollZ = sr * rayY + cr * rayZ;
    const double pitchX = cp * rollX + sp * rollZ;
    const double pitchY = rollY;
    const double pitchZ = -sp * rollX + cp * rollZ;
    const double rayNorth = cy * pitchX - sy * pitchY;
    const double rayEast = sy * pitchX + cy * pitchY;
    const double rayDown = pitchZ;

    if (!IsFinite(rayDown) || rayDown <= kMinimumDownRayComponent) {
        return Fail("camera ray does not intersect the ground", errorMessage);
    }

    const double scale = aircraft.heightAboveGroundM / rayDown;
    const double north = rayNorth * scale;
    const double east = rayEast * scale;
    const double latitudeRad = DegreesToRadians(aircraft.latitudeDeg);
    const double sinLatitude = std::sin(latitudeRad);
    const double curvature = std::sqrt(1.0 - kWgs84EccentricitySquared * sinLatitude * sinLatitude);
    const double primeVerticalRadius = kWgs84SemiMajorAxisM / curvature;
    const double meridianRadius = kWgs84SemiMajorAxisM * (1.0 - kWgs84EccentricitySquared) /
                                  (curvature * curvature * curvature);
    const double longitudeScale = (primeVerticalRadius + aircraft.altitudeM) * std::cos(latitudeRad);
    if (std::fabs(longitudeScale) < 1.0) {
        return Fail("longitude is unstable near a pole", errorMessage);
    }

    target->latitudeDeg = aircraft.latitudeDeg +
                          RadiansToDegrees(north / (meridianRadius + aircraft.altitudeM));
    target->longitudeDeg = aircraft.longitudeDeg + RadiansToDegrees(east / longitudeScale);
    target->altitudeM = aircraft.altitudeM - aircraft.heightAboveGroundM;
    target->northOffsetM = north;
    target->eastOffsetM = east;
    target->horizontalDistanceM = std::sqrt(north * north + east * east);
    if (errorMessage != NULL) {
        errorMessage->clear();
    }
    return true;
}

}  // namespace target_geolocation
