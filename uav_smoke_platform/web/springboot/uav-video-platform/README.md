# UAV Smoke Video Platform V0.5

Spring Boot 4.1.0 / JDK 26 的无人机视频管理与烟雾分析网站。浏览器只访问
Spring Boot；Spring Boot 负责视频、分析记录和页面，并调用 FastAPI 执行 YOLO
推理及代理分析产物。

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

该脚本会同时创建视频表和 `video_analysis_task` 分析记录表。已有 V0.3 数据库也
可以重新执行脚本补建分析表，不会删除原有视频记录。

## 准备 FastAPI

从产品目录 `uav_smoke_platform` 启动分析服务：

```powershell
$env:SMOKE_MODEL_PATH = "models/yolov8s.pt"
$env:SMOKE_DEVICE = "0"
C:\Users\jb\miniconda3\envs\psdk\python.exe -m uvicorn `
  ai_service.api:app --app-dir python --host 127.0.0.1 --port 8000
```

Spring Boot 默认连接 `http://127.0.0.1:8000`。使用其他地址时设置：

```powershell
$env:SMOKE_ANALYSIS_URL = "http://analysis-host:8000"
```

## 构建和启动

```powershell
mvn clean test
mvn spring-boot:run
```

打开 <http://localhost:8080>。上传的视频保存在工作目录下的
`storage/videos/{videoId}/original.mp4`，该目录不会提交到 Git。

## V0.5 分析接口

```text
POST /api/videos/{videoId}/analysis
GET  /api/videos/{videoId}/analysis
POST /api/videos/{videoId}/analysis/retry
```

标注视频、检测 JSON 和关键帧通过 `/media/videos/{videoId}/analysis/**`
代理访问。标注视频支持 HTTP Range 请求。分析状态和结果元数据保存在 MySQL，
因此刷新页面或重启 Spring Boot 后仍可恢复。
