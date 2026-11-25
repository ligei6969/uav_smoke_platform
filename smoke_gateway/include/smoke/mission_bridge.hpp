#ifndef SMOKE_MISSION_BRIDGE_HPP
#define SMOKE_MISSION_BRIDGE_HPP

#include <string>
#include <vector>

#include "smoke/packets.hpp"

namespace smoke {

// One accepted gateway event with geolocation and the recheck mission built
// from it. This mirrors uav::SmokeTargetEvent / MissionPlan shapes so the
// JSON stays compatible with visualize_mission.py.
struct BridgeMission
{
    std::string source;
    std::uint64_t frame_id = 0;
    std::int64_t timestamp_ms = 0;
    double confidence = 0.0;
    double target_latitude_deg = 0.0;
    double target_longitude_deg = 0.0;
    double target_altitude_m = 0.0;
    double north_offset_m = 0.0;
    double east_offset_m = 0.0;
    double horizontal_distance_m = 0.0;
    struct Waypoint
    {
        double latitude_deg = 0.0;
        double longitude_deg = 0.0;
        double altitude_m = 0.0;
        double yaw_deg = 0.0;
    };
    std::vector<Waypoint> waypoints;
};

struct BridgeTelemetry
{
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
    double height_above_ground_m = 0.0;
    double gimbal_pitch_deg = -90.0;
    double gimbal_roll_deg = 0.0;
    double gimbal_yaw_deg = 0.0;
    // Camera horizontal FOV at current zoom, degrees.
    double horizontal_fov_deg = 94.0;
    std::int64_t image_width = 1920;
    std::int64_t image_height = 1080;
};

// Converts an accepted gateway DETECT event into a geolocated smoke target
// and a rule-based recheck mission. Pure function: no I/O, fully testable.
// Returns false (with reason) when geolocation cannot produce a target.
bool build_bridge_mission(const DetectEvent& event, const BridgeTelemetry& telemetry,
                          BridgeMission& out, std::string& error);

// Serializes a mission in the same shape as the V0.1 mission_plan.json
// (event_type / timestamp_ms / confidence / target / waypoints).
std::string bridge_mission_to_json(const BridgeMission& mission);

}  // namespace smoke

#endif
