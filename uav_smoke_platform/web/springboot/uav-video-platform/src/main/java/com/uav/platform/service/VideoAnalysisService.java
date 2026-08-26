package com.uav.platform.service;

import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.dto.VideoAnalysisResponse;

/** 视频分析业务接口，连接本地视频记录、FastAPI 任务和分析结果。 */
public interface VideoAnalysisService {

    /** 为从未分析过的视频创建任务。 */
    VideoAnalysisResponse start(Long videoId);

    /** 查询最新分析记录，活动任务会同时向 FastAPI 同步状态。 */
    VideoAnalysisResponse getLatest(Long videoId);

    /** 为最近一次失败任务创建新的分析尝试。 */
    VideoAnalysisResponse retry(Long videoId);

    /** 从 FastAPI 打开分析产物的数据流。 */
    RemoteArtifact loadArtifact(
            Long videoId,
            String artifactType,
            String rangeHeader);
}
