#include "uav/mission_plan_writer.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

bool Contains(const std::string &text, const std::string &needle)
{
    return text.find(needle) != std::string::npos;
}

size_t CountOccurrences(const std::string &text, const std::string &needle)
{
    size_t count = 0;
    size_t position = text.find(needle);
    while (position != std::string::npos) {
        ++count;
        position = text.find(needle, position + needle.size());
    }
    return count;
}

std::string ReadFile(const std::string &path)
{
    std::ifstream input(path.c_str(), std::ios::in);
    std::string content;
    std::string line;
    while (std::getline(input, line)) {
        content += line;
        content += '\n';
    }
    return content;
}

}  // namespace

int main()
{
    uav::SmokeTargetEvent event = {};
    event.timestamp_ms = 12345;
    event.confidence = 0.85;
    event.target_latitude_deg = 40.0;
    event.target_longitude_deg = 116.0;
    event.target_altitude_m = 10.0;
    event.north_offset_m = 1.0;
    event.east_offset_m = 2.0;
    event.horizontal_distance_m = 3.0;

    uav::MissionPlan mission;
    mission.waypoints.push_back({40.0001, 116.0, 120.0, 180.0});
    mission.waypoints.push_back({40.0, 116.0001, 120.0, -90.0});
    mission.waypoints.push_back({39.9999, 116.0, 120.0, 0.0});
    mission.waypoints.push_back({40.0, 115.9999, 120.0, 90.0});

    const std::string json = uav::MissionPlanWriter::ToJson(event, mission);

    assert(Contains(json, "\"event_type\""));
    assert(Contains(json, "\"smoke_detected\""));
    assert(Contains(json, "\"confidence\""));
    assert(Contains(json, "\"target\""));
    assert(Contains(json, "\"timestamp_ms\": 12345"));
    assert(Contains(json, "\"confidence\": 0.85"));

    assert(Contains(json, "\"waypoints\""));
    assert(Contains(json, "\"latitude_deg\""));
    assert(Contains(json, "\"longitude_deg\""));
    assert(Contains(json, "\"altitude_m\""));
    assert(Contains(json, "\"yaw_deg\""));
    assert(CountOccurrences(json, "\"yaw_deg\"") == 4);
    assert(Contains(json, "\"latitude_deg\": 40.0001"));
    assert(Contains(json, "\"longitude_deg\": 116.0001"));
    assert(Contains(json, "\"latitude_deg\": 39.9999"));
    assert(Contains(json, "\"longitude_deg\": 115.9999"));

    const std::string output_path = "mission_plan_writer_test_output.json";
    std::remove(output_path.c_str());
    assert(uav::MissionPlanWriter::WriteJsonFile(event, mission, output_path));

    const std::string file_content = ReadFile(output_path);
    assert(!file_content.empty());
    assert(Contains(file_content, "\"event_type\""));
    assert(std::remove(output_path.c_str()) == 0);
    return 0;
}
