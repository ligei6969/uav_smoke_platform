package com.uav.platform.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

import java.time.Duration;

/**
 * Spring Boot 调用 FastAPI 时使用的连接配置。
 *
 * <p>配置集中放在这里，可以在不同环境中更换地址和超时时间，而不需要修改业务代码。</p>
 */
@ConfigurationProperties(prefix = "app.analysis")
public class AnalysisProperties {

    /** FastAPI 服务根地址。 */
    private String baseUrl = "http://127.0.0.1:8000";
    /** 建立 TCP 连接允许等待的最长时间。 */
    private Duration connectTimeout = Duration.ofSeconds(5);
    /** 单次 HTTP 请求允许等待的最长时间，视频上传和分析结果下载可能比较耗时。 */
    private Duration requestTimeout = Duration.ofMinutes(30);

    public String getBaseUrl() {
        return baseUrl;
    }

    public void setBaseUrl(String baseUrl) {
        this.baseUrl = baseUrl;
    }

    public Duration getConnectTimeout() {
        return connectTimeout;
    }

    public void setConnectTimeout(Duration connectTimeout) {
        this.connectTimeout = connectTimeout;
    }

    public Duration getRequestTimeout() {
        return requestTimeout;
    }

    public void setRequestTimeout(Duration requestTimeout) {
        this.requestTimeout = requestTimeout;
    }
}
