package com.uav.platform.exception;

import com.uav.platform.controller.HomeController;
import com.uav.platform.controller.MediaController;
import com.uav.platform.controller.VideoPageController;
import jakarta.servlet.http.HttpServletRequest;
import org.springframework.http.HttpStatus;
import org.springframework.web.bind.annotation.ControllerAdvice;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.multipart.MaxUploadSizeExceededException;
import org.springframework.web.servlet.ModelAndView;

import java.time.LocalDateTime;

/** 把页面 Controller 的异常转换成 Thymeleaf error.html 页面。 */
@ControllerAdvice(assignableTypes = {
        HomeController.class,
        VideoPageController.class,
        MediaController.class
})
public class GlobalExceptionHandler {

    /** 显示业务异常的真实提示和对应 HTTP 状态码。 */
    @ExceptionHandler(BusinessException.class)
    public ModelAndView handleBusiness(
            BusinessException exception,
            HttpServletRequest request) {
        return errorView(exception.getStatus(), exception.getMessage(), request);
    }

    /** 单独处理超过 Spring multipart 大小限制的上传。 */
    @ExceptionHandler(MaxUploadSizeExceededException.class)
    public ModelAndView handleUploadLimit(
            MaxUploadSizeExceededException exception,
            HttpServletRequest request) {
        return errorView(
                HttpStatus.PAYLOAD_TOO_LARGE,
                "上传文件超过 2GB 限制",
                request);
    }

    /** 其他异常显示通用 500 页面。 */
    @ExceptionHandler(Exception.class)
    public ModelAndView handleUnexpected(
            Exception exception,
            HttpServletRequest request) {
        return errorView(
                HttpStatus.INTERNAL_SERVER_ERROR,
                "服务器处理请求失败",
                request);
    }

    /** 组装 error.html 模板需要的数据。 */
    private ModelAndView errorView(
            HttpStatus status,
            String message,
            HttpServletRequest request) {
        ModelAndView modelAndView = new ModelAndView("error");
        modelAndView.setStatus(status);
        modelAndView.addObject("status", status.value());
        modelAndView.addObject("error", status.getReasonPhrase());
        modelAndView.addObject("message", message);
        modelAndView.addObject("path", request.getRequestURI());
        modelAndView.addObject("timestamp", LocalDateTime.now());
        return modelAndView;
    }
}
