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

## 2026-09-24 — ST-Link Utility 路径兼容问题与目录改名准备

用户确认：将此前的 HEX 原样复制到桌面纯英文目录后，STM32 ST-Link Utility 可以正常打开并烧录。因此该现象与原路径兼容性有关，不能归因于 HEX 损坏或地址越界。原目录名称的第一个字符为不可见的 `U+200C`（ZERO WIDTH NON-JOINER）；其来源无法追溯，复制粘贴是可能来源之一。没有单独复现 ST-Link Utility 内部的字符编码转换，不把具体实现机制写成已确认事实。

用户要求同级改名为 `Two-wheels-Car`。检查确认 Keil、VS Code、启动入口与构建脚本均使用相对路径或基于自身位置定位，无需修改工程配置。Windows 当前拒绝目录改名；只读检查发现当前 Codex 会话持有旧目录的监视/辅助进程句柄，目录及父目录 ACL 正常，新名称没有冲突。未强制退出 Codex，也未创建第二套工程。

**当时状态：目录尚未改名；完成结果见本日下方的“目录改名完成与关联同步”。** 已在项目目录外的桌面 `BalanceCar_Firmware` 文件夹准备 `Rename-Two-wheels-Car.cmd` 与同名 PowerShell 脚本。正常退出 Codex 后运行 CMD 即可再次尝试改名；脚本拒绝覆盖或合并已有目标目录，改名后检查 HEX、main.c、Keil 工程文件哈希一致，再执行 IntelliSense、工程产物和 Keil 环境检查。成功标志为新目录中的 `build/path-rename-completed.json`。重新打开项目时使用新路径；Codex 保存的旧项目路径也需重新选择。

本次脚本语法和 CheckOnly 预检通过，74 个活动输入、HEX/AXF/MAP 校验、VS Code 配置及 Arm Compiler 6.16 / DFP 2.17.1 环境检查均通过。改动文档前，396 个已跟踪文件与改名前快照逐项哈希一致，保留了用户 main.c 原有修改。HEX SHA-256 仍为 `8b60ffad57d7ac2e68ae3fcb543f9aed9a67cfa50e835739cb31e7fbe5f5ea03`。没有重编译、烧录或操作目标硬件；用户确认烧录成功也不代表新旧手柄功能已经验收。

## 2026-09-24 — 修复目录改名脚本的工作目录占用与错误记录

用户执行第一版改名脚本后仍失败，返回的是脚本统一替换后的提示，因此不能据此确定当时的 Windows 原始错误。进一步在 Windows PowerShell 5.1 临时目录复现：从待改名目录启动，仅 `Set-Location` 到父目录会保留进程原生当前目录，导致 `0x80070020` 共享占用；同时设置 `[Environment]::CurrentDirectory` 后改名成功。第一版仅做 CheckOnly 预检，未覆盖这一真实改名路径，验证不足。

已更新桌面 `BalanceCar_Firmware` 内同名 PS1/CMD：CMD 先 `cd /d "%~dp0"`，PS1 同时切换逻辑目录和原生工作目录；保留原始错误文字/HResult，保存脚本外部 transcript，失败时只读列出工程的打开句柄；哈希改用明确释放文件流的 .NET SHA-256，并显式创建完成报告目录。本次另观察到资源管理器打开了工程 `Firmware/build/keil` 目录，以及当前 Codex 的目录监视句柄，故真实改名前也需关闭指向工程的资源管理器窗口。打开句柄列表不等于每个句柄均阻止改名，不能笼统把所有列出的进程认定为根因。

修复版完成独立端到端验证：分别从临时工程旧目录内部启动 CMD 和直接启动 PowerShell，两条路径均真正完成改名且退出码为 0；396 个已跟踪文件及 HEX/AXF/MAP 逐文件 SHA-256 一致；VS Code 配置、74 个活动输入、固件产物和 Keil 环境检查通过；完成报告和 transcript 均生成。临时验证仅替换脚本的父目录常量，未操作真实工程或硬件。

该次排查结束时，真实项目仍位于原目录，尚未完成改名；需退出占用它的开发会话后运行桌面更新后的同名 CMD。新的失败记录为桌面 `BalanceCar_Firmware/Rename-Two-wheels-Car-*.log`，不再仅依赖统一错误提示。main.c 与 HEX 内容未修改，HEX SHA-256 保持 `8b60ffad57d7ac2e68ae3fcb543f9aed9a67cfa50e835739cb31e7fbe5f5ea03`。

## 2026-09-24 — 依据真实失败日志解除 Explorer 子目录占用

用户再次执行修复版失败。实际 `Rename-Two-wheels-Car-20260924-114156-471.log` 显示：根目录 DELETE 访问探针通过，项目句柄仅来自 explorer.exe PID 10588，两个句柄均指向 `Firmware/build/keil`；该次没有 Codex 或 PowerShell 的项目句柄。因此不能继续把这次失败归因于已修复的脚本工作目录问题。

检查时资源管理器显示的是桌面 `BalanceCar_Firmware`，但左侧快速访问仍有 `keil` 条目。通过正常关闭该资源管理器窗口后，再次枚举确认两个 Explorer 句柄消失。重新打开资源管理器运行桌面脚本存在再次引入工程引用的风险；本次改用独立一次性助手，在当前 Codex 正常退出后运行已经验证的改名脚本，避免用户再次打开资源管理器启动脚本。

一次性助手位于桌面 `BalanceCar_Firmware/Finish-Two-wheels-Car.ps1`，等待最长 15 分钟，退出后最多尝试 12 次；不结束用户应用、不创建计划任务、不开机自启。通过独立进程启动的 CheckOnly 实际通过，VS Code、74 个活动输入、HEX/AXF/MAP 和 Keil 环境检查成功。状态记录为同文件夹 `Finish-Two-wheels-Car-rename.json`；当时仍等待退出后执行；后续已取得新目录 `build/path-rename-completed.json`，完成结果见下节。

## 2026-09-24 — 目录改名完成与关联同步

用户确认改名成功。独立助手状态为 success，真实项目完成报告时间为 **11:58:08**，目标为 `Two-wheels-Car`，旧目录已不存在；助手进程已退出。改名过程保留 HEX、main.c 与 Keil 工程文件内容一致。本次未新增固件修改，main.c 原有用户尾空格改动保留在工作区，不纳入文档提交。

已检查 Git、Keil、VS Code、启动脚本和 GitHub Actions：活动配置均无旧工作区绝对路径硬编码，使用相对路径或按脚本位置解析；无需重建仓库或修改源码。README 的克隆命令显式使用 `Two-wheels-Car` 作为本地目录，更新基线路径说明、CHANGELOG，并关闭 KI-003。上方“尚未改名”记录描述的是当时状态，不能作为当前状态使用。

GitHub 继续使用私有仓库 `yanyunpeng1987/two-wheel-balancing-vehicle`，origin、分支和 `v0.1.0` 标签保留。文档改动沿用 `codex/relax-gamepad-model-filter` 分支并关联现有 [PR #1](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle/pull/1)；PR 保持草稿，未自动合并。Codex 项目列表仍显示旧目录入口，需要在应用内重新关联 `Two-wheels-Car`；本任务后续命令已显式使用新目录。

新目录实际执行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Rebuild`，日志 `Firmware/build/keil/rebuild-20260924-120923-c0a8af3a.log`，结果 **0 Error(s), 0 Warning(s)**。Code 84,748、RO 7,492、RW 104、ZI 28,048 字节；`python tools/verify_project.py --artifacts` 和 `python tools/sync_vscode.py --check` 通过。HEX 加载数据 92,348 字节、RAM 28,152 字节，PID 参数区无 HEX 数据。重新构建后的 HEX SHA-256 仍为 `8b60ffad57d7ac2e68ae3fcb543f9aed9a67cfa50e835739cb31e7fbe5f5ea03`，与迁移前完全一致。本次没有烧录或操作目标硬件。

## 2026-09-24 — 新采购电机的两组驱动极性适配

用户反馈新采购电机在相同线序下与参考样机转向相反，分别交换电机的两根驱动线后可以正常工作。本次依据该反馈，在软件中固定反转两个电机的驱动输出；编码器相序和计数极性保持原样。

### 修改范围

- `Firmware/Core/Inc/main.h`：M2_F/M2_B 改为 PB7/PB6，M1_F/M1_B 改为 PB9/PB8，GPIO 端口仍为 GPIOB。
- `Firmware/Hiwonder/src/control.c`：`set_pwm()` 内交换 TIM4 CCR1/CCR2 和 CCR3/CCR4。左轮正输入写 CCR2、负输入写 CCR1；右轮正输入写 CCR3、负输入写 CCR4，幅值保持原样。
- 仅交换 GPIO 宏不会改变实际方向，因为初始化将四个引脚按位 OR 后统一配置为 AF2，实际输出由固定 TIM4 通道决定，因此同步修改寄存器赋值。
- 保留函数接口、左右电机归属、编码器、PID、PWM 频率和停止逻辑。两份固件文件保持 UTF-8/CRLF，`main.c` 原有用户改动及其文件内容未变。

### 本地验证

实际执行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Rebuild`，使用 Arm Compiler 6.16 / Keil.STM32F4xx_DFP 2.17.1，结果 **0 Error(s), 0 Warning(s)**。日志为 `Firmware/build/keil/rebuild-20260924-130055-4e16748c.log`；Code 84,760、RO 7,496、RW 104、ZI 28,048 字节。

`python tools/verify_project.py --artifacts` 和 `python tools/sync_vscode.py --check` 均通过：74 个活动编译输入，HEX 加载数据 92,364 字节，RAM 28,152 字节，Flash/RAM 地址范围合规，PID Sector 1 无 HEX 数据。新 HEX SHA-256 为 `dfc2f1d54774eebe70131879682c9c5160c50c115b1a42d0530fef5a3023298f`。

从修改前后源码提取 `set_pwm()`，仅将成员访问与函数声明适配为 C# 等价语法，在主机内存寄存器桩中验证 16,051 个输入样例：分别遍历每侧 -4000..4000，覆盖 7×7 个正负、零及边界混合输入，每例额外执行停止检查；另通过 5 次连续正负切换/停止转换。新输出逐项等于原输出的两组通道互换，幅值不变，停止时四个 CCR 为零。该检查是等价语法寄存器逻辑验证，不是原生 C 主机测试或实车验证；实际 C 编译由上述 Keil 完整重编译验证。临时报告位于忽略目录 `build/motor-polarity-verification/result.txt`。

初次 `git diff --check` 将固件保留的 CRLF 误报为行尾空白；使用仅对该次命令生效的 `core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol` 后，本次改动的空白检查通过，未修改 Git 全局/仓库配置。另以字节比对确认 `control.c` 除上述通道互换及注释外没有变化，编码器与控制算法保持原样。

### 使用与回退

上述源码修改及本地验证阶段未烧录或操作目标硬件；随后经用户授权完成本地下载，记录见下一节。用户随后确认本项修改测试正常、通过，验收记录见下文。新固件配合未经人工交换的电机线序使用；若此前已交换驱动线，应恢复原线序，避免硬件与软件两次反转。TIM4 使用 PWM2，四个 CCR 为零表示保留原停止状态，不表示四个物理引脚均为低电平。

如需回退，应成对恢复 `main.h` 中两组管脚宏和 `set_pwm()` 的 CCR1/CCR2、CCR3/CCR4 映射，再重新编译；不涉及参数区修改。

## 2026-09-24 — 电机极性适配固件本地下载与回读验证

用户明确要求直接本地下载后自行验证。STM32CubeProgrammer v2.7.0 经 ST-Link/SWD 识别设备 ID `0x423`、STM32F401xB/C、128 KiB Flash，目标电压 3.25 V。先以软件复位方式连接并暂停内核，完整读取 131,072 字节 Flash 作为下载前备份。

下载使用前节已验证 HEX 的固定副本，SHA-256 保持 `dfc2f1d54774eebe70131879682c9c5160c50c115b1a42d0530fef5a3023298f`。显式擦除 Sector 0、2、3、4，使用 `--skipErase` 禁止下载阶段追加自动擦除；未擦除 Sector 1，未修改 Option Bytes。CLI 报告 `Download verified successfully`。

随后完整读取 128 KiB Flash，在主机端再次逐字节比对：HEX 的全部 92,364 字节与板上内容一致；PID Sector 1 的全部 16,384 字节与下载前备份一致。最后执行软件复位并运行内核，CLI 确认 `Software reset is performed`、`Core is running`。

备份、下载固定副本、回读、日志和 manifest 均保存在忽略目录 `build/motor-flash-20260924-131511/`。上述下载过程验证了固件内容与参数保留；用户随后完成本项修改的实车测试并确认通过，见下一节。

## 2026-09-24 — 两个电机方向互换适配通过用户实测

用户在下载上述固件后反馈：“此项修改测试正常，通过”。据此将本次两个电机驱动方向互换适配标记为**用户实测通过**，本项验证待办关闭。

验收对应本次已下载的 HEX，SHA-256 为 `dfc2f1d54774eebe70131879682c9c5160c50c115b1a42d0530fef5a3023298f`。该结论依据用户反馈，范围为本项电机方向适配。本次仅补充验收记录，未再次修改固件或操作硬件。

## 后续记录模板

复制以下条目并按日期追加；只填写实际完成和实际观察到的内容。

### YYYY-MM-DD — 版本或提交标识：简短主题

- 目标与范围：要解决的问题、涉及模块。
- 改动：关键行为变化，以及保留的兼容约束。
- 本地验证：命令、工具版本、结果和产物标识；失败也应记录。
- 实机证据：主板/固件标识、接线、复现步骤、实测结果；未测试则明确注明。
- 未完成事项：不确定结论、已知问题编号、下一步验证。
- 回退：可恢复的提交/标签，以及参数或数据兼容性要求。

## 2026-09-24 — 意外退出平衡：T1 加速度确认与停机取证

用户反馈平地自稳和运动时均可能突然倒下，Flag_move 回到0，Mode保持，通常无短鸣；要求按怀疑点逐一修改测试。首轮只改变加速度拿起条件：保持1.7g门限，连续有效超限至少20ms；失败读、低样本、相邻间隔>10ms、停机和重新使能清确认。使用DWT无符号周期差，不使用现有HAL_GetTick。20ms为试验值，尚未实车确认。

新增两个便携C模块pickup_accel_guard/control_stop_trace并加入实际Keil项目，当前76个活动输入。输出门控首次1→0时冻结64次以内的采样和ACC/SPD/KEY/ANG原因，自动放下重启不覆盖首因；正常明确KEY0→1开始新记录。独立审查发现同一轮pick_up、KEY重开、ANG停机的组合会因无条件reset漏记，已增加control_stop_pending==NONE条件并纳入实际函数提取测试。屏幕显示T1，停机后显示冻结的原因、倾角、Z、编码器读取间隔、轮速、电压。没有在控制ISR打印或写Flash。

本轮不改变原速度门限/整数截断/200Hz换算、±80°门控、PID、IMU低通、PC13及按键时基、自动放下行为。与本轮基线逐函数比对确认set_pwm、balance、velocity、turn_off、get_velocity_form_encoder、put_down、gamepad_scan完全一致；此前用户已验收的电机极性保留。main.c原有用户空格未动。

本地原生C验证采用仅解包在build/test-tools的Zig 0.15.2，未修改PATH或Keil工具链。直接编译生产C模块：加速度114项、停机记录199项；从当前control.c提取真实pick_up/myabs/turn_off/key_scan并链接生产模块的组合测试78项，总计391项，0失败。测试涵盖持续时间、尖峰、无效样本、间隔、DWT回绕、速度与角度边界、按键重启及首因冻结。测试不执行真实HAL/中断/电机。CI已添加对应检查，本次未提交推送，未宣称远程CI通过。

最终实际Keil全量编译日志：Firmware/build/keil/rebuild-20260924-141441-f902313d.log，Arm Compiler6.16，0错误0警告；Code87248、RO7604、RW104、ZI31464字节。项目/HEX/MAP/编辑器同步检查通过，HEX94960字节，RAM31568/65536，PID Sector1无HEX数据。HEX SHA-256为d65ebf3c41b42d2bd6d7e3ed93ada5892d482359cfa8eace3436e2a69ee15a1f。

固定T1产物见build/stop-test-stage1/release/；源码与产物哈希及测试结果见manifest.json。本轮之前的源码、HEX/AXF/MAP保存在build/stop-test-stage1/baseline/，回退HEX哈希dfc2f1d54774eebe70131879682c9c5160c50c115b1a42d0530fef5a3023298f。详细实车顺序与屏幕解释见STOP_DIAGNOSTIC_TEST.md。

截至本条记录，未连接或下载目标设备，T1实车效果待验证。T2速度周期与T3按键输入配置须按第一轮现场结果继续逐项推进，不认定根因已修复。

## 2026-09-24 — T1 最终缓冲调整、下载恢复与逐字节验收

用户确认设备已准备好并要求继续下载。下载前核查到原启动文件栈只有0x400（1KiB）；将本轮新增的中断采样临时结构和主循环显示快照改为静态RAM，减少新增栈占用。未顺带修改原栈配置；原有主循环/中断嵌套的总栈余量仍需单独验证，不将此次局部调整称为整体栈安全证明。

最后Keil全量构建日志为Firmware/build/keil/rebuild-20260924-141858-fd58bee3.log，0错误0警告；Code87188、RO7600、RW104、ZI31568。最终HEX94896字节，RAM31672/65536，PID Sector1无HEX数据；固定HEX SHA-256为6f9f22d6511258292c4abf56c3c9fc88e22a820765b1c2ea7afc860e56924900，取代上一节下载前初版。78项真实控制函数提取测试对最终control.c再次执行通过；此前114+199项生产模块检查保持通过，总计391项。测试runner已补充通过PATH解析编译器名，并以--cc zig实际复测，支持CI中的--cc gcc。

CubeProgrammer2.7.0识别同一ST-Link目标为0x423、STM32F401xB/C、128KiB、3.25V。先暂停并新备份完整128KiBFlash和16KiB PID。首次HOTPLUG组合擦除/下载中，CLI报告扇区擦除失败，却继续写入并在0x08000001报校验不匹配；未把这次输出当成功。保存失败读回和日志，确认PID保持一致。

随后显式软件复位/暂停，以NORMAL连接独立执行Sector0、2、3、4擦除；确认成功后才使用--skipErase写入固定T1HEX并校验，CLI报告Download verified successfully。再完整读取128KiBFlash，主机逐字节确认全部94896固件字节匹配，PID全部16384字节与本轮下载前备份一致。失败与恢复的直接原因尚未作寄存器级定位，不把HOTPLUG或DMA因素认定为根因。

最后执行软件复位与Core run。此版CLI仅打印Core run而非Core is running，原脚本字符串断言因此失败；没有据此反复复位，而是通过HOTPLUG只读DHCSR验证0x01010000，S_HALT和S_LOCKUP均为0，确认内核已运行。该调试状态不等于平衡功能验收。下载工具在build中的初版脚本保留为历史证据，失败后不再用其组合program动作；最终阶段和结果以device/state.json为准。

最终固件、基线备份、完整设备读回、失败及恢复日志与manifest均在build/stop-test-stage1/。T1已下载运行，等待用户先静止自稳、后运动的实测反馈及必要的Stop/Z/dt/轮速记录；尚未进入T2速度修正或T3按键配置改动，也未声称突然停机问题已修复。

## 2026-09-24 — MVTBOT Android 资料并入统一项目

按用户要求，将“提取 Wonderbot APK”任务的 Android 开发资料汇总到 Android/MVTBOT，使用已改名的 Two-wheels-Car 实际目录；Codex 保存的项目入口仍指向旧目录，未通过修改应用内部存储处理该入口。

保留原始 4 个拆分包与备份 ZIP、v20/v21 APK及安装证据、当前 v21 Apktool 工程、v20 重建历史、9156 个 smali 与 4113 个 Java 参考文件、三次闪退原始 .raw 和统计、环境记录、合并工具及历史脚本。逐文件来源/大小/SHA-256 见 Android/MVTBOT/materials/import-manifest.json。大型安装器、可重建缓存和已验证重复 split-input 按清单去重，原任务目录未删除。

当前 APP 仍为 versionCode21、2.3.6-mvtbot.2，既有图标版 DEX/业务逻辑未改变。新建相对路径构建脚本，只做本地重建、对齐、既有开发证书签名和载荷校验，不自动安装。私钥和 DPAPI 密文继续保留在当前 Windows 用户目录，没有复制进项目。归档/派生工程/产物由模块 .gitignore 排除，便于遵守现有源码提交约定。

新增项目入口、交接、APP-001 状态和设备端/APP 对照位置，保留 JADX 103 项反编译错误及“非完整源码工程”的边界。完整本地验证结果见 Android/MVTBOT/docs/CONSOLIDATION.md。本次不修改 Firmware，不触碰其他任务的未提交代码，不提交或推送 Git，也不执行手机更新或设备下载。

## 2026-09-24 — SWD停机现场与T2启动修复

用户连接SWD再次复现后，只读捕获64帧和Flash，未暂停/复位。右路0→89计数触发SPD（240.17cm/s），dt4.505ms，angle−2.671°，az0.99988g；此前PWM−86..316，周期4.501857–4.508381ms，没有超长周期或大输出先行证据。现场数据在build/stop-test-stage2/capture-20260924-143711/。

同时发现Flash四个中断向量与发布T1不同；独立复读物理和0地址别名一致，PG=1/LOCK=1、PGPERR=1。已精确追到ADC无DMA句柄却Start_DMA的三个NULL回调写，与受损向量字的AND结果全部吻合；0x18变零高度符合过早IMU中断下NULL GPIO BSRR写入。详见STARTUP_FLASH_CORRUPTION.md，未把它直接认定为编码器尖峰根因。

因此T2优先修启动完整性：清PG再锁定并检查结果、删除无人使用的ADC DMA启动、GPIO阶段禁用EXTI2、检查QMI初始化、全部资源/PID完成后清编码器及pending并补偿DRDY高电平再使能控制。屏幕标记T2，运行时control.c与T1逐字节相同，PID/SPD/编码器/按键/电机映射保持。已有Android/MVTBOT并行改动未触碰。

新增25项集成检查、246项真实C启动函数测试通过。Keil完整编译日志rebuild-20260924-144657-af4714b8.log：0错误0警告，Code86850/RO7606/RW104/ZI31568。项目/编辑器/HEX检查通过：94560个HEX字节，RAM31672，PID Sector1无数据。固定T2 HEX哈希4f2c44df38a244f14891f12579c461861c1e2d007c3c266803f09163fa3b5ae3。源码、产物、T1基线与manifest见build/stop-test-stage2/。截至本条，T2尚未下载；需固定设备后下载，且在启动后再次核对完整Flash及PG状态，再继续SPD实测。

## 2026-09-24 — APK 与固件持续迭代管理基线

新增跨端需求台账、协议索引、版本与联调矩阵、根 AGENTS、分支/提交规范和 GitHub 模板。MVTBOT 文档、构建脚本、自有图标及只含相对路径/大小/哈希的 v21 输入清单进入管理；派生项目、原包、APK、原始设备证据和签名材料按既有政策本地保管。后续 APP 业务改动必须有明确版本化补丁或正式源码模块；当前脚本仍仅支持保留原 DEX 的打包。

本次 GitHub 核对：私有仓库、main=499b420、现有功能分支=82f3b45、PR #1 为草稿且既有 Actions 成功。使用基于82f3b45的独立 worktree/分支 codex/apk-firmware-management，不提交另一任务正在调试的电机极性/T1/T2代码、测试和CI变更。主工作区76输入与本分支74输入分别记录，没有把本地固件状态描述成已进入GitHub。

只读复核 Android 导入17,711文件/332,529,270字节全部匹配；可提交project基线1,445文件/42,437,889字节匹配。默认Android管理检查、固件源码引用/Flash布局检查、VS Code同步检查通过。现有v21签名重建是前一任务的历史证据，本次没有重新打包、安装、连接手机或烧录。当前发布与验收边界见RELEASE_MATRIX.md。

回退可revert本次管理提交；原始材料与设备固件未被重排或删除。GitHub源码并不包含恢复APK所需全部本地输入，备份须另外保留。

管理实现已提交为 `2eca87f` 并推送，创建[草稿 PR #2](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle/pull/2)，base为现有功能分支。该提交的push和PR两次GitHub Actions均成功，PR检查见[运行35967583829](https://github.com/yanyunpeng1987/two-wheel-balancing-vehicle/actions/runs/35967583829)。DEV-001管理基线完成，main及PR #1保持不变。

管理文件同步回日常主工作区时保留原分支与未提交内容；同步前后383个Firmware/tests文件哈希一致，原T2日志与停机测试CI步骤保留。主目录Android实际输入、76输入工程及VS Code同步检查通过。此次同步的文件版本已在管理分支，后续提交须按需求选路径或整合分支，不能整体暂存共享工作区。

## 2026-09-24 — FW-START-001：T2下载与运行后完整性验证

用户明确要求下载T2。对当前目标先新备份128KiB Flash与PID，分步软件复位/暂停、仅擦Sector0/2/3/4、--skipErase写入固定HEX和校验，再完整回读。94560固件字节全部匹配，PID16384字节逐字节不变。复位运行后两次HOTPLUG回读的完整Flash均等于写入后内容，四个向量均正确；FLASH_SR=0、FLASH_CR=0x80000000(PG0/LOCK1)、DHCSR=0x01010000。原始证据build/stop-test-stage2/device/，manifest已记录下载状态。

第二次RAM记录已冻结，并非持续采样快照：SPD仍发生，右计数从+1跳到129，左0，右计算速度348.11cm/s，dt4.503ms，angle−2.509°，Z1.000244g，前一帧两侧PWM−86。用户明确反馈平地原地自稳下很快复现两次，当前倒地未再操作。T2证明启动Flash损坏在本次运行检查中消除，但FW-STOP-001没有解决。

## 2026-09-24 — FW-STOP-001：T3仅加强右编码器数字滤波

继续只读抓取最新记录和TIM2/TIM3/GPIOA。当前冻结现场与T2 repeat相同，不把同一份记录重复算成两次独立波形；用户报告两次复现是另行观察证据。TIM2/TIM3均SMCR=3、PSC0、ARRFFFF、CCMR1=A1A1；GPIOA的PA1/PA5为AF1输入上拉，映射符合实际右轮。T2全部HEX数据仍匹配，SR0/PG0。证据build/stop-test-stage3/capture-20260924-150416/。

未发现其它代码写TIM2计数或复用右编码器引脚；ST ES0222未列出此种编码器CNT读取/清零大跳变限制。这不排除硬件问题或未列出机制。只延后SPD会把异常原始计数先送入速度PID，因此本轮选择输入端数字滤波实验，不增加软件停机延时。

T3仅在MX_TIM2_Init将右轮IC1Filter/IC2Filter从10改15，TIM3保持10；屏幕改T3。84MHz/CKD1下滤波确认时间估计从约0.76–0.95us增至2.67–3.05us，可滤部分短毛刺，不承诺过滤超过3us的干扰。control.c、encoder.c与T2逐字节相同；SPD门限、PID、轮速公式、读后清零及电机输出不变。T2到T3 HEX加载内容仅2个字节不同：0x08000C22常量0A→0F（编译器复用两路滤波值），0x0801693E显示字符2→3。

Keil完整编译rebuild-20260924-150635-a60aae46.log：0错误0警告；Code86850/RO7606/RW104/ZI31568，94560个HEX数据字节、RAM31672，PID区无HEX数据。源码范围、76输入、编辑器、地址与HEX检查通过。固件SHA-256为018c43adabf2ee42e532ee31856769ad2667908c416bf779d8cb5656401f71ad。单纯配置变更以编译、二进制差异和实机寄存器读回验证，没有编造新的行为测试数。

沿用户逐项修改测试的授权推进；用户明确现场倒地未做其他操作，下载前只读确认实时flag_move=0。新备份后分步下载T3、完整回读、复位运行；94560字节匹配、PID16384字节保持。启动后Flash再次完整、PG0/LOCK1、IMU采样有效；实际TIM2 CCMR1=F1F1，TIM3=A1A1，证明确实只加强右路滤波。日志、读回及T2回退基线保存在build/stop-test-stage3/。T3已在机，等待同条件平地自稳实测；尚不能宣布编码器故障或整车问题已修复。

## 2026-09-24 — FW-STOP-001：T3自稳约5分钟未复现（用户实测）

用户反馈：“以前的版本，30秒之内都会复现问题。刚才的T3版本，目前自稳状态下测试了大约5分钟都是正常的。”对应已下载T3 HEX：018c43adabf2ee42e532ee31856769ad2667908c416bf779d8cb5656401f71ad。

记录为T3静止自稳首轮测试通过、相对旧版明显改善；本轮时长由用户估计，并非工具连续计时。T3只加强右TIM2输入滤波，PID/SPD门限未变，结合前序右计数大跳变证据，结果进一步支持右编码器输入瞬态毛刺/干扰方向。但尚不能区分编码器本体、线束、接插件、供电/地回路等来源，也不能把单次5分钟正常当作长期可靠性或完整修复证明。

FW-STOP-001转为待验证，KI-004保持未关闭。保持当前T3，后续继续较长时间自稳（建议累计至少30分钟），再验证前进/后退/左右转弯和受控拿起停机，观察误停、漏计或响应变化。若再现，保留供电与SWD现场再读取。本次仅更新用户验收记录、需求/版本/问题状态与本地manifest，没有改动固件、重新构建、连接或复位设备。

## 2026-09-24 — FW-VOLT-001：T4电压滤波与低压提示

用户再次确认T3持续自稳正常，要求优化电压滤波后一起测试。此前蜂鸣诊断只读快照位于build/buzzer-diagnostic/20260924-152559：当前11.83V、flag1、Normal；last beep1000Hz/50ms/2次、alarm_timer非零。说明低压分支曾触发，尚未区分测量毛刺、真实短暂下跌与同音型按键事件。

以T3为基线新增battery_monitor纯C模块，77个实际Keil输入。电池采样移至main，与CCD串行；启动前预采一笔原始电压。每约100ms单次ADC，EOC用DWT周期差有界等待1ms，已完成转换优先于超时；所有HAL错误返回无效并停止/恢复CH10。控制IRQ不再访问ADC1。中值3点预热后低于9V连续1s确认、>=9.5V连续1s恢复，异常/gap>350ms/过期>500ms中断待确认但保留已确认低压状态，避免陈旧值产生新蜂鸣。沿用<=5V排除区间并记range_error，不当作正常恢复。新低压周期清alarm_sent，避免超过一轮DWT回绕后被旧alarm时间延迟。

LCD显示滤波电压和LOW，未就绪显示--；SPD/蓝牙/停机快照继续使用最新成功原始voltage（改volatile单字发布）。control.c中balance/velocity/turn/set_pwm/pick_up/put_down/编码器换算/按键/get_angle/滚轮切模式等函数逐函数与T3相同，TIM2/3滤波及GPIO启动门控逐字节相同；本轮没有修改PID、SPD门限、通信帧或编码器计数逻辑。保留16笔诊断样本、raw低值/错误计数及报警快照，详情VOLTAGE_FILTER_TEST.md。

生产滤波模块1125项C检查、实际ADC函数提取1304项C检查+6项集成检查通过；现有控制78项C、启动246项C+25项集成回归也通过（共2753项C、31项集成，0失败）。最终Keil全量日志rebuild-20260924-155403-bee4856b.log：0错误0警告，Code87140/RO7548/RW104/ZI31920，HEX94792字节，RAM32024/65536，PID区无数据。HEX SHA-256为3986cf56d190009946df4338f6b5cdd13ce72849bf95478a187142cd695d1271。

T3基线备份、固定T4产物、源哈希和结果见build/voltage-filter-stage4/。截至本条T4尚未下载，仍需停止自稳并固定车辆后安排下载、运行后完整性/参数/采样状态核验和实车回归。T3用户稳定自稳结果保留，不将本地滤波测试当成电源噪声根因或整车验收证明。

## 2026-09-24 — FW-VOLT-001：T4下载与运行后电压采样验证

用户明确“已停稳并固定，继续下载T4”。先通过指定ST-Link新备份当前128KiB Flash与PID，再独立执行软件复位/暂停、Sector0/2/3/4擦除、--skipErase下载与校验、完整回读。全部94792固件字节匹配；PID16384字节与本次下载前备份相同。复位运行后完整Flash再次与写入后逐字节相同，PG0/LOCK1/SR0，右TIM2 CCMR1=F1F1、左TIM3=A1A1，T3输入滤波保持。

读取新的battery_monitor实机结构（0x20000604、364B）并由独立代理离线核对布局：100笔采样，raw/median均1178cV，ready1/stale0/low_active0；ADC错误、范围错误、低原始值和报警计数均0，启动以来有效raw范围1116–1183cV。最近16笔采样均有效且就绪，实际间隔131.927–133.269ms；100ms是主循环最小调度周期，实际随主循环耗时延后，仍明显小于350ms间断门限。未以“没有声音”代替采样健康检查。

同次控制快照未冻结，IMU有效、flag_move=1；这仅是捕获时状态，不代替长期自稳验收。记录和原始读回位于build/voltage-filter-stage4/device/，manifest已更新。T4已在机，等待用户继续验证自稳稳定性与偶发双短鸣是否改善；本次未做真实持续低电压供电实验或CCD实车回归。

## 2026-09-24 — HC-05D BLE与APP界面合并实现

用户授权完整LINK-001/002/003计划及异常防闪退/失联保护，随后要求合并并行APP界面任务。新建独立Two-wheels-Car-hc05d工作区，冻结既有T4及v21为946112e，再从aa00024显式导入UI补丁；没有覆盖原共享目录。实现E0FF/FFE0、会话/完整帧/生命周期、DMA收发、100ms心跳与500msDWT守卫；恢复用双零和主动RX代次同步，跨端回放覆盖首次恢复及快速松手重按。

真实Keil调用图发现原1KB栈不足，增至4KB并加入构建检查。保留平衡算法、电机映射及T4修复。界面与LINK的共享MainActivity方法按固定顺序组合、最终APK重新解码核查所有7096主DEX类及资源/行为。旧构建中的非语义汇编重排由严格比较器及24个正负测试确认。

交付版本统一为APK24、配套新HEX和UI-only回退APK25；完整哈希、测试项、产物路径见HC05D_TEST_REPORT.md。本轮未安装组合APK、配置HC-05D、烧录或操作小车；历史NUL来源仍未确定，原始证据保持。
