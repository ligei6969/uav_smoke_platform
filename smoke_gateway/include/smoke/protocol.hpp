#ifndef SMOKE_PROTOCOL_HPP
#define SMOKE_PROTOCOL_HPP

#include <cstdint>
#include <string>

#include "smoke/packets.hpp"

namespace smoke {

// Line protocol (ASCII, one request per line, one JSON object per response):
//   DETECT <source> <frame_id> <timestamp_ms> <confidence> <x> <y> <w> <h> <frame_w> <frame_h>\n
//   PING\n
//   STATS\n
// Response statuses: ok | accepted | duplicate | stale | busy | invalid | error
// Non-finite numbers, unknown verbs and over-long lines are "invalid".

constexpr std::size_t kMaxLineLength = 4096;

bool parse_detect(const std::string& line, DetectEvent& out);

std::string event_key(const DetectEvent& event);

}  // namespace smoke

#endif
