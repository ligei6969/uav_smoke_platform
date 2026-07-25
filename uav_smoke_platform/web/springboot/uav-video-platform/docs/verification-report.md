# V0.3 Step 1 验证报告

验证日期：2026-07-25
验证主机：Windows 11 x86_64

## JDK 与 Maven

执行 `java --version`：

```text
java 26.0.1 2026-04-21
Java(TM) SE Runtime Environment (build 26.0.1+8-34)
Java HotSpot(TM) 64-Bit Server VM (build 26.0.1+8-34, mixed mode, sharing)
```

执行 `mvn --version`（Maven Wrapper 下载的隔离 Maven 发行版加入当前命令的
PATH，不修改系统 PATH）：

```text
Apache Maven 3.9.16
Java version: 26.0.1, vendor: Oracle Corporation
Java home: C:\Program Files\Java\jdk-26.0.1
```

Maven 编译日志确认：

```text
Compiling 19 source files with javac [debug parameters release 26]
```

## 测试

执行 `mvn clean test`：

```text
Tests run: 8, Failures: 0, Errors: 0, Skipped: 0
BUILD SUCCESS
```

测试覆盖：

1. 空文件上传拒绝；
2. 非 MP4 文件上传拒绝；
3. 正常 MP4 元信息保存；
4. 不存在视频抛出业务异常；
5. 调用 MyBatis-Plus 逻辑删除并删除本地文件；
6. 标题与描述编辑；
7. 本地文件保存、读取和删除；
8. 存储路径穿越拒绝。

## 依赖审计

`mvn dependency:tree` 确认：

- Spring Boot 依赖均为 `4.1.0`；
- MyBatis-Plus starter 为
  `com.baomidou:mybatis-plus-spring-boot4-starter:3.5.17`；
- 未引入 `mybatis-plus-spring-boot3-starter`；
- 未重复声明原生 MyBatis Spring Boot starter；
- 未引入 H2；
- Connector/J 为 Spring Boot 4.1.0 管理的
  `com.mysql:mysql-connector-j:9.7.0`。
- Spring Boot 4.1.0 默认测试依赖中的 JUnit 6 已在 test scope 排除，
  并按项目技术栈显式使用 `org.junit.jupiter:junit-jupiter:5.14.3`。

## MySQL 8.4 与启动日志

使用隔离的官方 MySQL Community Server 8.4.10 LTS Windows ZIP 验证。
ZIP 的 MD5 校验值为：

```text
150F12262DF6AC88D43862A0E683EB81
```

MySQL 返回：

```text
VERSION()
8.4.10
```

Spring Boot 启动日志：

```text
:: Spring Boot :: (v4.1.0)
Starting UavVideoPlatformApplication v0.3.0-SNAPSHOT using Java 26.0.1
Tomcat started on port 18080 (http) with context path '/'
Started UavVideoPlatformApplication in 2.047 seconds
```

验证时用 18080/3307 临时端口避免占用开发默认端口；`application.yml`
仍按要求配置为 8080/3306。

## `11.mp4` 端到端验证

输入文件：

```text
uav_smoke_platform/data/videos/11.mp4
大小：290990442 字节
```

验证结果：

```text
GET /                         -> 200
GET /videos                   -> 200
GET /videos/upload            -> 200
POST /videos/upload           -> 302
GET /videos/{id}              -> 200
GET /media/videos/{id} Range  -> 206, video/mp4, 1024 bytes
POST /videos/{id}/edit        -> 302
POST /videos/{id}/delete      -> 302
```

上传后的数据库记录：

```text
original_filename = 11.mp4
stored_filename   = original.mp4
file_size         = 290990442
content_type      = video/mp4
status            = NORMAL
deleted           = 0
```

文件实际保存为：

```text
storage/videos/{videoId}/original.mp4
```

编辑后数据库标题和描述正确更新。删除后数据库记录为：

```text
status  = DELETED
deleted = 1
```

删除后本地文件不存在，`GET /api/videos` 返回空列表。

## 隔离说明

- 未安装或修改任何 Python/conda/venv 依赖；
- 未安装或修改任何 PSDK/C++ 依赖、源码或构建配置；
- Java 依赖只进入 Maven 用户缓存；
- 临时 MySQL 仅在工作区外层 `.runtime` 中运行，不注册 Windows 服务、
  不修改系统 PATH，并在验证后停止和清理；
- 视频、数据库数据、日志和 `target/` 均不提交 Git。
