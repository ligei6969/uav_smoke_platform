package com.uav.platform.client;

import org.springframework.core.io.Resource;

import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/**
 * 烟雾分析服务客户端抽象。
 *
 * <p>业务层只依赖这个接口，因此单元测试可以使用模拟客户端，不必真的启动 FastAPI。</p>
 */
public interface SmokeAnalysisClient {

    /** 上传视频并创建异步分析任务。 */
    CreateTaskResponse createTask(Resource video, String originalFilename);

    /** 查询任务当前状态。 */
    TaskStatusResponse getTaskStatus(String taskId);

    /** 获取已完成任务的统计和产物路径。 */
    AnalysisResultResponse getTaskResult(String taskId);

    /** 打开标注视频、JSON 或关键帧的数据流。 */
    RemoteArtifact openArtifact(String taskId, String path, String rangeHeader);

    /** FastAPI 创建任务接口的返回值。 */
    record CreateTaskResponse(String taskId, String status) {
    }

    /** FastAPI 状态查询接口的返回值。 */
    record TaskStatusResponse(
            String taskId,
            String status,
            String originalFilename,
            String errorMessage) {
    }

    /** FastAPI 汇总的检测统计。包装类型 Double 允许“没有检测结果”时返回 null。 */
    record VideoSummary(
            boolean smokeDetected,
            double fps,
            int frameCount,
            double durationSec,
            int positiveFrameCount,
            double positiveFrameRatio,
            Double firstDetectionSec,
            Double lastDetectionSec,
            Double maxConfidence,
            Double meanConfidence) {
    }

    /** 一个关键帧的业务类型和相对文件路径。 */
    record KeyframeArtifact(String type, String path) {
    }

    /** FastAPI 生成的全部文件产物。 */
    record ResultArtifacts(
            String annotatedVideo,
            String detectionResults,
            List<KeyframeArtifact> keyframes) {
    }

    /** 已完成分析任务的完整返回值。 */
    record AnalysisResultResponse(
            String taskId,
            String status,
            VideoSummary videoSummary,
            ResultArtifacts artifacts) {
    }

    /**
     * 远端文件响应。InputStream 支持边读边转发，AutoCloseable 确保连接最终关闭。
     */
    record RemoteArtifact(
            InputStream body,
            int statusCode,
            String contentType,
            long contentLength,
            String contentRange,
            String acceptRanges) implements AutoCloseable {

        @Override
        public void close() throws IOException {
            // 关闭响应流也会释放 HTTP 连接占用的资源。
            body.close();
        }
    }
}
