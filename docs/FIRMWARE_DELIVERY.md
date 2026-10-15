# BalanceCar 固件交付入口

统一输出目录：`C:\Users\Administrator\Desktop\CODEX_PROJECTS\BalanceCar-Deliveries`。后续日常取固件以该目录的 `CURRENT.json` 和本说明为准，不从临时编译目录选择同名文件。

首次整理（2026-10-14）推荐：**r5 / 新九字节手柄 / 当前这一只用户实测通过**。后续版本以交付目录顶部自动生成的当前入口和CURRENT.json为准。

在统一输出目录内打开：

`releases/BalanceCar_STM32F401RB_Gamepad_r5_20261014_89afb248_p1/`

其中的 **`BalanceCar_STM32F401RB_Gamepad_r5_20261014_89afb248.hex`** 是当前烧录入口，SHA-256 为 `89afb248572677cd21a493c526feb026b76bc89b9b60239f59b28bef26e17c6d`。根目录同名 `_p1.zip` 是便于复制/传输的完整交付包；解压后烧录里面的HEX，ZIP本身不用于烧录。

## 目录职责

| 内容 | 用途 |
|---|---|
| CURRENT.json | 唯一当前推荐版本、HEX路径/哈希、ZIP路径/哈希 |
| VERSIONS.md | 固件修改、兼容范围、历史验收与打包修改记录 |
| releases/版本目录/*.hex | 用户烧录文件；r5当前推荐，r2/r3仅对应旧型号历史版本 |
| releases/版本目录/debug/*.axf、*.map | 同一次构建的调试符号、地址与空间分布，供开发定位问题 |
| releases/版本目录/RELEASE_NOTES.md、manifest.json | 独立可读的变更、验收范围、原文件名与来源哈希、烧录边界 |
| releases/版本目录/SHA256SUMS.txt | 每个文件的内容校验，包括manifest |
| Verify-Release.ps1 | 只校验包文件，不连接、擦除或烧录设备 |

历史r2是旧八字节单手柄；r3是旧八字节双手柄。它们不是新九字节型号的已验证回退固件。r4及诊断版不作为用户交付；本轮不汇入Android APK或其他任务的产物，也不附带其他主板的PID/整区BIN备份。

## 命名和版本修改规则

固件文件：`产品_芯片_功能_rN_构建日期_HEX哈希前8位.hex`。本轮沿用已有r2/r3/r5，不虚构1.0版本；日期使用验收固件的原构建日期，不能使用复制日期冒充新构建。

包目录/ZIP另加 `_pN`：固件内容改变时必须增加r编号并重新构建、验证；仅修改说明/整理包时增加p编号，固件r编号和HEX哈希保持。已有版本目录和ZIP不可覆盖，发布工具遇到同名不同内容会拒绝。显示版本、Git标签、交付r编号是不同标识，必须在清单中说明，不能仅靠文件名判断内容。

后续版本必须记录：改动原因、兼容硬件/手柄、验收状态和范围、构建日期、完整SHA-256、原始文件名及来源清单哈希、代码是否未提交。确认新版本后才更新CURRENT.json；旧版本继续保留，未验收候选不得自动提升为当前推荐。

`firmware-catalog.json`中的`packageHistory`只追加、不改写已交付历史；工具会拒绝同一r编号对应不同HEX内容，或缺失p版本修改记录。单个历史包的定位是当时打包时的记录，当前推荐始终以根目录CURRENT.json为准。

## 格式选择与烧录要点

建议日常交付和烧录使用 **HEX**。Intel HEX记录包含数据地址和记录校验和，能明确表达本工程两段Flash，中间不放PID区的数据。[Keil HEX格式说明](https://www.keil.com/support/docs/1584/_hlp_hexfile.htm)

BIN适合明确要求该格式的Bootloader/OTA流程，但单独的裸BIN不携带目标地址；把稀疏镜像展平时还要明确如何处理空洞。因此本交付不生成容易误用的整区BIN。AXF/ELF主要保留给调试和构建追溯；STM32CubeProgrammer也支持它们，HEX不是唯一可用格式。[ST编程与擦除说明](https://dev.st.com/stm32cube-docs/prog/2.23.0/en/docs/markup/CubeProg_UserManual/Memory_programming_and_erasing.html)

本工程为STM32F401RB/128KiB，PID参数保存在 **Sector 1：0x08004000～0x08007FFF**。本次包中的HEX已逐条校验，不包含该区数据，但 **HEX文件本身不能阻止烧录工具执行全片擦除**。

烧录时核对主板、电机映射和版本；先备份当前板Flash/PID；只擦Sector **0、2、3、4**，保留Sector 1；按HEX内地址下载，校验程序和PID字节后再运行。不要把另一块主板的整区备份当通用固件。这里没有附带自动烧录脚本。

在统一输出目录运行只读校验：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Verify-Release.ps1
```

## 维护入口（开发人员）

仓库 `docs/releases/firmware-catalog.json` 固定要交付的版本、原产物位置、主HEX哈希及验收范围。`tools/package_firmware.py` 仅读取已归档成品，核对原manifest/SHA、逐行解析HEX和PID空洞，再生成独立包及推荐指针；不重新编译、不连接硬件、不移动或删除原产物。

```powershell
python tools/package_firmware.py
```

当前输出包括r5及历史r2/r3，均为原产物的逐字节副本。p1首次打包时来源源码存在未提交修改，包清单保留当时来源与源码哈希；该历史定位不随后续Git提交改写。2026-10-15已在独立整合分支纳入r5源码/测试及本规范，18个原manifest源码指纹匹配，Keil重建HEX SHA与上述交付一致，详情见[版本矩阵](RELEASE_MATRIX.md)。本次维护未重新打包或覆盖p1，不把源码提交等同于GitHub Release或新增设备验收。
