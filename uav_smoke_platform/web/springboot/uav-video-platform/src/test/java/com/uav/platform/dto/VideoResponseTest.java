package com.uav.platform.dto;

import org.junit.jupiter.api.Test;
import tools.jackson.databind.ObjectMapper;

import static org.assertj.core.api.Assertions.assertThat;

class VideoResponseTest {

    @Test
    void serializesLongIdAsStringSoBrowserDoesNotLosePrecision() throws Exception {
        VideoResponse response = new VideoResponse(
                9223372036854775806L,
                "模拟视频",
                null,
                "source.mp4",
                1024L,
                "video/mp4",
                "NORMAL",
                null,
                null);

        String json = new ObjectMapper().writeValueAsString(response);

        assertThat(json).contains("\"id\":\"9223372036854775806\"");
    }
}
