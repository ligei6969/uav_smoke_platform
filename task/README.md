# M350 / H30T 烟雾巡检：模块化主线

正式目录：`J:\Payload-SDK-3.10.0-task\task`。不再依赖历史 F 盘路径。

本阶段实现 **HOST 离线元数据回放闭环**：

检测记录 → 有界生产者/消费者队列 → 同源同时基遥测匹配 → 有条件的地面投影 → 云台 dry-run 建议 → JSONL。

这不是已经完成的机载系统。程序不解码视频、不执行模型推理、不连接飞机、不激活 PSDK、不发送云台指令。真实视频接入、Nano 推理后端和真机验证见 [任务清单](docs/TASKS.md)。

## 模块边界

| 目录 | 职责 |
|---|---|
| `include/smoke` | 数据契约、有界队列、模块公开接口 |
| `src/replay` | 有大小限制的 CSV 解析、JSON 字符串编码 |
| `src/telemetry` | 线程安全的有界历史、同源/同时基最近样本匹配 |
| `src/localization` | 显式镜头标定与有效遥测下的平地交点 |
| `src/gimbal` | 防旧帧、换源保护、限频、限幅、人工接管；只做决策 |
| `src/adapters` | PSDK 头文件字段转换；尚非实际订阅驱动 |
| `src/app` | 单会话元数据回放入口 |
| `tests` | 纯逻辑、并发和 CLI 故障回归测试 |
| `tools` | 已有检测 JSON 转换、只读设备检查、SDK 来源快照 |

## 构建和测试

需要 C++14、线程库、CMake >= 3.10；测试需要 Python >= 3.6，不依赖第三方 Python 包。

Windows 当前验证使用 MinGW / Ninja：

```powershell
cd J:\Payload-SDK-3.10.0-task
cmake -S task -B task/build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT=J:/Payload-SDK-3.10.0-task
cmake --build task/build-host -j 4
ctest --test-dir task/build-host --output-on-failure
```

Linux / Nano 的无硬件逻辑构建：

```sh
SDK_ROOT="$PWD"
mkdir -p task/build-host
cd task/build-host
cmake .. -DCMAKE_BUILD_TYPE=Debug -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT="$SDK_ROOT"
cmake --build . -- -j2
ctest --output-on-failure
```

Linux 命令从仓库根执行，使用兼容 CMake 3.10 的调用方式，执行后工作目录为 `task/build-host`；其他示例均从仓库根执行。这里开启的 PSDK adapter **只编译头文件字段映射**，不链接 `libpayloadsdk.a`，不代表 UART、Bulk、相机或云台驱动已完成。纯 HOST 构建可关闭该选项。旧 CMake 的 Python 检测提供兼容分支；尚未在 Nano 的 CMake 3.10 / Python 3.6 上运行，不能把兼容代码当作实测结果。

Linux 内存检查用独立构建目录加 `-DSMOKE_ASAN=ON`；线程检查用另一个目录加 `-DSMOKE_TSAN=ON`，两者不能同时开启。WSL 检查也不是 aarch64 或板卡验证。

## 已有检测结果接入

转换器只读取已有 `analyze_video` JSON，不执行推理，不伪造遥测。要求顶层 `width`、`height`、`detections`；每帧 `frame_id`、`timestamp_sec`、`boxes`；每框 `class_name`、`confidence`、四项 xyxy `bbox`。

```powershell
python task/tools/export_detections.py --detections <实际检测JSON路径> --output <新的CSV路径> --source h30t-1 --lens wide
task/build-host/smoke_replay.exe --detections <CSV路径> --output <新的JSONL路径>
```

镜头必须根据实际来源指定，不把 FPV 当作 H30T；无法确认时保留 `unknown`。每个阳性帧只选最高置信度的烟雾框供此版回放，所有输入框都先校验；这不是多目标跟踪。帧号严格递增，时间不得倒退。只接受显式 `smoke` / `烟雾` 类别；其他真实类别名称需先确认模型标签，不能直接改为烟雾。导出器的 epoch 固定为 0，限单段记录；输入 JSON 上限 64MiB。

没有遥测、没有标定、镜头不匹配时，输出 `location_valid=false`，不使用飞机 GPS 冒充烟雾坐标。必须提供实测水平视场角（不是对角视场角）才能显式启用定位：

```powershell
task/build-host/smoke_replay.exe --detections <CSV路径> --telemetry <遥测CSV路径> --output <新的JSONL路径> --lens wide --calibrated-hfov <实测水平角度> --max-time-delta 0.2
```

### 稳定事件与证据(T19a,HOST 部分)

可选启用稳定烟雾事件输出:连续 `--min-event-frames`(默认 3)个不低于置信度门限的阳性帧构成一个事件;阴性帧、弱置信帧、超过 1 秒的帧间隔或换源/换 epoch 都会结束当前事件段。事件写入 `--events-output` 指定的 JSONL(排他创建,`--max-event-bytes` 默认 1MiB),全部事件写入成功后生成 `<路径>.complete` 原子完成标记。

```powershell
task/build-host/smoke_replay.exe --detections <CSV路径> --telemetry <遥测CSV路径> --output <新的JSONL路径> --lens wide --calibrated-hfov <实测水平角度> --events-output <新的证据JSONL路径> --min-event-frames 3
```

约束:事件是**元数据稳定性判定**(`event_model=metadata_stability_decision`),不是火情结论,也不是多目标跟踪;证据行 `hardware_commands` 恒为 0。配额耗尽或写失败后证据存储永久失败,不打完成标记;不完整前缀不能当完整结果使用,重试用新的输出路径。零事件运行不在磁盘留下证据文件。

## CSV 契约

UTF-8 / ASCII，无表头、无引号；空行和 `#` 开头的注释跳过。检测每行：

```text
source,epoch,frame_id,time_sec,lens,width,height,x1,y1,x2,y2,confidence
```

遥测每行：

```text
source,time_sec,latitude_deg,longitude_deg,altitude_m,height_agl_m,pitch_deg,roll_deg,yaw_deg,gps_valid,height_valid,attitude_valid
```

质量标志为 0/1。遥测姿态必须是 **相机全局 NED 姿态（度）**，不是未经转换的机体姿态或云台相对角。GPS 海拔和相对地面高度不能互换。图像与遥测必须先映射到相同时间轴，当前回放统一为 Replay；飞行器时间戳到主机单调时钟的映射尚未实现。

一个检测输入只允许同一个 source / epoch / lens；遥测可以多源交错，但文件时间全局非递减。匹配同源最近的前/后样本，等距选较早者，质量无效不回退旧的有效记录。遥测契约暂不含 epoch，不能声称覆盖跨重连周期匹配。

## 资源和失败行为

- 检测队列默认 8，允许 1..4096；离线回放背压等待、不丢记录。未来实时视频适配可以使用已有 `PushLatest` 丢旧帧策略，但尚未接入。
- 回放仅保留前后两个遥测候选；共享模块默认历史 256、最大 4096。
- CSV 单行最多 1024 字节，输入必须是普通文件；读取前检查每个输入不超过 64MiB。输入需在回放期间保持不变，此检查不是针对并发增长文件的动态字节预算。
- 输出默认最多 16MiB，可配置 1024..1GiB；排他创建，拒绝覆盖现有文件。
- 非法输入、写失败或配额耗尽返回非零，线程取消并回收。失败可能留下有效前缀 JSONL / CSV，**不能作为完整结果使用**；完整成功必须同时满足进程返回 0 和最终 stdout 摘要。重试使用新的输出路径。
- 输出中的 `elapsed_sec` 是 HOST 元数据处理耗时，不是推理 FPS、视频延迟或端到端性能指标。

地理结果仅是“框底中点对应地面”的平地假设；烟雾可能离地，不能宣称火源真值。真实云台执行、航线改变、报警外发均未实现。

## 来源与版本管理

根 SDK、已有 `uav_smoke_platform`、`smoke_gateway` 保持原状。本目录不递归编译 SDK 全部样例。`tools/snapshot_vendor.ps1` 可对当前 SDK 指定文件建立只读参考副本与 SHA256 清单；参考文件默认忽略，不作为经过修复的编译源。

模型、视频、生成结果、构建产物、SDK 二进制和密钥不得提交。当前状态与交接见 [任务清单](docs/TASKS.md) 和 [验证记录](docs/VALIDATION.md)。
