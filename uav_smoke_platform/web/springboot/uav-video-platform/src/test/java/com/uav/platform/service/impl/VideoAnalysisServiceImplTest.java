package com.uav.platform.service.impl;

import com.uav.platform.client.AnalysisClientException;
import com.uav.platform.client.SmokeAnalysisClient;
import com.uav.platform.client.SmokeAnalysisClient.AnalysisResultResponse;
import com.uav.platform.client.SmokeAnalysisClient.KeyframeArtifact;
import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.client.SmokeAnalysisClient.ResultArtifacts;
import com.uav.platform.client.SmokeAnalysisClient.TaskStatusResponse;
import com.uav.platform.client.SmokeAnalysisClient.VideoSummary;
import com.uav.platform.dto.VideoResponse;
import com.uav.platform.entity.VideoAnalysisTask;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.mapper.VideoAnalysisTaskMapper;
import com.uav.platform.service.VideoService;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.springframework.core.io.ByteArrayResource;
import org.springframework.http.HttpStatus;

import java.io.ByteArrayInputStream;
import java.time.LocalDateTime;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

class VideoAnalysisServiceImplTest {

    private VideoAnalysisTaskMapper taskMapper;
    private VideoService videoService;
    private SmokeAnalysisClient analysisClient;
    private VideoAnalysisServiceImpl analysisService;

    @BeforeEach
    void setUp() {
        taskMapper = mock(VideoAnalysisTaskMapper.class);
        videoService = mock(VideoService.class);
        analysisClient = mock(SmokeAnalysisClient.class);
        analysisService = new VideoAnalysisServiceImpl(
                taskMapper, videoService, analysisClient);
    }

    @Test
    void startsAnalysisAndPersistsRemoteTaskId() {
        prepareVideo();
        when(taskMapper.selectOne(any())).thenReturn(null);
        when(taskMapper.insert(any(VideoAnalysisTask.class))).thenReturn(1);
        when(taskMapper.updateById(any(VideoAnalysisTask.class))).thenReturn(1);
        when(analysisClient.createTask(any(), eq("flight.mp4")))
                .thenReturn(new SmokeAnalysisClient.CreateTaskResponse(
                        "remote-1", "PENDING"));

        var response = analysisService.start(42L);

        assertThat(response.status()).isEqualTo("PENDING");
        assertThat(response.taskId()).isEqualTo("remote-1");
        ArgumentCaptor<VideoAnalysisTask> captor =
                ArgumentCaptor.forClass(VideoAnalysisTask.class);
        verify(taskMapper).insert(captor.capture());
        assertThat(captor.getValue().getVideoId()).isEqualTo(42L);
        verify(taskMapper).updateById(captor.getValue());
    }

    @Test
    void rejectsDuplicateActiveAnalysis() {
        prepareVideo();
        when(taskMapper.selectOne(any())).thenReturn(task("RUNNING"));

        assertThatThrownBy(() -> analysisService.start(42L))
                .isInstanceOf(BusinessException.class)
                .hasMessage("该视频已有正在进行的分析任务");
        verifyNoInteractions(analysisClient);
        verify(taskMapper, never()).insert(any(VideoAnalysisTask.class));
    }

    @Test
    void missingVideoIsRejectedBeforeCreatingTask() {
        when(videoService.getVideo(404L)).thenThrow(
                new BusinessException(HttpStatus.NOT_FOUND, "视频不存在"));

        assertThatThrownBy(() -> analysisService.start(404L))
                .isInstanceOf(BusinessException.class)
                .hasMessage("视频不存在");
        verifyNoInteractions(analysisClient);
        verify(taskMapper, never()).insert(any(VideoAnalysisTask.class));
    }

    @Test
    void fastApiUnavailablePersistsFailedAttempt() {
        prepareVideo();
        when(taskMapper.selectOne(any())).thenReturn(null);
        when(taskMapper.insert(any(VideoAnalysisTask.class))).thenReturn(1);
        when(taskMapper.updateById(any(VideoAnalysisTask.class))).thenReturn(1);
        when(analysisClient.createTask(any(), any()))
                .thenThrow(new AnalysisClientException(
                        "无法连接烟雾分析服务",
                        new RuntimeException("connection refused")));

        assertThatThrownBy(() -> analysisService.start(42L))
                .isInstanceOf(BusinessException.class)
                .extracting("status")
                .isEqualTo(HttpStatus.SERVICE_UNAVAILABLE);

        ArgumentCaptor<VideoAnalysisTask> captor =
                ArgumentCaptor.forClass(VideoAnalysisTask.class);
        verify(taskMapper).updateById(captor.capture());
        assertThat(captor.getValue().getStatus()).isEqualTo("FAILED");
        assertThat(captor.getValue().getErrorMessage())
                .isEqualTo("无法连接烟雾分析服务");
    }

    @Test
    void synchronizesFailedRemoteTask() {
        prepareVideo();
        VideoAnalysisTask task = task("RUNNING");
        when(taskMapper.selectOne(any())).thenReturn(task);
        when(taskMapper.updateById(task)).thenReturn(1);
        when(analysisClient.getTaskStatus("remote-1"))
                .thenReturn(new TaskStatusResponse(
                        "remote-1", "FAILED", "flight.mp4", "GPU error"));

        var response = analysisService.getLatest(42L);

        assertThat(response.status()).isEqualTo("FAILED");
        assertThat(response.errorMessage()).isEqualTo("GPU error");
        assertThat(task.getCompletedAt()).isNotNull();
    }

    @Test
    void completedResultIsSavedWithAllArtifacts() {
        prepareVideo();
        VideoAnalysisTask task = task("RUNNING");
        when(taskMapper.selectOne(any())).thenReturn(task);
        when(taskMapper.updateById(task)).thenReturn(1);
        when(analysisClient.getTaskStatus("remote-1"))
                .thenReturn(new TaskStatusResponse(
                        "remote-1", "COMPLETED", "flight.mp4", null));
        when(analysisClient.getTaskResult("remote-1")).thenReturn(completedResult());

        var response = analysisService.getLatest(42L);

        assertThat(response.status()).isEqualTo("COMPLETED");
        assertThat(response.smokeDetected()).isTrue();
        assertThat(response.positiveFrameCount()).isEqualTo(30);
        assertThat(response.maxConfidence()).isEqualTo(0.91);
        assertThat(response.annotatedVideoUrl())
                .isEqualTo("/media/videos/42/analysis/annotated?attempt=100");
        assertThat(response.firstKeyframeUrl())
                .isEqualTo("/media/videos/42/analysis/keyframes/first?attempt=100");
        assertThat(task.getDetectionResultsPath())
                .isEqualTo("detection_results.json");
        assertThat(task.getMaxKeyframePath())
                .isEqualTo("keyframes/max_confidence.jpg");
    }

    @Test
    void completedResultSurvivesSpringRestartWithoutFastApiStatusCall() {
        prepareVideo();
        VideoAnalysisTask task = task("COMPLETED");
        task.setSmokeDetected(true);
        task.setAnnotatedVideoPath("annotated_video.mp4");
        when(taskMapper.selectOne(any())).thenReturn(task);

        var response = analysisService.getLatest(42L);

        assertThat(response.status()).isEqualTo("COMPLETED");
        assertThat(response.smokeDetected()).isTrue();
        verifyNoInteractions(analysisClient);
    }

    @Test
    void retriesFailedAnalysisAsNewAttempt() {
        prepareVideo();
        when(taskMapper.selectOne(any())).thenReturn(task("FAILED"));
        when(taskMapper.insert(any(VideoAnalysisTask.class))).thenReturn(1);
        when(taskMapper.updateById(any(VideoAnalysisTask.class))).thenReturn(1);
        when(analysisClient.createTask(any(), any()))
                .thenReturn(new SmokeAnalysisClient.CreateTaskResponse(
                        "remote-retry", "PENDING"));

        var response = analysisService.retry(42L);

        assertThat(response.status()).isEqualTo("PENDING");
        assertThat(response.taskId()).isEqualTo("remote-retry");
        verify(taskMapper).insert(any(VideoAnalysisTask.class));
    }

    @Test
    void forwardsRangeHeaderWhenLoadingAnnotatedVideo() {
        prepareVideo();
        VideoAnalysisTask task = task("COMPLETED");
        task.setAnnotatedVideoPath("annotated_video.mp4");
        when(taskMapper.selectOne(any())).thenReturn(task);
        RemoteArtifact artifact = new RemoteArtifact(
                new ByteArrayInputStream(new byte[]{1, 2, 3, 4}),
                206,
                "video/mp4",
                4,
                "bytes 0-3/10",
                "bytes");
        when(analysisClient.openArtifact(
                "remote-1", "annotated_video.mp4", "bytes=0-3"))
                .thenReturn(artifact);

        var loaded = analysisService.loadArtifact(
                42L, "annotated", "bytes=0-3");

        assertThat(loaded).isSameAs(artifact);
    }

    private void prepareVideo() {
        when(videoService.getVideo(42L)).thenReturn(new VideoResponse(
                42L,
                "测试视频",
                null,
                "flight.mp4",
                8L,
                "video/mp4",
                "NORMAL",
                LocalDateTime.now(),
                LocalDateTime.now()));
        when(videoService.loadMedia(42L))
                .thenReturn(new ByteArrayResource(new byte[]{0, 1, 2, 3}));
    }

    private VideoAnalysisTask task(String status) {
        VideoAnalysisTask task = new VideoAnalysisTask();
        task.setId(100L);
        task.setVideoId(42L);
        task.setRemoteTaskId("remote-1");
        task.setStatus(status);
        task.setSubmittedAt(LocalDateTime.now());
        task.setCreatedAt(LocalDateTime.now());
        task.setUpdatedAt(LocalDateTime.now());
        return task;
    }

    private AnalysisResultResponse completedResult() {
        return new AnalysisResultResponse(
                "remote-1",
                "COMPLETED",
                new VideoSummary(
                        true, 30.0, 300, 10.0, 30, 0.1,
                        1.0, 9.0, 0.91, 0.82),
                new ResultArtifacts(
                        "annotated_video.mp4",
                        "detection_results.json",
                        List.of(
                                new KeyframeArtifact(
                                        "first_detection",
                                        "keyframes/first_detection.jpg"),
                                new KeyframeArtifact(
                                        "max_confidence",
                                        "keyframes/max_confidence.jpg"),
                                new KeyframeArtifact(
                                        "last_detection",
                                        "keyframes/last_detection.jpg"))));
    }
}
