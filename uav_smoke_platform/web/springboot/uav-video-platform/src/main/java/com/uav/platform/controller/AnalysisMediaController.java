package com.uav.platform.controller;

import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.service.VideoAnalysisService;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatusCode;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestHeader;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.StreamingResponseBody;

import java.util.concurrent.TimeUnit;

/**
 * 分析产物代理控制器。
 *
 * <p>浏览器只访问 Spring Boot；Spring Boot 再从 FastAPI 读取标注视频、检测 JSON
 * 和关键帧，避免向前端暴露 FastAPI 地址和服务器文件路径。</p>
 */
@RestController
@RequestMapping("/media/videos/{videoId}/analysis")
public class AnalysisMediaController {

    private final VideoAnalysisService analysisService;

    public AnalysisMediaController(VideoAnalysisService analysisService) {
        this.analysisService = analysisService;
    }

    /** 返回标注视频，并把浏览器的 Range 请求继续转发给 FastAPI。 */
    @GetMapping("/annotated")
    public ResponseEntity<StreamingResponseBody> annotated(
            @PathVariable Long videoId,
            @RequestHeader(value = HttpHeaders.RANGE, required = false) String range) {
        return stream(videoId, "annotated", range);
    }

    /** 返回逐帧检测结果 JSON。 */
    @GetMapping("/detections")
    public ResponseEntity<StreamingResponseBody> detections(
            @PathVariable Long videoId) {
        return stream(videoId, "detections", null);
    }

    /** 根据 first、max 或 last 返回对应关键帧。 */
    @GetMapping("/keyframes/{type}")
    public ResponseEntity<StreamingResponseBody> keyframe(
            @PathVariable Long videoId,
            @PathVariable String type) {
        return stream(videoId, "keyframe:" + type, null);
    }

    /** 把 FastAPI 响应状态、响应头和响应体尽量原样转发给浏览器。 */
    private ResponseEntity<StreamingResponseBody> stream(
            Long videoId,
            String artifactType,
            String range) {
        RemoteArtifact artifact = analysisService.loadArtifact(
                videoId, artifactType, range);
        // StreamingResponseBody 边读边写，不需要先把大型视频完整加载进 JVM 内存。
        StreamingResponseBody body = output -> {
            try (artifact) {
                artifact.body().transferTo(output);
            }
        };
        ResponseEntity.BodyBuilder response = ResponseEntity
                .status(HttpStatusCode.valueOf(artifact.statusCode()))
                .contentType(MediaType.parseMediaType(artifact.contentType()))
                .header(HttpHeaders.ACCEPT_RANGES, artifact.acceptRanges())
                .cacheControl(CacheControl.maxAge(1, TimeUnit.HOURS).cachePrivate());
        if (artifact.contentLength() >= 0) {
            response.contentLength(artifact.contentLength());
        }
        if (artifact.contentRange() != null) {
            response.header(HttpHeaders.CONTENT_RANGE, artifact.contentRange());
        }
        return response.body(body);
    }
}
