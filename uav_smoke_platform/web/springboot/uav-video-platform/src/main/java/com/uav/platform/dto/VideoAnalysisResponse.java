package com.uav.platform.dto;

import com.uav.platform.entity.VideoAnalysisTask;

import java.time.LocalDateTime;

/**
 * 页面查询分析状态时收到的数据。
 *
 * <p>它同时承载任务状态、检测统计和可公开访问的媒体 URL，不返回 FastAPI
 * 机器上的绝对文件路径。</p>
 */
public record VideoAnalysisResponse(
        Long attemptId,
        Long videoId,
        String taskId,
        String status,
        String errorMessage,
        LocalDateTime submittedAt,
        LocalDateTime startedAt,
        LocalDateTime completedAt,
        Boolean smokeDetected,
        Double fps,
        Integer frameCount,
        Double durationSec,
        Integer positiveFrameCount,
        Double positiveFrameRatio,
        Double firstDetectionSec,
        Double lastDetectionSec,
        Double maxConfidence,
        Double meanConfidence,
        String annotatedVideoUrl,
        String detectionResultsUrl,
        String firstKeyframeUrl,
        String maxKeyframeUrl,
        String lastKeyframeUrl) {

    /** 视频还没有分析记录时，构造 NOT_STARTED 状态。 */
    public static VideoAnalysisResponse notStarted(Long videoId) {
        return new VideoAnalysisResponse(
                null, videoId, null, "NOT_STARTED", null,
                null, null, null, null, null, null, null, null, null,
                null, null, null, null, null, null, null, null, null);
    }

    /** 将数据库中的最新分析记录转换成网页需要的响应。 */
    public static VideoAnalysisResponse from(VideoAnalysisTask task) {
        boolean completed = "COMPLETED".equals(task.getStatus());
        String mediaBase = "/media/videos/" + task.getVideoId() + "/analysis/";
        // 每次尝试使用不同参数，防止浏览器继续使用上一次分析缓存的媒体文件。
        String mediaVersion = "?attempt=" + task.getId();
        return new VideoAnalysisResponse(
                task.getId(),
                task.getVideoId(),
                task.getRemoteTaskId(),
                task.getStatus(),
                task.getErrorMessage(),
                task.getSubmittedAt(),
                task.getStartedAt(),
                task.getCompletedAt(),
                task.getSmokeDetected(),
                task.getFps(),
                task.getFrameCount(),
                task.getDurationSec(),
                task.getPositiveFrameCount(),
                task.getPositiveFrameRatio(),
                task.getFirstDetectionSec(),
                task.getLastDetectionSec(),
                task.getMaxConfidence(),
                task.getMeanConfidence(),
                completed && task.getAnnotatedVideoPath() != null
                        ? mediaBase + "annotated" + mediaVersion : null,
                completed && task.getDetectionResultsPath() != null
                        ? mediaBase + "detections" + mediaVersion : null,
                completed && task.getFirstKeyframePath() != null
                        ? mediaBase + "keyframes/first" + mediaVersion : null,
                completed && task.getMaxKeyframePath() != null
                        ? mediaBase + "keyframes/max" + mediaVersion : null,
                completed && task.getLastKeyframePath() != null
                        ? mediaBase + "keyframes/last" + mediaVersion : null);
    }
}
