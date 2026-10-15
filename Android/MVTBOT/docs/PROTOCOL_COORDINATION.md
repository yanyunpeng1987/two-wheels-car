# APP 与设备端协同入口

本文件是源码导航与记录约定，不表示已验证当前运行设备的协议或已修复异常。

## 对照位置

| 环节 | 项目内位置 |
|---|---|
| MCU 蓝牙收发、命令解析、周期上报 | `Firmware/BSP/bluetooth.c` / `bluetooth.h` |
| MCU 上报入口 | `bluetooth_periodic_update()`、`send_response()`、`bluetooth_send_data()` |
| MCU 串口与 DMA 配置 | `Firmware/Core/Src/usart.c` 及相关中断代码 |
| MCU 控制、轮速与停机诊断 | `Firmware/Hiwonder/src/control.c`；以最新设备端开发记录为准 |
| APP GATT 通知接收 | `materials/analysis/jadx-reference/sources/com/Wonder/bot/BluetoothConnect/BLEService.java` |
| APP MiniBalan 消息转换 | 同目录 `BLEManager.java` 的 MiniBalan 分支，消息类型 103 |
| APP 主页面转发 | `materials/analysis/jadx-reference/sources/com/Wonder/bot/MainActivity.java` |
| APP 速度/距离解析 | `materials/analysis/jadx-reference/sources/com/Wonder/bot/fragment/MiniBalan/BalanceCarHomePageFragment.java` |
| APP 字节码核对 | `materials/analysis/original-base-apktool/smali/com/Wonder/bot/` |

以上 `Firmware/` 路径相对仓库根目录，`materials/` 路径相对本模块。APP Java 路径仅供阅读，原始堆栈行号应以 smali `.line` 标记交叉核对。

## 已观察的上报形式

```text
CMD|4|<数值1>|<数值2>|$       速度
CMD|5|<距离>|$              距离
CMD|7|<电压整数>|$          电压
```

一次 BLE 通知中可能包含多个 CMD 子帧。现有 MiniBalan APP 路径把每次通知独立转为字符串；不能假定通知边界就是完整业务帧边界，也不能把发送端的 20 字节限制直接当作实测接收 MTU。

当前固件 `bluetooth_periodic_update()` 的 CMD4 填参为 `local_velocity_right, local_velocity_left`。APP 的显示函数接收两个整数；后续涉及左右轮、单位或符号约定时，应根据实际接线、当前固件和源码共同核验，不能只从变量名推定。

本次历史实测路径是 BLE/GATT。更换 HC-05 等模块的兼容性应作为独立传输层需求评估，不把既有 BLE 安装成功视作 SPP 已实现。

## 每轮联调最少记录

- APP 包名、versionCode、APK SHA-256 和签名证书；设备端提交/工作区状态与实际烧录文件 SHA-256。
- 手机型号、Android/系统构建、目标蓝牙设备标识及当前使用的传输模式。
- 同一时间窗的设备端 UART 原始 HEX、可获得的 ATT/回调字节、APP 解析前日志和崩溃堆栈。
- 原始数据与解码展示分开保存；字节位置相对 UART 流、通知包还是 CMD 子帧，必须注明。
- 用户观察、源码推断、重放结果和实机验收分别记录。改变一个变量后再比较，不用静止零速样本替代运动条件验证。

新的原始证据建议放在 `materials/evidence/<日期>-<主题>/`，对应结论与状态写在可进入 Git 的文档中。不要覆盖 2026-09-16 的原始日志。

## LINK-001 当前代码入口

新 MiniBalan BLE 会话及收发见 `link/src/com/mvtbot/link/MiniBalanLink.java`；完整帧与字段校验见同目录 `FrameDecoder.java`、`ProtocolValidation.java`。v21只在其他机器人分支保持原通信路径。桥接点和原始smali哈希由 `changes/LINK-001/patch_smali.py`、`smali-baseline.json` 固定。MCU新增 `Firmware/BSP/bluetooth_link.c` portable解析/守卫/队列与实际HAL适配。契约及验收参见仓库 `docs/HC05D_COMPATIBILITY.md`。
