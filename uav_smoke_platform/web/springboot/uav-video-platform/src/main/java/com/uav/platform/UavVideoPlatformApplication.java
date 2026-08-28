package com.uav.platform;

import com.uav.platform.config.AnalysisProperties;
import com.uav.platform.config.MapProperties;
import com.uav.platform.config.StorageProperties;
import com.uav.platform.config.TelemetryProperties;
import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.scheduling.annotation.EnableScheduling;

/**
 * Spring Boot 应用启动入口。
 *
 * <p>程序从 {@link #main(String[])} 开始运行。启动后，Spring 会扫描当前包及其
 * 子包中的 Controller、Service、Component 等组件，并负责创建和注入这些对象。</p>
 */
@SpringBootApplication
@MapperScan("com.uav.platform.mapper") // 让 MyBatis 为 mapper 接口生成数据库访问实现
@EnableScheduling
@EnableConfigurationProperties({
        StorageProperties.class,
        AnalysisProperties.class,
        MapProperties.class,
        TelemetryProperties.class
})
public class UavVideoPlatformApplication {

    public static void main(String[] args) {
        // 创建 Spring 容器并启动内嵌 Tomcat Web 服务器。
        SpringApplication.run(UavVideoPlatformApplication.class, args);
    }
}
