package com.uav.platform.service.impl;

import com.uav.platform.config.StorageProperties;
import com.uav.platform.exception.BusinessException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mock.web.MockMultipartFile;

import java.nio.file.Files;
import java.nio.file.Path;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

class LocalFileStorageServiceTest {

    @TempDir
    Path temporaryDirectory;

    @Test
    void storesLoadsAndDeletesUnderConfiguredRoot() throws Exception {
        Path root = temporaryDirectory.resolve("videos");
        LocalFileStorageService service = service(root);
        MockMultipartFile file = new MockMultipartFile(
                "file", "client-name.mp4", "video/mp4", new byte[]{1, 2, 3});

        String relativePath = service.store(99L, file);

        assertThat(relativePath).isEqualTo("99/original.mp4");
        assertThat(service.load(relativePath).contentLength()).isEqualTo(3);
        assertThat(root.resolve("99/original.mp4")).exists();

        service.delete(relativePath);

        assertThat(Files.exists(root.resolve("99/original.mp4"))).isFalse();
    }

    @Test
    void rejectsPathTraversal() {
        LocalFileStorageService service = service(temporaryDirectory.resolve("videos"));

        assertThatThrownBy(() -> service.load("../secret.mp4"))
                .isInstanceOf(BusinessException.class)
                .hasMessage("非法的视频文件路径");
    }

    private LocalFileStorageService service(Path root) {
        StorageProperties properties = new StorageProperties();
        properties.setVideoRoot(root);
        return new LocalFileStorageService(properties);
    }
}
