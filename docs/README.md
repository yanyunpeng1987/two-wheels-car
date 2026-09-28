# 开发文档入口

本仓库统一管理 BalanceCar 设备固件与 MVTBOT Android 控制应用。后续任务从需求台账开始，代码、协议、版本和验收记录随同一次变更更新。

| 文档 | 负责的内容 |
|---|---|
| [需求台账](REQUIREMENTS.md) | 需求 ID、范围、当前状态、验收与下一步；当前工作优先看这里 |
| [版本与联调矩阵](RELEASE_MATRIX.md) | 仓库版本、APK/固件组合、验证层级及本地实验边界 |
| [HC-05D合并测试报告](HC05D_TEST_REPORT.md) | 统一APK/HEX哈希、主机/成品检查及待实机项目 |
| [HC-05D现场验证记录](HC05D_DEVICE_VALIDATION_20260924.md) | 手机安装、固件回读、PID保护及模块写入异常的实际证据 |
| [HC-05D下次现场检查](HC05D_NEXT_BENCH_TEST.md) | 已安装组合、自动连接行为及待本人在场进行的联动/失联检查 |
| [HC-05D断连与重连调查](HC05D_DISCONNECT_INVESTIGATION_20260928.md) | 9月28日手机主动断连记录、健康截止/扫描/MCU查询回放及下一版取消自动连接要求 |
| [9月28日自稳倒下记录](FALL_CAPTURE_20260928.md) | 冻结的右编码器94计数/SPD首因、Flash及滤波配置核验、后续边界 |
| [HC-05D兼容与验证](HC05D_COMPATIBILITY.md) | 模块配置、APK/MCU实现、失联契约、构建与实物验收 |
| [通信协议基线](PROTOCOL.md) | 当前源码行为、传输边界与两端变更约定 |
| [贡献流程](../CONTRIBUTING.md) | 分支、并行开发、验证、PR 与回退 |
| [设备端开发记录](DEVELOPMENT_LOG.md) | 设备端已发生的修改、构建和实测证据，按时间追加 |
| [设备端问题登记](KNOWN_ISSUES.md) | KI 编号的现象、证据与处理状态 |
| [Android 开发入口](ANDROID_MVTBOT.md) | APP 项目结构、交接、构建和历史证据导航 |
| [Android 交接](../Android/MVTBOT/docs/HANDOFF.md) | APP-001/002、历史版本与原始材料可信度 |
| [工程基线](BASELINE.md) | Keil、Flash 布局、原始导入边界 |
| [阶段变更](../CHANGELOG.md) | 进入版本管理的阶段变化，不代替详细证据 |

文件归属：`Firmware/` 放设备源码；`Android/MVTBOT/` 放 APP 维护入口；`docs/` 放跨端需求/协议/验收；`tools/` 放通用检查；`tests/` 放设备端主机回归。已有原始材料保持原位置，不复制成第二套有效源码。

需求状态以需求台账为准；具体问题根因与证据以问题文档为准；实际发布组合以版本矩阵为准。历史日志不回写成今天的结论。发现矛盾时补充当前状态和依据，保留历史记录。

新 clone 可以检查和构建设备固件；APK 还需恢复本地材料，步骤见 [Android README](../Android/MVTBOT/README.md)。GitHub 不是 Android 完整材料或签名备份。
