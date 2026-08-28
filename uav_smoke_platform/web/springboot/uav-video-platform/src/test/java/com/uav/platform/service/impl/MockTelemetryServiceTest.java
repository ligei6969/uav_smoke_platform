package com.uav.platform.service.impl;

import com.uav.platform.config.TelemetryProperties;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThat;

class MockTelemetryServiceTest {

    @Test
    void generatesMovingTelemetryAndCapsTrackHistory() {
        TelemetryProperties properties = new TelemetryProperties();
        properties.setMaxTrackPoints(3);
        MockTelemetryService service = new MockTelemetryService(properties);
        TelemetrySampleResponse initial = service.latest();
        var initialFirePoint = service.snapshot().firePoint();

        for (int index = 0; index < 5; index++) {
            service.publishNext();
        }

        TelemetrySnapshotResponse snapshot = service.snapshot();
        assertThat(snapshot.latest().sequence()).isEqualTo(5);
        assertThat(snapshot.latest().latitude()).isNotEqualTo(initial.latitude());
        assertThat(snapshot.track()).hasSize(3);
        assertThat(snapshot.track()).extracting(TelemetrySampleResponse::sequence)
                .containsExactly(3L, 4L, 5L);
        assertThat(snapshot.firePoint().label()).isEqualTo("固定模拟着火点");
        assertThat(snapshot.firePoint()).isEqualTo(initialFirePoint);
        assertThat(snapshot.smokeTargets()).hasSize(2);
        assertThat(snapshot.missionWaypoints()).hasSize(4);
        assertThat(snapshot.simulation()).isTrue();
    }

    @Test
    void canPauseSimulationWithoutChangingLatestSample() {
        TelemetryProperties properties = new TelemetryProperties();
        properties.setEnabled(false);
        MockTelemetryService service = new MockTelemetryService(properties);

        service.publishNext();

        assertThat(service.latest().sequence()).isZero();
        assertThat(service.snapshot().track()).hasSize(1);
    }
}
