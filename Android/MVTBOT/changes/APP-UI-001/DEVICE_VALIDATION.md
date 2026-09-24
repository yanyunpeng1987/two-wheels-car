# v22 手机安装与界面测试

日期：2026-09-24。用户明确授权“手机已经连接到PC，请直接安装测试”。

结论：v22 已保留应用数据覆盖安装，启动、两轮车主页、菜单隐藏和中文公司信息显示通过本轮观察；官网点击传入浏览器的地址正确。测试同时复现既有 APP-002 页面销毁崩溃，不能据此版本宣称完整生命周期回归通过。

## 环境与安装

- 手机型号 `2211133C`，Android 16，ARM64；系统语言 `zh-CN`，字体缩放 `1.0`。
- 从 `com.Wonder.bot` / `2.3.6-mvtbot.2 (21)` 升级到 `2.3.6-mvtbot.3 (22)`，使用 `adb install -r`；未卸载或清空数据。升级前后首次安装时间相同。
- 安装前从手机读回旧 APK，确认签名证书与 v22 相同。安装后再读回新 APK，SHA-256 与交付件完全一致：`ba538bb8a9c925ff0b1301648de1c84ec732f04bd54a6f8f393215e00eeb74e0`。
- 原始记录保存在主工作区本地 `Android/MVTBOT/validation/ui-v22-device-20260924-172301/`，包含安装日志、前后包信息/读回 APK、启动录像、截图、活动 Intent 与崩溃日志；不纳入 Git。

## 观察结果

| 项目 | 结果与证据 |
|---|---|
| 启动动画 | 冷启动后约 1.26 秒截取的 `home-first.png` 已显示两轮车主控界面；原 Hiwonder 动画及固定 3 秒等待未出现。保留 `startup.mp4`。`am start -W` 的 575 ms 对应过渡权限 Activity，不作为主页绘制耗时。 |
| 机型入口 | `home-first.png` 与 `home.xml` 确认左上角只有电池，无型号菜单；两轮车摇杆、左右电机与 PID 页面入口正常显示。本轮没有触碰运动控制。 |
| 公司信息 | 用户手动点击信息图标后，`about.png` 视觉确认“公司：杭州矩视科技有限公司”、`http://www.mvtlabs.com/`，无原 logo、邮箱、公众号，当前字号下无裁切。 |
| 官网点击 | 用户点击链接后，系统活动记录确认发起 `android.intent.action.VIEW`，地址精确为 `http://www.mvtlabs.com/`，调用方为本 APP；见 `website-activities.txt` 和 `activity-logcat.txt`。 |
| 官网页面加载 | 尚未确认。截图 `website.png` 显示浏览器默认首页，不能声称官网内容已加载，也不能仅据此归因为 APK 的链接错误。 |
| 页面销毁 | 17:25:05.462 捕获 APP-002：`MainActivity.onDestroy → BLEManager.unregister → IllegalArgumentException: Receiver not registered`。 |
| 从浏览器返回 | 返回前 APP 进程已不存在；经原入口重新启动成功。没有将此记录成同一进程的后台恢复成功，也未据此推定进程消失原因。 |

信息弹窗是非聚焦 PopupWindow，默认 UIAutomator 树仅暴露背景主页，未包含弹窗内容。因此该页采用实际截图视觉验证；脚本按弹窗 ID 查找的失败不代表公司文字缺失。

## APP-002 进一步定位

`BLEManager.register()` 使用 `context.getApplicationContext()` 保存到 `registeredContext` 并注册接收器；`MainActivity.onDestroy()` 将 Activity 自身交给 `unregister()`，后者直接在该 Activity 上注销。Android 按注册 Context 查找接收器，因而抛出本次异常。不能笼统归为“重复注销”。

v21/v22 的完整 `BLEManager.smali` 字节一致，SHA-256 为 `f3693a9efffa4fd2902626eb074892695e06785c07e85e5a0e96b287358d480e`；`MainActivity.onCreate()`、`onDestroy()` 两方法也字节一致。本次界面补丁未修改该路径，问题已有原版 2026-09-16 的相同堆栈记录。本轮只定位、记录，没有扩展修改安装包。

后续修复需在同一个注册 Context 上注销并一致维护注册状态，另检查单例销毁后的静态注册标志；再独立测试关闭页面、后台回收和重进。APP-001 的 NUL 解析问题保持原状态。

## 测试边界

ADB 安装、启动、读取与截图可用，但点击/滑动始终被手机以 `INJECT_EVENTS` 权限不足拒绝；用户开启相关设置并重新连接后仍然如此，未尝试绕过权限。信息页和官网由用户手动点击，其余证据由 ADB 读取。

侧滑抽屉的动态测试、清空数据后的首次使用、旧的其他机型偏好升级、繁体/英文系统环境、其他字号及同一进程完整生命周期回归尚未通过本轮实测；其中资源/代码规则已有此前静态验证。本轮没有蓝牙连接、控制指令或车辆联调。
