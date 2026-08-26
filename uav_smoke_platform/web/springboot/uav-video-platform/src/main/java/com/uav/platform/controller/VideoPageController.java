package com.uav.platform.controller;

import com.uav.platform.dto.VideoResponse;
import com.uav.platform.dto.VideoUpdateRequest;
import com.uav.platform.dto.VideoUploadRequest;
import com.uav.platform.service.VideoService;
import jakarta.validation.Valid;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.validation.BindingResult;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.ModelAttribute;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.servlet.mvc.support.RedirectAttributes;

/**
 * 视频管理页面控制器。
 *
 * <p>负责 HTML 页面跳转和表单提交，具体业务交给 VideoService。</p>
 */
@Controller
@RequestMapping("/videos")
public class VideoPageController {

    private final VideoService videoService;

    public VideoPageController(VideoService videoService) {
        this.videoService = videoService;
    }

    /** 显示视频列表页面 GET /videos。 */
    @GetMapping
    public String list(Model model) {
        model.addAttribute("videos", videoService.listVideos());
        return "videos/list";
    }

    /** 显示上传表单 GET /videos/upload。 */
    @GetMapping("/upload")
    public String uploadForm(Model model) {
        if (!model.containsAttribute("form")) {
            model.addAttribute("form", new VideoUploadRequest());
        }
        return "videos/upload";
    }

    /**
     * 接收上传表单 POST /videos/upload。
     * @Valid 执行 DTO 上的校验规则，错误信息由 BindingResult 保存。
     */
    @PostMapping("/upload")
    public String upload(
            @Valid @ModelAttribute("form") VideoUploadRequest request,
            BindingResult bindingResult,
            RedirectAttributes redirectAttributes) {
        if (bindingResult.hasErrors()) {
            // 校验失败时不进入 Service，继续显示原表单和错误提示。
            return "videos/upload";
        }
        VideoResponse created = videoService.upload(request);
        redirectAttributes.addFlashAttribute("message", "视频上传成功");
        // redirect 会让浏览器发起新请求，防止刷新页面时重复提交文件。
        return "redirect:/videos/" + created.id();
    }

    /** 显示指定视频的详情页 GET /videos/{id}。 */
    @GetMapping("/{id}")
    public String detail(@PathVariable Long id, Model model) {
        model.addAttribute("video", videoService.getVideo(id));
        return "videos/detail";
    }

    /** 显示编辑表单，并用现有数据初始化表单对象。 */
    @GetMapping("/{id}/edit")
    public String editForm(@PathVariable Long id, Model model) {
        VideoResponse video = videoService.getVideo(id);
        VideoUpdateRequest form = new VideoUpdateRequest();
        form.setTitle(video.title());
        form.setDescription(video.description());
        model.addAttribute("video", video);
        model.addAttribute("form", form);
        return "videos/edit";
    }

    /** 接收视频标题和描述的修改。 */
    @PostMapping("/{id}/edit")
    public String update(
            @PathVariable Long id,
            @Valid @ModelAttribute("form") VideoUpdateRequest request,
            BindingResult bindingResult,
            Model model,
            RedirectAttributes redirectAttributes) {
        if (bindingResult.hasErrors()) {
            model.addAttribute("video", videoService.getVideo(id));
            return "videos/edit";
        }
        videoService.update(id, request);
        redirectAttributes.addFlashAttribute("message", "视频信息已更新");
        return "redirect:/videos/" + id;
    }

    /** 删除视频数据库记录和本地原始文件。 */
    @PostMapping("/{id}/delete")
    public String delete(@PathVariable Long id, RedirectAttributes redirectAttributes) {
        videoService.delete(id);
        redirectAttributes.addFlashAttribute("message", "视频及本地文件已删除");
        return "redirect:/videos";
    }
}
