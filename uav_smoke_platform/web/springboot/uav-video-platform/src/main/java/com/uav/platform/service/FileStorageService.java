package com.uav.platform.service;

import org.springframework.core.io.Resource;
import org.springframework.web.multipart.MultipartFile;

public interface FileStorageService {

    String store(Long videoId, MultipartFile file);

    Resource load(String relativePath);

    void delete(String relativePath);
}
