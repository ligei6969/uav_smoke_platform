package com.uav.platform.dto;

import com.uav.platform.entity.UavVideo;

import java.time.LocalDateTime;

public record VideoResponse(
        Long id,
        String title,
        String description,
        String originalFilename,
        Long fileSize,
        String contentType,
        String status,
        LocalDateTime createdAt,
        LocalDateTime updatedAt) {

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
