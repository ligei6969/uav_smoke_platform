package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

/** 视频检测框与同步遥测联合定位时使用的相机和任务参数。 */
@ConfigurationProperties(prefix = "app.geolocation")
public class GeolocationProperties {

    private double horizontalFovDeg = 84.0;
    private double detectionWindowSec = 0.75;
    private double recheckRadiusM = 50.0;
    private double recheckAltitudeM = 65.0;
    private long maxDetectionArtifactBytes = 50L * 1024L * 1024L;

    public double getHorizontalFovDeg() { return horizontalFovDeg; }
    public void setHorizontalFovDeg(double horizontalFovDeg) {
        this.horizontalFovDeg = horizontalFovDeg;
    }
    public double getDetectionWindowSec() { return detectionWindowSec; }
    public void setDetectionWindowSec(double detectionWindowSec) {
        this.detectionWindowSec = detectionWindowSec;
    }
    public double getRecheckRadiusM() { return recheckRadiusM; }
    public void setRecheckRadiusM(double recheckRadiusM) {
        this.recheckRadiusM = recheckRadiusM;
    }
    public double getRecheckAltitudeM() { return recheckAltitudeM; }
    public void setRecheckAltitudeM(double recheckAltitudeM) {
        this.recheckAltitudeM = recheckAltitudeM;
    }
    public long getMaxDetectionArtifactBytes() { return maxDetectionArtifactBytes; }
    public void setMaxDetectionArtifactBytes(long maxDetectionArtifactBytes) {
        this.maxDetectionArtifactBytes = maxDetectionArtifactBytes;
    }
}
