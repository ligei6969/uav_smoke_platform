#ifndef UAV_SMOKE_PLATFORM_MISSION_PLAN_HPP
#define UAV_SMOKE_PLATFORM_MISSION_PLAN_HPP

#include <vector>

namespace uav {

struct Waypoint {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
    double yaw_deg = 0.0;
};

struct MissionPlan {
    std::vector<Waypoint> waypoints;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_MISSION_PLAN_HPP
