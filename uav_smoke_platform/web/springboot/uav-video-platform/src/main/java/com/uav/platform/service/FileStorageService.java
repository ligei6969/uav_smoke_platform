package com.uav.platform.service;

import org.springframework.core.io.Resource;
import org.springframework.web.multipart.MultipartFile;

/** 文件存储抽象。以后可以用对象存储实现替换当前本地磁盘实现。 */
public interface FileStorageService {

    /** 保存文件并返回相对于存储根目录的路径。 */
    String store(Long videoId, MultipartFile file);

    /** 根据相对路径加载文件。 */
    Resource load(String relativePath);

    /** 删除相对路径指向的文件。 */
    void delete(String relativePath);
}
