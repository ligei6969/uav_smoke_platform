package com.uav.platform.controller;

import com.uav.platform.common.Result;
import com.uav.platform.dto.VideoAnalysisResponse;
import com.uav.platform.service.VideoAnalysisService;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/**
 * 视频烟雾分析 REST API，是详情页 JavaScript 与后端业务之间的入口。
 */
@RestController
@RequestMapping("/api/videos/{videoId}/analysis")
public class VideoAnalysisController {

    private final VideoAnalysisService analysisService;

    public VideoAnalysisController(VideoAnalysisService analysisService) {
        this.analysisService = analysisService;
    }

    /** POST /api/videos/{videoId}/analysis：创建分析任务。 */
    @PostMapping
    public ResponseEntity<Result<VideoAnalysisResponse>> start(
            @PathVariable Long videoId) {
        // 202 Accepted 表示请求已接受，但后台分析还没有完成。
        return ResponseEntity.status(HttpStatus.ACCEPTED)
                .body(Result.success(analysisService.start(videoId)));
    }

    /** GET /api/videos/{videoId}/analysis：查询并同步最新状态。 */
    @GetMapping
    public Result<VideoAnalysisResponse> status(@PathVariable Long videoId) {
        return Result.success(analysisService.getLatest(videoId));
    }

    /** POST /api/videos/{videoId}/analysis/retry：重新提交最近一次失败任务。 */
    @PostMapping("/retry")
    public ResponseEntity<Result<VideoAnalysisResponse>> retry(
            @PathVariable Long videoId) {
        return ResponseEntity.status(HttpStatus.ACCEPTED)
                .body(Result.success(analysisService.retry(videoId)));
    }
}
