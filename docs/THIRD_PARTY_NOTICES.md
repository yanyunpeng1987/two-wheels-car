# 第三方来源与授权文件

当前固件由用户提供的本地 MiniBalan 参考包导入。开发入口为原 `MDK-ARM/MiniBalan.uvprojx`，导入清单及 SHA-256 保存在 `reference-import.json`。

| 来源 | 仓库内位置 | 保留的声明 |
|---|---|---|
| 幻尔科技 MiniBalan 应用及板级代码 | `Firmware/Core`、`BSP`、`Hiwonder`、`USB_HOST` | 原文件中的作者、版权及许可说明 |
| ST STM32F4 HAL | `Firmware/Drivers/STM32F4xx_HAL_Driver` | 同目录 `LICENSE.txt` 和各文件头 |
| Arm CMSIS | `Firmware/Drivers/CMSIS` | 同目录 `LICENSE.txt` 和各文件头 |
| ST STM32F4 CMSIS Device | `Firmware/Drivers/CMSIS/Device/ST/STM32F4xx` | 同目录 `LICENSE.txt` 和各文件头 |
| ST USB Host 中间件 | `Firmware/Middlewares/ST/STM32_USB_Host_Library` | 同目录 `LICENSE.txt` 和各文件头 |
| QMI8658 常量定义 | `Firmware/BSP/QMI8658Constants.h` | 文件内 MIT 声明 |

本地 `MiniBalan/` 原始包及其中的 PDF 资料不上传，仍保存在原工作区。仓库已包含实际构建所需的代码和依赖，新克隆不依赖原始包。

参考包未发现覆盖整个工程的统一根 LICENSE；本次没有替第三方代码重新赋予统一许可证。2026-10-15 API核实仓库现为公开；可见性事实不等于已取得第三方材料的公开再发布授权。整包授权范围仍未确认，原版权和各组件许可继续保留，本次维护不重新声明统一开源许可证或更改仓库可见性。
