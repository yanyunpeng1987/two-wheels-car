# 2026-09-24 资料汇总与验证

## 已完成的整理

来源：`C:\Users\Administrator\Documents\Codex\2026-09-16\new-chat`。

目标：`C:\Users\Administrator\Desktop\CODEX_PROJECTS\Two-wheels-Car\Android\MVTBOT`。

共导入 **17,711 个文件、332,529,270 字节（约 317 MiB）**。复制时逐文件核对 SHA-256，之后再次完整校验全部导入文件和当前工程基线，均通过。逐文件来源与目标位置见 `materials/import-manifest.json`；原任务文件没有删除或移动。

保留原始拆分包与 ZIP、v20/v21 交付、当前 v21 工程、v20 工程历史、原版 smali/Java 分析、完整原始崩溃日志、环境记录和历史脚本。仅按清单省略大型安装器、可重建缓存、失败解码残留及重复的 `split-input`。过滤的是工程根目录 `build/`；合法的 `unknown/META-INF/com/android/build/gradle/app-metadata.properties` 已确认保留。

## 新目录重建

在 **Windows PowerShell 5.1** 中执行模块 `tools/Build-MVTBOT.ps1`，使用独立工程快照、JDK 21、Apktool 3.0.3、Build-Tools 36.0.0 和既有 MVTBOT 证书。

结果：**PASS**。应用 `com.Wonder.bot`，版本 `2.3.6-mvtbot.2 (21)`；18 个 DEX/资产/本机库/服务描述载荷校验通过，签名和 16 KB ZIP 对齐检查通过。

进一步与已有 v21 发布包对比，ZIP 内 **1,390 个非签名文件全部逐字节一致**，包括 Manifest、资源表、图标、DEX、本机库及布局。APK 整体哈希不同，因此不把它描述为逐字节相同的发行文件，也不替换历史发布包。

| 文件 | SHA-256 |
|---|---|
| 2026-09-16 v21 原交付 | `6b27d6af2261b0f4d559828f9fcc6df1a9e3f464c0d8b592278ceadc1a98463e` |
| 新目录重建验证 APK | `8e13ee2621b926614a3fc1f15a1baa2f25217ae443d0e792277c9de3d7268844` |

验证输出位于 `build/20260924-143418-808-9f7acef5/`。摘要副本见 `validation/20260924/build-summary.json` 和 `validation-result.json`；构建脚本未操作手机，也没有部署固件。

## 原有项目保护

- 对导入前后 **368 个已跟踪 Firmware 文件**比较哈希，没有变化。
- 根 README、开发记录和已知问题只追加 Android 索引/记录；原有字节作为前缀完整保留，其他任务的未提交修改未被回退。
- 新增 `docs/ANDROID_MVTBOT.md` 项目入口、模块交接与 APP/MCU 源码对照说明。
- 未暂存、提交或推送 Git。归档、派生工程和构建产物保持本地；文档、脚本、自有品牌素材可供后续统一提交。
- 没有复制私钥、KeyStore 或 DPAPI 密文。签名仍引用本机用户级既有材料。
- 本次没有修复 NUL 闪退，也没有将设备端后续实验固件视为 APP 问题已修复。

## 路径与后续使用

实际项目已经改名为 `Two-wheels-Car`，本次所有资料写入该真实目录。Codex 项目入口仍保存旧路径，需在项目设置中重新关联实际根目录；本次没有修改 Codex 的内部存储。

当前 Android 工作应从模块 README 和 `project/` 开始。`materials/` 中旧文档的绝对路径作为历史记录保留，统一构建脚本和导入校验脚本均从自身位置解析当前模块，不依赖旧聊天目录。

Git 新克隆或新 worktree 不包含被忽略的本地 `project/`、`materials/`。需要在那里开发 APK 时，应先从完整项目备份复制这些材料并运行 `Verify-Import.py --include-project`；本次没有把第三方原包和 APK 产物自动上传 GitHub。
