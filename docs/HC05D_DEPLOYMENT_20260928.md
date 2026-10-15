# HC-05D部署与手机权限回归记录（2026-09-28）

关联LINK-001/002/003/005、APP-REL-002、FW-STOP-001。用户明确授权直接烧录MCU并安装APK。本记录依据本地 `build/hc05d-v38-deployment/20260928-134138/state.json`、完整回读和手机日志，记录已发生的操作；此前[构建记录](HC05D_RECONNECT_FIX_20260928.md)中的“待安装/下载”是部署前状态。

**当前结论：MCU下载、完整Flash/PID验证已完成；v38手动扫描实测暴露的权限入口问题已在v39修复，v39已覆盖安装并回拉核验，启动无自动扫描、已授权手动扫描通过。用户选择本车后反馈连接及功能正常，日志确认业务握手和遥测持续更新。长时断连与受控运动验收仍待完成；MCU保持本次固件，不重复烧录。**

本页不记录设备完整UID、探针/手机序列号或蓝牙地址。完整原始证据保留在本地目录，不随本页提交。

## 实际安装与下载产物

| 对象 | 标识 / SHA-256 | 已发生结果 |
|---|---|---|
| 手机APK | v38 / `2.3.6-mvtbot.11-hc05d`，r2；`87ac15c758715f7a00bf786c25f21174dbc2a61618a973225cca4692daec174e` | 13:42:19安装完成；包信息versionCode=38，设备拉回APK与交付哈希相同 |
| MCU HEX | `720cf9af4f06efe5c86e1c8bee18648a2013d28697d1ca8613e5c97ceb088c51` | 13:42:35下载完成；98168个HEX数据字节，擦除扇区仅0/2/3/4 |
| 下载前完整128KiB Flash | `bdd6e57a63f46098b30b49aecc6593d5f9c28b0bfed109ccf35157c02caf6ac5` | 已保存作为本次回退基准 |
| 原PID Sector1，16384字节 | `633fe3a4d7835bbcd26e4e06d843d77b3adbabdae773d90e64526cbec3245757` | 下载前备份、写入前复查、烧录后及两次运行后回读逐字节一致 |
| 烧录后完整128KiB Flash | `bb5a0a410471b40d8608b24710257ef1ecddb9cf7876ad4ffcbcf8438c01bdff` | 与新HEX数据、原PID Sector1及其余擦除区组成的完整预期镜像一致；两次运行后完整回读保持同一哈希 |
| 对应MAP | `8f277b69c88ae5ae3d31a7b8731535e4fd4a4d209b426fc1c89917d88db890fa` | 运行采样使用此新MAP，不沿用旧固件变量地址 |

MCU已部署的变化包括蓝牙查询与运动epoch解耦，以及用户要求的SPD门限180/170→360/340cm/s临时调整。原始速度计算、速度环输入和PID参数未改；ACC、角度、按键及蓝牙500ms保护保留。临时门限调整不是[右编码器计数突跳](FALL_CAPTURE_20260928.md)的根因修复。

## MCU运行后验证

`state.json`记录首次运行后校验时间为13:42:47，第二次为13:50:39，`postbootChecks=2`；第二次约为下载后8分钟。

两次均完整读取131072字节Flash并与烧后镜像一致，PID Sector1保持。采样时Flash SR=`0x00000000`、CR=`0x80000000`，CFSR/HFSR均0，DHCSR=`0x01010000`，未处于halt状态。TIM2 CCMR1=`0xF1F1`、TIM3 CCMR1=`0xA1A1`，既有编码器滤波配置保持。

13:50:39的 `new-firmware-runtime.json` 记录如下；这些是软件与寄存器快照，不证明物理自稳、轮子状态或电机实测通过。

| 运行字段 | 采样值 |
|---|---|
| flag_move / running_mode / IMU有效 | 1 / 0（Normal）/ 1 |
| 电池电压 / 倾角 | 约11.90V / −2.524° |
| 电池采样次数 | 3642 |
| ADC错误 / 电压范围错误 | 0 / **3** |
| 蓝牙RX错误 / 重启错误 / 队列溢出 | 0 / 0 / 0 |
| 蓝牙非法帧 / 过期帧 / TX错误 | 0 / **1** / 0 |
| 跨epoch服务帧 / epoch拒绝帧 | **1 / 1** |
| 蓝牙运动armed / 停机记录frozen | 0 / 0 |

不能将上述结果概括为“全部计数为0”。跨epoch计数表明相应代码路径已被记录，不等于完成手机GATT握手、持续控制或丢包恢复验收。

## v38手机扫描实测与权限问题

v38安装成功与扫描流程可用是两个结论。现场两次手动搜索均确实启动了新扫描器，随后很快因Activity进入后台而结束，未收到扫描结果：

| 尝试 | scan start日志时间 | stop日志时间 | 停止原因 / 结果 |
|---|---|---|---|
| 第一次 | 13:44:38.846 | 13:44:38.866 | `background`，results=0；日志行相差20ms，扫描器自身elapsedMs=27 |
| 第二次 | 13:45:34.831 | 13:45:34.850 | `background`，results=0；日志行相差19ms，扫描器自身elapsedMs=23 |

同期系统记录出现 `GrantPermissionsActivity`。已安装v38的构建快照中，旧 `PermissionUtils.mayRequestLocation()` 在Android 12及以上先把SCAN/ADVERTISE/CONNECT加入列表，无条件调用 `requestPermissions()`，之后才逐项检查是否已有权限。该入口可在权限已满足时仍启动权限Activity，引发MainActivity的onPause，进而执行新扫描器预期的后台停止动作。这解释了本次“刚开始搜索便结束”的回归路径；不能据此认定模块未广播或MCU主动断开。

当前manifest对 `BLUETOOTH_SCAN` 声明了 `neverForLocation`。Android官方将Android 12及以上的蓝牙扫描/连接列为运行时权限，并允许不推导物理位置的扫描采用这一声明；这不免除SCAN/CONNECT授权要求。后续修复应核对缺失权限后再申请，避免无条件弹窗，同时保留真实后台/销毁时停止扫描的逻辑。参见[Android蓝牙权限说明](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)。

权限问题的本地证据包括 `picker-first.log`、`picker-third.log`、`permission-activity-context.log`、`activities-after-tap.txt` 与 `runtime-permissions.txt`。多份picker日志有重叠，不能把相同scan id重复累计为多次独立尝试。

## v39权限修复及实际安装

本次追加范围仅为手机权限入口的修复，继续使用已部署MCU HEX `720cf9af4f06efe5c86e1c8bee18648a2013d28697d1ca8613e5c97ceb088c51`。APK为 `2.3.6-mvtbot.12-hc05d (39)`，SHA-256 `1dd233c87241feadc6ebf62a374865da1fd22cad5d5ec589cec7ab8429eac153`，完整构建目录 `Android/MVTBOT/build/combined-ui-hc05d-v39-permissions-r1/`。同签名、全部主DEX/资源/桥接及16KB对齐校验通过，安装后拉回的APK与此哈希一致；未卸载或清理应用数据。

`ManualBlePermissions`在Android12及以上仅检查SCAN/CONNECT，已授权直接返回；缺项只申请缺失项，本次返回false，不同时开扫描。Android6～11检查Fine位置权限；Android6以前没有运行时申请。固定Mini界面启动不主动申请蓝牙权限，其他机型仍走旧路径。授权回调不自动扫描，用户需再次点击；真实后台清理和运动停止保持。

新增权限58项检查、BLE446、健康618、扫描97、协议1854、DEX正负24、smali/UI组合及跨端启动38调度/Java9、重新使能64 MCU帧/Java53均通过，证据为 `build/hc05d-permission-tests-20260928/regression-summary.json`。独立源码审查未见权限授予前触发需CONNECT的设备操作；这不替代新装未授权的OEM实测。

v39冷启动直接进入MainActivity，启动日志没有自动扫描、连接或FATAL EXCEPTION。14:00:00.558记录 `manual BLE permissions already granted; no request`，随后仅一次手动扫描，首结果约61ms，约10秒正常截止，结果事件1282次（不是1282台独立设备），未重现20ms后被权限页面取消。实际截图确认搜索列表可显示、滚动；UI自动化树未包含该PopupWindow，不能把树中缺少列表误判为弹窗未显示。

| 验证项 | 当前状态 |
|---|---|
| v39版本、签名、DEX、UI与完整成品 | 通过 |
| 同签名覆盖安装、versionCode及设备拉回哈希 | 通过，数据保留 |
| 已授权时不重复请求、手动扫描及10秒截止 | 实机通过；启动没有自动扫描 |
| 缺少权限、取消/拒绝权限的页面行为 | 主机通过，手机本次权限已授予，未撤销权限做实测 |
| 选中/取消/后台清理及重复连接 | 主机通过，完整实物回归待完成 |
| GATT业务握手及基本遥测 | 实机通过，见下节；用户反馈连接及功能正常 |
| 查询漏包恢复、持续控制、松手/中性重新使能及30分钟连续观察 | 待完整实测，本次未由代理操作摇杆或改PID |

用户在本次v39已构建、安装阶段提出下次更新在右上角“联系我们”增加APK版本信息，已列入APP-UI-004，要求从实际PackageInfo读取；按用户“下次”的范围，本次v39未追加该界面修改。

## 用户连接与日志复核

用户反馈办公室有大量蓝牙设备，已手动选择本车HC-05D，连接正常、功能正常。复核使用安装启动后同一APP进程，未清理内存或重启APP：14:06:10.520发起GATT连接，14:06:12.621通知订阅成功，确认实际FFE0/FFE1及QUALCOMM/HC-05D/1.1.4 profile后，14:06:13.425进入`operational`。

保存到14:07:29的日志包含CMD7电压61帧、CMD4速度257帧和CMD5距离257帧，首末遥测时间为14:06:13.450～14:07:29.283，未见FATAL EXCEPTION。该窗口的健康查询往返持续通过，仍按已验证profile处理模块响应13，并非将13当作普通成功或MCU ACK。证据为`v39-connected.log`与`v39-connected-summary.json`。

这是约76秒的基本通信证据与用户功能反馈，不作为30分钟稳定性、多次断线重连、实际运动或物理失联验收。完成记录时保留用户当前连接，不为取证主动断开或重启手机。

## 证据索引

本地根目录：`build/hc05d-v38-deployment/20260928-134138/`。

- 状态与安装：`state.json`、`apk-install.log`、`phone-after-package.log`、`phone-pull-installed.log`、`installed-v38.apk`。
- 原始备份与烧录：`before-full-flash.bin`、`before-pid-sector.bin`、`prewrite-pid-sector.bin`、`program-verify-reset-run.log`、`programmed-full-flash.bin`。
- 运行后：`postboot-1-*`、`postboot-2-*`、`new-firmware-runtime.json`；完整Flash与PID对应关系已离线逐字节复核。
- 现场扫描与权限：上节所列picker及权限Activity原始日志。原始文件可能包含设备标识，继续仅本地保存。
