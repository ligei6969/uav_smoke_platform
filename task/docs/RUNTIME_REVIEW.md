# 视频运行可靠性与 Nano 优化核查

核查与修改日期：2026-11-25。

## 本次已落地

### 多目标选择和云台决策

`task/include/smoke/target_selector.hpp`、`task/src/tracking/target_selector.cpp` 实现单关注目标选择，不是多目标轨迹跟踪器。视频消费者每一帧都调用，包括零检测帧；保留原始全部检测结果，同时写 `target_selection.jsonl`。

- 默认连续3帧确认获取；用IoU关联当前目标，允许低于获取阈值的保持阈值。
- 另一目标须持续高出当前置信度0.15，并连续3帧确认才切换，避免最高分波动带来云台来回跳。
- 空帧或匹配丢失立即不输出旧目标；连续3次丢失或0.75秒无匹配释放内部关注状态。帧过期上限0.5秒。
- source/lens/epoch变化清状态；拒绝重复和乱序帧；候选上限300，固定规模状态，单消费者持有，无新增锁或线程。
- 选择结果接入现有 `GimbalController` 生成带死区、步长与限频的控制建议，记录原因、当帧目标与角度步长，`hardware_commands=0`。

新策略已接入录制视频HOST管线。旧实机SDK执行器尚未接入这套新策略，不据此声称多目标真机闭环已经验收。画面目标居中是正常死区，不等于程序死锁；实际云台机械卡住需要角度反馈和指令回执，当前没有完成执行反馈闭环。

### 旧解码器的卡流相关修复

J盘 `samples/sample_c++/module_sample/liveview` 修改4个decoder/ImageHandler源码与头文件：

- 初始化和重复初始化的所有路径释放互斥锁；分配失败清理部分资源，支持重试。
- 先停止并join回调，再释放解码资源，最后销毁互斥量；运行标志使用atomic。
- 回调内重入生命周期接口在加锁前拒绝，避免自join及与停止线程互等；生命周期仍由owner线程串行操作。
- 条件变量的绝对超时正确分配秒/纳秒，循环检查图像谓词，避免忙等待。
- 拒绝FFmpeg parser负数、越界及持续零进展，保留一次合法缓冲包输出。

`tests/legacy_liveview` 故障注入测试直接编译上述真实源码，仅FFmpeg C API和logger用替身控制失败场景。F盘原基线未修改。

### 无法响应线程取消的底层调用

`task/tools/run_guarded.py` 提供独立进程级总时限，超时先TERM再KILL本次创建的进程组，返回124。不会只设置取消标志后无限等待 `join`。超时留下的部分输出不能当完整结果。不会自动重新使用原输出目录，也不调用飞机或云台。

```sh
python3 task/tools/run_guarded.py --timeout-sec 300 --grace-sec 2 -- \
  task/build-review-20261125/smoke_analyze_video \
  --video <视频路径> --model <已核验模型> --output-dir <全新输出目录>
```

这是一项可选启动保护，不是已经部署的实机断流自动重连。实时重连还需在采集owner线程串行关闭/重开SDK会话、增加epoch、清空旧缓存，并等待新关键帧；本次没有访问硬件。

## 定位和路径实际方案

定位为框底中心像素→相机射线→相机全局姿态旋转至NED→与相对地面高度定义的平面求交→WGS84经纬度估计。要求镜头参数、GPS/高度/姿态有效，并匹配同源同会话遥测；接近地平线、镜头不匹配、无效遥测或超距离拒绝定位。

这是平地近似的地面投影，不是SLAM、测绘级火源定位或已验证的山地地形定位。相机视场角、畸变、AGL、全局姿态以及真实时钟映射需要台架标定。

既有MissionPlanner仅在目标周围生成4个复核航点，没有障碍物地图、碰撞检测、局部绕障或飞控避障集成。障碍物处理不写为已实现功能；未来如执行任务，应先接入飞控/机载障碍信息，阻断或交由人工复核冲突路径，再讨论局部重规划。

## Nano 资源与推理加速的实施顺序

1. 先消除锁泄漏、忙循环和parser无进展；单业务流、单模型执行器、有界完整帧缓存。实时允许丢旧的应是独立解码帧，不能随意丢H.264字节块；录制视频继续背压全帧处理。
2. 复用模型、GPU输入输出缓冲、CUDA stream与CPU预处理内存；压低图像复制和RGB/BGR往返次数。现有J增强版已经复用GPU缓冲/stream，但host输入与bindings每次分配的可选补丁尚未应用。
3. 在实际L4T/JetPack组合上验证NVIDIA硬件H.264解码与NVMM通路，避免把当前FFmpeg CPU路径叫作硬解或零拷贝。
4. 在Nano本机为固定模型构建/加载TensorRT engine，对比FP32与FP16的准确率、p50/p95延迟、RSS/GPU内存、CPU占用和温度。engine文件名不能证明精度；原版Nano不采用INT8加速承诺。
5. 降采集分辨率、限制推理频率、禁用不必要显示/录像可省资源，但须同时评估远距离弱烟雾召回；当前固定640×640模型不可不经重新导出就改输入尺寸。

F盘原实机源码主要为OpenCV DNN；J盘增强源码有TensorRT接口。HOST回归不提供Nano GPU速度或峰值内存证据，不填无实测依据的FPS/百分比。

参考：

- https://github.com/dusty-nv/jetson-inference/blob/master/docs/aux-streaming.md
- https://docs.nvidia.com/jetson/archives/l4t-archived/l4t-3276/Tegra%20Linux%20Driver%20Package%20Development%20Guide/accelerated_gstreamer.html
- https://forums.developer.nvidia.com/t/ped-100-improvement/195121

## 本轮验证

- 全新Linux Release构建：`task/build-review-20261125`，OpenCV 4.6、FFmpeg 6、GCC 13.3，CTest 15/15通过，9.88秒。
- `target_selector` 独立检查83项通过；录制视频CLI验证逐帧选择记录、空目标不产生云台建议及epoch贯通。
- 解码故障注入覆盖分配失败、重复init、条件等待、parser无进展、回调重入与停止析构。使用同一测试的旧源码复现init/parser超时，修复版通过。
- 进程保护测试覆盖正常退出、错误码透传、永久等待、忽略TERM及参数安全传递。
- 新建 `task/build-review-asan-20261125`，gimbal、h264_stream、target_selector、legacy_lifecycle 的ASan/UBSan定向回归4/4通过，3.37秒，无sanitizer诊断。

本轮没有发布到GitHub、刷机、激活SDK或控制飞机。

SDK样例在原有Git忽略范围内；4个实际修复文件已在J盘更新，便于复用的差异与原/新SHA256另存于 `task/patches/`。故障注入测试位于可版本化的 `task/tests/legacy_liveview`。未应用仅静态验证的TensorRT host缓冲可选补丁。
