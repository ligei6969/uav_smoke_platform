package com.uav.platform.service;

import com.uav.platform.dto.TelemetrySampleResponse;
import org.springframework.stereotype.Component;

/** 将检测框中心的相机射线投影到局部地面；公式与 C++ 适配层保持一致。 */
@Component
public class TargetGeolocationCalculator {

    private static final double WGS84_SEMI_MAJOR_AXIS_M = 6378137.0;
    private static final double WGS84_ECCENTRICITY_SQUARED = 6.69437999014e-3;
    private static final double MINIMUM_DOWN_RAY_COMPONENT = 0.01;

    public TargetLocation calculate(
            TelemetrySampleResponse telemetry,
            double centerXPx,
            double centerYPx,
            int imageWidthPx,
            int imageHeightPx,
            double horizontalFovDeg) {
        validate(telemetry, centerXPx, centerYPx, imageWidthPx, imageHeightPx,
                horizontalFovDeg);

        double halfFovRad = Math.toRadians(horizontalFovDeg) * 0.5;
        double focalLengthPx = imageWidthPx / (2.0 * Math.tan(halfFovRad));
        double rayX = 1.0;
        double rayY = (centerXPx - imageWidthPx * 0.5) / focalLengthPx;
        double rayZ = (centerYPx - imageHeightPx * 0.5) / focalLengthPx;

        // 模拟遥测中云台横滚为 0，云台偏航使用同步样本的 headingDeg。
        double pitch = Math.toRadians(telemetry.gimbalPitchDeg());
        double yaw = Math.toRadians(telemetry.headingDeg());
        double pitchX = Math.cos(pitch) * rayX + Math.sin(pitch) * rayZ;
        double pitchY = rayY;
        double pitchZ = -Math.sin(pitch) * rayX + Math.cos(pitch) * rayZ;
        double rayNorth = Math.cos(yaw) * pitchX - Math.sin(yaw) * pitchY;
        double rayEast = Math.sin(yaw) * pitchX + Math.cos(yaw) * pitchY;
        double rayDown = pitchZ;
        if (!Double.isFinite(rayDown) || rayDown <= MINIMUM_DOWN_RAY_COMPONENT) {
            throw new IllegalArgumentException("相机射线未与地面相交");
        }

        // 当前模拟高度同时作为离地高度，地面目标高度为 0。
        double scale = telemetry.altitudeM() / rayDown;
        double north = rayNorth * scale;
        double east = rayEast * scale;
        double latitudeRad = Math.toRadians(telemetry.latitude());
        double sinLatitude = Math.sin(latitudeRad);
        double curvature = Math.sqrt(1.0 - WGS84_ECCENTRICITY_SQUARED
                * sinLatitude * sinLatitude);
        double primeVerticalRadius = WGS84_SEMI_MAJOR_AXIS_M / curvature;
        double meridianRadius = WGS84_SEMI_MAJOR_AXIS_M
                * (1.0 - WGS84_ECCENTRICITY_SQUARED)
                / (curvature * curvature * curvature);
        double longitudeScale = (primeVerticalRadius + telemetry.altitudeM())
                * Math.cos(latitudeRad);
        if (Math.abs(longitudeScale) < 1.0) {
            throw new IllegalArgumentException("极区经度计算不稳定");
        }

        double latitude = telemetry.latitude()
                + Math.toDegrees(north / (meridianRadius + telemetry.altitudeM()));
        double longitude = telemetry.longitude()
                + Math.toDegrees(east / longitudeScale);
        return new TargetLocation(
                latitude,
                longitude,
                0.0,
                north,
                east,
                Math.hypot(north, east));
    }

    private void validate(
            TelemetrySampleResponse telemetry,
            double centerXPx,
            double centerYPx,
            int imageWidthPx,
            int imageHeightPx,
            double horizontalFovDeg) {
        if (telemetry == null || imageWidthPx <= 1 || imageHeightPx <= 1) {
            throw new IllegalArgumentException("遥测或图像尺寸无效");
        }
        if (!Double.isFinite(horizontalFovDeg)
                || horizontalFovDeg <= 1.0 || horizontalFovDeg >= 179.0) {
            throw new IllegalArgumentException("相机水平视场角无效");
        }
        if (!Double.isFinite(telemetry.latitude())
                || !Double.isFinite(telemetry.longitude())
                || !Double.isFinite(telemetry.altitudeM())
                || telemetry.latitude() < -90.0 || telemetry.latitude() > 90.0
                || telemetry.longitude() < -180.0 || telemetry.longitude() > 180.0
                || telemetry.altitudeM() <= 0.5) {
            throw new IllegalArgumentException("无人机位置或高度无效");
        }
        if (!Double.isFinite(centerXPx) || !Double.isFinite(centerYPx)
                || centerXPx < 0.0 || centerXPx > imageWidthPx
                || centerYPx < 0.0 || centerYPx > imageHeightPx) {
            throw new IllegalArgumentException("检测框中心超出图像范围");
        }
    }

    public record TargetLocation(
            double latitude,
            double longitude,
            double altitudeM,
            double northOffsetM,
            double eastOffsetM,
            double horizontalDistanceM) {
    }
}
