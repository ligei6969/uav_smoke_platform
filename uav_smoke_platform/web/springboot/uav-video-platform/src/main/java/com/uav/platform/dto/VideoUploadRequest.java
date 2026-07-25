package com.uav.platform.dto;

import jakarta.validation.constraints.Size;
import org.springframework.web.multipart.MultipartFile;

public class VideoUploadRequest {

    @Size(max = 200, message = "视频标题不能超过 200 个字符")
    private String title;
    @Size(max = 5000, message = "视频描述不能超过 5000 个字符")
    private String description;
    private MultipartFile file;

    public String getTitle() {
        return title;
    }

    public void setTitle(String title) {
        this.title = title;
    }

    public String getDescription() {
        return description;
    }

    public void setDescription(String description) {
        this.description = description;
    }

    public MultipartFile getFile() {
        return file;
    }

    public void setFile(MultipartFile file) {
        this.file = file;
    }
}
