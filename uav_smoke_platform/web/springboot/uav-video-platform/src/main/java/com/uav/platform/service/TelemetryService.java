package com.uav.platform.service;

import com.uav.platform.dto.TelemetrySampleResponse;
import com.uav.platform.dto.TelemetrySnapshotResponse;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

/** 遥测来源抽象；未来真实 DJI 数据源实现同一接口即可替换模拟器。 */
public interface TelemetryService {

    TelemetrySampleResponse latest();

    TelemetrySnapshotResponse snapshot();

    /** 返回与视频相对时间同步的遥测；真实适配器可改为按采集时间查询。 */
    TelemetrySampleResponse sampleAtVideoTime(double videoTimeSec);

    SseEmitter subscribe();
}
