package com.uav.platform.controller;

import com.uav.platform.config.MapProperties;
import com.uav.platform.dto.GeoPointResponse;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import com.uav.platform.service.TelemetryService;
import org.junit.jupiter.api.Test;

import java.time.Instant;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

class MonitoringControllerTest {

    @Test
    void exposesMapConfigurationAndTelemetryState() {
        TelemetryService telemetryService = mock(TelemetryService.class);
        TelemetrySampleResponse sample = new TelemetrySampleResponse(
                7L, Instant.parse("2026-08-27T00:00:00Z"),
                31.23, 121.47, 80, 90, -45, 12);
        TelemetrySnapshotResponse snapshot = new TelemetrySnapshotResponse(
                sample,
                List.of(sample),
                new GeoPointResponse(
                        "fire", "固定模拟着火点", 31.231, 121.471, 0.0, null),
                List.of(),
                List.of(),
                true);
        when(telemetryService.latest()).thenReturn(sample);
        when(telemetryService.snapshot()).thenReturn(snapshot);
        MonitoringApiController controller = new MonitoringApiController(
                new MapProperties(), telemetryService);

        assertThat(controller.mapConfiguration(null, null).data().provider())
                .isEqualTo("maplibre");
        assertThat(controller.mapConfiguration("none", "osm").data().provider())
                .isEqualTo("none");
        assertThat(controller.latest().data()).isSameAs(sample);
        assertThat(controller.snapshot().data()).isSameAs(snapshot);
    }

    @Test
    void rendersMonitoringPage() {
        assertThat(new MonitoringPageController().monitor()).isEqualTo("monitor");
    }
}
