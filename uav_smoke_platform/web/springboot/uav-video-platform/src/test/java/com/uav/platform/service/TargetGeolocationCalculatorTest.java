package com.uav.platform.service;

import com.uav.platform.dto.TelemetrySampleResponse;
import org.junit.jupiter.api.Test;

import java.time.Instant;

import static org.assertj.core.api.Assertions.assertThat;

class TargetGeolocationCalculatorTest {

    @Test
    void projectsCenteredDetectionToGroundAlongCameraHeading() {
        TelemetrySampleResponse telemetry = new TelemetrySampleResponse(
                1L,
                Instant.parse("2026-01-01T00:00:01Z"),
                31.23,
                121.47,
                100.0,
                0.0,
                -45.0,
                12.0);

        var target = new TargetGeolocationCalculator().calculate(
                telemetry,
                500.0,
                250.0,
                1000,
                500,
                84.0);

        assertThat(target.horizontalDistanceM()).isCloseTo(100.0, within(0.01));
        assertThat(target.northOffsetM()).isCloseTo(100.0, within(0.01));
        assertThat(target.eastOffsetM()).isCloseTo(0.0, within(0.01));
        assertThat(target.latitude()).isGreaterThan(telemetry.latitude());
        assertThat(target.longitude()).isCloseTo(telemetry.longitude(), within(1e-8));
        assertThat(target.altitudeM()).isZero();
    }

    private static org.assertj.core.data.Offset<Double> within(double value) {
        return org.assertj.core.data.Offset.offset(value);
    }
}
