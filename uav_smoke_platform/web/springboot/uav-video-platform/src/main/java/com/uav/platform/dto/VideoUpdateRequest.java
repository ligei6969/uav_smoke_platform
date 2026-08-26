package com.uav.platform.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

/** 接收“修改视频信息”表单，只允许修改标题和描述。 */
public class VideoUpdateRequest {

    // @NotBlank 不允许 null、空字符串或只有空格的字符串。
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
