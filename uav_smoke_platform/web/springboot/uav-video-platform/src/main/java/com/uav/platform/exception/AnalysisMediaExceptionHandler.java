package com.uav.platform.exception;

import com.uav.platform.common.Result;
import com.uav.platform.controller.AnalysisMediaController;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

/**
 * 分析媒体代理的异常处理器。
 *
 * <p>这里只处理流开始前发生的业务异常，避免媒体接口错误时渲染 HTML 页面。</p>
 */
@RestControllerAdvice(assignableTypes = AnalysisMediaController.class)
public class AnalysisMediaExceptionHandler {

    /** 将产物不存在、任务未完成等错误转换成 JSON。 */
    @ExceptionHandler(BusinessException.class)
    public ResponseEntity<Result<Void>> handleBusiness(BusinessException exception) {
        return ResponseEntity
                .status(exception.getStatus())
                .body(Result.failure(exception.getMessage()));
    }
}
