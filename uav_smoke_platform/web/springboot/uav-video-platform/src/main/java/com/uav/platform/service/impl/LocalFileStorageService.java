package com.uav.platform.service.impl;

import com.uav.platform.config.StorageProperties;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.service.FileStorageService;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

@Service
public class LocalFileStorageService implements FileStorageService {

    private static final String STORED_FILENAME = "original.mp4";

    private final Path videoRoot;

    public LocalFileStorageService(StorageProperties properties) {
        this.videoRoot = properties.getVideoRoot().toAbsolutePath().normalize();
        try {
            Files.createDirectories(videoRoot);
        } catch (IOException exception) {
            throw new IllegalStateException("无法创建视频存储根目录", exception);
        }
    }

    @Override
    public String store(Long videoId, MultipartFile file) {
        String relativePath = videoId + "/" + STORED_FILENAME;
        Path target = resolveSafe(relativePath);
        try {
            Files.createDirectories(target.getParent());
            try (InputStream inputStream = file.getInputStream()) {
                Files.copy(inputStream, target, StandardCopyOption.REPLACE_EXISTING);
            }
            return relativePath.replace('\\', '/');
        } catch (IOException exception) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "视频文件保存失败",
                    exception);
        }
    }

    @Override
    public Resource load(String relativePath) {
        Path file = resolveSafe(relativePath);
        if (!Files.isRegularFile(file) || !Files.isReadable(file)) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "视频文件不存在");
        }
        return new FileSystemResource(file);
    }

    @Override
    public void delete(String relativePath) {
        Path file = resolveSafe(relativePath);
        if (!Files.exists(file)) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "待删除的视频文件不存在");
        }
        try {
            Files.delete(file);
            Path parent = file.getParent();
            if (parent != null && !parent.equals(videoRoot)) {
                Files.deleteIfExists(parent);
            }
        } catch (IOException exception) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "视频文件删除失败",
                    exception);
        }
    }

    private Path resolveSafe(String relativePath) {
        if (relativePath == null || relativePath.isBlank()) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "视频文件路径为空");
        }
        Path resolved = videoRoot.resolve(relativePath).normalize();
        if (!resolved.startsWith(videoRoot)) {
            throw new BusinessException(HttpStatus.BAD_REQUEST, "非法的视频文件路径");
        }
        return resolved;
    }
}
