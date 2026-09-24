# Android 开发交接

## 当前版本

| 项目 | 当前资料基线 |
|---|---|
| 应用名 / 包名 | MVTBOT / `com.Wonder.bot` |
| 当前 APK | `materials/deliverables/MVTBOT/icon-update-v21/MVTBOT.apk`（相对模块根目录） |
| 当前版本 | `2.3.6-mvtbot.2`，versionCode 21 |
| SHA-256 | `6b27d6af2261b0f4d559828f9fcc6df1a9e3f464c0d8b592278ceadc1a98463e` |
| 签名证书 SHA-256 | `71e85f94fb18c04425c908661e7321dce8dc182e7077cc541004aaf539e15287` |
| 最初原版 | Google Play Wonderbot 2.3.6 / 20，base + ARM64 + xxhdpi + 中文 4 包 |
| 适用架构 | ARM64，最低 API 23，目标 API 36 |

2026-09-16 的完成记录：提取并校验原始拆分包；安装 Android 工具链；合并拆分包、修改应用名称并安装 v20；随后只更新图标和版本标识，安装 v21，保留应用数据。v20 与 v21 的 DEX、本机库和业务逻辑相同。

v20 APK SHA-256 为 `f2a94782f8763ea6fe1867ba5b5ffa4f5e13878a67a66f632b644f7654c6655e`。原始 `base.apk` SHA-256 为 `5ee5b4739df70b569ab88453d7aba45724f233944d4b052a8068a9947ebcf34e`；其余拆分包哈希见原包 `export-manifest.json`。

## APP-001：CMD4 速度解析遇 NUL 时闪退

状态：**直接触发点已证实，上游来源未确认，用户明确延后修复，等待设备端日志。** 本次资料整合未修复或重新安装 APP。

Xiaomi 13 的 MVTBOT 在 13:54:02、13:54:43，以及 Xiaomi 14 的原始 Google Play 包在 14:18:39，均因为第二个电机速度字段为 `-1\0`，在 `BalanceCarHomePageFragment.handleRecv` 原源码行 239 调用 `Integer.parseInt` 时崩溃。原版 4 个 APK 哈希与提取时完全一致，不能将该异常认定为改名/合并才产生。

三段日志共 27 个 NUL，都位于 CMD4 子帧的零基索引 12。24 帧破坏 `$` 而被忽略；3 帧保留 `$` 且 NUL 落入数字，触发崩溃。Xiaomi 14 样本 879 条中有 860 条 `0|0`；两台手机的触发输入分布不同，不能用“2 次对 1 次”推定机型稳定性差异。

原始日志中的 `C0 80` 是 JNI Modified UTF-8 对一个 Java NUL 的表示，不是两个普通乱码，也不等于线上收到了 `C0 80`。必须保留 `.raw`，不要仅依靠早期错误解码的 `.txt`。固定位置提示检查组包/长度/缓冲区，但不能直接定责为某个 `snprintf` 或 MCU 故障。

主证据：[完整分析](../materials/deliverables/MVTBOT/diagnostics-20260916/原因分析.md)。原始证据位于 `materials/evidence/bluetooth-20260916/`。

## APP-002：页面销毁时 Receiver not registered

状态：独立记录，未修复。原版 2026-09-16 13:56:52 在 `MainActivity.onDestroy → BLEManager.unregister` 出现接收器注销异常。它与 APP-001 的 NUL/整数转换堆栈不同，不能混算为同一原因。

## 分析材料的可信度

- `project/`：完整单 APK 的 v21 重建输入，DEX 与原包相同；可以重编译资源和重新封装。
- `materials/analysis/original-base-apktool/`：原始主包的 smali/资源参考，缺少拆分资源，不应替代当前完整工程直接发布。
- `materials/analysis/jadx-reference/`：4113 个 Java 参考文件，103 项反编译错误，部分方法有 `Method not decompiled`；必须与 smali、真实 DEX 交叉验证。
- 本地构建通过、手机安装通过、主页面显示、真实蓝牙/小车功能验收是不同证据层次，分别记录。

## 后续协作方式

1. 先阅读设备端 `docs/DEVELOPMENT_LOG.md` 和 `docs/KNOWN_ISSUES.md` 的最新状态，再记录实际在机固件哈希与 APP 版本/哈希，避免混淆旧样本。
2. 以同一设备、同一固件、同一操作和时间窗采集 UART/蓝牙/APP 三层证据，定位 NUL 首次出现的位置。
3. 仅在获得对应开发范围后修改协议或容错，并同时设计设备端与 APP 端的回归样本。
4. 构建脚本不进行手机安装或固件烧录；部署时重新检查设备身份、当前版本和签名。

本模块资料于 2026-09-24 归档，根目录设备端还在其他任务中演进。本次未把设备端 T1 等后续修改视为 APP-001 的修复证据。

## 2026-09-24 LINK-001 后续实现

上面的版本与崩溃叙述为 v21 历史基线。用户现已明确授权 HC-05D BLE、完整帧防闪退与失联保护一起开发。维护输入保持 v21；默认构建输出升级到 v24，使用独立 Java 会话/协议组件和版本化 smali 桥接，处理 E0FF/FFE0、GATT ready、旧事件隔离、页面/传感器生命周期与100ms心跳。

防闪退回归已覆盖原NUL类型及电压/PID/波形入口，不将其表述为上游根因已确定。业务源码、DEX签名检查和主机回放不能代替真实ART、手机蓝牙与整车测试。版本组合、实物步骤和当前证据见仓库 `docs/HC05D_COMPATIBILITY.md`、`docs/HC05D_TEST_REPORT.md`。

## HC-05D 现场后续与当前构建入口

默认业务构建已推进到v36，UI-only回退候选为v37；v21保持不可变输入。手机曾安装v24/v26/v28，v28的固定20字节尝试未解决错误13，已从当前实现移除。真实模块自报QUALCOMM/HC-05D/1.1.4，响应写多字节会报13但实际UART转发、电压/PID回包成立；无响应写未得到交付证据。当前实现使用严格DIS/服务属性范围和CMD7双向握手，只有已验证会话按该厂商行为处理13，未知设备和其他错误仍失败关闭。

固件已获授权烧录、回读并运行，PID区保留；现场手机结果、边界与配对哈希以根目录`docs/HC05D_DEVICE_VALIDATION_20260924.md`为准，不使用前述部署前状态代替当前证据。`-VersionCode`与`-VersionName`可成对覆盖构建号，不能修改固定输入来升版本。

2026-09-24阶段收尾：用户已离开设备，明确下次再做联动。已安装并读回校验v36自动连接诊断包，基础数据和PID回读/波形显示已验证；后台取消、启动横屏500ms稳定等待及目标地址限定已纳入回归。原始现场记录与后续限制仍以HC05D_DEVICE_VALIDATION_20260924.md为准。不要把150px中立区内的触摸尝试写成非零控制验收；现场未发出对应CMD3。
