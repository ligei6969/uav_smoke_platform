#include <iostream>
#include <string>

#include "uav/mission_plan_writer.hpp"
#include "uav/mission_planner.hpp"
#include "uav/mock_telemetry_source.hpp"
#include "uav/smoke_event_builder.hpp"

namespace {

const char *kOutputPath = "mission_plan.json";

void PrintTelemetry(const uav::TelemetryPacket &telemetry)
{
    std::cout << "Telemetry:\n";
    std::cout << "- drone latitude: " << telemetry.latitudeDeg << "\n";
    std::cout << "- drone longitude: " << telemetry.longitudeDeg << "\n";
    std::cout << "- altitude / AGL: " << telemetry.altitudeM
              << " / " << telemetry.heightAboveGroundM << "\n";
    std::cout << "- gimbal pitch / roll / yaw: " << telemetry.gimbalPitchDeg
              << " / " << telemetry.gimbalRollDeg
              << " / " << telemetry.gimbalYawDeg << "\n";
}

void PrintDetection(const uav::DetectionResult &detection)
{
    std::cout << "Detection:\n";
    std::cout << "- confidence: " << detection.confidence << "\n";
    std::cout << "- center pixel: " << detection.centerXPx
              << ", " << detection.centerYPx << "\n";
}

void PrintSmokeTarget(const uav::SmokeTargetEvent &event)
{
    std::cout << "Smoke target:\n";
    std::cout << "- target latitude: " << event.target_latitude_deg << "\n";
    std::cout << "- target longitude: " << event.target_longitude_deg << "\n";
    std::cout << "- north offset: " << event.north_offset_m << "\n";
    std::cout << "- east offset: " << event.east_offset_m << "\n";
    std::cout << "- horizontal distance: " << event.horizontal_distance_m << "\n";
}

uav::DetectionResult MakeMockDetection(uint64_t timestamp_us)
{
    uav::DetectionResult detection = {};
    detection.timestampUs = timestamp_us;
    detection.className = "smoke";
    detection.confidence = 0.85;
    detection.centerXPx = 960.0;
    detection.centerYPx = 540.0;
    detection.widthPx = 80.0;
    detection.heightPx = 60.0;
    detection.imageWidthPx = 1920;
    detection.imageHeightPx = 1080;
    return detection;
}

}  // namespace

int main()
{
    std::cout << "Smoke mission demo started.\n\n";

    uav::MockTelemetrySource telemetry_source(1);
    uav::TelemetryPacket telemetry = {};
    if (!telemetry_source.NextTelemetry(&telemetry)) {
        std::cerr << "Failed to get mock telemetry data.\n";
        return 1;
    }

    const uav::DetectionResult detection = MakeMockDetection(telemetry.timestampUs);
    const target_geolocation::CameraModel camera = {84.0};
    const uav::SmokeEventBuilder event_builder(camera);

    uav::SmokeTargetEvent event = {};
    std::string error_message;
    if (!event_builder.Build(telemetry, detection, &event, &error_message)) {
        std::cerr << "Failed to build smoke target event: " << error_message << "\n";
        return 2;
    }

    const uav::MissionPlanner mission_planner;
    const uav::MissionPlan mission = mission_planner.BuildRecheckMission(event);

    if (!uav::MissionPlanWriter::WriteJsonFile(event, mission, kOutputPath)) {
        std::cerr << "Failed to write mission plan JSON: " << kOutputPath << "\n";
        return 3;
    }

    PrintTelemetry(telemetry);
    std::cout << "\n";
    PrintDetection(detection);
    std::cout << "\n";
    PrintSmokeTarget(event);
    std::cout << "\n";
    std::cout << "Mission:\n";
    std::cout << "- waypoint count: " << mission.waypoints.size() << "\n\n";
    std::cout << "Output:\n";
    std::cout << "- mission_plan.json path: " << kOutputPath << "\n";

    return 0;
}
