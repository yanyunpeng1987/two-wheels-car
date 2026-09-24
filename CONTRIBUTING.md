# 开发与提交流程

本项目使用私有仓库，默认分支 `main`。固件与 APP 在一个仓库中协同维护；以需求 ID 关联分支、代码、协议和验收，按功能分支提交 PR。

## 开始与并行开发

先读 [文档入口](docs/README.md)、[需求台账](docs/REQUIREMENTS.md) 和 [版本矩阵](docs/RELEASE_MATRIX.md)，再执行 `git status --short --branch`。有其他任务未提交修改时，不切换或清理共享工作区。

开始独立需求：

```powershell
git fetch origin
git worktree add -b codex/describe-change ../BalanceCar-describe-change origin/main
```

若依赖尚未合入的 PR，从其**已提交且核对过的提交**建立 worktree，并把 PR base 指向依赖分支；依赖合入后再调整到 `main` 并重新检查。不同任务不要共用构建输出。worktree 不包含未提交改动或被忽略的 Android 输入。

## 文件与源码归属

| 范围 | 维护规则 |
|---|---|
| `Firmware/` | 实际 Keil `.uvprojx` 为构建依据；保留 GBK/UTF-8 与 CRLF，不批量转码 |
| `Android/MVTBOT/` | 先读模块 `AGENTS.md`；派生输入/原包本地保存，文档/脚本/品牌/哈希清单进 Git |
| APP 业务变更 | 先选定版本化补丁或正式源码模块，见 `Android/MVTBOT/changes/README.md`；禁止只改忽略目录 |
| `docs/` | 需求、协议、版本、问题和证据摘要；与代码同一 PR 更新 |
| 本地产物/原始日志 | 摘要记录相对位置和 SHA-256，不上传设备标识、秘密或生成物 |

新增固件 `.c` 应加入 `Firmware/MDK-ARM/BalanceCar.uvprojx`，再运行 `python tools/sync_vscode.py`。`MiniBalan/` 不属于维护源码，新克隆可直接构建设备端。

## 验证

干净 clone 可执行：

```powershell
python tools/verify_project.py
python tools/verify_android.py
python tools/sync_vscode.py --check
```

设备代码变化时，用所需 Keil 环境执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Rebuild
python tools/verify_project.py --artifacts
```

主机 C 回归见 [tests/README.md](tests/README.md)。`--baseline` 仅用于原始固件导入核查，需要本地 `MiniBalan/`；日常开发不使用它。

APK 输入恢复后，先核验，后构建：

```powershell
python Android/MVTBOT/tools/Verify-Import.py --include-project
python tools/verify_android.py --project-root Android/MVTBOT/project
powershell -NoProfile -ExecutionPolicy Bypass -File Android/MVTBOT/tools/Build-MVTBOT.ps1 -Unsigned
```

输入核验针对未修改的 v21 基线。业务修改后的差异应版本化，不能覆盖代码来消除报错。正式签名使用本机既有材料；缺失时不创建替代密钥冒充原证书。当前打包脚本要求 DEX 保持原样，逻辑开发必须另行调整并验证构建路径。

GitHub Actions 检查固件源码布局、主机 C 回归、Android 管理文件/清单与工具语法。CI 不含私有 APK 输入、Android 签名和 Keil 许可，**CI 成功不等于 APK/Keil 完整编译、安装/烧录或整车验收**。

## 提交、版本与回退

1. 明确列出 `git add` 路径，检查 `git diff --cached --stat` 和完整差异，确认无其他任务改动、私钥、原始日志或生成物。
2. PR 填需求 ID、最终行为、两端兼容、实际验证及未完成项；更新需求和问题状态，开发日志追加证据。未实测不写“已修复/已验收”。
3. 固件、APK、协议分别管理版本，在 [联调矩阵](docs/RELEASE_MATRIX.md) 记录组合。APK versionCode 单调增加；固件 Git 标签不自动改变内部版本号。
4. 有阶段版本才更新 `CHANGELOG.md` 并在已核验提交上创建注释标签，不覆盖已有标签。实验 T1/T2 字样不能代替提交/哈希。
5. 已共享改动优先 `git revert`。旧版本用独立目录查看：`git worktree add --detach ../BalanceCar-v0.1.0 v0.1.0`，避免覆盖在研目录。

源码提交和构建不执行设备操作。手机安装、蓝牙控制、固件下载、复位和参数区操作根据用户明确范围另行执行；回退前核对应用签名/数据兼容、硬件电机映射以及 PID 保留区。
