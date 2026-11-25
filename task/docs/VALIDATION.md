# HOST 验证记录

日期：2026-11-25（Asia/Shanghai）。本记录只覆盖当前模块化主线，不覆盖原 SDK 整体构建、真实模型、真实视频或飞行硬件。

## 本轮修复与回归依据

1. 上轮缺少 `test_replay.py`，已补齐 10 个 unittest 方法；不是跳过失败测试。
2. 检测导出器新增 14 个 unittest 方法，其中新增边界曾出现 14 个失败子用例：非选中非法框、尺寸有损转换、倒序时间、非法镜头。修复后再运行。
3. PSDK 映射以前可能给 NaN 姿态、Inf 高度或越界经纬度标记 valid；已按各测量数值与时基判断质量，增加回归。后续校验仍保留。
4. CMake 3.10 分支使用 `FindPythonInterp`；新版使用 `FindPython3`，避免最低版本声明与模块可用性冲突。此兼容分支尚未在 CMake 3.10 / Python 3.6 实机执行。

## 已取得的暂存验证

Windows x86_64：CMake 4.3.4、MinGW GCC、Python 3.14.0，Debug；PSDK 映射使用当前 J 盘 `psdk_lib/include`。

```powershell
cmake -S .task_impl -B .task_impl/build-handoff -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DBUILD_TESTING=ON -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT=J:/Payload-SDK-3.10.0-task
cmake --build .task_impl/build-handoff -j 4
ctest --test-dir .task_impl/build-handoff --output-on-failure
```

最后暂存结果：7/7 CTest 组通过，1.52 秒。组为 queue、localization、gimbal、io、psdk_mapping、replay_integration、detection_export。Python 两组共 24 个测试方法，含多个边界子用例。queue=1 的 10000 条 metadata 测试验证完整排空，不代表无人机图像并发能力或 GPU FPS。

## 正式 task 目录重跑

27 个白名单源码/测试/工具文件从暂存区复制到 `task/`，逐文件 SHA256 一致；没有复制 exe、构建目录、视频、模型或旧输出。后续以 `task/` 为唯一维护目录，`.task_impl/` 只留作旧暂存备份。

| 平台与配置 | 构建目录 | CTest 结果 |
|---|---|---|
| Windows x86_64 Debug，GCC 15.2.0 / Python 3.14.0，PSDK mapping ON | `task/build-host` | 7/7，通过，1.73s |
| WSL2 Ubuntu 24.04 x86_64 Release，GCC 13.3 / Python 3.12.3，mapping ON | `task/build-linux-release` | 7/7，通过，1.27s |
| 同上 Debug + ASan/UBSan，mapping ON | `task/build-linux-asan` | 7/7，通过，1.68s，无 sanitizer 诊断 |

Linux 配置/构建/验证时间为 2026-11-25 11:41:57–11:43:00；WSL kernel 6.6.87.2、CMake 3.28.3。`/mnt/j` 出现亚秒级 clock-skew 警告，构建退出 0；不是 Nano 环境。

```powershell
cmake -S task -B task/build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DBUILD_TESTING=ON -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT=J:/Payload-SDK-3.10.0-task
cmake --build task/build-host -j 4
ctest --test-dir task/build-host --output-on-failure
ctest --test-dir task/build-host --output-on-failure --repeat until-fail:20 -R '^(queue|localization|gimbal|io|psdk_mapping)$'
```

五个 C++ 组各重复 20 次，共 100 次，全部通过，耗时 6.77s。这是短时并发/逻辑回归，不是长期压力测试，也不证明没有全部数据竞争。

Linux Release 的可复现命令（在现代 CMake 的 WSL 仓库根执行）：

```sh
cmake -S task -B task/build-linux-release -DCMAKE_BUILD_TYPE=Release -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT="$PWD"
cmake --build task/build-linux-release -j4
ctest --test-dir task/build-linux-release --output-on-failure
cmake -S task -B task/build-linux-asan -DCMAKE_BUILD_TYPE=Debug -DSMOKE_WITH_PSDK_ADAPTER=ON -DPSDK_ROOT="$PWD" -DSMOKE_ASAN=ON -DSMOKE_TSAN=OFF
cmake --build task/build-linux-asan -j4
ctest --test-dir task/build-linux-asan --output-on-failure
```

## 未取得有效结果：TSan

暂存区 `.task_impl/build-linux-tsan` 编译成功，但 CTest 仅 2/7 返回通过；queue、localization、io、replay 等在 TSan 启动时报 `FATAL: ThreadSanitizer: unexpected memory mapping`，mapping 测试启动时 segfault。当前 WSL 环境未取得有效竞争检查结论，**不能宣称 TSan 通过或已排除所有 race**。没有调整 ASLR、内核或安装替代工具来绕过限制。后续需在兼容的 Linux 环境补测。

## 来源与 Git 检查

`snapshot_vendor.ps1` 已对当前 J 盘 SDK 的 21 个源码/许可文件建立本地参考副本，复制前后 SHA256 一致；2 个外部资产只记录哈希，不复制。参考快照未参与编译且被 Git 忽略，`vendor/source_manifest.json` 记录当前来源，不将历史其他目录的源码版本混入。

`git check-ignore -v` 实测构建 exe、参考许可副本、results、models、视频和 `__pycache__` 均被忽略。可纳入 Git 的新增文件最大仅约 14KB，没有大文件、凭证或生成图像。本轮只修改根 `.gitignore`、新增 `task/`，未改原 SDK 和既有业务目录；未执行 git add / commit / push。最终文件状态以交接时 `git status` 为准。

最终复核：`task/` 有 32 个可纳入 Git 的文件，未发现超过 1MiB 的文件或生成/媒体/模型后缀；源快照二次执行按预期拒绝覆盖，原 manifest 哈希不变。Windows 完整 CTest 最终再跑为 7/7、1.47s。当前分支 `main`，HEAD `504e311` 为此前已有提交，不包含本轮尚未提交的 task 文件。根历史 `HANDOFF_2026-11-25.md` 只增加了现行状态覆盖说明，原 `HANDOFF.md` 未修改。

## 尚未验证

- Nano / aarch64 编译、JetPack 与 TensorRT 兼容性。
- UART、E-Port Bulk、PSDK 运行时、M350/H30T 开流与镜头并发。
- 视频解码、真实模型推理、端到端帧延迟、2GB 内存峰值与长期稳定性。
- 云台真实执行、目标地理精度和飞行安全。

所有集成 fixtures 明确为 synthetic metadata；没有伪造真实模型/视频运行结果。运行时程序始终 `hardware_commands=0` / `control_mode=dry_run`。

## T19a 追加验证(2026-11-25,无硬件)

新增 `src/events/stable_event.cpp`、`src/events/evidence_store.cpp`、`include/smoke/stable_event.hpp`、`include/smoke/evidence_store.hpp`、`tests/test_events.cpp`(12 组检查:阈值以下不成事件、连续帧成事件、阴性帧/弱置信/时间间隔断流、换 epoch 关合、乱序拒绝、事件存储有界、配额精确到字节、失败存储拒绝完成、排他创建、完成标记)。`smoke_replay` 新增三个选项,`test_replay.py` 由 10 例扩至 16 例(事件成证、阈值下零事件且零残留、阴性拆分、输出路径冲突、排他重建、配额失败无完成标记)。

| 平台与配置 | 结果 |
|---|---|
| Windows Debug(MinGW GCC 15.2 / Python 3.14,mapping ON) | CTest 8/8,10 次重复全过 |
| WSL2 Ubuntu 24.04 Release(GCC 13.3,mapping ON) | CTest 8/8 |
| WSL2 Debug + ASan/UBSan | CTest 8/8,无 sanitizer 诊断 |

注意:test_events.cpp 的 C++14 核心无 std::filesystem,文件用例用 `std::remove` 预清理唯一路径。证据存储为惰性创建:零事件运行不在磁盘留下未完成文件。配额耗尽后存储永久失败,`.complete` 标记只在最终 flush 成功后写出;不完整证据前缀不能当完整结果使用。

## T18a 追加验证(2026-11-25,无硬件)

遥测样本与 CSV 契约新增可选 `stream_epoch`(第 13 列;12 列历史格式兼容为 epoch 0);`TelemetryBuffer::Match` 增加 epoch 隔离,失败原因按契约顺序诊断(`no_telemetry_for_source` → `time_base_mismatch` → `telemetry_epoch_mismatch`)。新回归:io 3 项(13 列解析、非法 epoch、14 列拒绝),localization epoch 隔离组(旧 epoch 不可匹配、新 epoch 正常、无样本 epoch 报因),replay 集成 2 例(epoch 错位全部 `telemetry_epoch_mismatch` 且不定位、同 epoch 显式 13 列正常定位)。

| 平台与配置 | 结果 |
|---|---|
| Windows Debug(MinGW GCC 15.2 / Python 3.14,mapping ON) | CTest 8/8,10 次重复全过;Python 18 例 OK |
| WSL2 Ubuntu 24.04 Release(mapping ON) | CTest 8/8 |
| WSL2 Debug + ASan/UBSan | CTest 8/8,无 sanitizer 诊断 |

遗留:检测帧时间戳到主机单调时钟的映射、有效 AGL/全局姿态来源仍依赖 T14;本项只覆盖重连 epoch 失效保护的元数据部分。

## T18b 追加验证(2026-11-25,无硬件)

`smoke_analyze_video` 新增 `--stream-epoch`(写入 `video_summary.json` 与 `detection_results.json`);`export_detections.py` 新增 `--epoch`(默认 0,写进 CSV 第 2 列,与 T18a 遥测契约对齐;`rows()` 的 epoch 为默认参数,旧 3 参调用兼容)。导出测试由 14 例扩至 17 例(epoch 写入行、非法 epoch 拒绝、CLI 默认值与显式 epoch)。

端到端贯通检查(WSL 真模型跑合成 30 帧 MP4):`--stream-epoch 9` 后 `video_summary.json` 与 `detection_results.json` 的 `stream_epoch` 均为 9;该 JSON 经导出器加 `--epoch` 可变成带 epoch 的回放 CSV,交由 T18a 的匹配隔离使用。

| 平台与配置 | 结果 |
|---|---|
| Windows Debug(MinGW GCC 15.2 / Python 3.14) | CTest 8/8;Python 17+18 例 OK |
| WSL2 Release + OpenCV/FFmpeg 视频管线(build-video) | CTest 11/11(含 video_cli 真模型) |
| WSL2 Debug + ASan/UBSan | CTest 8/8,无 sanitizer 诊断 |

遗留:帧时间戳到主机单调时钟的映射、有效 AGL/全局姿态来源仍依赖 T14。

## T15a 追加验证(2026-11-25,无硬件)

新增 `src/video/h264_stream.cpp`、`include/smoke/h264_stream.hpp`、`tests/test_h264_stream.cpp`。`H264Packetizer` 把任意字节切块重组为完整 H.264 访问单元,显式帧所有权(每帧独立 payload),供未来 PSDK `DjiLiveview_H264Callback` 直连。关键实现点:增量扫描(每个字节只处理一次,跨块 3/4 字节起始码,无起始码时保留尾部 3 字节防止跨块码被误吞)、按切片头 `first_mb_in_slice` ue(v)(含去模拟字节,位级解析)区分首片/续片分帧、SPS/PPS/SEI 并入当前 AU、首个关键帧之前的数据按字节丢弃并计数、有界重组缓冲(2MiB 默认)与帧大小上限(512KiB 默认)、AUD 分帧、`Finish()` 结束流、`ResetStream()` 重连(序列号重置,失败状态粘滞)。测试 10 组 103 项检查:参数集+IDR+续片成一帧、VCL 首片分帧、逐字节切块透明、垃圾前缀丢弃、关键帧前数据丢弃、AUD 分帧、超帧失败粘滞、缓冲溢出失败、重连重置、空 Push 与非法配置。

| 平台与配置 | 结果 |
|---|---|
| Windows Debug(MinGW GCC 15.2) | CTest 9/9,h264 单测 10 次重复全过 |
| WSL2 Ubuntu 24.04 Release | CTest 9/9 |
| WSL2 Debug + ASan/UBSan | CTest 9/9,无 sanitizer 诊断 |
| WSL2 Release + OpenCV/FFmpeg 视频管线 | CTest 11/11(回归不受影响) |

遗留:解码器本体接入(FFmpeg/OpenCV 接口适配)与真实 H264 流验证依赖 T14 采集;本模块不解析 SPS 参数内容,分辨率契约由调用方提供。
