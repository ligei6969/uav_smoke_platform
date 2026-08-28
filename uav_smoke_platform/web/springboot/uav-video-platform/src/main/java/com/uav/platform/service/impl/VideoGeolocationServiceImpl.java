package com.uav.platform.service.impl;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.config.GeolocationProperties;
import com.uav.platform.dto.GeoPointResponse;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.VideoAnalysisResponse;
import com.uav.platform.dto.VideoGeolocationResponse;
import com.uav.platform.dto.VideoGeolocationResponse.DetectionBoxResponse;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.service.TargetGeolocationCalculator;
import com.uav.platform.service.TargetGeolocationCalculator.TargetLocation;
import com.uav.platform.service.TelemetryService;
import com.uav.platform.service.VideoAnalysisService;
import com.uav.platform.service.VideoGeolocationService;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import tools.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** 从真实 YOLO 检测 JSON 选择当前帧，并与模拟回放遥测进行同步定位。 */
@Service
public class VideoGeolocationServiceImpl implements VideoGeolocationService {

    private static final double EARTH_RADIUS_M = 6371008.8;

    private final VideoAnalysisService analysisService;
    private final TelemetryService telemetryService;
    private final TargetGeolocationCalculator calculator;
    private final GeolocationProperties properties;
    private final ObjectMapper objectMapper;
    private final Map<Long, CachedArtifact> cache = new ConcurrentHashMap<>();

    public VideoGeolocationServiceImpl(
            VideoAnalysisService analysisService,
            TelemetryService telemetryService,
            TargetGeolocationCalculator calculator,
            GeolocationProperties properties,
            ObjectMapper objectMapper) {
        this.analysisService = analysisService;
        this.telemetryService = telemetryService;
        this.calculator = calculator;
        this.properties = properties;
        this.objectMapper = objectMapper;
    }

    @Override
    public VideoGeolocationResponse locate(Long videoId, double videoTimeSec) {
        if (videoId == null || videoId <= 0L) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "视频不存在");
        }
        if (!Double.isFinite(videoTimeSec) || videoTimeSec < 0.0) {
            throw new BusinessException("视频时间必须是非负有限数值");
        }

        VideoAnalysisResponse analysis = analysisService.getLatest(videoId);
        if (!"COMPLETED".equals(analysis.status()) || analysis.attemptId() == null) {
            throw new BusinessException(HttpStatus.CONFLICT, "该视频尚无可用于定位的完整分析结果");
        }
        DetectionArtifact artifact = artifactFor(videoId, analysis.attemptId());
        TelemetrySampleResponse requestedTelemetry =
                telemetryService.sampleAtVideoTime(videoTimeSec);
        DetectionFrame frame = nearestFrame(artifact.detections(), videoTimeSec);
        double window = Math.max(0.0, properties.getDetectionWindowSec());
        if (frame == null || Math.abs(frame.timestampSec() - videoTimeSec) > window) {
            return emptyResponse(
                    videoId, analysis.attemptId(), videoTimeSec,
                    "NO_DETECTION", requestedTelemetry);
        }

        DetectionBox box = strongestBox(frame.boxes());
        if (box == null || box.bbox() == null || box.bbox().size() != 4) {
            return emptyResponse(
                    videoId, analysis.attemptId(), videoTimeSec,
                    "NO_DETECTION", requestedTelemetry);
        }

        TelemetrySampleResponse synchronizedTelemetry =
                telemetryService.sampleAtVideoTime(frame.timestampSec());
        DetectionBoxResponse boxResponse = new DetectionBoxResponse(
                box.bbox().get(0), box.bbox().get(1),
                box.bbox().get(2), box.bbox().get(3),
                artifact.width(), artifact.height());
        try {
            TargetLocation location = calculator.calculate(
                    synchronizedTelemetry,
                    (boxResponse.left() + boxResponse.right()) * 0.5,
                    (boxResponse.top() + boxResponse.bottom()) * 0.5,
                    boxResponse.imageWidth(),
                    boxResponse.imageHeight(),
                    properties.getHorizontalFovDeg());
            GeoPointResponse target = new GeoPointResponse(
                    "video-target-" + videoId,
                    "视频定位烟雾目标",
                    location.latitude(),
                    location.longitude(),
                    location.altitudeM(),
                    box.confidence());
            List<GeoPointResponse> waypoints = missionWaypoints(target);
            GeoPointResponse reference = telemetryService.snapshot().firePoint();
            Double referenceError = reference == null ? null : groundDistanceM(
                    target.latitude(), target.longitude(),
                    reference.latitude(), reference.longitude());
            return new VideoGeolocationResponse(
                    videoId,
                    analysis.attemptId(),
                    videoTimeSec,
                    "LOCATED",
                    true,
                    true,
                    frame.frameId(),
                    frame.timestampSec(),
                    box.confidence(),
                    boxResponse,
                    synchronizedTelemetry,
                    target,
                    waypoints,
                    location.northOffsetM(),
                    location.eastOffsetM(),
                    location.horizontalDistanceM(),
                    referenceError,
                    true);
        } catch (IllegalArgumentException exception) {
            return new VideoGeolocationResponse(
                    videoId,
                    analysis.attemptId(),
                    videoTimeSec,
                    "GEOMETRY_UNAVAILABLE",
                    true,
                    false,
                    frame.frameId(),
                    frame.timestampSec(),
                    box.confidence(),
                    boxResponse,
                    synchronizedTelemetry,
                    null,
                    List.of(),
                    null,
                    null,
                    null,
                    null,
                    true);
        }
    }

    private VideoGeolocationResponse emptyResponse(
            Long videoId,
            Long attemptId,
            double videoTimeSec,
            String status,
            TelemetrySampleResponse telemetry) {
        return new VideoGeolocationResponse(
                videoId, attemptId, videoTimeSec, status,
                false, false, null, null, null, null,
                telemetry, null, List.of(),
                null, null, null, null, true);
    }

    private DetectionArtifact artifactFor(Long videoId, Long attemptId) {
        CachedArtifact existing = cache.get(videoId);
        if (existing != null && existing.attemptId().equals(attemptId)) {
            return existing.artifact();
        }
        DetectionArtifact loaded = loadArtifact(videoId);
        cache.put(videoId, new CachedArtifact(attemptId, loaded));
        return loaded;
    }

    private DetectionArtifact loadArtifact(Long videoId) {
        long configuredLimit = properties.getMaxDetectionArtifactBytes();
        int limit = (int) Math.min(
                Math.max(1024L, configuredLimit),
                Integer.MAX_VALUE - 1L);
        try (RemoteArtifact remote =
                     analysisService.loadArtifact(videoId, "detections", null)) {
            if (remote.statusCode() != 200) {
                throw new BusinessException(
                        HttpStatus.BAD_GATEWAY,
                        "检测结果接口返回异常状态：" + remote.statusCode());
            }
            if (remote.contentLength() > limit) {
                throw new BusinessException(
                        HttpStatus.PAYLOAD_TOO_LARGE,
                        "检测结果文件超过定位服务读取上限");
            }
            byte[] payload = remote.body().readNBytes(limit + 1);
            if (payload.length > limit) {
                throw new BusinessException(
                        HttpStatus.PAYLOAD_TOO_LARGE,
                        "检测结果文件超过定位服务读取上限");
            }
            DetectionArtifact parsed = objectMapper.readValue(
                    payload, DetectionArtifact.class);
            if (parsed.width() <= 1 || parsed.height() <= 1
                    || parsed.detections() == null) {
                throw new BusinessException(
                        HttpStatus.BAD_GATEWAY,
                        "检测结果缺少图像尺寸或检测帧");
            }
            List<DetectionFrame> ordered = parsed.detections().stream()
                    .filter(item -> item != null && Double.isFinite(item.timestampSec()))
                    .sorted(Comparator.comparingDouble(DetectionFrame::timestampSec))
                    .toList();
            return new DetectionArtifact(
                    parsed.fps(), parsed.width(), parsed.height(), ordered);
        } catch (IOException exception) {
            throw new BusinessException(
                    HttpStatus.BAD_GATEWAY,
                    "检测结果读取失败",
                    exception);
        }
    }

    private DetectionFrame nearestFrame(List<DetectionFrame> frames, double timeSec) {
        if (frames == null || frames.isEmpty()) {
            return null;
        }
        int low = 0;
        int high = frames.size() - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            if (frames.get(middle).timestampSec() < timeSec) {
                low = middle + 1;
            } else {
                high = middle - 1;
            }
        }
        DetectionFrame after = low < frames.size() ? frames.get(low) : null;
        DetectionFrame before = low > 0 ? frames.get(low - 1) : null;
        if (before == null) {
            return after;
        }
        if (after == null) {
            return before;
        }
        return Math.abs(before.timestampSec() - timeSec)
                <= Math.abs(after.timestampSec() - timeSec) ? before : after;
    }

    private DetectionBox strongestBox(List<DetectionBox> boxes) {
        if (boxes == null) {
            return null;
        }
        return boxes.stream()
                .filter(item -> item != null
                        && item.bbox() != null
                        && item.bbox().size() == 4
                        && Double.isFinite(item.confidence()))
                .max(Comparator.comparingDouble(DetectionBox::confidence))
                .orElse(null);
    }

    private List<GeoPointResponse> missionWaypoints(GeoPointResponse target) {
        double radius = Math.max(1.0, properties.getRecheckRadiusM());
        double altitude = Math.max(1.0, properties.getRecheckAltitudeM());
        double latitudeRadians = Math.toRadians(target.latitude());
        double latitudeOffset = Math.toDegrees(radius / EARTH_RADIUS_M);
        double longitudeScale = EARTH_RADIUS_M * Math.cos(latitudeRadians);
        double longitudeOffset = Math.abs(longitudeScale) < 1.0
                ? 0.0 : Math.toDegrees(radius / longitudeScale);
        return List.of(
                new GeoPointResponse("located-wp-1", "定位复查航点 1",
                        target.latitude() + latitudeOffset, target.longitude(),
                        altitude, null),
                new GeoPointResponse("located-wp-2", "定位复查航点 2",
                        target.latitude(), target.longitude() + longitudeOffset,
                        altitude, null),
                new GeoPointResponse("located-wp-3", "定位复查航点 3",
                        target.latitude() - latitudeOffset, target.longitude(),
                        altitude, null),
                new GeoPointResponse("located-wp-4", "定位复查航点 4",
                        target.latitude(), target.longitude() - longitudeOffset,
                        altitude, null));
    }

    private double groundDistanceM(
            double firstLatitude,
            double firstLongitude,
            double secondLatitude,
            double secondLongitude) {
        double latitudeDelta = Math.toRadians(secondLatitude - firstLatitude);
        double longitudeDelta = Math.toRadians(secondLongitude - firstLongitude);
        double firstLatitudeRad = Math.toRadians(firstLatitude);
        double secondLatitudeRad = Math.toRadians(secondLatitude);
        double haversine = Math.pow(Math.sin(latitudeDelta / 2.0), 2.0)
                + Math.cos(firstLatitudeRad) * Math.cos(secondLatitudeRad)
                * Math.pow(Math.sin(longitudeDelta / 2.0), 2.0);
        return 2.0 * EARTH_RADIUS_M * Math.asin(Math.sqrt(haversine));
    }

    private record CachedArtifact(Long attemptId, DetectionArtifact artifact) {
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    private record DetectionArtifact(
            double fps,
            int width,
            int height,
            List<DetectionFrame> detections) {
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    private record DetectionFrame(
            @JsonProperty("frame_id") int frameId,
            @JsonProperty("timestamp_sec") double timestampSec,
            List<DetectionBox> boxes) {
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    private record DetectionBox(
            List<Double> bbox,
            double confidence) {
    }
}
