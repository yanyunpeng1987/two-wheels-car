# APP-UI-001 / 002 / 003：启动、单机型与公司信息

用户需求日期：2026-09-24。输入固定为 v21 `2.3.6-mvtbot.2`，输出为 v22 `2.3.6-mvtbot.3`，包名保持 `com.Wonder.bot`。

状态：实现、签名构建与最终 APK 静态检查通过；当前手机覆盖安装、启动及中文信息页已检查，仍有 APP-002 生命周期异常及未覆盖场景。产物哈希见 [VALIDATION.md](VALIDATION.md)，手机结果见 [DEVICE_VALIDATION.md](DEVICE_VALIDATION.md)。

## 验收定义

| ID | 行为与验收 |
|---|---|
| APP-UI-001 | 启动直接进入主页，不加载 Hiwonder 动画、不等待原来的 3 秒；系统自身的启动画面不计入应用动画。保留原启动组件，兼容原桌面入口。 |
| APP-UI-002 | 左上角无机型选择按钮；首次启动、曾选择其他机型后的升级、后台恢复均显示两轮平衡车；侧滑和旧菜单回调不能打开机型抽屉。保存的机型和当前机型均为 MiniBalan。 |
| APP-UI-003 | 右上角信息页去掉 Hiwonder 图标、邮箱、公众号；公司显示“杭州矩视科技有限公司”；官网显示并实际跳转到 `http://www.mvtlabs.com/`。简体、繁体、默认语言均不恢复旧品牌信息。 |

## 修改与重建边界

`patch.json` 保存最小 XML/字符串和 smali 文本替换，不包含完整反编译工程。构建时核对不可变 v21 基线，在输出快照中应用补丁、生成主 DEX、重新封装并核验；本地 `project/` 保持原字节。签名继续使用本机现有 MVTBOT 证书。

仅隐藏菜单 XML 不足以满足 APP-UI-002：原首次启动会主动打开抽屉，旧偏好和蓝牙名称也能选择其他页面。补丁在共同的页面选择入口固定 MiniBalan，并关闭/锁定抽屉。保留隐藏视图的 ID/类型以满足 ViewBinding。信息页代码会重新生成网址并按语言显示公众号，必须与资源同步修正。

本次不改变报文解析、传输协议或设备固件。APP-001 的 NUL 解析崩溃、APP-002 的 Receiver 注销问题仍是独立待处理事项。

## 构建

在包含完整 v21 `project/` 与 Apktool 框架的仓库根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Android/MVTBOT/tools/Build-MVTBOT.ps1 -PatchDirectory Android/MVTBOT/changes/APP-UI-001
```

不传 `-PatchDirectory` 继续重建原 v21。补丁只应用于新构建快照，不覆盖历史基线。构建报告记录每个修改文件和 DEX 的哈希及签名、对齐结果。

## 验证与回退

完成后在本目录记录本地验证结果；手机安装、界面实测和车辆联调分别记录，未进行的项目保持待验证。设备操作不由构建脚本执行。

源码回退：停止传入本补丁目录，使用保留的 v21 输入重建。手机已升级后不能假设低 versionCode 可直接覆盖安装；应用降级/卸载及数据处理需另行确定。历史 v21 APK 保留，绝不覆盖。

## HC-05D 合并交付

上述 v22 命令/证据保留为界面任务历史。用户后续要求与 LINK-001 合并，当前工作区默认 `Build-MVTBOT.ps1` 从固定 v21 先应用本补丁，再组合通信补丁，生成 v24。最终成品同时验证UI门禁和LINK桥接；本补丁的JSON原字节与来源提交继续保留。`-RollbackUiOnly`生成保留本界面改动、撤回LINK通信逻辑的v25，用于搭配T4回退。详见仓库 `docs/HC05D_COMPATIBILITY.md`。
