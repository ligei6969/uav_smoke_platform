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

CREATE TABLE IF NOT EXISTS `video_analysis_task` (
    `id` BIGINT NOT NULL COMMENT '分析记录 ID（应用生成）',
    `video_id` BIGINT NOT NULL COMMENT '关联视频 ID',
    `remote_task_id` VARCHAR(64) NULL COMMENT 'FastAPI 分析任务 ID',
    `status` VARCHAR(20) NOT NULL COMMENT 'PENDING、RUNNING、COMPLETED 或 FAILED',
    `error_message` VARCHAR(2000) NULL COMMENT '失败原因',
    `submitted_at` DATETIME(6) NOT NULL COMMENT '提交时间',
    `started_at` DATETIME(6) NULL COMMENT '开始推理时间',
    `completed_at` DATETIME(6) NULL COMMENT '完成或失败时间',
    `smoke_detected` TINYINT(1) NULL COMMENT '是否检测到烟雾',
    `fps` DOUBLE NULL,
    `frame_count` INT UNSIGNED NULL,
    `duration_sec` DOUBLE NULL,
    `positive_frame_count` INT UNSIGNED NULL,
    `positive_frame_ratio` DOUBLE NULL,
    `first_detection_sec` DOUBLE NULL,
    `last_detection_sec` DOUBLE NULL,
    `max_confidence` DOUBLE NULL,
    `mean_confidence` DOUBLE NULL,
    `annotated_video_path` VARCHAR(500) NULL,
    `detection_results_path` VARCHAR(500) NULL,
    `first_keyframe_path` VARCHAR(500) NULL,
    `max_keyframe_path` VARCHAR(500) NULL,
    `last_keyframe_path` VARCHAR(500) NULL,
    `created_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    `updated_at` DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
        ON UPDATE CURRENT_TIMESTAMP(6),
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_video_analysis_remote_task_id` (`remote_task_id`),
    KEY `idx_video_analysis_video_created` (`video_id`, `created_at` DESC),
    CONSTRAINT `fk_video_analysis_video`
        FOREIGN KEY (`video_id`) REFERENCES `uav_video` (`id`),
    CONSTRAINT `chk_video_analysis_status`
        CHECK (`status` IN ('PENDING', 'RUNNING', 'COMPLETED', 'FAILED')),
    CONSTRAINT `chk_video_analysis_smoke_detected`
        CHECK (`smoke_detected` IS NULL OR `smoke_detected` IN (0, 1)),
    CONSTRAINT `chk_video_analysis_positive_ratio`
        CHECK (`positive_frame_ratio` IS NULL
            OR (`positive_frame_ratio` >= 0 AND `positive_frame_ratio` <= 1))
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci
  COMMENT='视频烟雾分析任务及结果';
