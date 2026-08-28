package com.uav.platform.dto;

import com.uav.platform.entity.UavVideo;
import tools.jackson.databind.annotation.JsonSerialize;
import tools.jackson.databind.ser.std.ToStringSerializer;

import java.time.LocalDateTime;

/** 返回给页面或 API 调用方的视频信息，不暴露服务器上的真实文件路径。 */
public record VideoResponse(
        @JsonSerialize(using = ToStringSerializer.class)
        Long id,
        String title,
        String description,
        String originalFilename,
        Long fileSize,
        String contentType,
        String status,
        LocalDateTime createdAt,
        LocalDateTime updatedAt) {

    /** 将数据库实体转换成对外返回对象。 */
    public static VideoResponse from(UavVideo video) {
        return new VideoResponse(
                video.getId(),
                video.getTitle(),
                video.getDescription(),
                video.getOriginalFilename(),
                video.getFileSize(),
                video.getContentType(),
                video.getStatus(),
                video.getCreatedAt(),
                video.getUpdatedAt());
    }
}
