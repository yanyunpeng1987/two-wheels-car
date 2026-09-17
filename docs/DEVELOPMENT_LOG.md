# 开发记录

本文件按日期记录已完成的改动、验证证据和未完成事项。构建通过、用户实机反馈、完整功能验收分别记录，不能相互替代。

## 2026-09-17 — v0.1.0 初始化源码基线

`v0.1.0` 用作初始化源码基线标签，不改变固件内部版本号或通信协议中的版本字段。标签和提交的实际状态以 Git 仓库记录为准。

### 工程建立

- 以幻尔科技 MiniBalan 的实际 Keil 活动工程为依据，建立 `Firmware/MDK-ARM/BalanceCar.uvprojx`，目标名为 `BalanceCar`，芯片为 STM32F401RBTx。
- Keil 与 VS Code 共用 **Arm Compiler 6.16** 和同一份 `.uvprojx`。VS Code 通过 PowerShell 脚本调用 Keil 构建，不维护第二套 GCC 编译参数。产物统一写入 `Firmware/build/keil/`。
- 导入 364 个源码、头文件和授权文件，保留原始字节；活动输入为 72 个 C 文件和 1 个 Arm 汇编启动文件。参考目录 `MiniBalan/` 保持独立，不作为新工程的源码路径。
- 新工程使用本机已有的 `Keil.STM32F4xx_DFP.2.17.1`，保留随参考代码导入的本地 HAL/CMSIS/USB 依赖；不修改控制算法、驱动和初始化顺序。
- 新增显式链接脚本 `Firmware/MDK-ARM/STM32F401RB.sct`。保留 `0x08004000..0x08007FFF` 的 PID 参数区，将第二代码区长度由 `0x1C000` 修正为 `0x18000`，使链接上限符合 128 KiB Flash。**原参考工程的上限配置过大，但检查到的旧 HEX/MAP 实际代码没有越界**，不能把该修正解释为已发现旧固件越界故障。
- 增加构建脚本、工程/产物验证器、VS Code 配置和开发文档。详细清单及 SHA-256 见 [基线记录](BASELINE.md) 和 [导入清单](reference-import.json)。

### 本地验证

完整重编译为 **0 Error(s), 0 Warning(s)**。`python tools/verify_project.py --baseline --artifacts` 验证通过：364 个导入文件一致，原参考目录 1,742 个文件的树摘要不变；HEX 校验和、复位向量、Flash/RAM 上限及 PID 保留区检查通过。

本次验证产物包含 91,736 字节 Flash 加载数据，RAM 使用 28,136 字节（含原有栈/堆预留）。详细工具版本、区域大小和产物摘要以 [BASELINE.md](BASELINE.md) 为准。这些结果证明本地工程能够构建及满足所检查的布局约束，不代表整车功能验收。

### 用户实机反馈与当前决定

用户使用 ST-Link 下载新 `BalanceCar` 工程，连接为四线，**未接 NRST**。其中一块主板下载后屏幕及指示灯无反应，需要断电重新上电才能恢复，按 RST 未能恢复。随后用户更换新主板，反馈该现象消失。

用户决定将原主板的疑似个体硬件问题留待后续排查。当前记录的是两块主板表现不同；没有通过现场 PC、故障寄存器、复位波形或电源测量确定根因。没有因此修改启动代码、复位设置或控制逻辑，也没有完成全面的平衡、传感器、电机及通信功能验收。后续跟踪见 [已知问题](KNOWN_ISSUES.md)。

### GitHub 初始化提交

仓库为 [yanyunpeng1987/two-wheel-balancing-vehicle](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle)，可见性为私有，默认分支为 `main`，初始化源码标签为 `v0.1.0`。

提交范围为新工程源码、Keil/VS Code 配置、构建和验证工具、导入清单及开发文档。完整 `MiniBalan/` 参考包、编译产物、个人 `.uvoptx`、窗口布局及调试器本机配置保留在本地，不随 Git 上传。`.gitattributes` 对 `Firmware/` 禁止自动换行转换，避免改变 GBK/CRLF 文件字节。

建仓前再次使用当前工程完整重编译：**0 Error(s), 0 Warning(s)**；日志为 `Firmware/build/keil/rebuild-20260917-195304-99aa1f87.log`。导入基线、HEX/MAP/AXF 和 VS Code 配置同步检查通过，HEX SHA-256 与初始构建记录一致。源码检查工作流只验证工程引用、内存布局和工具/JSON 语法，不执行 Keil 编译或连接硬件。

初始化版本只建立源码与文档基线，未为暂缓排查的问题修改固件。后续采用功能分支和 Pull Request，阶段标签不覆盖已有版本。

## 2026-09-17 — 取消 USB 手柄型号白名单

开发分支：`codex/relax-gamepad-model-filter`。本次放宽接收器的型号准入，供现有报文格式兼容的其他手柄接入。

本节记录仅取消白名单阶段的结果。随后取得 `20BC:5500` 实机证据，确认它需要专用报文配置；最终方案见下一节及 [当前手柄兼容说明](GAMEPAD_COMPATIBILITY.md)。

### 改动与兼容边界

- 在接口初始化和设备类型识别两处取消 `VID=0x2563`、`PID=0x0526/0x0575` 白名单。手柄入口仍要求 HID class、subclass `0`、protocol `0`。
- 保留原先的 32 字节接收/解码缓冲、字节偏移 `1..7` 的摇杆、方向帽和按键映射；有第 21 字节时继续执行值为 `0x02` 的既有状态过滤。该状态字节的厂商语义尚未确认。
- 接收时拒绝实际长度不足 8 字节或超过本次请求长度的报文。合法短包的剩余缓冲尾部清零，避免沿用上一包数据；解码入口同步检查长度边界。
- 没有增加通用 HID report descriptor 解析器，不承诺 XInput、复合接口或不同输入报文布局自动兼容。没有修改平衡控制和按键动作映射。

### 验证状态

本次实际验证：Arm Compiler 6.16 完整重编译为 **0 Error(s), 0 Warning(s)**，日志为 `Firmware/build/keil/rebuild-20260917-200724-1028436d.log`。`python tools/verify_project.py --artifacts` 和 `python tools/sync_vscode.py --check` 通过；Flash 加载数据 91,780 字节，RAM 使用 28,136 字节，PID 参数区没有 HEX 数据。

HEX SHA-256：`c6a3934c9f61b64600a1c88581a6dc3faf38d5109f7162129b73287873c38fb2`。实际修改的固件源文件仅 `usbh_hiwonder_hid.c` 和 `usbh_hid_gamepad.c`，保持 UTF-8/CRLF；导入清单继续作为 v0.1.0 原始基线，不因后续修改而重写。

独立代码检查确认丢弃无效报告后仍能由 SOF 调度下一次接收，固定 FIFO 帧长保持一致，键鼠接收分支未改动。本机未执行 USB 行为仿真，以上不构成新旧手柄功能测试。本次尚未烧录，原手柄回归及新手柄实测均未完成。

用户复测按“原手柄 → 新手柄”顺序进行，分别使用与该手柄配对的接收器，检查中位、四向和按键。具体条件及无反应时需要采集的证据见 [手柄兼容说明](GAMEPAD_COMPATIBILITY.md)。若新手柄仍无反应，先取得接收器 VID/PID、接口及报告描述符、原始输入报文，再判断传输和布局差异，不继续盲改型号 ID。

## 2026-09-17 — 20BC:5500 接口 0 专用适配

沿用开发分支 `codex/relax-gamepad-model-filter`，用于更新现有 Pull Request；本节不表示已经合并或发布固件。

### 现场只读取证

用户将接收器插入 Windows PC，并授权只读检查。实际设备为 `VID=0x20BC`、`PID=0x5500`、`bcdDevice=0x1003`。USB 配置描述符包含三个接口；接口 `0` 和 `1` 均为 HID Game Pad，输入端点分别为 `0x81/0x82`，最大包长 64 字节，周期 10 ms。接口 `2` 为系统/用户控制集合，本次没有读取其输入。

Windows HidP 能力和纯内存逐 bit 解析确认：两个 Game Pad 报告均无 Report ID，Windows 输入缓冲为 10 字节，其中首字节零是 API 占位；实际 USB 数据为 9 字节。已读样本为 `00 00 0F 80 80 80 80 00 00`，按描述符解释为四轴 128、Hat 15、无按键。原固定偏移会把该样本误解为 `lx=-128`、`ly=112`、`buttons=0x8000`，因此单纯取消白名单不能完成适配。

精简证据见 [设备记录](devices/20bc-5500.md) 和 [结构化证据](devices/20bc-5500-evidence.json)。证据不包含个人设备路径；没有向接收器发送输出/Feature 报告，没有采集键盘输入。尚未逐个移动摇杆和按键，因此不能从这个样本确认物理布局或无线失联行为。

### 实现范围

- 按 VID/PID 选择 `20BC:5500` 专用配置，明确固定 USB 接口号 `0`，使用该接口的中断 IN 接收；跳过该非 Boot 配置的 `SET_PROTOCOL` 和启动 `GET_REPORT`。
- 使用 64 字节接收缓冲匹配端点最大包长，按实际传输长度解码；专用报告要求恰为 9 字节，不再把手柄数据按端点长度写入固定帧 FIFO。
- 新配置解码字节 `3..6` 的 X/Y/Z/Rz、字节 `0..1` 的小端 15 位按键和字节 `2` 低四位的 Hat。新配置方向位为 UP=`1`、RIGHT=`2`、DOWN=`4`、LEFT=`8`、中位=`0`，斜向合并相邻方向；legacy Hat 保持原算法，没有据此确认两者方向位语义相同。新 Y 轴按 `128 - raw_y` 换算并夹到 `[-127, 127]`，中位为 `0`。
- legacy 保留原先 8–32 字节格式、轴/按键映射及实际存在第 21 字节时的 `0x02` 过滤。
- 在 `gamepad_report.c` 集中维护按键映射。未取得物理按键编号前，只暂映射 Usage `5..12` 为 L1/R1/L2/R2/Select/Start/L3/R3；Usage `1..4/13..15` 不映射，避免错误触发原有 PID/校准动作。
- 无效包、USB 断开及重连清理手柄信息，并补充控制层空指针防护。没有据此承诺识别接收器在线时的无线失联。
- 专用配置使用端点声明的 10 ms 周期（最低 1 ms），legacy 仍为最低 50 ms。读取时保留最新有效报告；没有增加报文超时清零，避免把仅在状态变化时发包的设备误判为失联。
- 增加 `gamepad_raw_buttons` 和接收成功/拒绝计数，便于逐键标定及确认解析路径；重新连接时计数清零。

### 验证状态

本次 USB 集成首次使用 Arm Compiler 6.16 完整重编译为 **0 Error(s), 0 Warning(s)**，日志为 `Firmware/build/keil/rebuild-20260917-204902-01b164aa.log`。构建报告：Code 84,748、RO 7,492、RW 104、ZI 28,048 字节。默认工程验证通过，活动输入增至 74 个；`python tools/sync_vscode.py --check` 通过。

本次 `python tools/verify_project.py --artifacts` 通过：HEX 加载数据 92,348 字节，RAM 使用 28,152 字节；两个 Flash 加载区分别为 5,256 / 16,384 字节和 87,092 / 98,304 字节，PID 参数保留区没有 HEX 数据。初始 MSP 为 `0x20006DF8`，复位向量为 `0x08000261`。

HEX SHA-256：`8b60ffad57d7ac2e68ae3fcb543f9aed9a67cfa50e835739cb31e7fbe5f5ea03`。

代码提交 `bfba85f` 的 [GitHub Actions 回归](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle/actions/runs/35223822254) 已通过。Ubuntu runner 使用 GCC 实际编译并执行 `gamepad_report_test.c` 与 `gamepad_usb_adapter_test.c`，覆盖实测中位样本、独立四轴、极值、Hat、按键转换、异常长度、legacy 解码和输入清理/重连。测试直接链接生产解析与适配代码；适配层只用最小 USB 数据结构桩，不将结果表述为已验证 STM32 USB 枚举、控制传输或物理按键。

本次没有烧录，原手柄回归、新手柄在 STM32 接口 `0` 的控制验证、逐键标定和整车验收均未完成。复测步骤见 [手柄兼容说明](GAMEPAD_COMPATIBILITY.md)。

## 后续记录模板

复制以下条目并按日期追加；只填写实际完成和实际观察到的内容。

### YYYY-MM-DD — 版本或提交标识：简短主题

- 目标与范围：要解决的问题、涉及模块。
- 改动：关键行为变化，以及保留的兼容约束。
- 本地验证：命令、工具版本、结果和产物标识；失败也应记录。
- 实机证据：主板/固件标识、接线、复现步骤、实测结果；未测试则明确注明。
- 未完成事项：不确定结论、已知问题编号、下一步验证。
- 回退：可恢复的提交/标签，以及参数或数据兼容性要求。
