package com.uav.platform.exception;

import com.uav.platform.common.Result;
import com.uav.platform.controller.VideoApiController;
import com.uav.platform.controller.VideoAnalysisController;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

/** 把视频 REST API 抛出的异常统一转换成 Result JSON。 */
@RestControllerAdvice(assignableTypes = {
        VideoApiController.class,
        VideoAnalysisController.class
})
public class ApiExceptionHandler {

    /** 业务异常使用异常中指定的状态码和提示。 */
    @ExceptionHandler(BusinessException.class)
    public ResponseEntity<Result<Void>> handleBusiness(BusinessException exception) {
        return ResponseEntity
                .status(exception.getStatus())
                .body(Result.failure(exception.getMessage()));
    }

    /** 未预料的异常对外只返回通用 500，避免泄露服务器内部信息。 */
    @ExceptionHandler(Exception.class)
    public ResponseEntity<Result<Void>> handleUnexpected(Exception exception) {
        return ResponseEntity
                .status(HttpStatus.INTERNAL_SERVER_ERROR)
                .body(Result.failure("服务器处理请求失败"));
    }
}
