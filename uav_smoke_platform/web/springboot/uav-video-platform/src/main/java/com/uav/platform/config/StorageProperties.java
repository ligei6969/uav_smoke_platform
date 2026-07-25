package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

import java.nio.file.Path;

@ConfigurationProperties(prefix = "app.storage")
public class StorageProperties {

    private Path videoRoot = Path.of("storage", "videos");

    public Path getVideoRoot() {
        return videoRoot;
    }

    public void setVideoRoot(Path videoRoot) {
        this.videoRoot = videoRoot;
    }
}
