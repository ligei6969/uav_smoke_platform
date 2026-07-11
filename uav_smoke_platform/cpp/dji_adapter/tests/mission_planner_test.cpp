#include "uav/mission_planner.hpp"

#include <cassert>
#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;
const double kEarthRadiusM = 6378137.0;

double DegreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double ApproxDistanceM(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    const double mean_latitude_rad = DegreesToRadians((lat1_deg + lat2_deg) * 0.5);
    const double north_m = DegreesToRadians(lat2_deg - lat1_deg) * kEarthRadiusM;
    const double east_m = DegreesToRadians(lon2_deg - lon1_deg) * kEarthRadiusM * std::cos(mean_latitude_rad);
    return std::sqrt(north_m * north_m + east_m * east_m);
}

}  // namespace

int main()
{
    uav::SmokeTargetEvent event = {};
    event.timestamp_ms = 100;
    event.confidence = 0.91;
    event.target_latitude_deg = 30.0;
    event.target_longitude_deg = 120.0;
    event.target_altitude_m = 0.0;

    const uav::MissionPlanner planner;
    const uav::MissionPlan plan = planner.BuildRecheckMission(event);

    assert(plan.waypoints.size() == 4);

    for (size_t i = 0; i < plan.waypoints.size(); ++i) {
        assert(std::fabs(plan.waypoints[i].altitude_m - 120.0) < 1.0e-9);
        const double distance_m = ApproxDistanceM(event.target_latitude_deg,
                                                  event.target_longitude_deg,
                                                  plan.waypoints[i].latitude_deg,
                                                  plan.waypoints[i].longitude_deg);
        assert(std::fabs(distance_m - 50.0) < 0.5);
    }

    assert(plan.waypoints[0].latitude_deg > event.target_latitude_deg);
    assert(plan.waypoints[1].longitude_deg > event.target_longitude_deg);
    assert(plan.waypoints[2].latitude_deg < event.target_latitude_deg);
    assert(plan.waypoints[3].longitude_deg < event.target_longitude_deg);
    return 0;
}
