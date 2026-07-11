#ifndef UAV_SMOKE_PLATFORM_MOCK_TELEMETRY_SOURCE_HPP
#define UAV_SMOKE_PLATFORM_MOCK_TELEMETRY_SOURCE_HPP

#include <cstdint>

#include "uav/telemetry_source.hpp"

namespace uav {

class MockTelemetrySource : public TelemetrySource {
public:
    explicit MockTelemetrySource(uint32_t packetCount);
    bool NextTelemetry(TelemetryPacket *packet) override;

private:
    uint32_t packetCount_;
    uint32_t nextSequence_;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_MOCK_TELEMETRY_SOURCE_HPP
