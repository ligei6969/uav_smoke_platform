package com.uav.platform.entity;

import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;

import java.time.LocalDateTime;

/**
 * 一次视频分析尝试，对应 video_analysis_task 表。
 *
 * <p>一个视频可以产生多条分析记录，例如第一次失败后重新分析会新增一条记录，
 * 不会覆盖旧记录。</p>
 */
@TableName("video_analysis_task")
public class VideoAnalysisTask {

    // 本地分析记录 ID、所属视频 ID，以及 FastAPI 返回的远端任务 ID。
    @TableId
    private Long id;
    private Long videoId;
    private String remoteTaskId;

    // 任务生命周期：PENDING、RUNNING、COMPLETED 或 FAILED。
    private String status;
    private String errorMessage;
    private LocalDateTime submittedAt;
    private LocalDateTime startedAt;
    private LocalDateTime completedAt;

    // FastAPI 完成推理后返回的视频统计结果。
    private Boolean smokeDetected;
    private Double fps;
    private Integer frameCount;
    private Double durationSec;
    private Integer positiveFrameCount;
    private Double positiveFrameRatio;
    private Double firstDetectionSec;
    private Double lastDetectionSec;
    private Double maxConfidence;
    private Double meanConfidence;

    // 分析产物使用相对路径保存，浏览器通过 Spring Boot 代理接口访问。
    private String annotatedVideoPath;
    private String detectionResultsPath;
    private String firstKeyframePath;
    private String maxKeyframePath;
    private String lastKeyframePath;

    // 本地记录的创建和最后更新时间。
    private LocalDateTime createdAt;
    private LocalDateTime updatedAt;

    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }
    public Long getVideoId() { return videoId; }
    public void setVideoId(Long videoId) { this.videoId = videoId; }
    public String getRemoteTaskId() { return remoteTaskId; }
    public void setRemoteTaskId(String remoteTaskId) { this.remoteTaskId = remoteTaskId; }
    public String getStatus() { return status; }
    public void setStatus(String status) { this.status = status; }
    public String getErrorMessage() { return errorMessage; }
    public void setErrorMessage(String errorMessage) { this.errorMessage = errorMessage; }
    public LocalDateTime getSubmittedAt() { return submittedAt; }
    public void setSubmittedAt(LocalDateTime submittedAt) { this.submittedAt = submittedAt; }
    public LocalDateTime getStartedAt() { return startedAt; }
    public void setStartedAt(LocalDateTime startedAt) { this.startedAt = startedAt; }
    public LocalDateTime getCompletedAt() { return completedAt; }
    public void setCompletedAt(LocalDateTime completedAt) { this.completedAt = completedAt; }
    public Boolean getSmokeDetected() { return smokeDetected; }
    public void setSmokeDetected(Boolean smokeDetected) { this.smokeDetected = smokeDetected; }
    public Double getFps() { return fps; }
    public void setFps(Double fps) { this.fps = fps; }
    public Integer getFrameCount() { return frameCount; }
    public void setFrameCount(Integer frameCount) { this.frameCount = frameCount; }
    public Double getDurationSec() { return durationSec; }
    public void setDurationSec(Double durationSec) { this.durationSec = durationSec; }
    public Integer getPositiveFrameCount() { return positiveFrameCount; }
    public void setPositiveFrameCount(Integer positiveFrameCount) { this.positiveFrameCount = positiveFrameCount; }
    public Double getPositiveFrameRatio() { return positiveFrameRatio; }
    public void setPositiveFrameRatio(Double positiveFrameRatio) { this.positiveFrameRatio = positiveFrameRatio; }
    public Double getFirstDetectionSec() { return firstDetectionSec; }
    public void setFirstDetectionSec(Double firstDetectionSec) { this.firstDetectionSec = firstDetectionSec; }
    public Double getLastDetectionSec() { return lastDetectionSec; }
    public void setLastDetectionSec(Double lastDetectionSec) { this.lastDetectionSec = lastDetectionSec; }
    public Double getMaxConfidence() { return maxConfidence; }
    public void setMaxConfidence(Double maxConfidence) { this.maxConfidence = maxConfidence; }
    public Double getMeanConfidence() { return meanConfidence; }
    public void setMeanConfidence(Double meanConfidence) { this.meanConfidence = meanConfidence; }
    public String getAnnotatedVideoPath() { return annotatedVideoPath; }
    public void setAnnotatedVideoPath(String annotatedVideoPath) { this.annotatedVideoPath = annotatedVideoPath; }
    public String getDetectionResultsPath() { return detectionResultsPath; }
    public void setDetectionResultsPath(String detectionResultsPath) { this.detectionResultsPath = detectionResultsPath; }
    public String getFirstKeyframePath() { return firstKeyframePath; }
    public void setFirstKeyframePath(String firstKeyframePath) { this.firstKeyframePath = firstKeyframePath; }
    public String getMaxKeyframePath() { return maxKeyframePath; }
    public void setMaxKeyframePath(String maxKeyframePath) { this.maxKeyframePath = maxKeyframePath; }
    public String getLastKeyframePath() { return lastKeyframePath; }
    public void setLastKeyframePath(String lastKeyframePath) { this.lastKeyframePath = lastKeyframePath; }
    public LocalDateTime getCreatedAt() { return createdAt; }
    public void setCreatedAt(LocalDateTime createdAt) { this.createdAt = createdAt; }
    public LocalDateTime getUpdatedAt() { return updatedAt; }
    public void setUpdatedAt(LocalDateTime updatedAt) { this.updatedAt = updatedAt; }
}
