package com.uav.platform.service;

import com.uav.platform.dto.VideoResponse;
import com.uav.platform.dto.VideoUpdateRequest;
import com.uav.platform.dto.VideoUploadRequest;
import org.springframework.core.io.Resource;

import java.util.List;

/** 视频管理业务接口。Controller 只依赖接口，不关心文件和数据库的实现细节。 */
public interface VideoService {

    /** 查询视频列表。 */
    List<VideoResponse> listVideos();

    /** 根据 ID 查询视频，不存在时抛出业务异常。 */
    VideoResponse getVideo(Long id);

    /** 校验并保存上传视频。 */
    VideoResponse upload(VideoUploadRequest request);

    /** 修改视频标题和描述。 */
    VideoResponse update(Long id, VideoUpdateRequest request);

    /** 逻辑删除数据库记录并删除本地文件。 */
    void delete(Long id);

    /** 加载原始视频文件供播放或提交分析。 */
    Resource loadMedia(Long id);
}
