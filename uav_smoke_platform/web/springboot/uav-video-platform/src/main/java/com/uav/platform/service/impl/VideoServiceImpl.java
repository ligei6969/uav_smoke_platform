package com.uav.platform.service.impl;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.toolkit.IdWorker;
import com.uav.platform.dto.VideoResponse;
import com.uav.platform.dto.VideoUpdateRequest;
import com.uav.platform.dto.VideoUploadRequest;
import com.uav.platform.entity.UavVideo;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.mapper.UavVideoMapper;
import com.uav.platform.service.FileStorageService;
import com.uav.platform.service.VideoService;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.util.StringUtils;
import org.springframework.web.multipart.MultipartFile;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Locale;

/** 视频管理业务的本地文件加 MySQL 实现。 */
@Service
public class VideoServiceImpl implements VideoService {

    private static final String MP4_CONTENT_TYPE = "video/mp4";

    // Mapper 操作数据库，FileStorageService 操作磁盘文件。
    private final UavVideoMapper videoMapper;
    private final FileStorageService fileStorageService;

    public VideoServiceImpl(UavVideoMapper videoMapper, FileStorageService fileStorageService) {
        this.videoMapper = videoMapper;
        this.fileStorageService = fileStorageService;
    }

    /** 按创建时间倒序查询，并把 Entity 转成对外使用的 DTO。 */
    @Override
    public List<VideoResponse> listVideos() {
        return videoMapper.selectList(
                        new LambdaQueryWrapper<UavVideo>()
                                .orderByDesc(UavVideo::getCreatedAt))
                .stream()
                .map(VideoResponse::from)
                .toList();
    }

    /** 查询单个视频。 */
    @Override
    public VideoResponse getVideo(Long id) {
        return VideoResponse.from(requireVideo(id));
    }

    /**
     * 保存上传视频。@Transactional 保证数据库操作失败时事务可以回滚；
     * 磁盘文件不属于数据库事务，因此 catch 中还要手动清理文件。
     */
    @Override
    @Transactional
    public VideoResponse upload(VideoUploadRequest request) {
        MultipartFile file = request == null ? null : request.getFile();
        validateFile(file);

        String originalFilename = StringUtils.cleanPath(
                file.getOriginalFilename() == null ? "video.mp4" : file.getOriginalFilename());
        String title = resolveTitle(request.getTitle(), originalFilename);
        String description = trimToNull(request.getDescription());
        if (description != null && description.length() > 5000) {
            throw new BusinessException("视频描述不能超过 5000 个字符");
        }
        // 先生成 ID，这样磁盘目录和数据库主键可以使用同一个标识。
        Long id = IdWorker.getId();
        String filePath = fileStorageService.store(id, file);
        LocalDateTime now = LocalDateTime.now();

        UavVideo video = new UavVideo();
        video.setId(id);
        video.setTitle(title);
        video.setDescription(description);
        video.setOriginalFilename(originalFilename);
        video.setStoredFilename("original.mp4");
        video.setFilePath(filePath);
        video.setFileSize(file.getSize());
        video.setContentType(MP4_CONTENT_TYPE);
        video.setStatus("NORMAL");
        video.setCreatedAt(now);
        video.setUpdatedAt(now);
        video.setDeleted(0);

        try {
            if (videoMapper.insert(video) != 1) {
                throw new BusinessException(
                        HttpStatus.INTERNAL_SERVER_ERROR,
                        "视频元信息保存失败");
            }
        } catch (RuntimeException exception) {
            // 数据库保存失败时删除已经写入的文件，避免留下无主文件。
            try {
                fileStorageService.delete(filePath);
            } catch (RuntimeException cleanupException) {
                exception.addSuppressed(cleanupException);
            }
            throw exception;
        }
        return VideoResponse.from(video);
    }

    /** 更新允许用户修改的字段，不允许修改磁盘路径等系统字段。 */
    @Override
    @Transactional
    public VideoResponse update(Long id, VideoUpdateRequest request) {
        UavVideo video = requireVideo(id);
        String title = request == null ? null : trimToNull(request.getTitle());
        if (title == null) {
            throw new BusinessException("视频标题不能为空");
        }
        if (title.length() > 200) {
            throw new BusinessException("视频标题不能超过 200 个字符");
        }
        String description = trimToNull(request.getDescription());
        if (description != null && description.length() > 5000) {
            throw new BusinessException("视频描述不能超过 5000 个字符");
        }
        video.setTitle(title);
        video.setDescription(description);
        video.setUpdatedAt(LocalDateTime.now());
        if (videoMapper.updateById(video) != 1) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "视频信息更新失败");
        }
        return VideoResponse.from(video);
    }

    /** 先更新业务状态，再执行 MyBatis-Plus 逻辑删除，最后删除磁盘文件。 */
    @Override
    @Transactional
    public void delete(Long id) {
        UavVideo video = requireVideo(id);
        video.setStatus("DELETED");
        video.setUpdatedAt(LocalDateTime.now());
        if (videoMapper.updateById(video) != 1) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "视频状态更新失败");
        }
        if (videoMapper.deleteById(id) != 1) {
            throw new BusinessException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "视频逻辑删除失败");
        }
        fileStorageService.delete(video.getFilePath());
    }

    @Override
    public Resource loadMedia(Long id) {
        return fileStorageService.load(requireVideo(id).getFilePath());
    }

    /** 统一完成 ID 校验和“记录必须存在”的检查。 */
    private UavVideo requireVideo(Long id) {
        if (id == null || id <= 0) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "视频不存在");
        }
        UavVideo video = videoMapper.selectById(id);
        if (video == null) {
            throw new BusinessException(HttpStatus.NOT_FOUND, "视频不存在");
        }
        return video;
    }

    /** 当前版本只接受扩展名和 Content-Type 都正确的 MP4。 */
    private void validateFile(MultipartFile file) {
        if (file == null || file.isEmpty()) {
            throw new BusinessException("上传文件不能为空");
        }
        String filename = file.getOriginalFilename();
        String extension = StringUtils.getFilenameExtension(filename);
        if (extension == null || !"mp4".equals(extension.toLowerCase(Locale.ROOT))) {
            throw new BusinessException("只允许上传 MP4 文件");
        }
        if (!MP4_CONTENT_TYPE.equalsIgnoreCase(file.getContentType())) {
            throw new BusinessException("文件媒体类型必须为 video/mp4");
        }
    }

    private String resolveTitle(String title, String originalFilename) {
        String normalized = trimToNull(title);
        if (normalized == null) {
            String filename = StringUtils.getFilename(originalFilename);
            normalized = filename == null ? "未命名视频" :
                    filename.replaceFirst("(?i)\\.mp4$", "");
        }
        if (normalized.length() > 200) {
            throw new BusinessException("视频标题不能超过 200 个字符");
        }
        return normalized;
    }

    private String trimToNull(String value) {
        return value == null || value.trim().isEmpty() ? null : value.trim();
    }
}
