package com.uav.platform.dto;

import tools.jackson.databind.annotation.JsonSerialize;
import tools.jackson.databind.ser.std.ToStringSerializer;

import java.util.List;

/** 某个视频时刻的检测框、同步遥测、目标定位和复查航点。 */
public record VideoGeolocationResponse(
        @JsonSerialize(using = ToStringSerializer.class)
        Long videoId,
        @JsonSerialize(using = ToStringSerializer.class)
        Long analysisAttemptId,
        double videoTimeSec,
        String status,
        boolean detected,
        boolean geolocated,
        Integer frameId,
        Double detectionTimeSec,
        Double confidence,
        DetectionBoxResponse detectionBox,
        TelemetrySampleResponse telemetry,
        GeoPointResponse target,
        List<GeoPointResponse> missionWaypoints,
        Double northOffsetM,
        Double eastOffsetM,
        Double horizontalDistanceM,
        Double referenceErrorM,
        boolean simulation) {

    public record DetectionBoxResponse(
            double left,
            double top,
            double right,
            double bottom,
            int imageWidth,
            int imageHeight) {
    }
}
