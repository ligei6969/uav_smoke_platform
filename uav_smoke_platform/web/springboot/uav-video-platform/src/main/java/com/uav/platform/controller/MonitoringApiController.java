package com.uav.platform.controller;

import com.uav.platform.common.Result;
import com.uav.platform.config.MapProperties;
import com.uav.platform.dto.MapConfigurationResponse;
import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import com.uav.platform.service.TelemetryService;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

/** 地图配置、遥测快照和实时 SSE 数据接口。 */
@RestController
@RequestMapping("/api")
public class MonitoringApiController {

    private final MapProperties mapProperties;
    private final TelemetryService telemetryService;

    public MonitoringApiController(
            MapProperties mapProperties,
            TelemetryService telemetryService) {
        this.mapProperties = mapProperties;
        this.telemetryService = telemetryService;
    }

    @GetMapping("/map/config")
    public Result<MapConfigurationResponse> mapConfiguration(
            @RequestParam(required = false) String provider,
            @RequestParam(required = false) String style) {
        return Result.success(MapConfigurationResponse.from(
                mapProperties,
                provider,
                style));
    }

    @GetMapping("/telemetry/latest")
    public Result<TelemetrySampleResponse> latest() {
        return Result.success(telemetryService.latest());
    }

    @GetMapping("/telemetry/snapshot")
    public Result<TelemetrySnapshotResponse> snapshot() {
        return Result.success(telemetryService.snapshot());
    }

    @GetMapping(path = "/telemetry/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream() {
        return telemetryService.subscribe();
    }
}
