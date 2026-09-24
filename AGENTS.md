# BalanceCar / MVTBOT 协作规则

- 先读 `docs/README.md`、`docs/REQUIREMENTS.md`、`docs/RELEASE_MATRIX.md`；修改 Android 前再读 `Android/MVTBOT/AGENTS.md`。
- 设备端唯一维护目录为 `Firmware/`，真实构建依据为 `Firmware/MDK-ARM/BalanceCar.uvprojx`。保留原文件编码和换行；新增 C 文件须加入 Keil 工程并同步 VS Code。
- `Android/MVTBOT/project/` 是本地 Apktool 派生输入，不是原始 Android Studio 工程。业务改动必须有 Git 可审查的补丁或正式源码模块，不能只留在被忽略的目录中。细节见 `Android/MVTBOT/changes/README.md`。
- 每次开发关联需求 ID，先定义验收，再实现；更新需求状态、问题记录和验证证据。不得将本机构建、历史安装或源码推断写成当前整车验收。
- 先检查 `git status`。其他任务的未提交修改保留；并行任务用独立分支/worktree，提交时显式列出路径。不要在共享脏工作区执行切分支、reset、stash、clean 或批量 add。
- 通信修改先核对 `docs/PROTOCOL.md`，同时评估手机端和设备端；版本组合记录 APK/HEX 哈希及证据级别。
- `materials/`、本地重建输入、原始日志、APK/HEX、签名私钥和口令不进入 Git。公有证书指纹可以记录，秘密不得输出。
- 构建与源码提交不代表手机安装、蓝牙控制、设备复位、Flash 擦写或烧录授权；这些操作根据用户当轮明确范围和当前目标状态执行。
- 用户指令优先；仓库保持私有，不更改可见性、不覆盖标签或重写共享历史。发布和回退流程见 `CONTRIBUTING.md`。
