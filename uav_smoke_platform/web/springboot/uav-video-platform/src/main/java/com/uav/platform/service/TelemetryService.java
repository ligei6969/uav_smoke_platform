package com.uav.platform.service;

import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

/** 遥测来源抽象；未来真实 DJI 数据源实现同一接口即可替换模拟器。 */
public interface TelemetryService {

    TelemetrySampleResponse latest();

    TelemetrySnapshotResponse snapshot();

    SseEmitter subscribe();
}
