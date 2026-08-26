# UAV 烟雾分析平台 V0.5 使用与验证指南

本文说明如何在 Windows 环境中启动 UAV 烟雾分析平台，并完成 V0.5 的功能验收。

## 1. 环境与目录

项目目录：

```text
J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform
```

运行平台需要：

- MySQL 8.4
- JDK 26
- Maven 3.9
- Python 3.9（`psdk` Conda 环境）
- NVIDIA GPU 和可用的 CUDA 环境
- `models/yolov8s.pt` 模型文件

推荐直接使用项目根目录的 `start.ps1` 一键启动。脚本会自动启动 MySQL、FastAPI 和 Spring Boot，等待服务就绪，并打开网站。

## 2. 一键启动与停止

### 一键启动

打开 PowerShell，执行：

```powershell
cd J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform
powershell -ExecutionPolicy Bypass -File .\start.ps1
```

脚本会自动完成：

1. 启动 `.runtime` 中的 MySQL 8.4，并使用端口 3307。
2. 使用 `psdk` Python 环境启动 FastAPI，并使用端口 8000。
3. 查找 Maven、构建 Spring Boot JAR，并使用端口 8080 启动网站。
4. 等待三个服务就绪。
5. 打开 <http://127.0.0.1:8080>。

运行日志保存在：

```text
J:\uav-smoke-platform\uav_smoke_platform\.runtime\logs
```

### 停止全部服务

在上述项目目录中执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\start.ps1 -Stop
```

如果不希望启动后自动打开浏览器：

```powershell
powershell -ExecutionPolicy Bypass -File .\start.ps1 -NoBrowser
```

可选参数：

```text
-MySqlPort          MySQL 端口，默认 3307
-FastApiPort        FastAPI 端口，默认 8000
-WebPort            Spring Boot 端口，默认 8080
-DbUsername         MySQL 用户名，默认 root
-DbPassword         MySQL 密码，默认读取 DB_PASSWORD
-PythonExecutable   Python 路径，默认自动查找 psdk 环境
-MavenExecutable    Maven 路径，默认自动查找
```

以下第 3～5 节是单独启动各服务的备用方法，主要用于故障排查。

## 3. 手动启动 MySQL

打开第一个 PowerShell 窗口，执行：

```powershell
$projectRoot = "J:\uav-smoke-platform\uav_smoke_platform"
$mysqlBase = "$projectRoot\.runtime\mysql-dist\mysql-8.4.11-winx64"

& "$mysqlBase\bin\mysqld.exe" `
  --no-defaults `
  "--basedir=$mysqlBase" `
  "--datadir=$projectRoot\.runtime\mysql-data" `
  --port=3307 `
  --bind-address=127.0.0.1 `
  --console
```

控制台出现 `ready for connections` 表示启动成功。

当前 `.runtime/mysql-data` 是本机临时验证数据库，`root` 用户为空密码。该配置只能用于本机开发验证，不能用于生产环境。

## 4. 手动启动 FastAPI 分析服务

打开第二个 PowerShell 窗口，执行：

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

$env:SMOKE_MODEL_PATH = "models/yolov8s.pt"
$env:SMOKE_DEVICE = "0"

& "C:\Users\jb\miniconda3\envs\psdk\python.exe" -m uvicorn `
  ai_service.api:app `
  --app-dir python `
  --host 127.0.0.1 `
  --port 8000
```

启动后访问：

<http://127.0.0.1:8000/docs>

能够看到 FastAPI 接口文档表示分析服务正常。

## 5. 手动启动 Spring Boot 网站

打开第三个 PowerShell 窗口，执行：

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform\web\springboot\uav-video-platform"

$env:DB_HOST = "127.0.0.1"
$env:DB_PORT = "3307"
$env:DB_USERNAME = "root"
$env:SMOKE_ANALYSIS_URL = "http://127.0.0.1:8000"

$maven = "C:\Users\jb\.m2\wrapper\dists\apache-maven-3.9.16-bin\5grr65jo27hi51sujmtcldfovl\apache-maven-3.9.16\bin\mvn.cmd"

& $maven `
  "-Dspring-boot.run.arguments=--spring.datasource.password=" `
  spring-boot:run
```

控制台出现 `Started UavVideoPlatformApplication` 后访问：

<http://localhost:8080>

如果使用的 MySQL 已设置密码，请改为设置环境变量：

```powershell
$env:DB_PASSWORD = "你的 MySQL 密码"
& $maven spring-boot:run
```

## 6. 正常分析流程

本地测试视频位于：

```text
J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform\data\videos
```

建议先使用较小的 `16.mp4` 快速验证页面流程，最后使用 `11.mp4` 完成真实烟雾分析验收。

操作步骤：

1. 打开 <http://localhost:8080>。
2. 进入上传页面，选择 MP4 视频并填写标题。
3. 上传成功后进入视频详情页。
4. 点击“开始分析”。
5. 等待状态依次变为 `PENDING`、`RUNNING`、`COMPLETED`。
6. 检查烟雾统计、关键帧和标注视频是否正常显示。
7. 在单张关键帧预览下点击“首次检测”“最高置信度”“最后检测”，确认预览图会切换，标注视频会滚动到视野中并定位到对应时间。
8. 播放标注视频，确认能够播放、暂停和拖动进度条。
9. 刷新详情页，确认分析结果仍然存在。

完成分析后，页面应显示：

- 是否检测到烟雾
- 视频帧数和时长
- 阳性帧数和阳性帧比例
- 首次及最后检测时间
- 最大及平均置信度
- 首次、最大置信度和最后检测关键帧
- 带检测框的标注视频

`11.mp4` 已有验收结果可供大致对照：

```text
视频时长          约 243.7 秒
阳性帧数          约 2273
阳性帧比例        约 31.09%
首次检测时间      约 3.37 秒
最大置信度        约 0.937
```

在 RTX 4070 Laptop GPU 上，`11.mp4` 的完整推理通常需要约 5 分钟。

## 7. 失败与重试验证

1. 上传一个新视频。
2. 在 FastAPI 窗口按 `Ctrl+C` 停止分析服务。
3. 在视频详情页点击“开始分析”。
4. 确认页面显示 `FAILED`，并出现类似“无法连接烟雾分析服务”的错误信息。
5. 按照第 4 节的命令重新启动 FastAPI。
6. 返回详情页点击“重试分析”。
7. 确认不需要重新上传视频，任务即可进入 `PENDING`、`RUNNING` 并最终变为 `COMPLETED`。

## 8. 重启恢复验证

在一个视频分析完成后执行以下检查：

1. 在 Spring Boot 窗口按 `Ctrl+C` 停止网站。
2. 按照第 5 节的命令重新启动 Spring Boot。
3. 打开原视频详情页。
4. 确认分析状态、统计、关键帧和标注视频仍然存在。
5. 停止并重新启动 FastAPI。
6. 再次打开原详情页，确认已经完成的标注视频仍能播放。

这说明分析记录保存在 MySQL 中，分析产物保存在磁盘中，不依赖 Spring Boot 或 FastAPI 的进程内存。

## 9. HTTP Range 验证

从视频详情页地址中取得视频 ID，然后执行：

```powershell
$videoId = "替换为视频ID"

curl.exe -s -D - -o NUL `
  -H "Range: bytes=0-1023" `
  "http://localhost:8080/media/videos/$videoId/analysis/annotated"
```

通过标准：

```text
HTTP 206
Content-Type: video/mp4
Content-Length: 1024
Content-Range: bytes 0-1023/文件总大小
Accept-Ranges: bytes
```

HTTP Range 正常只是传输层验收，仍需在浏览器中实际播放并拖动视频，以确认浏览器能够解码 H.264 视频。

## 10. 自动化测试

### 10.1 Python 测试

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

& "C:\Users\jb\miniconda3\envs\psdk\python.exe" `
  -m unittest discover -s python\tests -v
```

通过标准：16 个测试全部通过并显示 `OK`。

### 10.2 Spring Boot 测试

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform\web\springboot\uav-video-platform"

$maven = "C:\Users\jb\.m2\wrapper\dists\apache-maven-3.9.16-bin\5grr65jo27hi51sujmtcldfovl\apache-maven-3.9.16\bin\mvn.cmd"

& $maven test
```

通过标准：21 个测试全部通过并显示 `BUILD SUCCESS`。

### 10.3 C++ 测试

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

cmake -G "MinGW Makefiles" `
  -S cpp\dji_adapter `
  -B build\dji_adapter-validation

cmake --build build\dji_adapter-validation

ctest `
  --test-dir build\dji_adapter-validation `
  --output-on-failure
```

通过标准：4 个测试全部通过，显示 `100% tests passed`。

### 10.4 浏览器脚本语法检查

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

node --check `
  web\springboot\uav-video-platform\src\main\resources\static\js\video-detail.js
```

命令无输出且退出码为 0 表示通过。

## 11. V0.5 验收清单

- [ ] MySQL、FastAPI 和 Spring Boot 均可正常启动
- [ ] 浏览器可以上传并管理视频
- [ ] 分析状态可以从 `PENDING` 进入 `RUNNING` 和 `COMPLETED`
- [ ] 页面可以显示分析统计和关键帧
- [ ] 标注视频可以播放并拖动进度条
- [ ] 刷新页面后分析结果仍然存在
- [ ] Spring Boot 重启后分析结果仍然存在
- [ ] FastAPI 停止时页面显示明确失败信息
- [ ] FastAPI 恢复后可以直接重试，不需要重新上传
- [ ] 标注视频代理返回 HTTP 206 Range 响应
- [ ] Python、Spring Boot、C++ 和 JavaScript 检查全部通过
- [ ] 已保存运行中、完成、失败重试和标注视频的验收截图

已有的详细验证记录位于：

```text
web/springboot/uav-video-platform/docs/v0.5-verification.md
```
