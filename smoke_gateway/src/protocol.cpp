#include "smoke/protocol.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace smoke {
namespace {

bool parse_double(const std::string& token, double& out)
{
    if (token.empty() || token.size() > 64)
    {
        return false;
    }
    for (char c : token)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!((u >= '0' && u <= '9') || c == '.' || c == '-' || c == '+' ||
              c == 'e' || c == 'E'))
        {
            return false;
        }
    }
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value))
    {
        return false;
    }
    out = value;
    return true;
}

bool parse_int64(const std::string& token, std::int64_t& out)
{
    if (token.empty() || token.size() > 20)
    {
        return false;
    }
    std::size_t index = (token[0] == '-') ? 1 : 0;
    if (index >= token.size())
    {
        return false;
    }
    for (std::size_t i = index; i < token.size(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
    }
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(token.c_str(), &end, 10);
    if (end != token.c_str() + token.size() || errno == ERANGE)
    {
        return false;
    }
    out = static_cast<std::int64_t>(value);
    return true;
}

bool parse_u64(const std::string& token, std::uint64_t& out)
{
    std::int64_t value = 0;
    if (!parse_int64(token, value) || value < 0)
    {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

}  // namespace

bool parse_detect(const std::string& line, DetectEvent& out)
{
    // DETECT source frame_id timestamp_ms confidence x y w h frame_w frame_h
    std::string tokens[11];
    std::size_t count = 0;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t space = line.find(' ', start);
        if (space == std::string::npos)
        {
            if (count != 10 || start >= line.size())
            {
                return false;
            }
            tokens[count++] = line.substr(start);
            break;
        }
        if (count >= 10 || space == start)
        {
            return false;
        }
        tokens[count++] = line.substr(start, space - start);
        start = space + 1;
    }
    if (count != 11 || tokens[0] != "DETECT")
    {
        return false;
    }
    DetectEvent event;
    event.source = tokens[1];
    if (event.source.empty() || event.source.size() > 64)
    {
        return false;
    }
    for (char c : event.source)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool allowed = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                             (u >= '0' && u <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed)
        {
            return false;
        }
    }
    if (!parse_u64(tokens[2], event.frame_id) ||
        !parse_int64(tokens[3], event.timestamp_ms) ||
        !parse_double(tokens[4], event.confidence) ||
        !parse_double(tokens[5], event.x) ||
        !parse_double(tokens[6], event.y) ||
        !parse_double(tokens[7], event.w) ||
        !parse_double(tokens[8], event.h) ||
        !parse_int64(tokens[9], event.frame_width) ||
        !parse_int64(tokens[10], event.frame_height))
    {
        return false;
    }
    if (event.frame_width <= 0 || event.frame_height <= 0 ||
        event.confidence < 0.0 || event.confidence > 1.0)
    {
        return false;
    }
    out = event;
    return true;
}

std::string event_key(const DetectEvent& event)
{
    // source + frame_id uniquely identifies a detection; retries are idempotent.
    return event.source + ":" + std::to_string(event.frame_id);
}

}  // namespace smoke
