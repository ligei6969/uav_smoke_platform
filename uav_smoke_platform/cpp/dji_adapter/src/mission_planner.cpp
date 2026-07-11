#include "uav/mission_planner.hpp"

#include <cmath>

namespace uav {
namespace {

const double kPi = 3.14159265358979323846;
const double kWgs84SemiMajorAxisM = 6378137.0;
const double kWgs84EccentricitySquared = 6.69437999014e-3;
const double kRecheckRadiusM = 50.0;
const double kRecheckAltitudeM = 120.0;

double DegreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double RadiansToDegrees(double radians)
{
    return radians * 180.0 / kPi;
}

Waypoint MakeOffsetWaypoint(const SmokeTargetEvent &event,
                            double north_m,
                            double east_m,
                            double yaw_deg)
{
    const double latitude_rad = DegreesToRadians(event.target_latitude_deg);
    const double sin_latitude = std::sin(latitude_rad);
    const double curvature = std::sqrt(1.0 - kWgs84EccentricitySquared * sin_latitude * sin_latitude);
    const double prime_vertical_radius = kWgs84SemiMajorAxisM / curvature;
    const double meridian_radius = kWgs84SemiMajorAxisM * (1.0 - kWgs84EccentricitySquared) /
                                   (curvature * curvature * curvature);
    const double longitude_scale = prime_vertical_radius * std::cos(latitude_rad);

    Waypoint waypoint;
    waypoint.latitude_deg = event.target_latitude_deg + RadiansToDegrees(north_m / meridian_radius);
    waypoint.longitude_deg = event.target_longitude_deg + RadiansToDegrees(east_m / longitude_scale);
    waypoint.altitude_m = kRecheckAltitudeM;
    waypoint.yaw_deg = yaw_deg;
    return waypoint;
}

}  // namespace

MissionPlan MissionPlanner::BuildRecheckMission(const SmokeTargetEvent &event) const
{
    MissionPlan plan;
    plan.waypoints.reserve(4);
    plan.waypoints.push_back(MakeOffsetWaypoint(event, kRecheckRadiusM, 0.0, 180.0));
    plan.waypoints.push_back(MakeOffsetWaypoint(event, 0.0, kRecheckRadiusM, -90.0));
    plan.waypoints.push_back(MakeOffsetWaypoint(event, -kRecheckRadiusM, 0.0, 0.0));
    plan.waypoints.push_back(MakeOffsetWaypoint(event, 0.0, -kRecheckRadiusM, 90.0));
    return plan;
}

}  // namespace uav
