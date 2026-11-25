# 模块化主线交接

日期：2026-11-25。执行：Codex / 桐乃（Implementation Engineer）。

## 当前事实

- 唯一主线：`J:\Payload-SDK-3.10.0-task\task`。不依赖历史 F 盘路径；不要再以访问 F 盘作为开发前置条件。
- 原 SDK 保留；新增模块不递归编译旧样例。`.task_impl/` 是旧暂存备份，不再维护；不要在两处同时修改。
- 已完成检测元数据之后的 HOST 逻辑基座：有界并发、遥测时间匹配、条件定位、云台 dry-run、回放与 JSON 桥接。
- Windows Debug、WSL Linux Release、WSL ASan/UBSan 均 7/7 组通过。TSan 当前环境启动异常，未取得有效结果。详见 [验证记录](VALIDATION.md)。
- 没有 Nano / M350 / H30T 实机连接，没有执行刷机、SDK 激活、USB 角色调整或云台运动。
- 根 `.gitignore` 已允许管理 `task/`，目录内忽略构建、结果、模型、视频、SDK 参考副本等。本轮未 stage、commit、push，原 Git 提交不代表本轮新增模块已入库。

## 下一实施顺序

1. 完成当前源码 review 和版本保存，不混入 `uav_smoke_platform` / `smoke_gateway` 的其他路线变更。
2. 确认实际 Nano 硬件和安装栈；按 2GB 预算，不因用户同时提 B01 就假定可用 4GB。可在设备上运行只读 `tools/inspect_device.sh`。
3. 基于当前 SDK 样例建立最小采集/解码适配，先单业务流、单执行器、显式帧所有权；验证坏流与退出路径。
4. 校验真实烟雾模型，接入实际 Nano 兼容推理后端，做真实视频闭环；不默认替换模型，不用回放吞吐代替推理指标。
5. 实现设备/主机时钟映射、有效 AGL、镜头标定和全局相机姿态，再验证地面投影；遥测目前没有 epoch 字段，重连场景需扩充契约。
6. 加稳定事件、证据存储与故障恢复；云台仍先 dry-run，台架通过且获用户授权后才接执行。

完整任务与验收门见 [TASKS.md](TASKS.md)。未勾选项不得在汇报中写为完成。

## 快速验证

在仓库根执行 README 中的构建命令，然后：

```powershell
ctest --test-dir task/build-host --output-on-failure
task/build-host/smoke_replay.exe --help
git status --short
```

回放测试中的数据均是标注清楚的 synthetic metadata，不是模型输出的伪造。成功必须看退出码；异常可能留下输出前缀，不能当作完整报告。禁止覆盖已有输出以重试。

`vendor/reference/` 是仅供检查的 SDK 参考副本，不是已修复的可执行代码。来源清单记录本次 J 盘内容；不要根据旧的其他目录审计结论认定当前 SDK 已有或仍有同样缺陷。后续修改旧实现前需重新检查当前源码。
