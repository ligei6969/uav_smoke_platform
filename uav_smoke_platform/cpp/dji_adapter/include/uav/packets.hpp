#ifndef UAV_SMOKE_PLATFORM_PACKETS_HPP
#define UAV_SMOKE_PLATFORM_PACKETS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace uav {

struct VideoPacket {
    uint64_t timestampUs;
    uint32_t sequenceNumber;
    bool isKeyFrame;
    std::vector<uint8_t> h264Data;
};

struct TelemetryPacket {
    uint64_t timestampUs;
    double latitudeDeg;
    double longitudeDeg;
    double altitudeM;
    double heightAboveGroundM;
    double gimbalPitchDeg;
    double gimbalRollDeg;
    double gimbalYawDeg;
    bool gpsValid;
};

struct DetectionResult {
    uint64_t timestampUs;
    std::string className;
    double confidence;
    double centerXPx;
    double centerYPx;
    double widthPx;
    double heightPx;
    int imageWidthPx;
    int imageHeightPx;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_PACKETS_HPP
