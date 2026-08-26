package com.uav.platform.controller;

import com.uav.platform.client.SmokeAnalysisClient.RemoteArtifact;
import com.uav.platform.service.VideoAnalysisService;
import org.junit.jupiter.api.Test;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

class AnalysisMediaControllerTest {

    @Test
    void preservesRangeResponseWhileStreamingAnnotatedVideo() throws Exception {
        VideoAnalysisService service = mock(VideoAnalysisService.class);
        when(service.loadArtifact(42L, "annotated", "bytes=0-3"))
                .thenReturn(new RemoteArtifact(
                        new ByteArrayInputStream(new byte[]{1, 2, 3, 4}),
                        206,
                        "video/mp4",
                        4,
                        "bytes 0-3/10",
                        "bytes"));
        AnalysisMediaController controller = new AnalysisMediaController(service);

        var response = controller.annotated(42L, "bytes=0-3");

        assertThat(response.getStatusCode()).isEqualTo(HttpStatus.PARTIAL_CONTENT);
        assertThat(response.getHeaders().getFirst(HttpHeaders.CONTENT_RANGE))
                .isEqualTo("bytes 0-3/10");
        assertThat(response.getHeaders().getFirst(HttpHeaders.ACCEPT_RANGES))
                .isEqualTo("bytes");
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        response.getBody().writeTo(output);
        assertThat(output.toByteArray()).containsExactly(1, 2, 3, 4);
    }
}
