#ifndef UAV_SMOKE_PLATFORM_VIDEO_SOURCE_HPP
#define UAV_SMOKE_PLATFORM_VIDEO_SOURCE_HPP

#include "uav/packets.hpp"

namespace uav {

class VideoSource {
public:
    virtual ~VideoSource() {}
    virtual bool NextPacket(VideoPacket *packet) = 0;
};

}  // namespace uav

#endif  // UAV_SMOKE_PLATFORM_VIDEO_SOURCE_HPP
