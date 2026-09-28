# BalanceCar 两轮自平衡小车

统一维护两轮自平衡小车的 **STM32F401RB 固件与 MVTBOT Android 控制应用**。设备端基于幻尔科技 MiniBalan；Keil 与 VS Code **共用同一个 `.uvprojx`、Arm Compiler 6.16、源码清单和链接脚本**。

持续迭代从 [开发文档入口](docs/README.md) 开始：[需求台账](docs/REQUIREMENTS.md)、[通信协议](docs/PROTOCOL.md)、[版本与联调矩阵](docs/RELEASE_MATRIX.md)。Android 入口为 [Android/MVTBOT](Android/MVTBOT/README.md)，当前是 v21 Apktool 派生工程，尚无原始 Android Studio 源码；新克隆需另外恢复本地输入，不能只凭 GitHub 重建 APK。

GitHub：[yanyunpeng1987/two-wheels-car](https://github.com/yanyunpeng1987/two-wheels-car)（私有）。初始源码基线标签：`v0.1.0`；这不改变固件内部版本号。

2026-09-28当前开发组合为已安装的MVTBOT APK40与HC-05D配套固件，用户确认基本测试通过。源码、APP业务补丁、构建工具、模块说明和验证摘要在 `codex/hc05d-ble-reliability` 分支；[当前APK版本显示与安装记录](docs/APP_VERSION_INFO_20260928.md)、[蓝牙及MCU部署记录](docs/HC05D_DEPLOYMENT_20260928.md)记录完整产物哈希和测试范围。APK原始输入、签名、成品与原始设备日志按既有规则本地保存。

新电脑先克隆仓库并安装下文所列工具：

```powershell
git clone https://github.com/yanyunpeng1987/two-wheels-car.git Two-wheels-Car
cd Two-wheels-Car
```

`Two-wheels-Car` 是本地目录名；GitHub仓库已更名为 `two-wheels-car`，上面的链接和克隆地址已同步。已保存的编辑器和Codex项目入口应指向实际本地路径。

仓库包含设备固件构建必需的源码和依赖；原始 `MiniBalan/` 包仅保留在原工作区，不随 Git 上传，设备端新克隆也不需要它。Android 原包、派生输入、APK、原始日志及签名材料按模块说明分别保管。

## 快速开始

### Keil

双击根目录 `Open-Keil.cmd`，打开新工程 `Firmware/MDK-ARM/BalanceCar.uvprojx`，选择 `BalanceCar`，按 **F7** 编译。首次建议执行 **Project → Rebuild all target files**。

本地工程已迁移到纯英文目录 `Two-wheels-Car`，可直接打开 `.uvprojx`。建议后续克隆也使用上面的目录名，避免复制粘贴带入不可见字符。`Open-Keil.cmd` 与 VS Code 仍操作同一份工程；脚本保留对旧 Unicode 路径的 8.3 短路径兼容处理。

### VS Code

打开根目录 `BalanceCar.code-workspace`（或整个项目根目录），按 **Ctrl+Shift+B**。默认任务依次同步代码提示配置、调用 Keil 构建、检查固件地址范围。

通过 **Terminal → Run Task** 可选择：

| 任务 | 用途 |
|---|---|
| `Firmware: Build` | 增量编译并检查固件 |
| `Firmware: Rebuild` | 完整重编译并检查固件 |
| `Keil: Check environment` | 检查工具链和设备包 |
| `Keil: Open IDE` | 打开 Keil，供后续使用其调试功能 |
| `Project: Sync IntelliSense` | 按 Keil 工程刷新宏和头文件路径 |
| `Project: Verify firmware` | 检查工程和已有 HEX/MAP |

VS Code 用于编辑、跳转和构建；本阶段在线调试使用 Keil。没有配置 VS Code 原生硬件调试器，构建任务不会启动下载或调试。

### 命令行

在根目录的 PowerShell 中运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Check
python tools/sync_vscode.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Rebuild
python tools/verify_project.py --artifacts
```

输出位于 `Firmware/build/keil/`：`BalanceCar.axf`、`BalanceCar.hex`、`BalanceCar.map` 及编译日志。使用带地址信息的 HEX 作为烧录输入；Flash 中间有参数保留区，不应简单拼成连续 BIN 后整片擦写。

## 目录与后续开发

| 目录/文件 | 作用 |
|---|---|
| `MiniBalan/` | 仅原工作区保留的参考代码和资料，不随 Git 上传 |
| `Firmware/Core/` | 主函数、时钟、HAL 初始化、中断 |
| `Firmware/BSP/` | 板级外设和传感器驱动 |
| `Firmware/Hiwonder/` | 现有控制、滤波和手柄逻辑 |
| `Firmware/Drivers/` | 本地 STM32 HAL、CMSIS 及授权文件 |
| `Firmware/USB_HOST/`、`Firmware/Middlewares/` | USB Host 与 HID |
| `Firmware/MDK-ARM/` | 新 Keil 工程、启动文件、显式 scatter |
| `tools/` | 构建、VS Code 配置同步和固件检查 |
| `Android/MVTBOT/` | APP 文档、打包脚本、品牌素材、输入哈希清单；本地材料由模块忽略规则隔离 |
| `docs/README.md` | 两端开发文档导航 |
| `docs/REQUIREMENTS.md` | 统一需求、状态和验收条件 |
| `docs/PROTOCOL.md`、`docs/RELEASE_MATRIX.md` | 两端协议与版本配对 |
| `docs/BASELINE.md` | 迁移范围、存储布局、验证结果 |
| `docs/reference-import.json` | 原始参考树及导入源码 SHA-256 |
| `docs/DEVELOPMENT_LOG.md` | 开发过程、验证证据和用户反馈 |
| `docs/KNOWN_ISSUES.md` | 延后排查事项与独立待核查隐患 |

开发和提交约定见 [CONTRIBUTING.md](CONTRIBUTING.md)，阶段版本见 [CHANGELOG.md](CHANGELOG.md)，第三方来源见 [授权文件索引](docs/THIRD_PARTY_NOTICES.md)。GitHub Actions 执行源码检查和手柄 C 回归测试，Keil 完整编译在已安装工具链的 Windows 上执行。

USB 手柄型号放行范围、仍需匹配的报文布局及复测方法见 [手柄兼容性说明](docs/GAMEPAD_COMPATIBILITY.md)。

`20BC:5500` 接收器使用独立的 9 字节解析，当前固定接口 0；原配手柄保持原格式解析。纯 C 报文解析、保护/停机诊断及电池监测模块均已加入 Keil 工程，当前编译输入为 77 个。已打开的 Keil 工程需重新加载项目文件以看到新增模块。

设备端后续修改 `Firmware/`；APP 从 `Android/MVTBOT/` 开始。新增 `.c` 文件时在 **Keil 工程组中添加**，然后运行 `Project: Sync IntelliSense`；两套环境都依据 Keil 文件清单编译。不要把磁盘上存在但未加入工程的文件当成已编译模块。

本基线保留参考固件的完整功能和 73 个活动编译输入，不是空白 HAL 工程。新增功能前先验证原板基本功能。原始 `.ioc` 保存在 `MiniBalan/`；直接用 CubeMX 覆盖新工程可能丢失手工驱动、源码组和参数分区，需要单独比对生成结果。

导入核查命令 `python tools/verify_project.py --baseline` 需要完整本地参考包；新克隆和日常开发使用 `python tools/verify_project.py`，编译后再加 `--artifacts`。

部分原文件为 GBK，其余主要为 UTF-8。导入过程保持字节和换行不变，VS Code 启用自动编码识别；编辑前留意状态栏编码。

## 工具要求

- Windows、Keil MDK，**Arm Compiler 6.16**；本机已验证 µVision 5.36。
- **Keil.STM32F4xx_DFP 2.17.1**；工程使用本地 CMSIS/HAL，无额外 CMSIS RTE 组件依赖。
- VS Code 的 Microsoft C/C++ 扩展；Python 3.10+ 用于配置同步和验证。
- 默认 Keil 安装位置 `C:\Keil_v5`。换电脑时可设置环境变量 `KEIL_ROOT`，或向构建脚本传 `-KeilRoot`、同步脚本传 `--keil-root`。重新同步后更新代码提示中的编译器路径。

不自动安装或更改全局工具链。Keil 编译许可仍由本机安装提供。

## 技术依据与授权

构建调用遵循 [Keil µVision 命令行文档](https://www.keil.com/support/man/docs/uv4cl/uv4cl_commandline.htm)，代码提示配置依据 [VS Code C/C++ 配置文档](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)。工程配置以本地 `.uvprojx` 和实际构建结果为准。

导入文件保留原有版权声明，HAL、CMSIS、USB 中间件的授权文件随源码保留。当前参考资料没有覆盖整个项目的统一授权文件，因此没有给整包重新声明统一开源许可证。

## Android APP 与设备端协同开发

MVTBOT 的 APK、重建输入、原始拆分包、反编译参考、蓝牙闪退原始日志和 Android 环境记录已归入 [Android/MVTBOT](Android/MVTBOT/README.md)。项目级入口见 [APP 开发资料索引](docs/ANDROID_MVTBOT.md)。

当前 APP 基线为 v21（2.3.6-mvtbot.2，MVTBOT 图标版）；蓝牙 NUL 解析闪退仍待结合设备端日志分析，未因资料合并而修复。设备端继续编辑 Firmware/；APP 的本地重建和签名脚本不执行手机安装或固件下载。原始包、派生工程与证据按现有仓库约定本地保存，文档和脚本可统一管理。

## HC-05D BLE 兼容开发

独立兼容版本支持 HC-05D 的 E0FF 和原模块 FFE0，新增完整帧容错、DMA收发保护及100ms/500ms遥控失联契约。配置、构建、回退和实物验收见 [HC-05D兼容说明](docs/HC05D_COMPATIBILITY.md)，主机/构建证据见 [测试报告](docs/HC05D_TEST_REPORT.md)。新固件应与新APP配套使用；本地通过不代表已经安装、烧录或整车验收。
