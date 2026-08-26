package com.uav.platform.client;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import com.uav.platform.config.AnalysisProperties;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.core.io.ByteArrayResource;
import tools.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicReference;

import static org.assertj.core.api.Assertions.assertThat;

class HttpSmokeAnalysisClientTest {

    private HttpServer server;
    private HttpSmokeAnalysisClient client;
    private final AtomicReference<String> receivedRange = new AtomicReference<>();

    @BeforeEach
    void setUp() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/api/v1/analysis-tasks", this::handle);
        server.start();

        AnalysisProperties properties = new AnalysisProperties();
        properties.setBaseUrl("http://127.0.0.1:" + server.getAddress().getPort());
        properties.setConnectTimeout(Duration.ofSeconds(1));
        properties.setRequestTimeout(Duration.ofSeconds(5));
        client = new HttpSmokeAnalysisClient(properties, new ObjectMapper());
    }

    @AfterEach
    void tearDown() {
        server.stop(0);
    }

    @Test
    void uploadsVideoAndReadsTaskContracts() {
        var created = client.createTask(
                new ByteArrayResource("mock-video".getBytes(StandardCharsets.UTF_8)),
                "flight.mp4");
        var status = client.getTaskStatus("task-1");
        var result = client.getTaskResult("task-1");

        assertThat(created.taskId()).isEqualTo("task-1");
        assertThat(status.status()).isEqualTo("COMPLETED");
        assertThat(result.videoSummary().smokeDetected()).isTrue();
        assertThat(result.artifacts().keyframes()).hasSize(1);
    }

    @Test
    void streamsArtifactAndForwardsRangeHeader() throws Exception {
        try (var artifact = client.openArtifact(
                "task-1", "annotated_video.mp4", "bytes=0-3")) {
            assertThat(artifact.statusCode()).isEqualTo(206);
            assertThat(artifact.contentRange()).isEqualTo("bytes 0-3/10");
            assertThat(artifact.body().readAllBytes()).containsExactly(1, 2, 3, 4);
        }
        assertThat(receivedRange.get()).isEqualTo("bytes=0-3");
    }

    private void handle(HttpExchange exchange) throws IOException {
        String path = exchange.getRequestURI().getPath();
        if ("POST".equals(exchange.getRequestMethod())) {
            String requestBody = new String(
                    exchange.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
            assertThat(requestBody).contains("name=\"video\"")
                    .contains("filename=\"flight.mp4\"")
                    .contains("mock-video");
            json(exchange, 202, "{\"taskId\":\"task-1\",\"status\":\"PENDING\"}");
        } else if (path.endsWith("/result")) {
            json(exchange, 200, """
                    {"taskId":"task-1","status":"COMPLETED",
                     "videoSummary":{"smokeDetected":true,"fps":30.0,"frameCount":10,
                     "durationSec":1.0,"positiveFrameCount":2,"positiveFrameRatio":0.2,
                     "firstDetectionSec":0.1,"lastDetectionSec":0.8,
                     "maxConfidence":0.9,"meanConfidence":0.8},
                     "artifacts":{"annotatedVideo":"annotated_video.mp4",
                     "detectionResults":"detection_results.json",
                     "keyframes":[{"type":"first_detection",
                     "path":"keyframes/first_detection.jpg"}]}}
                    """);
        } else if (path.contains("/artifacts/")) {
            receivedRange.set(exchange.getRequestHeaders().getFirst("Range"));
            byte[] body = new byte[]{1, 2, 3, 4};
            exchange.getResponseHeaders().set("Content-Type", "video/mp4");
            exchange.getResponseHeaders().set("Content-Range", "bytes 0-3/10");
            exchange.getResponseHeaders().set("Accept-Ranges", "bytes");
            exchange.sendResponseHeaders(206, body.length);
            exchange.getResponseBody().write(body);
            exchange.close();
        } else {
            json(exchange, 200, "{\"taskId\":\"task-1\",\"status\":\"COMPLETED\","
                    + "\"originalFilename\":\"flight.mp4\",\"errorMessage\":null}");
        }
    }

    private void json(HttpExchange exchange, int status, String json) throws IOException {
        byte[] body = json.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "application/json");
        exchange.sendResponseHeaders(status, body.length);
        exchange.getResponseBody().write(body);
        exchange.close();
    }
}
