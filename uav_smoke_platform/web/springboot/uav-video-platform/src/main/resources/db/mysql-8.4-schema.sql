CREATE DATABASE IF NOT EXISTS `uav_smoke_platform`
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_0900_ai_ci;

USE `uav_smoke_platform`;

CREATE TABLE IF NOT EXISTS `uav_video` (
    `id` BIGINT NOT NULL COMMENT '视频 ID（应用生成）',
    `title` VARCHAR(200) NOT NULL COMMENT '视频标题',
    `description` TEXT NULL COMMENT '视频描述',
    `original_filename` VARCHAR(255) NOT NULL COMMENT '上传时的原始文件名',
    `stored_filename` VARCHAR(255) NOT NULL COMMENT '磁盘存储文件名',
    `file_path` VARCHAR(500) NOT NULL COMMENT '相对于视频存储根目录的路径',
    `file_size` BIGINT UNSIGNED NOT NULL COMMENT '文件大小（字节）',
    `content_type` VARCHAR(100) NOT NULL COMMENT '媒体类型',
    `status` VARCHAR(20) NOT NULL DEFAULT 'NORMAL' COMMENT 'NORMAL 或 DELETED',
    `created_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    `updated_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    `deleted` TINYINT(1) NOT NULL DEFAULT 0 COMMENT 'MyBatis-Plus 逻辑删除标记',
    PRIMARY KEY (`id`),
    KEY `idx_uav_video_deleted_created_at` (`deleted`, `created_at`),
    CONSTRAINT `chk_uav_video_status` CHECK (`status` IN ('NORMAL', 'DELETED')),
    CONSTRAINT `chk_uav_video_file_size` CHECK (`file_size` > 0),
    CONSTRAINT `chk_uav_video_deleted` CHECK (`deleted` IN (0, 1))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci
  COMMENT='无人机视频元信息';
