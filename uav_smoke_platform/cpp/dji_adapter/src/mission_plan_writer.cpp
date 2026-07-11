#include "uav/mission_plan_writer.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace uav {

std::string MissionPlanWriter::ToJson(const SmokeTargetEvent &event, const MissionPlan &mission)
{
    std::ostringstream json;
    json << std::setprecision(15);
    json << "{\n";
    json << "  \"event_type\": \"smoke_detected\",\n";
    json << "  \"timestamp_ms\": " << event.timestamp_ms << ",\n";
    json << "  \"confidence\": " << event.confidence << ",\n";
    json << "  \"target\": {\n";
    json << "    \"latitude_deg\": " << event.target_latitude_deg << ",\n";
    json << "    \"longitude_deg\": " << event.target_longitude_deg << ",\n";
    json << "    \"altitude_m\": " << event.target_altitude_m << ",\n";
    json << "    \"north_offset_m\": " << event.north_offset_m << ",\n";
    json << "    \"east_offset_m\": " << event.east_offset_m << ",\n";
    json << "    \"horizontal_distance_m\": " << event.horizontal_distance_m << "\n";
    json << "  },\n";
    json << "  \"waypoints\": [\n";
    for (size_t i = 0; i < mission.waypoints.size(); ++i) {
        const Waypoint &waypoint = mission.waypoints[i];
        json << "    {\n";
        json << "      \"latitude_deg\": " << waypoint.latitude_deg << ",\n";
        json << "      \"longitude_deg\": " << waypoint.longitude_deg << ",\n";
        json << "      \"altitude_m\": " << waypoint.altitude_m << ",\n";
        json << "      \"yaw_deg\": " << waypoint.yaw_deg << "\n";
        json << "    }";
        if (i + 1 < mission.waypoints.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

bool MissionPlanWriter::WriteJsonFile(const SmokeTargetEvent &event,
                                      const MissionPlan &mission,
                                      const std::string &output_path)
{
    std::ofstream output(output_path.c_str(), std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << ToJson(event, mission);
    return output.good();
}

}  // namespace uav
