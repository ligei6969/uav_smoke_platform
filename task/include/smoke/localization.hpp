#ifndef SMOKE_LOCALIZATION_HPP
#define SMOKE_LOCALIZATION_HPP

#include "smoke/telemetry.hpp"

namespace smoke {

struct GeoResult {
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double altitude_m = 0.0;
    double north_m = 0.0;
    double east_m = 0.0;
    std::string reason;
};

// Projects the box bottom-center onto a FLAT ground plane. This is an assumed
// ground projection, not a proven smoke origin or fire-source coordinate.
// Requires an explicit calibration for the active lens and a valid matched
// telemetry sample. Unknown/FPV cameras are deliberately unsupported.
GeoResult Locate(const Detection &detection, const TelemetryMatch &telemetry,
                 const CameraCalibration &camera);

} // namespace smoke
#endif
