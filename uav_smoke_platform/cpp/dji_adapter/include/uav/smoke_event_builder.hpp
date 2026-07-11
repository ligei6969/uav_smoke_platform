#ifndef UAV_SMOKE_PLATFORM_SMOKE_EVENT_BUILDER_HPP
#define UAV_SMOKE_PLATFORM_SMOKE_EVENT_BUILDER_HPP

#include <string>

#include "uav/packets.hpp"
#include "uav/smoke_event.hpp"
#include "uav/target_geolocation.hpp"

namespace uav {

class SmokeEventBuilder {
public:
    explicit SmokeEventBuilder(const target_geolocation::CameraModel &camera);

    bool Build(const TelemetryPacket &telemetry,
               const DetectionResult &detection,
               SmokeTargetEvent *event,
               std::string *errorMessage) const;

private:
    target_geolocation::CameraModel camera_;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_SMOKE_EVENT_BUILDER_HPP
