#ifndef UAV_SMOKE_PLATFORM_MISSION_PLANNER_HPP
#define UAV_SMOKE_PLATFORM_MISSION_PLANNER_HPP

#include "uav/mission_plan.hpp"
#include "uav/smoke_event.hpp"

namespace uav {

class MissionPlanner {
public:
    MissionPlan BuildRecheckMission(const SmokeTargetEvent &event) const;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_MISSION_PLANNER_HPP
