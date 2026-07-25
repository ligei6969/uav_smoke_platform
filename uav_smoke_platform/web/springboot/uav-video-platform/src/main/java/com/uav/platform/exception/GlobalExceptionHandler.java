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

@ControllerAdvice(assignableTypes = {
        HomeController.class,
        VideoPageController.class,
        MediaController.class
})
public class GlobalExceptionHandler {

    @ExceptionHandler(BusinessException.class)
    public ModelAndView handleBusiness(
            BusinessException exception,
            HttpServletRequest request) {
        return errorView(exception.getStatus(), exception.getMessage(), request);
    }

    @ExceptionHandler(MaxUploadSizeExceededException.class)
    public ModelAndView handleUploadLimit(
            MaxUploadSizeExceededException exception,
            HttpServletRequest request) {
        return errorView(
                HttpStatus.PAYLOAD_TOO_LARGE,
                "上传文件超过 2GB 限制",
                request);
    }

    @ExceptionHandler(Exception.class)
    public ModelAndView handleUnexpected(
            Exception exception,
            HttpServletRequest request) {
        return errorView(
                HttpStatus.INTERNAL_SERVER_ERROR,
                "服务器处理请求失败",
                request);
    }

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
