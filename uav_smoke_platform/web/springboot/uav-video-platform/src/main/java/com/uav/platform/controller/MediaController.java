package com.uav.platform.controller;

import com.uav.platform.service.VideoService;
import org.springframework.core.io.Resource;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

/** 提供原始视频文件，供 HTML video 元素播放。 */
@RestController
@RequestMapping("/media/videos")
public class MediaController {

    private final VideoService videoService;

    public MediaController(VideoService videoService) {
        this.videoService = videoService;
    }

    /** GET /media/videos/{id}：读取磁盘文件并以 video/mp4 返回。 */
    @GetMapping("/{id}")
    public ResponseEntity<Resource> video(@PathVariable Long id) throws IOException {
        Resource resource = videoService.loadMedia(id);
        return ResponseEntity.ok()
                .contentType(MediaType.valueOf("video/mp4"))
                .contentLength(resource.contentLength())
                // 告诉浏览器服务器支持按字节读取，便于拖动播放进度条。
                .header(HttpHeaders.ACCEPT_RANGES, "bytes")
                .cacheControl(CacheControl.maxAge(1, TimeUnit.HOURS).cachePrivate())
                .body(resource);
    }
}
