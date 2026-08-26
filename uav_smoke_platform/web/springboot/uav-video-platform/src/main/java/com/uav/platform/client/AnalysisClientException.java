package com.uav.platform.client;

/** FastAPI 调用失败时使用的内部异常，保留远端 HTTP 状态码。 */
public class AnalysisClientException extends RuntimeException {

    // 0 表示尚未得到 HTTP 响应，例如连接超时或服务未启动。
    private final int statusCode;

    public AnalysisClientException(String message, int statusCode) {
        super(message);
        this.statusCode = statusCode;
    }

    public AnalysisClientException(String message, Throwable cause) {
        super(message, cause);
        this.statusCode = 0;
    }

    public int getStatusCode() {
        return statusCode;
    }
}
