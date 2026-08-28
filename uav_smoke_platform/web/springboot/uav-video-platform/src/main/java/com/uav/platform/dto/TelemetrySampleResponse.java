package com.uav.platform.dto;

import java.time.Instant;

/** 浏览器收到的一帧无人机遥测。 */
public record TelemetrySampleResponse(
        long sequence,
        Instant timestamp,
        double latitude,
        double longitude,
        double altitudeM,
        double headingDeg,
        double gimbalPitchDeg,
        double speedMps) {
}
