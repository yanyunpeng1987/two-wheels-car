# 新型号手柄适配暂停交接（2026-10-13）

> 2026-10-14续接已完成：r5已烧录，单只手柄12项车端输入台架检查和两次运行后Flash/PID复核通过。以下为昨天暂停时的历史状态；最新结论见[正式r5记录](2026-10-14-gamepad-r5.md)。

用户要求今天暂停，明天继续。已停止硬件操作；没有安排自动烧录或定时调试。下一轮先确认设备上电/车轮离地/SWD和接收器连接状态，读取当前证据再决定动作。不要在用户离场后操作设备。

## 当前状态

- 有效工作区：`Two-wheels-Car-gamepad-oct13`，分支 `codex/gamepad-oct13`。默认环境中含不可见字符的旧路径已失效，命令必须指定当前工作区。代码和文档未提交/推送。
- 已有旧型号r3用户反馈工作正常，原工作区 `Two-wheels-Car-twin0810` 和既有产物保留。新型号尚未适配通过，FW-HID-004仍开发中。
- 新设备5个描述符匹配，只有A/MI_01对应手柄完成18项物理标定。USB有效报告9字节、EP64、描述符123；右X/Y为byte5/6，六个控制按键Usage7～12。细节见[设备记录](../devices/20bc-5500-new9-20261013.md)。
- r4与SET_IDLE0诊断版车端均只有两帧初始中位报告；用户四向操作后计数不增长。已确认PC同型号接收器全部拔除（枚举数量0），手柄重启和再次操作仍未恢复。
- 当前车上是第二实验 `diagnostic-if2-drain`，不是已验收控制固件。恢复r4的SET_IDLE50，仅增加严格匹配IF2/IN83/64/10/描述符101的独立丢弃管道；系统输入不进入控制、事件或500ms期限，无IF2控制请求。
- 此版已完成整区Flash/PID校验和一次运行后复核，但最后快照停在HOST_CLASS_REQUEST（6）、GET_REPORT_DESC（2），profile0、accepted0、IF2 active1但submits0。第三端点尚未开始接收，**不能据此判断IF2排空假设成立或失败**。下一步先定位为何报告描述符请求未完成。

## 当前产物及证据

- HEX：`build/new-model/diagnostic-if2-drain/BalanceCar-diagnostic-if2-drain.hex`
- HEX SHA-256：`d58efba9a531d3bc8a10c8838b3f56d4b176481176c750142134e1b6def00f73`
- 部署：`build/new-model/deployment/20261013-184922-diagnostic-if2-drain/`
- 实验记录入口：[r4及后续诊断](2026-10-13-new9-gamepad.md)。原r4和idle0源码/产物分别冻结在 `build/new-model/delivery/source/` 与 `build/new-model/diagnostic-idle0/source/`。
- 本版主机104933项断言通过（5766+93562+852+4753），Keil0错误0警告，79活动输入，HEX102824字节。Flash10512/16384＋92312/98304；RAM36408/65536。主机stub不模拟实际STM32总线。
- 当前板PID SHA-256始终为 `e9307006f7f27042226eae2b2c03bb24c76d6abd2b9ce2c85579c56844117170`，保留Sector1；当前板不同于旧r3记录的板，烧录前原程序实际为r2。不要套用旧板的PID/UID。
- 所有探针/芯片身份仅在本地state文件。烧录脚本备份、核对当前身份及完整Flash，只擦Sector0/2/3/4，完整回读通过后才复位。

## 继续时的定位入口

- `build/new-model/deployment/observe.py` 只读HOTPLUG RAM，按当前MAP读取两路HID/HCD及IF2统计；`observe.py 3 --compact`只缩短屏幕输出，完整JSON仍保存。
- `read_usb_regs.py`读取有据可查的USB寄存器，不读取会弹出FIFO的GRXSTSP（0x50000020）。
- `deploy.py`、`prepare_revision.py`为本轮本地部署辅助，不应在恢复聊天时自动运行烧录。`latest-path.txt`当前指向IF2实验。
- ARM真实结构：HID52字节；host的pActiveClass偏移1148、Timer1220；HCD的hc偏移20，每项60，urb_state56、state57、xfer_count36、toggle_in40。IF2实验wrapper236字节，drain位于116，buffer168，active232。MAP所有全局地址随构建更新。
- IF2实现/主机测试的独立复核已完成，未发现阻止受控实验的问题；当前额外管道分配成功，最后未进入报告输入阶段，需先查EP0状态。不要为了“修好”盲目调整数据toggle或放宽报告期限。
- 上报恢复后仍须验证持续推杆、四向及回中、实际关机归零；未操作其他4只和旧型号实物回归应分别保留边界。手机/蓝牙不属于本轮适配。

今天所有采集进程已结束，子代理已完成。用户已被提示关闭小车与手柄电源；是否已关机未经工具确认。
