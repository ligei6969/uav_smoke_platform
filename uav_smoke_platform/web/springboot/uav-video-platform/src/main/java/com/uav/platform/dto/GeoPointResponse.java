package com.uav.platform.dto;

/** 地图上的烟雾目标或复查航点。 */
public record GeoPointResponse(
        String id,
        String label,
        double latitude,
        double longitude,
        Double altitudeM,
        Double confidence) {
}
