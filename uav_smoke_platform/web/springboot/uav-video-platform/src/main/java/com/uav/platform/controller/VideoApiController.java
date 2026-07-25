package com.uav.platform.controller;

import com.uav.platform.common.Result;
import com.uav.platform.dto.VideoResponse;
import com.uav.platform.service.VideoService;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController
@RequestMapping("/api/videos")
public class VideoApiController {

    private final VideoService videoService;

    public VideoApiController(VideoService videoService) {
        this.videoService = videoService;
    }

    @GetMapping
    public Result<List<VideoResponse>> list() {
        return Result.success(videoService.listVideos());
    }

    @GetMapping("/{id}")
    public Result<VideoResponse> detail(@PathVariable Long id) {
        return Result.success(videoService.getVideo(id));
    }

    @DeleteMapping("/{id}")
    public Result<Void> delete(@PathVariable Long id) {
        videoService.delete(id);
        return Result.success("视频及本地文件已删除", null);
    }
}
