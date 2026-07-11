#ifndef UAV_SMOKE_PLATFORM_TARGET_GEOLOCATION_HPP
#define UAV_SMOKE_PLATFORM_TARGET_GEOLOCATION_HPP

#include <string>

namespace target_geolocation {

struct CameraModel {
    // Horizontal field of view at the current zoom level, in degrees.
    double horizontalFovDeg;
};

struct AircraftPose {
    double latitudeDeg;
    double longitudeDeg;
    double altitudeM;
    double heightAboveGroundM;
    // DJI gimbal angles in the global NED frame, in degrees.
    double gimbalPitchDeg;
    double gimbalRollDeg;
    double gimbalYawDeg;
};

struct ImagePoint {
    double xPx;
    double yPx;
    int imageWidthPx;
    int imageHeightPx;
};

struct TargetLocation {
    double latitudeDeg;
    double longitudeDeg;
    double altitudeM;
    double northOffsetM;
    double eastOffsetM;
    double horizontalDistanceM;
};

// Intersects the camera ray with a locally flat ground plane. The camera optical
// axis is assumed to align with the gimbal forward axis, with square pixels and
// the principal point at the image center.
bool CalculateGroundTarget(const CameraModel &camera,
                           const AircraftPose &aircraft,
                           const ImagePoint &point,
                           TargetLocation *target,
                           std::string *errorMessage);

}  // namespace target_geolocation

#endif  // UAV_SMOKE_PLATFORM_TARGET_GEOLOCATION_HPP
