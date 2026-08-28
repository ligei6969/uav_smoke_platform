package com.uav.platform.service.impl;

import com.uav.platform.config.TelemetryProperties;
import com.uav.platform.dto.GeoPointResponse;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import com.uav.platform.service.TelemetryService;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import java.io.IOException;
import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 生成绕中心点飞行的可重复模拟遥测，并用 SSE 广播给所有监控页面。
 */
@Service
public class MockTelemetryService implements TelemetryService {

    private final TelemetryProperties properties;
    private final AtomicLong sequence = new AtomicLong();
    private final Deque<TelemetrySampleResponse> track = new ArrayDeque<>();
    private final List<SseEmitter> subscribers = new CopyOnWriteArrayList<>();
    private final GeoPointResponse firePoint;
    private final List<GeoPointResponse> smokeTargets;
    private final List<GeoPointResponse> missionWaypoints;
    private volatile TelemetrySampleResponse latest;

    public MockTelemetryService(TelemetryProperties properties) {
        this.properties = properties;
        this.latest = createSample(0L);
        this.track.add(latest);
        this.firePoint = createFirePoint();
        this.smokeTargets = createSmokeTargets();
        this.missionWaypoints = createMissionWaypoints();
    }

    @Override
    public TelemetrySampleResponse latest() {
        return latest;
    }

    @Override
    public TelemetrySnapshotResponse snapshot() {
        List<TelemetrySampleResponse> trackCopy;
        synchronized (track) {
            trackCopy = List.copyOf(track);
        }
        return new TelemetrySnapshotResponse(
                latest,
                trackCopy,
                firePoint,
                smokeTargets,
                missionWaypoints,
                true);
    }

    @Override
    public SseEmitter subscribe() {
        SseEmitter emitter = new SseEmitter(0L);
        subscribers.add(emitter);
        emitter.onCompletion(() -> subscribers.remove(emitter));
        emitter.onTimeout(() -> subscribers.remove(emitter));
        emitter.onError(_error -> subscribers.remove(emitter));
        send(emitter, latest);
        return emitter;
    }

    /** 定时生成下一帧；public 便于离线单元测试直接驱动模拟器。 */
    @Scheduled(fixedRateString = "${app.telemetry.interval-ms:1000}")
    public void publishNext() {
        if (!properties.isEnabled()) {
            return;
        }
        long nextSequence = sequence.incrementAndGet();
        TelemetrySampleResponse sample = createSample(nextSequence);
        latest = sample;
        synchronized (track) {
            track.addLast(sample);
            int limit = Math.max(2, properties.getMaxTrackPoints());
            while (track.size() > limit) {
                track.removeFirst();
            }
        }
        for (SseEmitter subscriber : subscribers) {
            send(subscriber, sample);
        }
    }

    private void send(SseEmitter emitter, TelemetrySampleResponse sample) {
        try {
            emitter.send(SseEmitter.event()
                    .id(Long.toString(sample.sequence()))
                    .name("telemetry")
                    .data(sample));
        } catch (IOException | IllegalStateException exception) {
            subscribers.remove(emitter);
            emitter.complete();
        }
    }

    private TelemetrySampleResponse createSample(long sampleSequence) {
        double angle = sampleSequence * Math.toRadians(3.0);
        double latitudeOffset = 0.0022 * Math.sin(angle);
        double longitudeOffset = 0.0028 * Math.sin(angle) * Math.cos(angle);
        double northDerivative = 0.0022 * Math.cos(angle);
        double eastDerivative = 0.0028 * Math.cos(2.0 * angle);
        double heading = Math.toDegrees(Math.atan2(eastDerivative, northDerivative));
        if (heading < 0.0) {
            heading += 360.0;
        }
        return new TelemetrySampleResponse(
                sampleSequence,
                Instant.now(),
                properties.getCenterLatitude() + latitudeOffset,
                properties.getCenterLongitude() + longitudeOffset,
                82.0 + 12.0 * Math.sin(angle * 2.0),
                heading,
                -48.0 - 8.0 * Math.sin(angle),
                12.5 + 1.5 * Math.cos(angle * 2.0));
    }

    private List<GeoPointResponse> createSmokeTargets() {
        double lat = properties.getCenterLatitude();
        double lon = properties.getCenterLongitude();
        return List.of(
                new GeoPointResponse(
                        "smoke-1", "疑似烟雾目标 A", lat + 0.00105, lon + 0.00085,
                        0.0, 0.93),
                new GeoPointResponse(
                        "smoke-2", "疑似烟雾目标 B", lat - 0.00075, lon - 0.00115,
                        0.0, 0.81));
    }

    /** 第二条测试坐标：固定在地面的模拟着火点，不随无人机移动。 */
    private GeoPointResponse createFirePoint() {
        return new GeoPointResponse(
                "fire-origin",
                "固定模拟着火点",
                properties.getCenterLatitude() + 0.00135,
                properties.getCenterLongitude() + 0.00120,
                0.0,
                null);
    }

    private List<GeoPointResponse> createMissionWaypoints() {
        double lat = firePoint.latitude();
        double lon = firePoint.longitude();
        List<GeoPointResponse> points = new ArrayList<>();
        points.add(new GeoPointResponse("wp-1", "复查航点 1", lat + 0.00035, lon, 65.0, null));
        points.add(new GeoPointResponse("wp-2", "复查航点 2", lat, lon + 0.00040, 60.0, null));
        points.add(new GeoPointResponse("wp-3", "复查航点 3", lat - 0.00035, lon, 65.0, null));
        points.add(new GeoPointResponse("wp-4", "复查航点 4", lat, lon - 0.00040, 60.0, null));
        return List.copyOf(points);
    }
}
