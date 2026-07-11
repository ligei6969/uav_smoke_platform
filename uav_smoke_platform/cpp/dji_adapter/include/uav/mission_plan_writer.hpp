#ifndef UAV_SMOKE_PLATFORM_MISSION_PLAN_WRITER_HPP
#define UAV_SMOKE_PLATFORM_MISSION_PLAN_WRITER_HPP

#include <string>

#include "uav/mission_plan.hpp"
#include "uav/smoke_event.hpp"

namespace uav {

class MissionPlanWriter {
public:
    static std::string ToJson(const SmokeTargetEvent &event, const MissionPlan &mission);

    static bool WriteJsonFile(const SmokeTargetEvent &event,
                              const MissionPlan &mission,
                              const std::string &output_path);
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_MISSION_PLAN_WRITER_HPP
