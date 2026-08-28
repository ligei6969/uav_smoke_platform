package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

/** 实时监控页面使用的地图提供商和底图配置。 */
@ConfigurationProperties(prefix = "app.map")
public class MapProperties {

    /** maplibre、google（预留）或 none。 */
    private String provider = "maplibre";
    /** osm 或 satellite。 */
    private String style = "osm";
    private String mapTilerApiKey = "";
    private String mapLibreModuleUrl =
            "https://unpkg.com/maplibre-gl@6.2.0/dist/maplibre-gl.mjs";
    private String mapLibreStylesheetUrl =
            "https://unpkg.com/maplibre-gl@6.2.0/dist/maplibre-gl.css";
    private String osmTileUrl = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    private String osmAttribution = "© OpenStreetMap contributors";
    private String mapTilerSatelliteStyleUrl =
            "https://api.maptiler.com/maps/satellite/style.json?key={key}";
    private double centerLatitude = 31.2304;
    private double centerLongitude = 121.4737;
    private double zoom = 15.0;

    public String getProvider() { return provider; }
    public void setProvider(String provider) { this.provider = provider; }
    public String getStyle() { return style; }
    public void setStyle(String style) { this.style = style; }
    public String getMapTilerApiKey() { return mapTilerApiKey; }
    public void setMapTilerApiKey(String mapTilerApiKey) {
        this.mapTilerApiKey = mapTilerApiKey;
    }
    public String getMapLibreModuleUrl() { return mapLibreModuleUrl; }
    public void setMapLibreModuleUrl(String mapLibreModuleUrl) {
        this.mapLibreModuleUrl = mapLibreModuleUrl;
    }
    public String getMapLibreStylesheetUrl() { return mapLibreStylesheetUrl; }
    public void setMapLibreStylesheetUrl(String mapLibreStylesheetUrl) {
        this.mapLibreStylesheetUrl = mapLibreStylesheetUrl;
    }
    public String getOsmTileUrl() { return osmTileUrl; }
    public void setOsmTileUrl(String osmTileUrl) { this.osmTileUrl = osmTileUrl; }
    public String getOsmAttribution() { return osmAttribution; }
    public void setOsmAttribution(String osmAttribution) {
        this.osmAttribution = osmAttribution;
    }
    public String getMapTilerSatelliteStyleUrl() { return mapTilerSatelliteStyleUrl; }
    public void setMapTilerSatelliteStyleUrl(String mapTilerSatelliteStyleUrl) {
        this.mapTilerSatelliteStyleUrl = mapTilerSatelliteStyleUrl;
    }
    public double getCenterLatitude() { return centerLatitude; }
    public void setCenterLatitude(double centerLatitude) {
        this.centerLatitude = centerLatitude;
    }
    public double getCenterLongitude() { return centerLongitude; }
    public void setCenterLongitude(double centerLongitude) {
        this.centerLongitude = centerLongitude;
    }
    public double getZoom() { return zoom; }
    public void setZoom(double zoom) { this.zoom = zoom; }
}
