# MVTBOT 与设备端统一开发入口

Android 材料已汇总到 [`Android/MVTBOT`](../Android/MVTBOT/README.md)，设备端继续位于 `Firmware/`。

| 需要做什么 | 从哪里开始 |
|---|---|
| 查看最新 APP 与构建方法 | [模块 README](../Android/MVTBOT/README.md) |
| 了解当前版本和未解决问题 | [开发交接](../Android/MVTBOT/docs/HANDOFF.md) |
| 同时核查 APP 和 MCU 的蓝牙处理 | [协同入口](../Android/MVTBOT/docs/PROTOCOL_COORDINATION.md) |
| 查看原始 NUL 闪退证据 | [2026-09-16 原始分析](../Android/MVTBOT/materials/deliverables/MVTBOT/diagnostics-20260916/原因分析.md) |
| 查看归档完整性与本地重建结果 | [汇总验证记录](../Android/MVTBOT/docs/CONSOLIDATION.md) |
| 查看设备端最新状态 | [开发记录](DEVELOPMENT_LOG.md)、[已知问题](KNOWN_ISSUES.md) |

当前 APP 是从安装包派生的 Apktool 重建工程，不是原始 Android Studio Java/Kotlin 工程。JADX 文件供阅读且存在未还原方法；应用协议变更应结合 smali/实际 DEX、当前设备端代码和实测日志共同确认。

原有 v20/v21 安装与手机测试记录来自 2026-09-16，不能当作 2026-09-24 手机或小车当前状态。构建、签名、安装、设备下载和实机功能验证分别记录。

实际项目根目录为 `C:\Users\Administrator\Desktop\CODEX_PROJECTS\Two-wheels-Car`。Codex 保存的“两轮自平衡小车”入口仍是改名前的路径；后续在该项目启动任务前，应在项目设置中将主文件夹指向这个实际目录。资料已写入真实项目，无需继续使用旧聊天目录作为 Android 工作目录。
