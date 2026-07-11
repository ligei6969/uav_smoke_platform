#include <iostream>

#include "uav/mock_telemetry_source.hpp"
#include "uav/mock_video_source.hpp"

int main()
{
    uav::MockVideoSource videoSource(5);
    uav::MockTelemetrySource telemetrySource(5);

    uav::VideoPacket videoPacket = {};
    uav::TelemetryPacket telemetryPacket = {};

    while (videoSource.NextPacket(&videoPacket) &&
           telemetrySource.NextTelemetry(&telemetryPacket)) {
        std::cout << "video seq=" << videoPacket.sequenceNumber
                  << " ts_us=" << videoPacket.timestampUs
                  << " h264_bytes=" << videoPacket.h264Data.size()
                  << " keyframe=" << (videoPacket.isKeyFrame ? "yes" : "no")
                  << " | telemetry ts_us=" << telemetryPacket.timestampUs
                  << " lat=" << telemetryPacket.latitudeDeg
                  << " lon=" << telemetryPacket.longitudeDeg
                  << " agl_m=" << telemetryPacket.heightAboveGroundM
                  << " gimbal_pitch=" << telemetryPacket.gimbalPitchDeg
                  << std::endl;
    }

    return 0;
}
