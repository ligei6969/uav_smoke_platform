#include "smoke/dji_telemetry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smoke {
TelemetrySample ConvertDjiSnapshot(const DjiSnapshot &s, double max_skew_sec)
{
    if (!std::isfinite(max_skew_sec) || max_skew_sec < 0 || max_skew_sec > 10) {
        throw std::invalid_argument("invalid_dji_max_skew");
    }
    TelemetrySample result;
    result.source = s.source;
    result.time_sec = s.gps_time_sec;
    result.time_base = s.time_base;
    // PSDK 3.10.0 dji_fc_subscription.h: GPS x=longitude,y=latitude in deg*1e7; z in mm.
    result.latitude_deg = s.gps.y * 1.0e-7;
    result.longitude_deg = s.gps.x * 1.0e-7;
    result.altitude_m = s.gps.z * 1.0e-3;
    // The SDK documents x=pitch,y=roll,z=yaw in global NED, all in degrees.
    result.pitch_deg = s.angles.x;
    result.roll_deg = s.angles.y;
    result.yaw_deg = s.angles.z;
    result.height_agl_m = s.height_agl_m;
    bool coherent = s.clock_mapping_valid &&
                    (s.time_base == TimeBase::Replay || s.time_base == TimeBase::HostMonotonic);
    const double times[] = {s.gps_time_sec, s.quality_time_sec, s.attitude_time_sec, s.height_time_sec};
    for (double t : times) { coherent = coherent && std::isfinite(t) && t >= 0; }
    if (coherent) {
        const auto limits = std::minmax_element(times, times + 4);
        coherent = *limits.second - *limits.first <= max_skew_sec;
    }
    const bool fix_ok = s.quality.fixState == DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_3D_FIX ||
                        s.quality.fixState == DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_GPS_PLUS_DEAD_RECKONING;
    const bool gps_numeric_valid = std::isfinite(result.latitude_deg) &&
        std::isfinite(result.longitude_deg) && std::isfinite(result.altitude_m) &&
        std::fabs(result.latitude_deg) <= 90.0 && std::fabs(result.longitude_deg) <= 180.0 &&
        result.altitude_m >= -1000.0 && result.altitude_m <= 100000.0;
    const bool height_numeric_valid = std::isfinite(result.height_agl_m) &&
        result.height_agl_m > 0.5 && result.height_agl_m <= 10000.0;
    const bool attitude_numeric_valid = std::isfinite(result.pitch_deg) &&
        std::isfinite(result.roll_deg) && std::isfinite(result.yaw_deg) &&
        std::fabs(result.pitch_deg) <= 180.0 && std::fabs(result.roll_deg) <= 180.0 &&
        std::fabs(result.yaw_deg) <= 360.0;
    result.gps_valid = coherent && fix_ok && s.quality.totalSatelliteNumberUsed >= 6 && gps_numeric_valid;
    result.height_valid = coherent && s.height_measurement_valid && height_numeric_valid;
    result.attitude_valid = coherent && attitude_numeric_valid;
    return result;
}
}
