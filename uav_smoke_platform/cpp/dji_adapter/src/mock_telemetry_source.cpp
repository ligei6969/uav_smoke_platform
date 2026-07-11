#include "uav/mock_telemetry_source.hpp"

namespace uav {

MockTelemetrySource::MockTelemetrySource(uint32_t packetCount)
    : packetCount_(packetCount),
      nextSequence_(0)
{
}

bool MockTelemetrySource::NextTelemetry(TelemetryPacket *packet)
{
    if (packet == nullptr || nextSequence_ >= packetCount_) {
        return false;
    }

    packet->timestampUs = static_cast<uint64_t>(nextSequence_) * 33333;
    packet->latitudeDeg = 30.0 + static_cast<double>(nextSequence_) * 0.000001;
    packet->longitudeDeg = 120.0 + static_cast<double>(nextSequence_) * 0.000001;
    packet->altitudeM = 100.0;
    packet->heightAboveGroundM = 100.0;
    packet->gimbalPitchDeg = -90.0;
    packet->gimbalRollDeg = 0.0;
    packet->gimbalYawDeg = 0.0;
    packet->gpsValid = true;

    ++nextSequence_;
    return true;
}

}  // namespace uav
