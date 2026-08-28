package com.uav.platform.service;

import com.uav.platform.dto.VideoGeolocationResponse;

/** 使用视频检测结果和同步遥测计算烟雾目标位置。 */
public interface VideoGeolocationService {

    VideoGeolocationResponse locate(Long videoId, double videoTimeSec);
}
