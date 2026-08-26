package com.uav.platform.controller;

import com.uav.platform.dto.VideoAnalysisResponse;
import com.uav.platform.service.VideoAnalysisService;
import org.junit.jupiter.api.Test;
import org.springframework.http.HttpStatus;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

class VideoAnalysisControllerTest {

    @Test
    void exposesStartStatusAndRetryEndpoints() {
        VideoAnalysisService service = mock(VideoAnalysisService.class);
        VideoAnalysisResponse pending = VideoAnalysisResponse.notStarted(42L);
        when(service.start(42L)).thenReturn(pending);
        when(service.getLatest(42L)).thenReturn(pending);
        when(service.retry(42L)).thenReturn(pending);
        VideoAnalysisController controller = new VideoAnalysisController(service);

        assertThat(controller.start(42L).getStatusCode())
                .isEqualTo(HttpStatus.ACCEPTED);
        assertThat(controller.status(42L).data()).isSameAs(pending);
        assertThat(controller.retry(42L).getStatusCode())
                .isEqualTo(HttpStatus.ACCEPTED);
    }
}
