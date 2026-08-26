package com.uav.platform.client;

import com.uav.platform.config.AnalysisProperties;
import org.springframework.core.io.Resource;
import org.springframework.stereotype.Component;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.util.UUID;

/** 使用 JDK HttpClient 调用 FastAPI 的真实实现。 */
@Component
public class HttpSmokeAnalysisClient implements SmokeAnalysisClient {

    private final AnalysisProperties properties;
    private final ObjectMapper objectMapper;
    private final HttpClient httpClient;

    /** 创建一次可复用的 HTTP 客户端，并从配置读取连接超时。 */
    public HttpSmokeAnalysisClient(
            AnalysisProperties properties,
            ObjectMapper objectMapper) {
        this.properties = properties;
        this.objectMapper = objectMapper;
        this.httpClient = HttpClient.newBuilder()
                .connectTimeout(properties.getConnectTimeout())
                .version(HttpClient.Version.HTTP_1_1)
                .build();
    }

    /** 手工构造 multipart/form-data 请求，把 Resource 视频流发送给 FastAPI。 */
    @Override
    public CreateTaskResponse createTask(Resource video, String originalFilename) {
        // boundary 用于分隔 multipart 请求中的不同部分，每个请求使用随机值。
        String boundary = "uav-smoke-" + UUID.randomUUID();
        String safeFilename = safeFilename(originalFilename);
        byte[] prefix = (
                "--" + boundary + "\r\n"
                        + "Content-Disposition: form-data; name=\"video\"; filename=\""
                        + safeFilename + "\"\r\n"
                        + "Content-Type: video/mp4\r\n\r\n")
                .getBytes(StandardCharsets.UTF_8);
        byte[] suffix = ("\r\n--" + boundary + "--\r\n")
                .getBytes(StandardCharsets.UTF_8);

        // ofInputStream 延迟打开文件，避免把大型视频一次性加载到 JVM 堆内存。
        HttpRequest.BodyPublisher videoBody = HttpRequest.BodyPublishers.ofInputStream(() -> {
            try {
                return video.getInputStream();
            } catch (IOException exception) {
                throw new UncheckedIOException(exception);
            }
        });
        HttpRequest request = request("/api/v1/analysis-tasks")
                .header("Content-Type", "multipart/form-data; boundary=" + boundary)
                .POST(HttpRequest.BodyPublishers.concat(
                        HttpRequest.BodyPublishers.ofByteArray(prefix),
                        videoBody,
                        HttpRequest.BodyPublishers.ofByteArray(suffix)))
                .build();
        return sendJson(request, CreateTaskResponse.class);
    }

    /** 调用 FastAPI 的任务状态接口。 */
    @Override
    public TaskStatusResponse getTaskStatus(String taskId) {
        HttpRequest request = request("/api/v1/analysis-tasks/" + encode(taskId))
                .GET()
                .build();
        return sendJson(request, TaskStatusResponse.class);
    }

    /** 调用 FastAPI 的任务结果接口。 */
    @Override
    public AnalysisResultResponse getTaskResult(String taskId) {
        HttpRequest request = request(
                "/api/v1/analysis-tasks/" + encode(taskId) + "/result")
                .GET()
                .build();
        return sendJson(request, AnalysisResultResponse.class);
    }

    /**
     * 打开远端产物流。标注视频请求会继续携带 Range 头，支持浏览器拖动进度条。
     */
    @Override
    public RemoteArtifact openArtifact(
            String taskId,
            String path,
            String rangeHeader) {
        HttpRequest.Builder builder = request(
                "/api/v1/analysis-tasks/" + encode(taskId)
                        + "/artifacts/" + encodePath(path))
                .GET();
        if (rangeHeader != null && !rangeHeader.isBlank()) {
            // 例如 Range: bytes=0-1023，只请求视频的一小段。
            builder.header("Range", rangeHeader);
        }
        try {
            HttpResponse<InputStream> response = httpClient.send(
                    builder.build(),
                    HttpResponse.BodyHandlers.ofInputStream());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                try (InputStream body = response.body()) {
                    throw new AnalysisClientException(
                            readError(body, response.statusCode()),
                            response.statusCode());
                }
            }
            // 同时保留状态码和媒体响应头，供 Spring Controller 原样转发。
            return new RemoteArtifact(
                    response.body(),
                    response.statusCode(),
                    response.headers().firstValue("Content-Type")
                            .orElse("application/octet-stream"),
                    response.headers().firstValueAsLong("Content-Length")
                            .orElse(-1L),
                    response.headers().firstValue("Content-Range").orElse(null),
                    response.headers().firstValue("Accept-Ranges").orElse("bytes"));
        } catch (IOException exception) {
            throw unavailable(exception);
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw unavailable(exception);
        }
    }

    /** 创建带统一地址、超时时间和 Accept 头的请求构造器。 */
    private HttpRequest.Builder request(String path) {
        return HttpRequest.newBuilder(resolve(path))
                .timeout(properties.getRequestTimeout())
                .header("Accept", "application/json");
    }

    /** 发送 JSON 请求，检查 HTTP 状态后反序列化成指定 record。 */
    private <T> T sendJson(HttpRequest request, Class<T> responseType) {
        try {
            HttpResponse<String> response = httpClient.send(
                    request,
                    HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new AnalysisClientException(
                        readError(response.body(), response.statusCode()),
                        response.statusCode());
            }
            return objectMapper.readValue(response.body(), responseType);
        } catch (AnalysisClientException exception) {
            throw exception;
        } catch (IOException | UncheckedIOException exception) {
            throw unavailable(exception);
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw unavailable(exception);
        }
    }

    /** 将相对 API 路径拼接到配置的 FastAPI 根地址。 */
    private URI resolve(String path) {
        String baseUrl = properties.getBaseUrl();
        if (baseUrl == null || baseUrl.isBlank()) {
            throw new AnalysisClientException("未配置 FastAPI 服务地址", 0);
        }
        return URI.create(baseUrl.replaceAll("/+$", "") + path);
    }

    private String readError(InputStream body, int statusCode) throws IOException {
        return readError(new String(body.readAllBytes(), StandardCharsets.UTF_8), statusCode);
    }

    /** 尽量提取 FastAPI JSON 中的 detail 字段，生成容易诊断的错误信息。 */
    private String readError(String body, int statusCode) {
        try {
            JsonNode root = objectMapper.readTree(body);
            JsonNode detail = root.get("detail");
            if (detail != null && !detail.isNull()) {
                return "FastAPI 返回 " + statusCode + "：" + detail.asText();
            }
        } catch (RuntimeException ignored) {
            // Fall through to the generic response below.
        }
        return "FastAPI 返回异常状态 " + statusCode;
    }

    private AnalysisClientException unavailable(Exception cause) {
        return new AnalysisClientException("无法连接烟雾分析服务", cause);
    }

    /** 清理文件名中可能破坏 multipart 请求头的字符。 */
    private String safeFilename(String filename) {
        String value = filename == null || filename.isBlank() ? "video.mp4" : filename;
        return value.replace("\\", "_")
                .replace("\"", "_")
                .replace("\r", "_")
                .replace("\n", "_");
    }

    /** 对相对路径的每一段进行 URL 编码，同时保留目录分隔符。 */
    private String encodePath(String path) {
        if (path == null || path.isBlank()) {
            throw new AnalysisClientException("分析产物路径为空", 0);
        }
        return String.join("/", java.util.Arrays.stream(path.replace('\\', '/').split("/"))
                .map(this::encode)
                .toList());
    }

    private String encode(String value) {
        return URLEncoder.encode(value, StandardCharsets.UTF_8).replace("+", "%20");
    }
}
