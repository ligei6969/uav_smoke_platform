package com.uav.platform.controller;

import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.GetMapping;

/** 实时飞行监控页面。 */
@Controller
public class MonitoringPageController {

    @GetMapping("/monitor")
    public String monitor() {
        return "monitor";
    }
}
