package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

import java.nio.file.Path;

/**
 * 视频文件存储配置。
 *
 * <p>字段会自动绑定到 application.yml 中以 {@code app.storage} 开头的配置。</p>
 */
@ConfigurationProperties(prefix = "app.storage")
public class StorageProperties {

    /** 原始视频的存储根目录，默认是项目运行目录下的 storage/videos。 */
    private Path videoRoot = Path.of("storage", "videos");

    public Path getVideoRoot() {
        return videoRoot;
    }

    public void setVideoRoot(Path videoRoot) {
        this.videoRoot = videoRoot;
    }
}
