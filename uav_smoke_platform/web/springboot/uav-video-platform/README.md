# UAV Video Platform V0.3

Spring Boot 4.1.0 / JDK 26 的无人机视频管理网站 MVP。

## 准备 MySQL

使用 MySQL 8.4 执行：

```powershell
mysql -u root -p < src/main/resources/db/mysql-8.4-schema.sql
```

应用不会在配置文件中保存数据库密码。启动前设置环境变量：

```powershell
$env:DB_PASSWORD = "你的 MySQL 密码"
$env:DB_USERNAME = "root"
```

如数据库不在本机，可额外设置 `DB_HOST` 和 `DB_PORT`。

## 构建和启动

```powershell
mvn clean test
mvn spring-boot:run
```

打开 <http://localhost:8080>。上传的视频保存在工作目录下的
`storage/videos/{videoId}/original.mp4`，该目录不会提交到 Git。
