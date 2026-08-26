package com.uav.platform.exception;

import org.springframework.http.HttpStatus;

/**
 * 可预期的业务异常，例如视频不存在、重复提交分析或上传格式错误。
 *
 * <p>异常携带 HTTP 状态码，统一异常处理器会把它转换成 JSON 或错误页面。</p>
 */
public class BusinessException extends RuntimeException {

    private final HttpStatus status;

    /** 默认按 400 Bad Request 处理。 */
    public BusinessException(String message) {
        this(HttpStatus.BAD_REQUEST, message);
    }

    /** 创建带指定 HTTP 状态码的业务异常。 */
    public BusinessException(HttpStatus status, String message) {
        super(message);
        this.status = status;
    }

    /** 保留底层异常，便于调试调用链。 */
    public BusinessException(HttpStatus status, String message, Throwable cause) {
        super(message, cause);
        this.status = status;
    }

    public HttpStatus getStatus() {
        return status;
    }
}
