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

## 后续记录模板

复制以下条目并按日期追加；只填写实际完成和实际观察到的内容。

### YYYY-MM-DD — 版本或提交标识：简短主题

- 目标与范围：要解决的问题、涉及模块。
- 改动：关键行为变化，以及保留的兼容约束。
- 本地验证：命令、工具版本、结果和产物标识；失败也应记录。
- 实机证据：主板/固件标识、接线、复现步骤、实测结果；未测试则明确注明。
- 未完成事项：不确定结论、已知问题编号、下一步验证。
- 回退：可恢复的提交/标签，以及参数或数据兼容性要求。

## 2026-09-24 — APK 与固件持续迭代管理基线

新增跨端需求台账、协议索引、版本与联调矩阵、根 AGENTS、分支/提交规范和 GitHub 模板。MVTBOT 文档、构建脚本、自有图标及只含相对路径/大小/哈希的 v21 输入清单进入管理；派生项目、原包、APK、原始设备证据和签名材料按既有政策本地保管。后续 APP 业务改动必须有明确版本化补丁或正式源码模块；当前脚本仍仅支持保留原 DEX 的打包。

本次 GitHub 核对：私有仓库、main=499b420、现有功能分支=82f3b45、PR #1 为草稿且既有 Actions 成功。使用基于82f3b45的独立 worktree/分支 codex/apk-firmware-management，不提交另一任务正在调试的电机极性/T1/T2代码、测试和CI变更。主工作区76输入与本分支74输入分别记录，没有把本地固件状态描述成已进入GitHub。

只读复核 Android 导入17,711文件/332,529,270字节全部匹配；可提交project基线1,445文件/42,437,889字节匹配。默认Android管理检查、固件源码引用/Flash布局检查、VS Code同步检查通过。现有v21签名重建是前一任务的历史证据，本次没有重新打包、安装、连接手机或烧录。当前发布与验收边界见RELEASE_MATRIX.md。

回退可revert本次管理提交；原始材料与设备固件未被重排或删除。GitHub源码并不包含恢复APK所需全部本地输入，备份须另外保留。
