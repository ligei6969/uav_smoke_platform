#ifndef UAV_SMOKE_PLATFORM_MOCK_VIDEO_SOURCE_HPP
#define UAV_SMOKE_PLATFORM_MOCK_VIDEO_SOURCE_HPP

#include <cstdint>

#include "uav/video_source.hpp"

namespace uav {

class MockVideoSource : public VideoSource {
public:
    explicit MockVideoSource(uint32_t packetCount);
    bool NextPacket(VideoPacket *packet) override;

private:
    uint32_t packetCount_;
    uint32_t nextSequence_;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_MOCK_VIDEO_SOURCE_HPP
