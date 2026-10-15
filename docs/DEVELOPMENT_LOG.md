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

仓库为 [yanyunpeng1987/two-wheel-balancing-vehicle](https://github.com/yanyunpeng1987/two-wheels-car)，可见性为私有，默认分支为 `main`，初始化源码标签为 `v0.1.0`。

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

代码提交 `bfba85f` 的 [GitHub Actions 回归](https://github.com/yanyunpeng1987/two-wheels-car/actions/runs/35223822254) 已通过。Ubuntu runner 使用 GCC 实际编译并执行 `gamepad_report_test.c` 与 `gamepad_usb_adapter_test.c`，覆盖实测中位样本、独立四轴、极值、Hat、按键转换、异常长度、legacy 解码和输入清理/重连。测试直接链接生产解析与适配代码；适配层只用最小 USB 数据结构桩，不将结果表述为已验证 STM32 USB 枚举、控制传输或物理按键。

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

GitHub 继续使用私有仓库 `yanyunpeng1987/two-wheel-balancing-vehicle`，origin、分支和 `v0.1.0` 标签保留。文档改动沿用 `codex/relax-gamepad-model-filter` 分支并关联现有 [PR #1](https://github.com/yanyunpeng1987/two-wheels-car/pull/1)；PR 保持草稿，未自动合并。Codex 项目列表仍显示旧目录入口，需要在应用内重新关联 `Two-wheels-Car`；本任务后续命令已显式使用新目录。

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

管理实现已提交为 `2eca87f` 并推送，创建[草稿 PR #2](https://github.com/yanyunpeng1987/two-wheels-car/pull/2)，base为现有功能分支。该提交的push和PR两次GitHub Actions均成功，PR检查见[运行35967583829](https://github.com/yanyunpeng1987/two-wheels-car/actions/runs/35967583829)。DEV-001管理基线完成，main及PR #1保持不变。

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

## 2026-09-24 — 最终T4用户验收与主程序分支整合

用户确认最新T4测试正常、符合预期，明确要求同步主程序分支并推送GitHub、保留修改记录；范围确认包含此前手柄和APP/固件管理基线。以已提交管理分支2a531d2建立独立codex/t4-firmware-stability工作区，按显式白名单导入31个固件/测试/文档文件，保留原共享目录及本地产物。补充验收说明与CHANGELOG，修正文档中已被后续证据替代的实验计划和不适用的旧固件回退指令。

独立工作区5个纯C模块和3个集成runner共3619项检查通过，77输入/Android管理基线/编辑器同步检查通过。Keil全量日志rebuild-20260924-161813-07cacba5.log，0错误0警告；重建HEX SHA-256仍为3986cf56d190009946df4338f6b5cdd13ce72849bf95478a187142cd695d1271，精确复现已在机测试的T4。测试证据位于本地整合工作区build/release-tests，原始日志与二进制产物不提交。本次源代码同步未连接、复位或再次烧录设备。


## 2026-09-24 — HC-05D BLE与APP界面合并实现

用户授权完整LINK-001/002/003计划及异常防闪退/失联保护，随后要求合并并行APP界面任务。新建独立Two-wheels-Car-hc05d工作区，冻结既有T4及v21为946112e，再从aa00024显式导入UI补丁；没有覆盖原共享目录。实现E0FF/FFE0、会话/完整帧/生命周期、DMA收发、100ms心跳与500msDWT守卫；恢复用双零和主动RX代次同步，跨端回放覆盖首次恢复及快速松手重按。

真实Keil调用图发现原1KB栈不足，增至4KB并加入构建检查。保留平衡算法、电机映射及T4修复。界面与LINK的共享MainActivity方法按固定顺序组合、最终APK重新解码核查所有7096主DEX类及资源/行为。旧构建中的非语义汇编重排由严格比较器及24个正负测试确认。

交付版本统一为APK24、配套新HEX和UI-only回退APK25；完整哈希、测试项、产物路径见HC05D_TEST_REPORT.md。本轮未安装组合APK、配置HC-05D、烧录或操作小车；历史NUL来源仍未确定，原始证据保持。

## 2026-09-28 — DEV-002：GitHub 仓库重命名

用户确认账号为 `yanyunpeng1987`，将私有仓库从 `yanyunpeng1987/two-wheel-balancing-vehicle` 重命名为 `yanyunpeng1987/two-wheels-car`。仓库 ID 仍为 `1374359972`，所有者、私有可见性与默认分支 `main` 保持一致；这次没有转移到其他账号。

重命名前后 Git 远程 refs 逐项一致：`main` 为 `d5ca3c91a2cdcc68e169a9ebf3314f9a7af7ef1f`，`v0.1.0` 标签对象为 `1b0a86ac7e012e9182240f09425f49a4ee7059f3`；本次文档提交会在该 main 基线上继续产生新提交。PR #1、#2、#3 在新地址仍为已合并状态，Source checks 工作流 ID `360476011` 保留；旧 Git 地址的 HEAD 查询返回相同提交。

本机 5 个已登记 worktree 共用一个 Git 配置，`origin` 已更新为 `https://github.com/yanyunpeng1987/two-wheels-car.git`。本地 `Two-wheels-Car` 目录、Keil 工程与 Android 包名保持原样。README 的仓库/克隆地址、需求台账及版本矩阵已更新；历史日志保留当时仓库名称，历史 PR/Actions 链接改为现地址。

改名前 Git 配置、refs 和工作区文件哈希保存在本地 `.git/codex-repository-migration/20260928-120719/`。需要恢复旧名称时，先在 GitHub 将同一仓库重命名回原名，再同步 origin；文档改动可单独 revert。不要在旧地址另建仓库，以免旧链接重定向失效。本次不包含固件或 APP 功能改动，也不进行设备操作。

## 2026-10-08 — 新0810:0001双手柄2.4G接收器，先交付单只适配固件

用户暂停旧20BC:5500适配，重新采购Twin USB Joystick并要求只适配一只；随后明确为2.4G，不涉及蓝牙。本任务从4f14c1b建立独立 `Two-wheels-Car-twin0810` / `codex/twin-usb-0810`，保留当前已部署HC-05D与APK40的固件基线，未覆盖其他工作区或修改Android/蓝牙。

PC只读描述符与输入取证确认0810:0001/bcd0206、接口0、202字节report descriptor、8字节带ID输入、共享IN0x81端点的ID1/2。用户操作锁定ID1。MODE短/长按仍未启用右模拟轴；用户明确接受左杆前后、右杆数字转向及左右面键同效。逐项标定页面采集12项稳定样本，右左Usage4/2、L1/L2/R1/R2与START/SELECT已对应真实按键。

实现严格单帧解码、ID2隔离且不续期、非Boot初始化、2ms端点周期及仅0810的500ms无有效ID1清零；数字左右合成-127/+127、对向抵消、原生右轴死区外优先，保持面键PID动作禁用。用户推住前进后关机再松杆，PC确认接收器回中，确认关机后约5秒继续1228帧ID1中位；未精确量化停车延迟。

decoder1652、adapter368、真实class/EventCallback534项共2554项检查通过；Keil ARMCLANG6.16编译0错误0警告，78个输入及HEX/MAP/栈静态检查通过，PID Sector1无HEX数据。最终HEX SHA-256为 `0b96c82a145256c5a1462d53a320b33e7700982d09b9d162b951979b20a5ebac`。

未检测到ST-Link，用户明确选择“先交付固件”；本轮不烧录、未做车上USB/控制验收。交付目录为 `build/twin-0810/delivery/`，含HEX/AXF/MAP/清单；原始采集和日志留在本地忽略目录。源码未提交或推送。完整范围与后续步骤见[交付记录](releases/2026-10-08-twin0810.md)。

## 2026-10-08 — SWD烧录、车端八字节格式修正及MCU输入验证

用户随后接入SWD明确要求完成烧录，并再次指示开始。唯一连接的STM32F401/128KiB、3.25V目标的芯片UID与9月记录不同，但PID外全部114688字节与已部署HC-05D镜像完全一致；当前PID有8字节调校差异且magic有效，本轮备份并原样保留当前PID。首次烧录0b96…ebac后完整Flash及运行后核验通过。

用户把PC上的同一个接收器移到小车，实际枚举为20BC:5500/bcd1003、端点8/报告描述符90，无Report ID，持续收到8字节报告；旧9字节分支因长度拒绝。初次根据VID将实物称为旧接收器不准确，复位重新枚举和完整描述符已确认车端实际格式，根本差异的模式来源仍未确定。用户四向操作显示右水平改变byte6，不能照搬描述符Z为右水平的推断。

r2新增精确匹配车端8字节格式的profile3，固定接口0，rx=byte6居中、ry=byte5反向；车端直接用轴值，面键不合成转向/PID，旧9字节与PC0810分支保持。decoder3656、adapter510、实际class1079共5245检查通过；Keil0错误0警告，78输入，HEX98832程序字节，PID区无数据。

13:59:48（Asia/Shanghai）重新下载r2，完整128KiB与新程序/原PID/擦除间隙逐字节相同才复位运行；两次运行后完整回读、FLASH_SR/PG/LOCK、CFSR/HFSR/DHCSR与编码器滤波保持均通过。最终HEX `9b1581cf099562ce481b29024b557bed32e06e507a723f07e3353b566d8f8f4b`，全Flash `975ca7b92340d8205bee8a7df0d40fb6b38a9321dd2b3fe335c1ac3db9384c74`，当前PID `b0eceb77277e63daccccc6725ff63e331b5c382b18b7e1d3f60e51e97e3836a4` 保持。

用户回复“最后复核完成”；RAM读取左前/后请求speed=±15、右左/右turn=±160，回中及推住前进后关机均0，累计28907有效报告且拒绝0。采样时flag_move=0、倾角约67～70°，不把请求数值当作实际轮子方向或落地行驶验收。接收器已留在车上，未操作手机APP或蓝牙。源码未提交/推送；完整证据和待物理复测范围见[现场记录](releases/2026-10-08-twin0810-flash.md)。

## 2026-10-08 — 双手柄扩展r3及实物控制权数据验证

用户反馈单只新手柄基本功能正常，要求同一对另一只也能使用，并选择“任意一只可用，当前手柄回中松键后可切换”。新增FW-HID-003，旧FW-HID-001保持暂停；先保留r2完整源码/产物快照，再扩展当前分支，不改手机/蓝牙、平衡/电机映射或PID。

车端接口0/1分别有独立IN81/82管道、缓冲、URB和toggle，HID descriptor/SET_IDLE请求按对应wIndex串行；PC0810按ID1/2分流。新增纯C仲裁模块并加入Keil/CI：两路先有中位才可新动作取得owner，当前中位松键释放并清arming，10ms零窗口，另一只持续held不排队、需重新中位/新动作；500ms只由该路有效包续期，非owner坏包/超时不清当前owner。

decoder3657、arbiter93562、adapter408、真实class1291共98918项断言通过；独立审查通过。实际ARMCLANG6.16完整编译0错误0警告，79活动输入、HEX101496字节，PID区无HEX数据，栈静态检查通过。

用户回复“已架空，SWD已连接”后，14:39:36（Asia/Shanghai）下载r3，之前完整备份Flash/PID并写入前复核；仅擦除0/2/3/4，完整校验后才复位。HEX `c0e1fbdc394f043bda1c42034922b75eeb4f69e6eb7f3e9cd6606440b41277a0`；两次运行后全Flash `593b57ff04be94b87b1e7749a247f3b78fb19ab983a142397fe7606b64799eff` 相同，PID `b0eceb77277e63daccccc6725ff63e331b5c382b18b7e1d3f60e51e97e3836a4` 原样。SR/PG/LOCK、CPU故障状态及TIM2/TIM3滤波通过。

14:54:36～14:56:38页面采得122份快照，用户回复页面验证完成。严格计时评分4/13如实保留，逐帧审查确认动作较晚出现而非输入与输出矛盾；两只独立控制、冲突不混合、B已held约17秒无接管、B实际回中后新推才接管、B按指引关机归零及A再取回均获有效证据。step8实际B仍右推，不能把其自动pass称为B已回中；原始SHA保持不变，独立审查另存semantic-review.json。

最终两路有效计数183106/183102、拒绝0/0、owner空闲、输出零，15:07第二次运行后完整回读通过。采样flag_move始终0，未替代实际车轮行驶、物理停车时延或镜像冲突专项。原始数据与备份留忽略目录build/twin-dual，源码未提交/推送；详见[双手柄记录](releases/2026-10-08-twin-dual.md)。


## 2026-10-13 — 新九字节手柄PC标定、r4烧录与持续报告故障

用户反馈此前适配手柄工作正常，随后接入5只新型号接收器。独立分支 `codex/gamepad-oct13` 继承r3软件，不覆盖原已验收工作区；本轮不修改APP、蓝牙、控制/PID参数。新增FW-HID-004，FW-HID-003只追加用户基本验收，保留原时序评分与未验证专项。

A～E均为20BC:5500/bcd1003、三接口；10份Game Pad原始描述符均123字节且SHA一致，IN81/82最大包64、周期10ms。Windows10字节含零ID占位，实际USB9字节。仅A/MI_01一只完成18项物理标定：右水平/垂直byte5/6，六个已知按键Usage7～12。B～E未做完整动作标定，E采集中曾断开，不能推断5只全部通过。

profile4严格采用bcd1003＋64/123选择条件，实际长度9，独立双接口接入r3互斥仲裁和每路500ms期限。旧profile1分支保留，但旧20BC九字节实体有相同元数据，也会满足profile4选择条件；旧实体仍未重新验收。原八字节/0810/legacy软件回归保留。

decoder5766、arbiter93562、adapter852、class3052合计103232项C断言通过；class依赖的URB是stub，不模拟真实USB控制器。Keil79输入、0错误0警告，HEX102044程序字节，PID Sector1无数据。r4 HEX SHA为 `1466c9a900e148f06b93818623698aac2ac66ea648ce0fadf823ed7f2b9c506d`，产物保存 `build/new-model/delivery/`。

用户明确要求烧录后，先只读核对目标：不是历史r3芯片，当前程序与已知r2一致，当前PID有效但与旧板不同。保留当前整区备份后仅擦Sector0/2/3/4；18:01:12完成烧录及完整回读，再复位运行。18:01:14运行后第1次完整回读通过，全Flash SHA `1ac5d5e36486ea781a60c5b9c796ee919ec3e4a6cf5eff1c15ac430379865a0f`；PID SHA `e9307006f7f27042226eae2b2c03bb24c76d6abd2b9ce2c85579c56844117170`保持，CPU/Flash/滤波检查通过。18:36:52第2次运行后完整回读和CPU/Flash/滤波检查同样通过，原r4源码与HEX未改。

用户移入新接收器、打开手柄并按指引左杆前推5秒再回中。车端正确枚举HOST_CLASS/profile4，两接口各接受一帧中位后，18:09:58至18:27:40计数一直为1/1、拒绝0/0，总超时2；动作没有新报告，发布请求零。故新型号持续接收未通过，根因待查；最后flag_move=1，不能沿用旧r3采样全程flag_move=0的条件。固件烧录完整性与功能验收分别记录，详见[设备记录](devices/20bc-5500-new9-20261013.md)和[r4阶段记录](releases/2026-10-13-new9-gamepad.md)。


## 2026-10-14 — IF2因果确认、r5辅助服务与独立输入失效保护

用户恢复设备条件后继续。昨日IF2诊断首轮停在类请求未发起辅助输入；今日先发现PortEnabled0而连接标志仍1、SOF Timer停16599。用户拔插后，同一诊断固件三路恢复约25Hz。在同连接/固件、中位和零请求下，仅临时暂停再恢复已核验的辅助服务状态，Game Pad计数随之停止再恢复；USB Timer暂停窗口仍增长。原r4、SET_IDLE0与IF2首轮停摆记录完整保留，同连接因果过程见[10月13日记录的追加证据](releases/2026-10-13-new9-gamepad.md#2026-10-14恢复与同连接因果验证)。

r5将精确匹配的IF2/IN83做正式持续服务，数据独立接收后丢弃，不触发Game Pad事件、owner或期限续期；STALL停服务直到重新枚举。保留SET_IDLE50、9字节实测映射和原互斥控制；旧八字节/0810/legacy的解码和正常行为有软件回归，不把它写成r5旧实物回归。

针对SOF停止时旧缓存可能继续被gamepad_scan消费的问题，USB主机过程前后新增DWT原始周期安全轮询：逐周期通道500ms有效报告期限独立计时，USB连接/端口/类状态及真实custom Gamepad身份失效清发布和两路arming；恢复需新中位再操作。保留profile与诊断计数，legacy/profile1没有新增周期要求；control.c、平衡/PID、蓝牙和HAL驱动均未改。现场停摆时为中位，未发生已观察的非零残留，源码反例与硬件事实分开记录。

主机解析器5766、仲裁93562、适配1002、class4886共105216项，实际GetDeviceType/USB入口/回调/gamepad_scan抽取集成267项，共105483断言通过；CI已接入新增集成入口。Keil79输入、0错误0警告，HEX103624程序字节，Flash10912/16384＋92712/98304，RAM36424/65536，PID Sector1无HEX数据。

正式候选HEX `89afb248572677cd21a493c526feb026b76bc89b9b60239f59b28bef26e17c6d`，产物 `build/new-model/gamepad-r5/`。08:50:42烧录、完整Flash校验后启动，08:50:43首次运行后全区仍为 `12e332be9fd11fe676dadd6fbffd717a24c8312710d7f2c62103abb884797307`；当前板PID SHA `e9307006f7f27042226eae2b2c03bb24c76d6abd2b9ce2c85579c56844117170`保持，CPU/Flash检查通过。

r5初始三路报告计数增长后，08:57:59～09:03:13完成单只12项车端输入验证；510份快照与原始RAM/SHA独立核对通过，四向±15/±160及关机中位报文归零有证据。用户重新开机回中，档位恢复3/2。09:06:35第二次运行后完整Flash/PID/CPU/滤波复核通过，最终47038帧、拒绝0、超时0；全程flagMove0，不将输入台架通过当作物理行驶或停车验收。未覆盖的剩余样本、另一只手柄、旧型号r5实物回归、长时射频与精确物理停车时延仍分项保留，完整证据与边界见[r5记录](releases/2026-10-14-gamepad-r5.md)。


## 2026-10-14 — r5用户实测确认

用户在正式r5交付后反馈“我实测也是OK了”。FW-HID-004据此记为当前单只新型号手柄用户实测通过；保留台架12项及两次Flash/PID回读证据，未扩大到其余4只或未说明的长时/超距、精确停车时延。仅更新验收记录，没有重新编译、烧录或操作硬件。


## 2026-10-14 — 统一最终输出与发布规则（FW-REL-001）

用户要求直接使用最终输出、统一收集并管理命名与版本修改记录。以r5为当前推荐，将r2/r3历史成品一并汇总至项目旁BalanceCar-Deliveries；只复制经原manifest及SHA核验的HEX/AXF/MAP，不复制整区BIN、PID原始值、UID或诊断固件。增加catalog、不可覆盖的r/p包目录、版本记录、只读校验脚本和当前r5 ZIP，HEX逐条验证地址、校验和及PID空洞，未重新编译或操作硬件。


## 2026-10-15 — DEV-003：开发资料维护与当前源码整合

远程API核实仓库为 `yanyunpeng1987/two-wheels-car`、public/private=false、默认分支main。本次没有更改可见性；9月28日改名时私有的历史记录保留。PR #1～#4已合并，本次整合前main为 `ddeeb64d3facfd969ced8f21dced4bcff98ee8b3`；远程HC-05D/APP v40来源为 `4f14c1b91b8d6c7b5be89ffb4f98483ee4cc03a9`，核对时无开放PR/Issue或GitHub Release。

在独立 `codex/development-sync-20261015` 分支整合已提交HC-05D/APP资料及10月14日r5工作区源码/测试/交付规范，保留已合并的T4和DEV-002改名历史。更新README、协作/贡献规则、文档入口、需求、版本矩阵、已知问题及HC-05D入口，修正v21输入与v40当前版本混淆、SPD已部署仍写待下载、T4门控/电压历史验收缺失及旧DEX构建说明。仓库公开不代表整包第三方再发布授权已确认，继续保留组件许可与授权未决事实。

离线核对r5交付CURRENT.json、VERSIONS.md、HEX与ZIP，主HEX SHA为 `89afb248572677cd21a493c526feb026b76bc89b9b60239f59b28bef26e17c6d`，p1 ZIP SHA为 `ca1413330af265635cc3b0c59db2915c852cb8d554d262400a57d2c952fc9eed`；原归档HEX/AXF/MAP均匹配，18个源码/测试指纹0差异，本次完整Firmware共378文件与r5工作区逐字节一致。未重新打包或覆盖p1；来源指纹保留当时历史，不用当前提交号冒充原打包时状态。

独立整合Keil全量重建日志 `Firmware/build/keil/rebuild-20261015-144606-118e6d6a.log`：79输入，ARMCLANG6.16，0错误0警告；HEX103624字节，Flash10912/16384＋92712/98304，RAM36424/65536，PID Sector1无数据，SHA与已验收r5精确一致。栈4096字节、已知调用链加异常帧1972字节；verify_project、verify_android和sync_vscode检查通过。主机回归21/21组通过、0失败，包含r5的105483项断言、JVM协议1854、BLE446、健康618、扫描97、权限58、跨端重新使能/启动订阅、DEX合成与UI补丁检查；汇总为 `build/maintenance-20261015-tests/summary.json`。本轮整合worktree没有原始APK输入，可选v21 smali/组合回放及真实DEX快照比较未执行，未重建APK。交付目录 `Verify-Release.ps1` 对r5/p1只读校验通过。远程CI由本次PR检查记录，不以本地成功提前声明远端通过。

本次未连接、安装、复位或烧录设备。APP最新已安装记录为v40，其9月28日基本验证对应当时HC-05D HEX；后续r5为当前单只手柄用户基本实测通过。其余样本、另一只手柄、旧实体r5复验、长时/超距、精确停车时延及APK40＋r5完整跨端/物理失联与运动验收不因本次源码整合自动补齐；右编码器异常根因继续未确认。具体提交及PR以本次Git历史为准。
