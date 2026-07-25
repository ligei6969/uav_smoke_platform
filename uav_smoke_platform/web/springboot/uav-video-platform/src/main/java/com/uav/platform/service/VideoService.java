package com.uav.platform.service;

import com.uav.platform.dto.VideoResponse;
import com.uav.platform.dto.VideoUpdateRequest;
import com.uav.platform.dto.VideoUploadRequest;
import org.springframework.core.io.Resource;

import java.util.List;

public interface VideoService {

    List<VideoResponse> listVideos();

    VideoResponse getVideo(Long id);

    VideoResponse upload(VideoUploadRequest request);

    VideoResponse update(Long id, VideoUpdateRequest request);

    void delete(Long id);

    Resource loadMedia(Long id);
}
