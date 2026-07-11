#include "uav/target_geolocation.hpp"

#include <cassert>
#include <cmath>
#include <string>

using target_geolocation::AircraftPose;
using target_geolocation::CameraModel;
using target_geolocation::ImagePoint;
using target_geolocation::TargetLocation;

int main()
{
    const CameraModel camera = {82.0};
    const ImagePoint center = {960.0, 540.0, 1920, 1080};
    std::string error;
    TargetLocation target = {};

    AircraftPose straightDown = {30.0, 120.0, 100.0, 100.0, -90.0, 0.0, 0.0};
    assert(target_geolocation::CalculateGroundTarget(camera, straightDown, center, &target, &error));
    assert(std::fabs(target.latitudeDeg - straightDown.latitudeDeg) < 1.0e-9);
    assert(std::fabs(target.longitudeDeg - straightDown.longitudeDeg) < 1.0e-9);
    assert(std::fabs(target.altitudeM) < 1.0e-9);

    AircraftPose eastAt45Degrees = {30.0, 120.0, 100.0, 100.0, -45.0, 0.0, 90.0};
    assert(target_geolocation::CalculateGroundTarget(camera, eastAt45Degrees, center, &target, &error));
    assert(std::fabs(target.northOffsetM) < 1.0e-8);
    assert(std::fabs(target.eastOffsetM - 100.0) < 1.0e-8);

    AircraftPose horizon = {30.0, 120.0, 100.0, 100.0, 0.0, 0.0, 0.0};
    assert(!target_geolocation::CalculateGroundTarget(camera, horizon, center, &target, &error));
    assert(!error.empty());
    return 0;
}
