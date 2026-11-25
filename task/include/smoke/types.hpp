#ifndef SMOKE_TYPES_HPP
#define SMOKE_TYPES_HPP

#include <cstdint>
#include <string>

namespace smoke {

enum class Lens { Unknown, Wide, Zoom, Infrared, Fpv };
enum class TimeBase { Replay, HostMonotonic };

struct FrameStamp {
    std::string source;
    std::uint64_t frame_id = 0;
    std::uint64_t stream_epoch = 0;
    double time_sec = 0.0;
    TimeBase time_base = TimeBase::Replay;
    Lens lens = Lens::Unknown;
};

struct BoundingBox {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double confidence = 0.0;
};

struct Detection {
    FrameStamp stamp;
    int width = 0;
    int height = 0;
    BoundingBox box;
};

struct TelemetrySample {
    std::string source;
    double time_sec = 0.0;
    TimeBase time_base = TimeBase::Replay;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
    double height_agl_m = 0.0;
    double pitch_deg = 0.0;
    double roll_deg = 0.0;
    double yaw_deg = 0.0; // Absolute camera orientation in global NED, not body-relative.
    bool gps_valid = false;
    bool height_valid = false;
    bool attitude_valid = false;
};

struct CameraCalibration {
    Lens lens = Lens::Unknown;
    double horizontal_fov_deg = 0.0;
    bool calibrated = false;
};

bool ValidateDetection(const Detection &detection, std::string *reason = nullptr);
bool ValidateTelemetry(const TelemetrySample &sample, std::string *reason = nullptr);
Lens ParseLens(const std::string &text);
std::string LensName(Lens lens);

} // namespace smoke
#endif
