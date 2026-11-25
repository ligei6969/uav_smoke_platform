#ifndef SMOKE_PACKETS_HPP
#define SMOKE_PACKETS_HPP

#include <cstdint>
#include <string>

namespace smoke {

struct DetectEvent
{
    std::string source;
    std::uint64_t frame_id = 0;
    std::int64_t timestamp_ms = 0;
    double confidence = 0.0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    std::int64_t frame_width = 0;
    std::int64_t frame_height = 0;
};

}

#endif
