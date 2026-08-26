package com.uav.platform.controller;

import com.uav.platform.service.VideoService;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;

/** 首页页面控制器。@Controller 方法通常返回 Thymeleaf 模板名称。 */
@Controller
public class HomeController {

    private final VideoService videoService;

    /** 构造器注入：Spring 会自动把 VideoService 实现对象传进来。 */
    public HomeController(VideoService videoService) {
        this.videoService = videoService;
    }

    /** 处理 GET /，把视频数量放进模板数据 Model。 */
    @GetMapping("/")
    public String index(Model model) {
        model.addAttribute("videoCount", videoService.listVideos().size());
        // 返回 src/main/resources/templates/index.html，不是直接返回字符串给浏览器。
        return "index";
    }
}
