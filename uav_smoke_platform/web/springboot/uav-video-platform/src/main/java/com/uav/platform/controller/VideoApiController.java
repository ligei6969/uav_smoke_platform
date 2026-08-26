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

/**
 * 视频管理 REST API。
 *
 * <p>@RestController 会把返回的 Java 对象序列化成 JSON，而不是寻找 HTML 模板。</p>
 */
@RestController
@RequestMapping("/api/videos")
public class VideoApiController {

    private final VideoService videoService;

    public VideoApiController(VideoService videoService) {
        this.videoService = videoService;
    }

    /** GET /api/videos：查询全部未删除视频。 */
    @GetMapping
    public Result<List<VideoResponse>> list() {
        return Result.success(videoService.listVideos());
    }

    /** GET /api/videos/{id}：查询一个视频。 */
    @GetMapping("/{id}")
    public Result<VideoResponse> detail(@PathVariable Long id) {
        return Result.success(videoService.getVideo(id));
    }

    /** DELETE /api/videos/{id}：通过 API 删除视频。 */
    @DeleteMapping("/{id}")
    public Result<Void> delete(@PathVariable Long id) {
        videoService.delete(id);
        return Result.success("视频及本地文件已删除", null);
    }
}
