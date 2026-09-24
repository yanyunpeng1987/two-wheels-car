# T1现场取证与T2启动修复

2026-09-24。当前状态：T2已下载；两次运行后完整Flash与向量均匹配、PG=0/LOCK=1、SR=0、PID保持。平地自稳仍复现SPD，现进入T3右编码器滤波实验。

## 已取得的停机证据

用户连接SWD再次复现。使用CubeProgrammer HOTPLUG只读，不halt、不reset，读取冻结的3392字节RAM及完整128KiBFlash。
64帧覆盖约283.8ms；采样周期4.501857–4.508381ms。最后一帧左计数0、右计数89（上一帧右0），右计算速度240.17cm/s，角度−2.671°，Z=0.99988g，电压11.99V，KEY未按下，IMU读成功，原因SPD。此前PWM在−86..316，没有饱和或大输出先行证据；上一帧两侧均−82。右路较早还有5、4、−9等偏离。

这支持右路计数突变，不能仅凭一次窗口认定具体线材、传感器或软件内存根因。当前4.5ms周期也不支持长时间阻塞导致轮速被放大。原始证据保存于build/stop-test-stage2/capture-20260924-143711/，解析结果trace-analysis.json。

## 同时确认的Flash损坏

当前所有T1源码/构建与发布清单一致。设备Flash有10个不同字节，全部位于4个中断向量；其他发布数据及PID Sector1一致。再次读取物理Flash首256字节和0地址映射区，结果相同；VTOR=0。现场FLASH_CR=0x80000201，PG=1、LOCK=1；FLASH_SR=0x40；CFSR/HFSR=0。

| 向量偏移 | T1正确值 | 现场值 |
|---|---|---|
| 0x18 UsageFault | 08001041 | 00000000 |
| 0x3C SysTick | 08011DDD | 08000081 |
| 0x40 WWDG | 0800802B | 08000001 |
| 0x4C RTC_WKUP | 0800802B | 08000001 |

main原启动代码预先设置Flash PG后Lock。HAL_FLASH_Lock只锁控制寄存器，并不清PG。
同时main调用HAL_ADC_Start_DMA，但ADC_MspInit未配置或链接DMA；现场hadc1.DMA_Handle（0x20006704）确认为0。
HAL_ADC_Start_DMA仍写入三个DMA回调，NULL指针加偏移恰好落在0x3C/0x40/0x4C的Flash别名。
使用发布AXF的回调地址计算，旧Flash字与拟写值按位AND恰好得到每个实际损坏值：

- 08011DDD & ADC_DMAConvCplt(0800A2A1) = 08000081。
- 0800802B & ADC_DMAHalfConvCplt(08000555) = 08000001。
- 0800802B & ADC_DMAError(08000541) = 08000001。

这三个向量的证据精确对应ADC空DMA句柄写入。随后DMA Lock的字节写与Flash WORD宽度不匹配，也符合现场PGPERR。
0x18变零则高度符合过早EXTI调用未初始化QMI的cs_port=NULL，GPIO BSRR(NULL+0x18)写零；该写指令没有在线捕获，置信度低于上述三字精确匹配。

[ST RM0368](https://www.st.com.cn/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)的Flash章节说明PG、LOCK及PGPERR含义。
SysTick在本项目由DWT时基替代，向量受损而MCU仍运行并不矛盾。
这些启动缺陷必须修复，但尚未证明它们直接制造了右轮89计数。

## T2修复范围

- 启动时明确Unlock、清PG、Lock，并检查最终PG=0/LOCK=1；失败不进入后续初始化。
- 移除无人使用且未链接句柄的ADC DMA启动；电池/CCD原轮询采样不变。
- GPIO初始化保持EXTI2禁用；检查QMI初始化成功，全部模块及PID就绪后才开启控制。
- 开启前停止PWM、flag_move=0、清掉初始化期间的编码器累计量。
- 清旧pending；若DRDY已保持高电平，用EXTI软件事件补一次，再开启NVIC，避免等不到新边沿。
- 屏幕标记T2。control.c、PID、SPD门限、速度换算、编码器滤波、按键与电机极性保持T1内容。

新增25项集成检查、246项真实C函数/HAL桩检查通过；Keil0错误0警告。最终HEX94560字节，RAM31672/65536，PID区无HEX内容。
HEX SHA-256：4f2c44df38a244f14891f12579c461861c1e2d007c3c266803f09163fa3b5ae3。
产物build/stop-test-stage2/release/BalanceCar-T2-startup-safe.hex；T1完整源码/产物备份在同目录上级baseline/。

## 验证顺序

1. 固定设备后备份并分步擦除/下载，仅Sector0、2、3、4，保留PID Sector1。
2. 下载后校验所有HEX数据和PID；复位启动后再次HOTPLUG读完整Flash及FLASH_SR/CR，证明启动未再次改写向量，PG保持0。
3. 确认正常启动、传感器/屏幕更新后，再进行同条件自稳测试并抓停机记录。
4. 若SPD依然复现，下一轮专门处理右轮异常计数进入保护及速度PID的方式，并继续核查硬件信号；不能只放宽门限。

当前T2修复不能视作右轮计数问题已经解决。T1回退镜像含上述启动缺陷，只保留用于对比；常规测试优先验证T2，不主动回刷缺陷版。
