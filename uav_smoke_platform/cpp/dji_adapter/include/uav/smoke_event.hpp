#ifndef UAV_SMOKE_PLATFORM_SMOKE_EVENT_HPP
#define UAV_SMOKE_PLATFORM_SMOKE_EVENT_HPP

#include <cstdint>

namespace uav {

struct SmokeTargetEvent {
    uint64_t timestamp_ms;
    double confidence;
    double target_latitude_deg;
    double target_longitude_deg;
    double target_altitude_m;
    double north_offset_m;
    double east_offset_m;
    double horizontal_distance_m;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_SMOKE_EVENT_HPP
