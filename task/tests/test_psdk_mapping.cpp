#include "smoke/dji_telemetry.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

int main()
{
    try {
        smoke::DjiSnapshot raw;
        raw.source = "h30t-1";
        raw.gps.x = 1200000000; raw.gps.y = 300000000; raw.gps.z = 120000;
        raw.quality.fixState = DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_3D_FIX;
        raw.quality.totalSatelliteNumberUsed = 10;
        raw.angles.x = -45; raw.angles.y = 2; raw.angles.z = 90;
        raw.height_agl_m = 100;
        raw.gps_time_sec = raw.quality_time_sec = raw.attitude_time_sec = raw.height_time_sec = 1;
        raw.clock_mapping_valid = true; raw.height_measurement_valid = true;
        auto result = smoke::ConvertDjiSnapshot(raw, .2);
        CHECK(std::abs(result.latitude_deg - 30) < 1e-8);
        CHECK(std::abs(result.longitude_deg - 120) < 1e-8);
        CHECK(result.altitude_m == 120 && result.pitch_deg == -45 && result.roll_deg == 2 && result.yaw_deg == 90);
        CHECK(smoke::ValidateTelemetry(result));
        raw.height_measurement_valid = false;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).height_valid);
        raw.height_measurement_valid = true; raw.attitude_time_sec = 2;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).gps_valid);
        raw.attitude_time_sec = 1; raw.clock_mapping_valid = false;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).attitude_valid);
        raw.clock_mapping_valid = true; raw.quality.fixState = DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_2D_FIX;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).gps_valid);
        raw.quality.fixState = DJI_FC_SUBSCRIPTION_GPS_FIX_STATE_3D_FIX;
        const auto valid_raw = raw;
        raw.angles.x = std::numeric_limits<float>::quiet_NaN();
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).attitude_valid);
        raw = valid_raw; raw.angles.z = 361;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).attitude_valid);
        raw = valid_raw; raw.height_agl_m = std::numeric_limits<double>::infinity();
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).height_valid);
        raw = valid_raw; raw.height_agl_m = .5;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).height_valid);
        raw = valid_raw; raw.gps.y = 1000000000;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).gps_valid);
        raw = valid_raw; raw.gps.x = 2000000000;
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).gps_valid);
        raw = valid_raw; raw.time_base = static_cast<smoke::TimeBase>(99);
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).attitude_valid);
        raw = valid_raw; raw.quality_time_sec = std::numeric_limits<double>::quiet_NaN();
        CHECK(!smoke::ConvertDjiSnapshot(raw, .2).gps_valid);
        std::cout << "PSDK field mapping checks passed (no SDK calls or hardware)\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
