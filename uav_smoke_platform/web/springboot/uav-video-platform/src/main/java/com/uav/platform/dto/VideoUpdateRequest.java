package com.uav.platform.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

public class VideoUpdateRequest {

    @NotBlank(message = "视频标题不能为空")
    @Size(max = 200, message = "视频标题不能超过 200 个字符")
    private String title;
    @Size(max = 5000, message = "视频描述不能超过 5000 个字符")
    private String description;

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
}
