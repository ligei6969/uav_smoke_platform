package com.uav.platform.dto;

import com.uav.platform.config.MapProperties;
import org.junit.jupiter.api.Test;

import static org.assertj.core.api.Assertions.assertThat;

class MapConfigurationResponseTest {

    @Test
    void usesOpenStreetMapByDefault() {
        MapConfigurationResponse response = MapConfigurationResponse.from(new MapProperties());

        assertThat(response.provider()).isEqualTo("maplibre");
        assertThat(response.effectiveStyle()).isEqualTo("osm");
        assertThat(response.tileUrl()).contains("openstreetmap.org");
        assertThat(response.styleUrl()).isNull();
        assertThat(response.warning()).isNull();
    }

    @Test
    void fallsBackToOsmWhenSatelliteKeyIsMissing() {
        MapProperties properties = new MapProperties();
        properties.setStyle("satellite");

        MapConfigurationResponse response = MapConfigurationResponse.from(properties);

        assertThat(response.requestedStyle()).isEqualTo("satellite");
        assertThat(response.effectiveStyle()).isEqualTo("osm");
        assertThat(response.warning()).contains("MAPTILER_API_KEY");
    }

    @Test
    void buildsSatelliteStyleWhenMapTilerKeyExists() {
        MapProperties properties = new MapProperties();
        properties.setStyle("satellite");
        properties.setMapTilerApiKey("test-key");

        MapConfigurationResponse response = MapConfigurationResponse.from(properties);

        assertThat(response.effectiveStyle()).isEqualTo("satellite");
        assertThat(response.styleUrl()).endsWith("key=test-key");
        assertThat(response.warning()).isNull();
    }

    @Test
    void keepsGoogleAsAnExplicitReservedProvider() {
        MapProperties properties = new MapProperties();
        properties.setProvider("google");

        MapConfigurationResponse response = MapConfigurationResponse.from(properties);

        assertThat(response.provider()).isEqualTo("google");
        assertThat(response.warning()).contains("尚未启用");
    }

    @Test
    void pageSelectionOverridesServerDefaultsWithoutMutatingThem() {
        MapProperties properties = new MapProperties();

        MapConfigurationResponse response = MapConfigurationResponse.from(
                properties,
                "none",
                "osm");

        assertThat(response.provider()).isEqualTo("none");
        assertThat(properties.getProvider()).isEqualTo("maplibre");
    }
}
