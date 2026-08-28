package com.uav.platform.controller;

import com.uav.platform.common.Result;
import com.uav.platform.dto.VideoGeolocationResponse;
import com.uav.platform.service.VideoGeolocationService;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/** 视频播放时间与无人机遥测同步后的烟雾目标定位接口。 */
@RestController
@RequestMapping("/api/videos/{videoId}/geolocation")
public class VideoGeolocationController {

    private final VideoGeolocationService geolocationService;

    public VideoGeolocationController(VideoGeolocationService geolocationService) {
        this.geolocationService = geolocationService;
    }

    @GetMapping
    public Result<VideoGeolocationResponse> locate(
            @PathVariable Long videoId,
            @RequestParam double timeSec) {
        return Result.success(geolocationService.locate(videoId, timeSec));
    }
}
