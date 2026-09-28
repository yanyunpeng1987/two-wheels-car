# MVTBOT Android 应用

此目录是“两轮自平衡小车”的 Android 开发资料入口，2026-09-24 从“提取 Wonderbot APK”任务汇总。设备端继续使用仓库根目录的 `Firmware/`。

**不可变输入基线：MVTBOT 2.3.6-mvtbot.2，versionCode 21。** 当前 LINK-001 默认构建为 HC-05D BLE 版 `2.3.6-mvtbot.11-hc05d (38)`，保留另一任务的 APP-UI-001 界面改动、原包名和证书。v38 已完成本地构建与成品校验，**待安装实测**；启动自动连接已完全移除，现代手动扫描及健康查询恢复已实现。当前产物、配对和验证边界以[蓝牙重连修复报告](../../docs/HC05D_RECONNECT_FIX_20260928.md)为准。历史 NUL 上游来源仍未确认，v36 手机证据保留在[2026-09-24现场记录](../../docs/HC05D_DEVICE_VALIDATION_20260924.md)。

## 从这里开始

- [开发交接与当前结论](docs/HANDOFF.md)
- [v38 蓝牙重连修复与验证边界](../../docs/HC05D_RECONNECT_FIX_20260928.md)
- [APP 与设备端协同入口](docs/PROTOCOL_COORDINATION.md)
- [原始崩溃分析与三次复现证据](materials/deliverables/MVTBOT/diagnostics-20260916/原因分析.md)
- [Android 工具环境记录](materials/deliverables/Android_Development_Environment/环境检查与使用说明.md)
- [原 v21 基线 APK](materials/deliverables/MVTBOT/icon-update-v21/MVTBOT.apk)
- [图标版验证与安装说明](materials/deliverables/MVTBOT/icon-update-v21/README_图标更新说明.md)

## 目录约定

| 位置 | 用途 |
|---|---|
| `project/` | 当前 v21 的完整 Apktool 重建输入；不含旧构建缓存 |
| `branding/app-icon.png` | 用户提供的 MVTBOT 原始图标，1254×1254 |
| `tools/Build-MVTBOT.ps1` | 相对路径构建、对齐、同证书签名与校验；不安装到手机 |
| `tools/Verify-Import.py` | 核验迁移材料的 SHA-256；可选择核验当前工程基线 |
| `baseline/project-files.json` | 进入 Git 的 v21 工程指纹：1,445 个相对路径、大小、SHA-256；不包含源码或本机路径 |
| `changes/README.md`、`changes/LINK-001/` | 最小 smali 桥接、固定输入哈希与补丁重放 |
| `link/src/`、`link/tests/` | 自有 Java BLE/协议实现及主机回归 |
| `materials/deliverables/` | 之前所有正式交付，包括原始拆分包、v20/v21 APK及报告 |
| `materials/history/v20-apktool-project/` | v20 的旧重建输入，供版本差异对照 |
| `materials/analysis/original-base-apktool/` | 原始主包资源/smali 分析参考，不是完整单 APK 的当前工程 |
| `materials/analysis/jadx-reference/` | Java 阅读参考，共 4113 个文件；有 103 项反编译错误 |
| `materials/evidence/bluetooth-20260916/` | 原始 `.raw` 日志、转义文本、接收统计、截图与分析脚本 |
| `materials/tooling/` | APKEditor 合并工具、来源校验及 Apktool 框架 |
| `materials/provenance/` | 历史构建/环境脚本、日志与配置快照，只作来源记录 |
| `materials/import-manifest.json` | 逐文件来源、目标、大小、SHA-256 与去重清单 |
| `build/` | 后续本地构建输出，每次使用新目录 |

原任务目录仍保留，便于回溯。当前开发应使用本目录；`materials/` 中的旧文档和脚本按原字节归档，里面的旧任务路径、设备序列号和安装时间是历史记录，不是当前操作入口。

## 本地构建

在项目根目录打开 PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Android/MVTBOT/tools/Build-MVTBOT.ps1
```

构建脚本验证不可变 v21 `project/`，在快照中生成新业务 DEX，输出到 `Android/MVTBOT/build/<时间戳>/`。默认沿用本机原 MVTBOT 开发签名；没有签名材料的新电脑可加 `-Unsigned` 先验证重建。工具路径支持参数覆盖，具体参数见脚本帮助。

本机已有工具默认位于 `%LOCALAPPDATA%\Programs\AndroidTools` 和 `%LOCALAPPDATA%\Android\Sdk`。Studio 自带 JBR 25，Gradle 工程使用 JDK 21；这两者不要混用。完整工具版本见历史环境记录，实际执行前以脚本检测为准。

干净 Git clone 可直接进行仓库管理检查，无需本地归档和 Android 工具链：

```powershell
python tools/verify_android.py
```

该检查验证清单结构、必需文档/脚本和 Git 中的本地材料隔离，不代表 APK 构建或实机验证。恢复完整归档后，可只读核验 `project/` 的 1,445 个文件与导入基线：

```powershell
python tools/verify_android.py --project-root Android/MVTBOT/project
```

核验完整本地归档：

```powershell
python Android/MVTBOT/tools/Verify-Import.py
python Android/MVTBOT/tools/Verify-Import.py --include-project
```

`--project-root` 和 `--include-project` 都要求当前工程仍与导入基线相同；后续开始正式修改工程后出现差异应作为变更处理，不要为了让检查通过而覆盖代码。清单只提供指纹，不能代替完整项目备份。

## Git 与私有签名

遵循根目录 `CONTRIBUTING.md`：原始资料包、反编译参考、APK及本机证据留在项目内本地管理，由本目录 `.gitignore` 排除。模块索引、交接文档、脚本、自有图标、脱敏的工程指纹和变更管理约定纳入 Git 统一维护；原始材料不随之上传。

`project/` 是派生的本地重建目录，Git clone 或新 worktree 不包含它与 `materials/`。APK 工作开始前须从完整归档恢复本地材料并核验；仅克隆仓库无法重建 APK。

业务改动遵循[变更管理约定](changes/README.md)。LINK-001 用自有 Java + 最小 smali 桥接，在快照中重建主 DEX和辅助 DEX，classes2/3 保持原样；最终检查方法签名、类唯一性和 APK 载荷。JADX 仍只供阅读，不能当成完整可构建工程。`-BaselineOnly` 重建原 v21；`-RollbackUiOnly` 当前默认输出 versionCode 39，保留新界面、旧通信逻辑及同一签名。该参数默认值不代表 v39 已构建或安装，回退还须按实际在机版本与配套固件核验。

私钥及 DPAPI 密文仍位于 `%LOCALAPPDATA%\Android\Signing\MVTBOT`，不在本项目中。迁移到其他 Windows 账户前需要单独规划签名备份；仅复制 DPAPI 密文不能保证可用。

用户已要求与APP-UI-001合并。默认v38同时包含新界面和HC-05D通信；保留原UI补丁的来源与验收，不以旧v22或v36手机记录代替新组合包实测。最终成品重新解码核验UI规则及全部主DEX类。

## 当前 v38 构建产物

产物为 `build/combined-ui-hc05d-v38-reconnect-r2/MVTBOT.apk`，SHA-256 `87ac15c758715f7a00bf786c25f21174dbc2a61618a973225cca4692daec174e`。已通过同签名、DEX/全部主类、资源及16KB对齐校验；尚未安装，不作为手机或整车验收证据。

v38 不含 `DebugAutoConnect` 类或自动连接目标 asset，构建入口也已移除该选项。用户打开手动列表后才启动一次最长10秒扫描，关闭、选中、外部取消、后台及销毁都会停止；失败有明确提示与有界冷却，不自动重试。健康恢复期间暂停旧运动，恢复后需要新的松手或回中；具体时间预算、主机回归及现场待测项目见[修复报告](../../docs/HC05D_RECONNECT_FIX_20260928.md)。

## 2026-09-24 v36 历史排查包

2026-09-24已安装v36诊断包（版本名追加`-diag`），SHA-256 `699aeba6d2d799f3757619b40e34bb28891b053887726905a70c6b0396124428`。启动后按本机配置扫描已确认的目标，实际约7秒进入通信就绪，速度/距离/电压/PID回读/波形有手机证据；联动控制和完整失联验收留待用户现场进行。结束本轮时已停止手机APP的主动连接。

当时的自动连接仅在显式提供`-DebugAutoConnectTarget`时启用，地址只存在输出快照的`assets/mvtbot-debug-autoconnect.properties`，未写入源码或原v21输入；前台稳定500ms后尝试一次最长10秒的精确地址扫描。这是已归档 v36 的历史行为，当前 v38 已删除该实现和构建选项，不应再沿用旧命令。
