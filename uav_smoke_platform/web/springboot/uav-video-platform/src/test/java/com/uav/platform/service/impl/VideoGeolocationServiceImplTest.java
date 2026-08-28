package com.uav.platform.service.impl;

import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.config.GeolocationProperties;
import com.uav.platform.dto.GeoPointResponse;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import com.uav.platform.dto.VideoAnalysisResponse;
import com.uav.platform.service.TargetGeolocationCalculator;
import com.uav.platform.service.TelemetryService;
import com.uav.platform.service.VideoAnalysisService;
import org.junit.jupiter.api.Test;
import tools.jackson.databind.ObjectMapper;

import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.anyDouble;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class VideoGeolocationServiceImplTest {

    @Test
    void synchronizesDetectionTelemetryAndCachesAnalysisArtifact() {
        VideoAnalysisService analysisService = mock(VideoAnalysisService.class);
        TelemetryService telemetryService = mock(TelemetryService.class);
        VideoAnalysisResponse analysis = mock(VideoAnalysisResponse.class);
        when(analysis.status()).thenReturn("COMPLETED");
        when(analysis.attemptId()).thenReturn(88L);
        when(analysisService.getLatest(42L)).thenReturn(analysis);

        String json = """
                {
                  "fps": 10.0,
                  "width": 1000,
                  "height": 500,
                  "detections": [
                    {
                      "frame_id": 10,
                      "timestamp_sec": 1.0,
                      "boxes": [
                        {"bbox": [450.0, 200.0, 550.0, 300.0], "confidence": 0.91}
                      ]
                    }
                  ]
                }
                """;
        when(analysisService.loadArtifact(42L, "detections", null))
                .thenAnswer(_invocation -> new RemoteArtifact(
                        new ByteArrayInputStream(json.getBytes(StandardCharsets.UTF_8)),
                        200,
                        "application/json",
                        json.length(),
                        null,
                        "bytes"));

        TelemetrySampleResponse telemetry = new TelemetrySampleResponse(
                1L,
                Instant.parse("2026-01-01T00:00:01Z"),
                31.23,
                121.47,
                100.0,
                0.0,
                -45.0,
                12.0);
        when(telemetryService.sampleAtVideoTime(anyDouble())).thenReturn(telemetry);
        when(telemetryService.snapshot()).thenReturn(new TelemetrySnapshotResponse(
                telemetry,
                List.of(telemetry),
                new GeoPointResponse("fire", "固定模拟着火点",
                        31.2309, 121.47, 0.0, null),
                List.of(),
                List.of(),
                true));

        VideoGeolocationServiceImpl service = new VideoGeolocationServiceImpl(
                analysisService,
                telemetryService,
                new TargetGeolocationCalculator(),
                new GeolocationProperties(),
                new ObjectMapper());

        var located = service.locate(42L, 1.05);
        var noDetection = service.locate(42L, 3.0);

        assertThat(located.status()).isEqualTo("LOCATED");
        assertThat(located.detected()).isTrue();
        assertThat(located.geolocated()).isTrue();
        assertThat(located.frameId()).isEqualTo(10);
        assertThat(located.confidence()).isEqualTo(0.91);
        assertThat(located.horizontalDistanceM()).isCloseTo(100.0, within(0.01));
        assertThat(located.target().latitude()).isGreaterThan(telemetry.latitude());
        assertThat(located.missionWaypoints()).hasSize(4);
        assertThat(noDetection.status()).isEqualTo("NO_DETECTION");
        assertThat(noDetection.geolocated()).isFalse();
        verify(analysisService, times(1)).loadArtifact(42L, "detections", null);
    }

    private static org.assertj.core.data.Offset<Double> within(double value) {
        return org.assertj.core.data.Offset.offset(value);
    }
}
