# 剪藏器已迁出

2026-10-15，用户要求将剪藏器作为独立项目维护。

- 独立源码：[Defenceforce1/WizNotePlus-Clipper](https://github.com/Defenceforce1/WizNotePlus-Clipper)
- 构建：在独立仓库执行 `bash scripts/build.sh /absolute/new-output-folder`，不需要 Qt 或主 App。
- 迁移基线：[7b1e284](https://github.com/Defenceforce1/WizNotePlus-Clipper/commit/7b1e284f42fd75242d142b047d2c659c0b73b0cc)，仍是 0.2.0 候选，未顺带修复网页保真／轮询。
- 详细边界：[项目拆分说明](../../../docs/project-separation.md)。

本目录仅为旧链接留迁移入口，不保留第二份实现或构建包装。主 App 的旧桥接兼容代码／隔离探针仍保留，但正常主程序不启用 8849。本次不安装或替换已有剪藏器、不改变钥匙串／分享注册。
