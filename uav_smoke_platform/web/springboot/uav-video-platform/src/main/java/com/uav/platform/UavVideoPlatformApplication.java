package com.uav.platform;

import com.uav.platform.config.StorageProperties;
import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.EnableConfigurationProperties;

@SpringBootApplication
@MapperScan("com.uav.platform.mapper")
@EnableConfigurationProperties(StorageProperties.class)
public class UavVideoPlatformApplication {

    public static void main(String[] args) {
        SpringApplication.run(UavVideoPlatformApplication.class, args);
    }
}
