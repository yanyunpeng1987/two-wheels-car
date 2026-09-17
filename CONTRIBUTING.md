# 开发与提交流程

本项目当前使用私有仓库，默认分支为 `main`。首次提交建立 `v0.1.0` 初始化源码基线；以后按功能分支开发，通过 Pull Request 汇入 `main`。

## 开始开发

```powershell
git switch main
git pull --ff-only
git switch -c codex/describe-change
```

修改 `Firmware/` 中的代码。`MiniBalan/` 是本机参考资料，不属于仓库；新克隆不需要这个目录即可构建。

新增编译输入应加入 `Firmware/MDK-ARM/BalanceCar.uvprojx`，再运行 `python tools/sync_vscode.py`。不要另建与 Keil 不一致的 VS Code 源文件清单。

## 本地检查

```powershell
python tools/verify_project.py
python tools/sync_vscode.py --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Action Rebuild
python tools/verify_project.py --artifacts
git diff --stat
git status --short
```

`--baseline` 专用于原始导入复核，需要完整本地 `MiniBalan/` 包；新克隆和日常修改不使用该选项。编译器、设备包要求见 README。

GitHub Actions 运行源码引用与内存布局的静态检查、工具语法检查。托管 runner 未安装 Keil 及其许可，因此 **CI 通过不等于 Keil 编译通过，更不等于硬件验收通过**。PR 中记录实际本地编译和硬件验证范围。

## 提交与记录

- 按本次范围明确指定 `git add` 路径，检查 `git diff --cached --stat` 后提交。
- 在 `docs/DEVELOPMENT_LOG.md` 追加目的、改动、验证和结论；问题状态更新到 `docs/KNOWN_ISSUES.md`。
- 用户现象反馈、源码推断、本机构建结果、实车测量分别记录，未证实的问题不能标记已修复。
- 有阶段性版本时更新 `CHANGELOG.md`，在通过检查的提交上创建注释标签；不要覆盖已有标签。
- `.gitattributes` 保留 `Firmware/` 原字节，避免 Git 自动转换 GBK/CRLF。不要批量转码、格式化或重新生成整个固件目录。
- 构建产物、原始资料包、调试器个人选项和本机身份信息不提交。当前 `.uvoptx` 保留本地；换电脑后按实际下载器重新配置。

源码提交和 Git 标签不执行固件下载。烧录、参数扇区擦写和实体硬件验收单独进行。

## 回看基线

用独立目录查看旧版本，避免覆盖当前工作区：

```powershell
git worktree add --detach ..\BalanceCar-v0.1.0 v0.1.0
```

如需撤销已共享的后续改动，优先使用 `git revert` 保留历史。
