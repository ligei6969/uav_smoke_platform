#include "smoke/types.hpp"

#include <cmath>

namespace smoke {
namespace {
bool Fail(const char *message, std::string *reason)
{
    if (reason) { *reason = message; }
    return false;
}

bool ValidSource(const std::string &source)
{
    if (source.empty() || source.size() > 64) { return false; }
    for (unsigned char c : source) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}
}

bool ValidateDetection(const Detection &d, std::string *reason)
{
    if (!ValidSource(d.stamp.source)) { return Fail("invalid_source", reason); }
    if (!std::isfinite(d.stamp.time_sec) || d.stamp.time_sec < 0.0) {
        return Fail("invalid_frame_time", reason);
    }
    if (d.width < 2 || d.height < 2 || d.width > 16384 || d.height > 16384) {
        return Fail("invalid_dimensions", reason);
    }
    const auto &b = d.box;
    if (!std::isfinite(b.x1) || !std::isfinite(b.y1) || !std::isfinite(b.x2) ||
        !std::isfinite(b.y2) || !std::isfinite(b.confidence) ||
        b.confidence < 0.0 || b.confidence > 1.0 || b.x1 < 0.0 || b.y1 < 0.0 ||
        b.x2 > d.width || b.y2 > d.height || b.x2 <= b.x1 || b.y2 <= b.y1) {
        return Fail("invalid_box", reason);
    }
    if (reason) { reason->clear(); }
    return true;
}

bool ValidateTelemetry(const TelemetrySample &s, std::string *reason)
{
    if (!ValidSource(s.source)) { return Fail("invalid_source", reason); }
    if (!std::isfinite(s.time_sec) || s.time_sec < 0.0) {
        return Fail("invalid_telemetry_time", reason);
    }
    if (!s.gps_valid) { return Fail("gps_invalid", reason); }
    if (!s.height_valid) { return Fail("height_invalid", reason); }
    if (!s.attitude_valid) { return Fail("attitude_invalid", reason); }
    if (!std::isfinite(s.latitude_deg) || !std::isfinite(s.longitude_deg) ||
        !std::isfinite(s.altitude_m) || s.latitude_deg < -90.0 ||
        s.latitude_deg > 90.0 || s.longitude_deg < -180.0 || s.longitude_deg > 180.0) {
        return Fail("invalid_gps", reason);
    }
    if (!std::isfinite(s.height_agl_m) || s.height_agl_m <= 0.5 || s.height_agl_m > 10000.0) {
        return Fail("invalid_height", reason);
    }
    if (!std::isfinite(s.pitch_deg) || !std::isfinite(s.roll_deg) || !std::isfinite(s.yaw_deg)) {
        return Fail("invalid_attitude", reason);
    }
    if (reason) { reason->clear(); }
    return true;
}

Lens ParseLens(const std::string &text)
{
    if (text == "wide") { return Lens::Wide; }
    if (text == "zoom") { return Lens::Zoom; }
    if (text == "ir") { return Lens::Infrared; }
    if (text == "fpv") { return Lens::Fpv; }
    return Lens::Unknown;
}

std::string LensName(Lens lens)
{
    switch (lens) {
        case Lens::Wide: return "wide";
        case Lens::Zoom: return "zoom";
        case Lens::Infrared: return "ir";
        case Lens::Fpv: return "fpv";
        default: return "unknown";
    }
}
} // namespace smoke
