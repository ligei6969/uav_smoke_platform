package com.uav.platform.dto;

import com.uav.platform.config.MapProperties;

import java.util.Locale;
import java.util.Set;

/** 浏览器使用的地图配置，只返回当前提供者初始化所需的字段。 */
public record MapConfigurationResponse(
        String provider,
        String requestedStyle,
        String effectiveStyle,
        String styleUrl,
        String tileUrl,
        String attribution,
        String mapLibreModuleUrl,
        String mapLibreStylesheetUrl,
        double centerLatitude,
        double centerLongitude,
        double zoom,
        String warning) {

    private static final Set<String> PROVIDERS = Set.of("maplibre", "google", "none");

    public static MapConfigurationResponse from(MapProperties properties) {
        return from(properties, null, null);
    }

    /**
     * 根据页面选择生成一次性配置；空参数继续使用服务端默认值。
     */
    public static MapConfigurationResponse from(
            MapProperties properties,
            String providerOverride,
            String styleOverride) {
        String provider = normalized(
                firstNonBlank(providerOverride, properties.getProvider()),
                "maplibre");
        if (!PROVIDERS.contains(provider)) {
            provider = "none";
        }
        String requestedStyle = normalized(
                firstNonBlank(styleOverride, properties.getStyle()),
                "osm");
        String effectiveStyle = requestedStyle;
        String styleUrl = null;
        String warning = null;

        if ("google".equals(provider)) {
            warning = "Google 地图适配器尚未启用，已切换为坐标面板";
        } else if ("maplibre".equals(provider) && "satellite".equals(requestedStyle)) {
            String key = properties.getMapTilerApiKey();
            if (key == null || key.isBlank()) {
                effectiveStyle = "osm";
                warning = "未配置 MAPTILER_API_KEY，已使用 OpenStreetMap 普通地图";
            } else {
                styleUrl = properties.getMapTilerSatelliteStyleUrl()
                        .replace("{key}", key.trim());
            }
        } else if (!"osm".equals(requestedStyle)) {
            effectiveStyle = "osm";
            warning = "未知地图样式，已使用 OpenStreetMap 普通地图";
        }

        return new MapConfigurationResponse(
                provider,
                requestedStyle,
                effectiveStyle,
                styleUrl,
                properties.getOsmTileUrl(),
                properties.getOsmAttribution(),
                properties.getMapLibreModuleUrl(),
                properties.getMapLibreStylesheetUrl(),
                properties.getCenterLatitude(),
                properties.getCenterLongitude(),
                properties.getZoom(),
                warning);
    }

    private static String firstNonBlank(String preferred, String fallback) {
        return preferred == null || preferred.isBlank() ? fallback : preferred;
    }

    private static String normalized(String value, String fallback) {
        return value == null || value.isBlank()
                ? fallback
                : value.trim().toLowerCase(Locale.ROOT);
    }
}
