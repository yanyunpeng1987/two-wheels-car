# 初始工程基线与验证记录

验证日期：2026-09-17。本文件记录初始导入阶段的结果；后续功能修改后应重新构建和验证。后续 GitHub 建仓及用户换板反馈见 [开发记录](DEVELOPMENT_LOG.md)。

## 本次范围

建立独立的 `Firmware/MDK-ARM/BalanceCar.uvprojx`，保留 MiniBalan 现有功能作为二次开发起点。Keil 和 VS Code 共用这个工程，不引入 GCC 或第二套源码清单。

原始资料仍位于原工作区的 `MiniBalan/`，该目录仅本地保留、不随 Git 上传。新工程导入 364 个源码、头文件和授权文件，逐字节保留其编码及换行；其中真正参加编译的输入为 72 个 C 文件和 1 个 Arm 汇编启动文件。未把参考目录中的实验 `.cc`、`main_example.c` 或未入组的驱动加入构建。

| 编译输入类别 | 数量 |
|---|---:|
| Core（含 system） | 11 |
| USB_HOST | 2 |
| STM32 HAL | 24 |
| USB Host 中间件 | 8 |
| BSP | 20 |
| Hiwonder | 7 |
| 启动汇编 | 1 |
| 合计 | 73 |

源码清单、每个导入文件的 SHA-256、完整参考目录的树摘要见 `reference-import.json`。基线验收使用 `python tools/verify_project.py --baseline`，需要原工作区中的完整参考包；新克隆及后续正常开发时使用不带 `--baseline` 的检查，允许源码变化、新增文件及宏。

## 工程配置调整

1. 工程/Target/输出名称改为 `BalanceCar`，所有构建产物集中到 `Firmware/build/keil/`。
2. 保持 **Arm Compiler 6.16、C99、Cortex-M4、单精度硬件 FPU、标准 Arm C 库**，宏仍为 `USE_HAL_DRIVER,STM32F401xC`，12 条源码包含目录保持对应关系。
3. 原工程声明 `Keil.STM32F4xx_DFP.2.16.0`，新工程使用本机已安装的 **2.17.1**。移除 RTE CMSIS 组件依赖，使用随参考源码导入的本地 CMSIS 头文件。没有安装或修改全局工具链。
4. 使用显式 `MDK-ARM/STM32F401RB.sct`，将第二 Flash 区长度从原来的 `0x1C000` 修正为 **`0x18000`**，防止链接器允许代码超出 STM32F401RB 的 128 KiB Flash。原参考文件未修改。
5. Keil 器件描述中的晶振信息由 25 MHz 改为 **16 MHz**，与现有 `Core/Inc/stm32f4xx_hal_conf.h`、`main.c` 一致；硬件初始化代码未改动。现有 PLL 配置得到 84 MHz SYSCLK 和 48 MHz USB 时钟。
6. 没有复制原来的个人 `.uvoptx`、窗口布局、旧构建产物和 VS Code 桌面 GCC 配置。VS Code 的宏、包含路径及 ArmClang 参数由 `tools/sync_vscode.py` 从新 Keil 工程生成。

本阶段保留启动汇编中的 1 KiB 栈、512 B 堆，以及参考固件的电机控制、参数结构和初始化顺序；没有对算法、任务调度或驱动进行重构。

## 内存布局

| 区域 | 起始地址 | 长度 | 用途 |
|---|---|---:|---|
| Flash 首区 | `0x08000000` | 16 KiB | 向量表、代码及初始化数据 |
| Flash Sector 1 | `0x08004000` | 16 KiB | PID 参数，链接脚本留空 |
| Flash 第二区 | `0x08008000` | 96 KiB | 剩余代码/常量 |
| SRAM | `0x20000000` | 64 KiB | 数据、栈、堆 |

Flash 最后有效地址为 `0x0801FFFF`。参考代码 `BSP/persistent_storage.h` 定义参数地址，`persistent_storage.c` 实际擦写 Sector 1，因此此保留区是必须维持的行为约束。

输出为带地址信息的 HEX；没有生成覆盖中间空洞的合并 BIN。后续硬件下载还需按实际调试器配置检查擦除策略：HEX 中留空不等于下载器整片擦除时会保留参数。

## 本机验证结果

| 验证项 | 结果 |
|---|---|
| 原参考目录 | 1,742 个文件，树 SHA-256 与导入前一致 |
| 导入源码 | 364 个文件 SHA-256 全部一致 |
| 活动源码/宏/包含目录 | 与参考 `.uvprojx` 对应一致 |
| Keil 直接完整构建 | 0 错误，0 警告 |
| Windows PowerShell 5.1 构建脚本 | 环境检查、增量构建、完整重编译通过 |
| VS Code IntelliSense | 生成后 `--check` 通过，使用 Cortex-M4 配置 |
| HEX/MAP/AXF | 地址、HEX 校验和、初始向量和分区检查通过 |
| 验证器负向检查 | 损坏校验和、PID 区数据、错误 Flash 上限均被拒绝 |

最后完整构建日志：`Firmware/build/keil/rebuild-20260917-181151-28700d2f.log`。

本次构建：`Code=84140, RO-data=7492, RW-data=104, ZI-data=28032`。

| 实际占用 | 字节 |
|---|---:|
| Flash 首区 load size | 4,936 / 16,384 |
| Flash 第二区 load size | 86,800 / 98,304 |
| 总 Flash load data | 91,736 / 114,688（扣除参数区） |
| RAM（含原有栈/堆预留） | 28,136 / 65,536 |

初始 MSP 为 `0x20006DE8`，Reset 向量为 `0x08008049`，具有有效 Thumb 位并指向 HEX 内的代码。PID 区无 HEX 数据。

本次 HEX SHA-256：

```text
db0ecd419080b82dfaaa2640941e51b48cb7a4f2baee34ea1b91f6ff5c1dc2fe
```

复核命令：

```powershell
python tools/sync_vscode.py --check
python tools/verify_project.py --baseline --artifacts
```

## 当前路径兼容处理

工作区目录名称以不可见 Unicode 字符开头。本机 µVision 5.36 从该长路径运行时返回 15 且不产生日志；从 Windows 已有的 8.3 别名运行成功。构建和打开脚本通过 `GetShortPathNameW` 自动选择现有短路径，没有更改目录名称、创建盘符映射或修改系统设置。

若迁移到其他电脑/分区，没有可用的 ASCII 短路径，脚本会明确报错。将整个工作区复制到普通英文路径即可解决；仍需保留相对目录结构。推荐通过根目录 `Open-Keil.cmd` 或 VS Code 的 `Keil: Open IDE` 打开工程。

## 验证边界与回退

以上是初始工程迁移阶段的本地编译验证；该阶段未由助手连接、复位、烧录或运行小车。后续用户自行下载并反馈更换主板后启动异常消失，详见开发记录；这不代表完成平衡、IMU、电机、USB、蓝牙等功能的全面验收。参考固件本身的实时性、栈/堆余量和硬件映射问题也不属于此次工程迁移的验证范围。

新旧工程的业务源文件相同，但设备包/头文件依赖来源和链接区域上限已明确调整，不能把这一结果描述为与厂商历史 HEX 二进制完全相同。

原工作区保留 `MiniBalan/MDK-ARM/MiniBalan.uvprojx` 作为参考入口；新增 `Firmware/`、`tools/`、`docs/` 和根目录编辑器配置独立于参考工程。Git 仓库不包含原包，后续回看初始化源码使用 `v0.1.0` 标签。初始迁移阶段未建立 Git，随后按用户要求另行建仓并提交。
