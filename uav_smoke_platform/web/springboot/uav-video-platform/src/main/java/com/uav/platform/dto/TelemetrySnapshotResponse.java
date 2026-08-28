package com.uav.platform.dto;

import java.util.List;

/** 实时监控页面首次加载所需的完整状态。 */
public record TelemetrySnapshotResponse(
        TelemetrySampleResponse latest,
        List<TelemetrySampleResponse> track,
        GeoPointResponse firePoint,
        List<GeoPointResponse> smokeTargets,
        List<GeoPointResponse> missionWaypoints,
        boolean simulation) {
}
