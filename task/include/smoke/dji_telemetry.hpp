#ifndef SMOKE_DJI_TELEMETRY_HPP
#define SMOKE_DJI_TELEMETRY_HPP

#include "smoke/types.hpp"
#include "dji_fc_subscription.h"

namespace smoke {
// Filled by an eventual PSDK subscriber. All times MUST already use one clock.
struct DjiSnapshot {
    std::string source;
    TimeBase time_base = TimeBase::HostMonotonic;
    T_DjiFcSubscriptionGpsPosition gps = {};
    T_DjiFcSubscriptionGpsDetails quality = {};
    T_DjiFcSubscriptionGimbalAngles angles = {};
    double height_agl_m = 0;
    double gps_time_sec = 0;
    double quality_time_sec = 0;
    double attitude_time_sec = 0;
    double height_time_sec = 0;
    bool clock_mapping_valid = false;
    bool height_measurement_valid = false; // Cannot infer this from HEIGHT_FUSION alone.
};

TelemetrySample ConvertDjiSnapshot(const DjiSnapshot &snapshot, double max_skew_sec);
}
#endif
