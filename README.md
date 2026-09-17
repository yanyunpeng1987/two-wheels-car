# BalanceCar 两轮自平衡小车

基于幻尔科技 MiniBalan 的 STM32F401RB 固件建立的独立二次开发工程。Keil 与 VS Code **共用同一个 `.uvprojx`、Arm Compiler 6.16、源码清单和链接脚本**。

GitHub：[yanyunpeng1987/two-wheel-balancing-vehicle](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle)（私有）。初始源码基线标签：`v0.1.0`；这不改变固件内部版本号。

新电脑先克隆仓库并安装下文所列工具：

```powershell
git clone https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle.git
cd two-wheel-balancing-vehicle
```

仓库包含构建必需的源码和依赖；原始 `MiniBalan/` 包仅保留在原工作区，不随 Git 上传，新克隆也不需要它。

## 快速开始

### Keil

双击根目录 `Open-Keil.cmd`，打开新工程 `Firmware/MDK-ARM/BalanceCar.uvprojx`，选择 `BalanceCar`，按 **F7** 编译。首次建议执行 **Project → Rebuild all target files**。

当前项目目录开头有不可见字符，直接双击 `.uvprojx` 可能使旧版 Keil 读文件失败。打开脚本使用 Windows 已有的 8.3 短路径；它和 VS Code 操作的是同一份文件。移动到普通英文路径后也可直接打开 `.uvprojx`。

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
| `docs/BASELINE.md` | 迁移范围、存储布局、验证结果 |
| `docs/reference-import.json` | 原始参考树及导入源码 SHA-256 |
| `docs/DEVELOPMENT_LOG.md` | 开发过程、验证证据和用户反馈 |
| `docs/KNOWN_ISSUES.md` | 延后排查事项与独立待核查隐患 |

开发和提交约定见 [CONTRIBUTING.md](CONTRIBUTING.md)，阶段版本见 [CHANGELOG.md](CHANGELOG.md)，第三方来源见 [授权文件索引](docs/THIRD_PARTY_NOTICES.md)。GitHub Actions 只做源码静态检查，Keil 完整编译在已安装工具链的 Windows 上执行。

USB 手柄型号放行范围、仍需匹配的报文布局及复测方法见 [手柄兼容性说明](docs/GAMEPAD_COMPATIBILITY.md)。

后续修改 `Firmware/`。新增 `.c` 文件时在 **Keil 工程组中添加**，然后运行 `Project: Sync IntelliSense`；两套环境都依据 Keil 文件清单编译。不要把磁盘上存在但未加入工程的文件当成已编译模块。

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
