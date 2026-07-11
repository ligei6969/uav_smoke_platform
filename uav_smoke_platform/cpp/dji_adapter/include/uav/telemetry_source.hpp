#ifndef UAV_SMOKE_PLATFORM_TELEMETRY_SOURCE_HPP
#define UAV_SMOKE_PLATFORM_TELEMETRY_SOURCE_HPP

#include "uav/packets.hpp"

namespace uav {

class TelemetrySource {
public:
    virtual ~TelemetrySource() {}
    virtual bool NextTelemetry(TelemetryPacket *packet) = 0;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_TELEMETRY_SOURCE_HPP
