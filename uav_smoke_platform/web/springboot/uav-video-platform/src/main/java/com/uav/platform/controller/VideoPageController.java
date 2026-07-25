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

@Controller
@RequestMapping("/videos")
public class VideoPageController {

    private final VideoService videoService;

    public VideoPageController(VideoService videoService) {
        this.videoService = videoService;
    }

    @GetMapping
    public String list(Model model) {
        model.addAttribute("videos", videoService.listVideos());
        return "videos/list";
    }

    @GetMapping("/upload")
    public String uploadForm(Model model) {
        if (!model.containsAttribute("form")) {
            model.addAttribute("form", new VideoUploadRequest());
        }
        return "videos/upload";
    }

    @PostMapping("/upload")
    public String upload(
            @Valid @ModelAttribute("form") VideoUploadRequest request,
            BindingResult bindingResult,
            RedirectAttributes redirectAttributes) {
        if (bindingResult.hasErrors()) {
            return "videos/upload";
        }
        VideoResponse created = videoService.upload(request);
        redirectAttributes.addFlashAttribute("message", "视频上传成功");
        return "redirect:/videos/" + created.id();
    }

    @GetMapping("/{id}")
    public String detail(@PathVariable Long id, Model model) {
        model.addAttribute("video", videoService.getVideo(id));
        return "videos/detail";
    }

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

    @PostMapping("/{id}/delete")
    public String delete(@PathVariable Long id, RedirectAttributes redirectAttributes) {
        videoService.delete(id);
        redirectAttributes.addFlashAttribute("message", "视频及本地文件已删除");
        return "redirect:/videos";
    }
}
