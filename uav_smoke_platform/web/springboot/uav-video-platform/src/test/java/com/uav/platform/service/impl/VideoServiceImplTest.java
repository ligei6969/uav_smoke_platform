package com.uav.platform.service.impl;

import com.uav.platform.dto.VideoUpdateRequest;
import com.uav.platform.dto.VideoUploadRequest;
import com.uav.platform.entity.UavVideo;
import com.uav.platform.exception.BusinessException;
import com.uav.platform.mapper.UavVideoMapper;
import com.uav.platform.service.FileStorageService;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.ArgumentCaptor;
import org.springframework.mock.web.MockMultipartFile;

import java.time.LocalDateTime;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.inOrder;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class VideoServiceImplTest {

    private UavVideoMapper videoMapper;
    private FileStorageService fileStorageService;
    private VideoServiceImpl videoService;

    @BeforeEach
    void setUp() {
        videoMapper = mock(UavVideoMapper.class);
        fileStorageService = mock(FileStorageService.class);
        videoService = new VideoServiceImpl(videoMapper, fileStorageService);
    }

    @Test
    void rejectsEmptyUpload() {
        VideoUploadRequest request = request(
                new MockMultipartFile("file", "empty.mp4", "video/mp4", new byte[0]));

        assertThatThrownBy(() -> videoService.upload(request))
                .isInstanceOf(BusinessException.class)
                .hasMessage("上传文件不能为空");
        verify(videoMapper, never()).insert(any(UavVideo.class));
        verify(fileStorageService, never()).store(anyLong(), any());
    }

    @Test
    void rejectsNonMp4Upload() {
        VideoUploadRequest request = request(
                new MockMultipartFile("file", "notes.txt", "text/plain", "not-video".getBytes()));

        assertThatThrownBy(() -> videoService.upload(request))
                .isInstanceOf(BusinessException.class)
                .hasMessage("只允许上传 MP4 文件");
        verify(videoMapper, never()).insert(any(UavVideo.class));
        verify(fileStorageService, never()).store(anyLong(), any());
    }

    @Test
    void savesMetadataForNormalMp4() {
        MockMultipartFile file = new MockMultipartFile(
                "file",
                "flight-11.mp4",
                "video/mp4",
                new byte[]{0, 0, 0, 24, 102, 116, 121, 112});
        VideoUploadRequest request = request(file);
        request.setTitle("烟雾巡检");
        request.setDescription("11 号航线");
        when(fileStorageService.store(anyLong(), any()))
                .thenAnswer(invocation -> invocation.getArgument(0) + "/original.mp4");
        when(videoMapper.insert(any(UavVideo.class))).thenReturn(1);

        var response = videoService.upload(request);

        ArgumentCaptor<UavVideo> captor = ArgumentCaptor.forClass(UavVideo.class);
        verify(videoMapper).insert(captor.capture());
        UavVideo saved = captor.getValue();
        assertThat(saved.getId()).isPositive();
        assertThat(saved.getTitle()).isEqualTo("烟雾巡检");
        assertThat(saved.getDescription()).isEqualTo("11 号航线");
        assertThat(saved.getOriginalFilename()).isEqualTo("flight-11.mp4");
        assertThat(saved.getStoredFilename()).isEqualTo("original.mp4");
        assertThat(saved.getFilePath()).isEqualTo(saved.getId() + "/original.mp4");
        assertThat(saved.getFileSize()).isEqualTo(file.getSize());
        assertThat(saved.getContentType()).isEqualTo("video/mp4");
        assertThat(saved.getStatus()).isEqualTo("NORMAL");
        assertThat(saved.getDeleted()).isZero();
        assertThat(response.id()).isEqualTo(saved.getId());
    }

    @Test
    void throwsBusinessExceptionWhenVideoDoesNotExist() {
        when(videoMapper.selectById(404L)).thenReturn(null);

        assertThatThrownBy(() -> videoService.getVideo(404L))
                .isInstanceOf(BusinessException.class)
                .hasMessage("视频不存在");
    }

    @Test
    void performsLogicalDeleteAndDeletesLocalFile() {
        UavVideo video = existingVideo(42L);
        when(videoMapper.selectById(42L)).thenReturn(video);
        when(videoMapper.updateById(video)).thenReturn(1);
        when(videoMapper.deleteById(42L)).thenReturn(1);

        videoService.delete(42L);

        var ordered = inOrder(videoMapper, fileStorageService);
        ordered.verify(videoMapper).updateById(video);
        ordered.verify(videoMapper).deleteById(42L);
        ordered.verify(fileStorageService).delete("42/original.mp4");
        assertThat(video.getStatus()).isEqualTo("DELETED");
    }

    @Test
    void updatesTitleAndDescription() {
        UavVideo video = existingVideo(7L);
        when(videoMapper.selectById(7L)).thenReturn(video);
        when(videoMapper.updateById(video)).thenReturn(1);
        VideoUpdateRequest request = new VideoUpdateRequest();
        request.setTitle("  更新后的标题  ");
        request.setDescription("  新描述  ");

        var updated = videoService.update(7L, request);

        assertThat(updated.title()).isEqualTo("更新后的标题");
        assertThat(updated.description()).isEqualTo("新描述");
    }

    private VideoUploadRequest request(MockMultipartFile file) {
        VideoUploadRequest request = new VideoUploadRequest();
        request.setFile(file);
        return request;
    }

    private UavVideo existingVideo(Long id) {
        UavVideo video = new UavVideo();
        video.setId(id);
        video.setTitle("测试视频");
        video.setOriginalFilename("test.mp4");
        video.setStoredFilename("original.mp4");
        video.setFilePath(id + "/original.mp4");
        video.setFileSize(8L);
        video.setContentType("video/mp4");
        video.setStatus("NORMAL");
        video.setCreatedAt(LocalDateTime.now());
        video.setUpdatedAt(LocalDateTime.now());
        video.setDeleted(0);
        return video;
    }
}
