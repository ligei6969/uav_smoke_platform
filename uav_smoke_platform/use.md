# UAV 烟雾分析平台 V0.7.1 使用与验证指南

本文说明如何在 Windows 环境中一键启动 UAV 烟雾分析平台，使用实时监控地图，并完成 V0.7.1 的功能验收。V0.5 的视频上传与烟雾分析功能仍然保留。

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

V0.7.1 的实时飞行数据为模拟数据，不需要连接真实无人机。烟雾检测框来自已完成的 YOLO 视频分析结果，定位时使用与视频时间同步的模拟遥测和相机姿态。普通地图默认使用 MapLibre GL JS 和 OpenStreetMap，无需申请 API Key；卫星地图可选用 MapTiler，需要自行申请 Key。Google 地图只保留接口，当前版本不会调用 Google API 或产生相关费用。

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

实时监控页面：

<http://127.0.0.1:8080/monitor>

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
-VideoStorageRoot   上传视频存储目录，默认使用 .runtime\storage\videos
-MapProvider        地图提供者：maplibre、google 或 none，默认 maplibre
-MapStyle           地图样式：osm 或 satellite，默认 osm
-MapTilerApiKey     MapTiler Key，仅卫星地图需要
```

一键脚本会统一使用以下稳定目录保存上传视频：

```text
J:\uav-smoke-platform\uav_smoke_platform\.runtime\storage\videos
```

第一次使用 V0.6.3 启动时，脚本会把旧版 Spring Boot 工作目录中的上传视频复制到该目录；原文件会保留。这可以避免升级或从不同目录启动后出现“数据库有视频记录，但播放接口返回 404”。

## 3. 实时地图使用方法

### 3.1 默认免费普通地图

直接执行一键启动命令即可：

```powershell
cd J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform
powershell -ExecutionPolicy Bypass -File .\start.ps1
```

进入“实时监控”后可以看到：

- 蓝色无人机标记及实时航向
- 蓝色历史飞行轨迹
- 红色烟雾目标
- 橙色固定模拟着火点（经纬度不会随时间变化）
- 黄色复查航点和虚线任务路线
- 纬度、经度、高度、速度、航向和云台俯仰角
- 无人机到固定着火点的实时地面距离（不计飞行高度）
- SSE 实时连接状态
- “地图显示”下拉框，可在 OSM、卫星地图、仅坐标和 Google 预留模式之间切换
- 地图右上角的 `+`、`−` 和“定位”按钮，用于缩放和重新跟随无人机

地图切换会立即生效，不需要停止或重新启动服务。其中 Google 选项用于验证预留接口，目前会明确进入坐标面板；卫星地图没有配置 MapTiler Key 时会自动回退到 OSM，并说明原因。

默认启用“跟随无人机”。手动滚轮缩放、双击缩放、触控缩放或拖动地图时，页面会自动关闭跟随，避免每秒一次的遥测定位动画打断用户操作。点击地图右上角“定位”可以重新开启跟随并回到无人机位置。

目标列表中的“识别准确率”以百分比显示。进入视频定位联动后，它来自当前检测框的 YOLO 模型分数，表示模型倾向于把该区域识别为烟雾的程度；它不是人工统计的准确率，也不表示火情严重程度。尚未取得视频检测结果时，页面保留的初始目标仅为地图演示数据。

默认底图需要访问 MapLibre 脚本 CDN 和 OpenStreetMap 在线瓦片。它适合本机开发和功能演示；如果外网不可用，页面会自动进入无地图模式，坐标和遥测数据仍会继续更新。

### 3.2 可选卫星地图

先在 MapTiler 申请 API Key，然后在当前 PowerShell 窗口执行：

```powershell
$env:MAPTILER_API_KEY = "你的 MapTiler Key"
powershell -ExecutionPolicy Bypass -File .\start.ps1 -MapStyle satellite
```

如果没有设置 Key，系统不会报错退出，而是显示提示并自动回退到 OpenStreetMap 普通地图。MapTiler 的免费额度和使用范围以其账户页面的当前条款为准，正式部署前应设置 Key 的来源限制。

### 3.3 无地图与 Google 预留接口

完全禁用在线地图，只查看坐标和遥测：

```powershell
powershell -ExecutionPolicy Bypass -File .\start.ps1 -MapProvider none
```

验证 Google 预留适配器：

```powershell
powershell -ExecutionPolicy Bypass -File .\start.ps1 -MapProvider google
```

Google 模式目前会明确显示“接口已保留”，并回退到坐标面板。后续接入时只需新增 Google 地图提供者实现，不需要修改遥测接口和页面业务数据结构。

### 3.4 实时数据接口验证

查看当前完整快照：

```powershell
curl.exe http://127.0.0.1:8080/api/telemetry/snapshot
```

持续查看 SSE 数据流，确认 `sequence`、经纬度和飞行参数不断变化：

```powershell
curl.exe -N http://127.0.0.1:8080/api/telemetry/stream
```

查看实际生效的地图模式：

```powershell
curl.exe http://127.0.0.1:8080/api/map/config
```

### 3.5 地图与上传视频同屏模拟实时传输

实时监控页会同时显示地图和“模拟实时视频”播放器。播放器会自动读取视频库中所有状态正常的上传视频；当前已有三条视频时，下拉框会显示“视频源 1、视频源 2、视频源 3”，每一条都可以单独切换和播放。

使用步骤：

1. 先在“上传视频”页面上传需要演示的 MP4；已经上传的视频不需要重复上传。
2. 打开 <http://127.0.0.1:8080/monitor>。
3. 在“选择上传的视频源”下拉框中选择任意一条视频。
4. 页面会从一个随系统时间推进的位置开始静音播放，并循环播放，以模拟无人机实时回传画面。
5. 可以暂停或拖动进度条查看历史画面；点击“回到实时”会重新定位到当前模拟直播位置并继续播放。
6. 同时观察地图上的无人机轨迹、固定着火点和“无人机距火点（地面）”，确认视频播放期间遥测仍持续更新。

浏览器通常只允许网页自动静音播放，所以播放器默认静音；需要声音时可在播放器控制条中手动取消静音。本功能使用已上传的本地视频模拟实时视频流，不代表当前已经接入无人机 RTSP/RTMP/WebRTC 图传。后续接入真实图传时可以保留同一播放器区域并替换视频源适配器。

固定模拟着火点由遥测快照接口的 `firePoint` 字段提供。它的经纬度始终不变；无人机经纬度继续移动，页面使用两组坐标计算并显示实时距离。

### 3.6 视频检测与目标定位联动

V0.7.1 会把播放器当前时间与该视频已完成的 YOLO 分析结果对齐。当当前时间附近存在烟雾检测框时，系统会同步取得同一视频时刻的模拟无人机位置、航向和云台俯仰角，计算目标经纬度，并在地图上更新烟雾目标和四个复查航点。

使用步骤：

1. 确认所选视频已经完成烟雾分析；没有完成分析的视频会显示“等待分析结果”。
2. 打开实时监控页并选择一个视频源。
3. 播放或拖动到有烟雾的画面，查看右侧“视频定位联动”卡片。
4. 定位成功时，卡片显示视频时间、检测帧、识别准确率、同步遥测坐标、目标坐标和固定着火点偏差。
5. 地图上的红色目标与黄色复查航点会切换为本次计算结果。
6. 当前画面附近没有检测框时会显示“当前时刻未检测到烟雾”，最近一次有效定位仍保留在地图上，避免画面闪烁。

直接验证接口时，先从视频库或 `/api/videos` 取得视频 ID，再执行：

```powershell
$videoId = 1
curl.exe "http://127.0.0.1:8080/api/videos/$videoId/geolocation?timeSec=3.4"
```

通过标准：响应中的 `status` 为 `LOCATED` 时，`detected` 和 `geolocated` 为 `true`，并包含 `frameId`、`detectionBox`、`telemetry`、`target` 和 4 个 `missionWaypoints`。`NO_DETECTION` 表示该视频时刻附近没有检测框，不是系统故障。

当前定位用于无硬件演示：检测框是实际 YOLO 结果，但遥测、云台姿态和固定着火点是模拟数据，因此“固定着火点偏差”只用于验证计算链路，不代表真实测量误差。

以下第 4～6 节是单独启动各服务的备用方法，主要用于故障排查。

## 4. 手动启动 MySQL

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

## 5. 手动启动 FastAPI 分析服务

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

## 6. 手动启动 Spring Boot 网站

打开第三个 PowerShell 窗口，执行：

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform\web\springboot\uav-video-platform"

$env:DB_HOST = "127.0.0.1"
$env:DB_PORT = "3307"
$env:DB_USERNAME = "root"
$env:SMOKE_ANALYSIS_URL = "http://127.0.0.1:8000"
$env:APP_STORAGE_VIDEO_ROOT = "J:\uav-smoke-platform\uav_smoke_platform\.runtime\storage\videos"

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

## 7. 正常分析流程

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

## 8. 失败与重试验证

1. 上传一个新视频。
2. 在 FastAPI 窗口按 `Ctrl+C` 停止分析服务。
3. 在视频详情页点击“开始分析”。
4. 确认页面显示 `FAILED`，并出现类似“无法连接烟雾分析服务”的错误信息。
5. 按照第 5 节的命令重新启动 FastAPI。
6. 返回详情页点击“重试分析”。
7. 确认不需要重新上传视频，任务即可进入 `PENDING`、`RUNNING` 并最终变为 `COMPLETED`。

## 9. 重启恢复验证

在一个视频分析完成后执行以下检查：

1. 在 Spring Boot 窗口按 `Ctrl+C` 停止网站。
2. 按照第 6 节的命令重新启动 Spring Boot。
3. 打开原视频详情页。
4. 确认分析状态、统计、关键帧和标注视频仍然存在。
5. 停止并重新启动 FastAPI。
6. 再次打开原详情页，确认已经完成的标注视频仍能播放。

这说明分析记录保存在 MySQL 中，分析产物保存在磁盘中，不依赖 Spring Boot 或 FastAPI 的进程内存。

## 10. HTTP Range 验证

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

## 11. 自动化测试

### 11.1 Python 测试

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

& "C:\Users\jb\miniconda3\envs\psdk\python.exe" `
  -m unittest discover -s python\tests -v
```

通过标准：16 个测试全部通过并显示 `OK`。

### 11.2 Spring Boot 测试

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform\web\springboot\uav-video-platform"

$maven = "C:\Users\jb\.m2\wrapper\dists\apache-maven-3.9.16-bin\5grr65jo27hi51sujmtcldfovl\apache-maven-3.9.16\bin\mvn.cmd"

& $maven test
```

通过标准：34 个测试全部通过并显示 `BUILD SUCCESS`。

### 11.3 C++ 测试

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

### 11.4 浏览器脚本语法检查

```powershell
Set-Location "J:\uav-smoke-platform\uav_smoke_platform\uav_smoke_platform"

node --check `
  web\springboot\uav-video-platform\src\main\resources\static\js\video-detail.js

node --check `
  web\springboot\uav-video-platform\src\main\resources\static\js\live-monitor.js
```

命令无输出且退出码为 0 表示通过。

## 12. V0.7.1 验收清单

- [ ] 一键启动后首页、实时监控、视频库和 FastAPI 均可访问
- [ ] 实时监控页显示“实时连接”和“模拟数据”
- [ ] 无人机位置、轨迹点数量及遥测数值持续变化
- [ ] 地图显示 2 个疑似烟雾目标、4 个复查航点和任务路线
- [ ] 地图显示橙色固定着火点，刷新和持续运行后其经纬度不变
- [ ] “无人机距火点（地面）”随无人机移动持续变化
- [ ] 关闭“跟随无人机”后可以自由拖动地图
- [ ] 页面内可以切换 OSM、卫星地图、仅坐标和 Google 预留模式
- [ ] 不配置 MapTiler Key 并选择卫星图时自动回退 OSM
- [ ] 使用 `-MapProvider none` 时坐标与遥测仍正常更新
- [ ] 使用 `-MapProvider google` 时显示预留接口提示且页面不报错
- [ ] SSE 接口持续输出递增的 `sequence`
- [ ] 地图和模拟实时视频在同一个监控页面显示
- [ ] 地图和视频卡片等高显示，地图下方没有大块空白
- [ ] 桌面端右侧信息栏文字不拥挤，中等屏幕自动变为两列信息卡
- [ ] 视频源下拉框能列出并播放全部三条已上传视频
- [ ] 暂停或拖动视频后，“回到实时”可以恢复模拟直播位置
- [ ] 已完成分析的视频在有检测框的时刻显示 `LOCATED`
- [ ] “视频定位联动”显示检测帧、识别准确率、同步遥测和目标坐标
- [ ] 定位成功后地图目标和 4 个复查航点随当前视频检测结果更新
- [ ] 无检测框的时刻显示明确提示，并保留最近一次有效定位
- [ ] 定位接口返回检测框、同步遥测、目标坐标和 4 个复查航点
- [ ] Spring Boot 34 个测试和两个 JavaScript 语法检查通过

V0.5 回归检查：

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
