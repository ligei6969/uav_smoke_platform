package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

/** 无真实无人机时使用的模拟遥测配置。 */
@ConfigurationProperties(prefix = "app.telemetry")
public class TelemetryProperties {

    private boolean enabled = true;
    private long intervalMs = 1000;
    private int maxTrackPoints = 600;
    private double centerLatitude = 31.2304;
    private double centerLongitude = 121.4737;

    public boolean isEnabled() { return enabled; }
    public void setEnabled(boolean enabled) { this.enabled = enabled; }
    public long getIntervalMs() { return intervalMs; }
    public void setIntervalMs(long intervalMs) { this.intervalMs = intervalMs; }
    public int getMaxTrackPoints() { return maxTrackPoints; }
    public void setMaxTrackPoints(int maxTrackPoints) {
        this.maxTrackPoints = maxTrackPoints;
    }
    public double getCenterLatitude() { return centerLatitude; }
    public void setCenterLatitude(double centerLatitude) {
        this.centerLatitude = centerLatitude;
    }
    public double getCenterLongitude() { return centerLongitude; }
    public void setCenterLongitude(double centerLongitude) {
        this.centerLongitude = centerLongitude;
    }
}
