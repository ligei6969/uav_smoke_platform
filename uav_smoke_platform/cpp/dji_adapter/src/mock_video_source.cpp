#include "uav/mock_video_source.hpp"

namespace uav {

MockVideoSource::MockVideoSource(uint32_t packetCount)
    : packetCount_(packetCount),
      nextSequence_(0)
{
}

bool MockVideoSource::NextPacket(VideoPacket *packet)
{
    if (packet == nullptr || nextSequence_ >= packetCount_) {
        return false;
    }

    packet->timestampUs = static_cast<uint64_t>(nextSequence_) * 33333;
    packet->sequenceNumber = nextSequence_;
    packet->isKeyFrame = (nextSequence_ % 30) == 0;
    packet->h264Data.clear();
    packet->h264Data.push_back(0x00);
    packet->h264Data.push_back(0x00);
    packet->h264Data.push_back(0x00);
    packet->h264Data.push_back(0x01);
    packet->h264Data.push_back(static_cast<uint8_t>(nextSequence_ & 0xFF));

    ++nextSequence_;
    return true;
}

}  // namespace uav
