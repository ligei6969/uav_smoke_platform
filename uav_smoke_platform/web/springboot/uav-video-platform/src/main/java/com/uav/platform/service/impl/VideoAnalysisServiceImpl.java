package com.uav.platform.service.impl;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.toolkit.IdWorker;
import com.uav.platform.client.AnalysisClientException;
import com.uav.platform.client.SmokeAnalysisClient;
import com.uav.platform.client.SmokeAnalysisClient.AnalysisResultResponse;
import com.uav.platform.client.SmokeAnalysisClient.KeyframeArtifact;
import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.client.SmokeAnalysisClient.TaskStatusResponse;
import com.uav.platform.dto.VideoAnalysisResponse;
import com.uav.platform.dto.VideoResponse;
import com.uav.platform.entity.VideoAnalysisTask;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.mapper.VideoAnalysisTaskMapper;
import com.uav.platform.service.VideoAnalysisService;
import com.uav.platform.service.VideoService;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.Set;

/**
 * 视频分析流程的核心业务实现。
 *
 * <p>它不执行 YOLO 推理，而是管理本地任务记录、调用 FastAPI、同步状态并保存结果。</p>
 */
@Service
public class VideoAnalysisServiceImpl implements VideoAnalysisService {

    // 本地状态名称必须与数据库约束和 FastAPI 返回值保持一致。
    private static final String PENDING = "PENDING";
    private static final String RUNNING = "RUNNING";
    private static final String COMPLETED = "COMPLETED";
    private static final String FAILED = "FAILED";
    private static final Set<String> ACTIVE_STATUSES = Set.of(PENDING, RUNNING);

    // 分析记录 Mapper、视频业务服务和 FastAPI 客户端。
    private final VideoAnalysisTaskMapper taskMapper;
    private final VideoService videoService;
    private final SmokeAnalysisClient analysisClient;

    public VideoAnalysisServiceImpl(
            VideoAnalysisTaskMapper taskMapper,
            VideoService videoService,
            SmokeAnalysisClient analysisClient) {
        this.taskMapper = taskMapper;
        this.videoService = videoService;
        this.analysisClient = analysisClient;
    }

    /**
     * 创建第一次分析。synchronized 防止同一 JVM 中两个请求同时越过重复检查。
     */
    @Override
    public synchronized VideoAnalysisResponse start(Long videoId) {
        VideoResponse video = videoService.getVideo(videoId);
        VideoAnalysisTask existing = findLatest(videoId);
        if (existing != null) {
            if (ACTIVE_STATUSES.contains(existing.getStatus())) {
                throw new BusinessException(
                        HttpStatus.CONFLICT,
                        "该视频已有正在进行的分析任务");
            }
            throw new BusinessException(
                    HttpStatus.CONFLICT,
                    "该视频已有分析记录，请根据结果选择是否重试");
        }
        return submit(video);
    }

    /** 读取最新任务；只有活动任务才需要访问 FastAPI，同步结束后结果直接读数据库。 */
    @Override
    public VideoAnalysisResponse getLatest(Long videoId) {
        videoService.getVideo(videoId);
        VideoAnalysisTask task = findLatest(videoId);
        if (task == null) {
            return VideoAnalysisResponse.notStarted(videoId);
        }
        if (!ACTIVE_STATUSES.contains(task.getStatus())) {
            return VideoAnalysisResponse.from(task);
        }
        return VideoAnalysisResponse.from(synchronize(task));
    }

    /** 仅允许 FAILED 状态重试，每次重试都会新增记录以保留历史。 */
    @Override
    public synchronized VideoAnalysisResponse retry(Long videoId) {
        VideoResponse video = videoService.getVideo(videoId);
        VideoAnalysisTask latest = findLatest(videoId);
        if (latest == null || !FAILED.equals(latest.getStatus())) {
            throw new BusinessException(
                    HttpStatus.CONFLICT,
                    "只有失败的分析任务可以重试");
        }
        return submit(video);
    }

    /** 校验任务完成状态，然后从 FastAPI 打开相应产物。 */
    @Override
    public RemoteArtifact loadArtifact(
            Long videoId,
            String artifactType,
            String rangeHeader) {
        videoService.getVideo(videoId);
        VideoAnalysisTask task = findLatest(videoId);
        if (task == null || !COMPLETED.equals(task.getStatus())) {
            throw new BusinessException(HttpStatus.CONFLICT, "分析结果尚未完成");
        }
        String path = artifactPath(task, artifactType);
        if (path == null || path.isBlank()) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "分析产物不存在");
        }
        try {
            return analysisClient.openArtifact(
                    task.getRemoteTaskId(), path, rangeHeader);
        } catch (AnalysisClientException exception) {
            if (exception.getStatusCode() == 404) {
                throw new BusinessException(
                        HttpStatus.NOT_FOUND,
                        "分析产物不存在",
                        exception);
            }
            throw serviceUnavailable(exception);
        }
    }

    /**
     * 先写入本地 PENDING 记录，再上传视频到 FastAPI，并保存远端 taskId。
     */
    private VideoAnalysisResponse submit(VideoResponse video) {
        Resource resource = videoService.loadMedia(video.id());
        LocalDateTime now = LocalDateTime.now();
        VideoAnalysisTask task = new VideoAnalysisTask();
        task.setId(IdWorker.getId());
        task.setVideoId(video.id());
        task.setStatus(PENDING);
        task.setSubmittedAt(now);
        task.setCreatedAt(now);
        task.setUpdatedAt(now);
        if (taskMapper.insert(task) != 1) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "分析任务保存失败");
        }

        try {
            // 大文件通过 Resource 流式交给 HTTP 客户端，不一次性读入 byte[]。
            SmokeAnalysisClient.CreateTaskResponse response =
                    analysisClient.createTask(resource, video.originalFilename());
            if (response.taskId() == null || response.taskId().isBlank()) {
                throw new AnalysisClientException("FastAPI 未返回任务 ID", 0);
            }
            task.setRemoteTaskId(response.taskId());
            task.setStatus(normalizeActiveStatus(response.status()));
            if (RUNNING.equals(task.getStatus())) {
                task.setStartedAt(LocalDateTime.now());
            }
            task.setUpdatedAt(LocalDateTime.now());
            update(task);
            return VideoAnalysisResponse.from(task);
        } catch (AnalysisClientException exception) {
            // 即使 FastAPI 不可用，也保留 FAILED 记录和错误原因供页面展示、重试。
            fail(task, exception.getMessage());
            throw serviceUnavailable(exception);
        }
    }

    /** 根据 FastAPI 当前状态更新本地记录，完成时继续获取完整结果。 */
    private VideoAnalysisTask synchronize(VideoAnalysisTask task) {
        if (task.getRemoteTaskId() == null || task.getRemoteTaskId().isBlank()) {
            fail(task, "分析任务提交未完成，请重试");
            return task;
        }
        try {
            TaskStatusResponse status = analysisClient.getTaskStatus(
                    task.getRemoteTaskId());
            return switch (status.status()) {
                case PENDING -> updateStatus(task, PENDING);
                case RUNNING -> updateStatus(task, RUNNING);
                case COMPLETED -> complete(
                        task,
                        analysisClient.getTaskResult(task.getRemoteTaskId()));
                case FAILED -> {
                    fail(task, status.errorMessage());
                    yield task;
                }
                default -> {
                    fail(task, "FastAPI 返回未知任务状态：" + status.status());
                    yield task;
                }
            };
        } catch (AnalysisClientException exception) {
            if (exception.getStatusCode() == 404) {
                fail(task, "远端分析任务不存在，可能是 FastAPI 已重启，请重试");
                return task;
            }
            throw serviceUnavailable(exception);
        }
    }

    /** 把 FastAPI 的统计结果和产物相对路径完整保存到数据库。 */
    private VideoAnalysisTask complete(
            VideoAnalysisTask task,
            AnalysisResultResponse result) {
        if (result.videoSummary() == null || result.artifacts() == null) {
            fail(task, "FastAPI 返回的分析结果不完整");
            return task;
        }
        var summary = result.videoSummary();
        task.setStatus(COMPLETED);
        task.setErrorMessage(null);
        task.setCompletedAt(LocalDateTime.now());
        task.setSmokeDetected(summary.smokeDetected());
        task.setFps(summary.fps());
        task.setFrameCount(summary.frameCount());
        task.setDurationSec(summary.durationSec());
        task.setPositiveFrameCount(summary.positiveFrameCount());
        task.setPositiveFrameRatio(summary.positiveFrameRatio());
        task.setFirstDetectionSec(summary.firstDetectionSec());
        task.setLastDetectionSec(summary.lastDetectionSec());
        task.setMaxConfidence(summary.maxConfidence());
        task.setMeanConfidence(summary.meanConfidence());
        task.setAnnotatedVideoPath(result.artifacts().annotatedVideo());
        task.setDetectionResultsPath(result.artifacts().detectionResults());
        if (result.artifacts().keyframes() != null) {
            // FastAPI 使用 type 区分三类关键帧，这里写入对应数据库字段。
            for (KeyframeArtifact keyframe : result.artifacts().keyframes()) {
                if (keyframe == null) {
                    continue;
                }
                switch (keyframe.type()) {
                    case "first_detection" -> task.setFirstKeyframePath(keyframe.path());
                    case "max_confidence" -> task.setMaxKeyframePath(keyframe.path());
                    case "last_detection" -> task.setLastKeyframePath(keyframe.path());
                    default -> { }
                }
            }
        }
        task.setUpdatedAt(LocalDateTime.now());
        update(task);
        return task;
    }

    /** 更新 PENDING 或 RUNNING，并在第一次进入 RUNNING 时记录开始时间。 */
    private VideoAnalysisTask updateStatus(VideoAnalysisTask task, String status) {
        task.setStatus(status);
        if (RUNNING.equals(status) && task.getStartedAt() == null) {
            task.setStartedAt(LocalDateTime.now());
        }
        task.setUpdatedAt(LocalDateTime.now());
        update(task);
        return task;
    }

    /** 统一把任务设置为失败状态并保存可读错误原因。 */
    private void fail(VideoAnalysisTask task, String message) {
        task.setStatus(FAILED);
        task.setErrorMessage(
                message == null || message.isBlank() ? "烟雾分析失败" : message);
        task.setCompletedAt(LocalDateTime.now());
        task.setUpdatedAt(LocalDateTime.now());
        update(task);
    }

    private String normalizeActiveStatus(String status) {
        return RUNNING.equals(status) ? RUNNING : PENDING;
    }

    private void update(VideoAnalysisTask task) {
        if (taskMapper.updateById(task) != 1) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "分析任务状态保存失败");
        }
    }

    /** 查询一个视频创建时间最新的分析记录。 */
    private VideoAnalysisTask findLatest(Long videoId) {
        return taskMapper.selectOne(
                new LambdaQueryWrapper<VideoAnalysisTask>()
                        .eq(VideoAnalysisTask::getVideoId, videoId)
                        .orderByDesc(VideoAnalysisTask::getCreatedAt)
                        .last("LIMIT 1"));
    }

    /** 把网页请求的产物类型映射成数据库中保存的 FastAPI 相对路径。 */
    private String artifactPath(VideoAnalysisTask task, String artifactType) {
        return switch (artifactType) {
            case "annotated" -> task.getAnnotatedVideoPath();
            case "detections" -> task.getDetectionResultsPath();
            case "keyframe:first" -> task.getFirstKeyframePath();
            case "keyframe:max" -> task.getMaxKeyframePath();
            case "keyframe:last" -> task.getLastKeyframePath();
            default -> throw new BusinessException(
                    HttpStatus.NOT_FOUND,
                    "未知的分析产物类型");
        };
    }

    private BusinessException serviceUnavailable(AnalysisClientException exception) {
        return new BusinessException(
                HttpStatus.SERVICE_UNAVAILABLE,
                exception.getMessage(),
                exception);
    }
}
