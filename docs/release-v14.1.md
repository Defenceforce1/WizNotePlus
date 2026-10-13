# Apple Silicon 收尾试用版 v14.1

日期：2026-10-14。v14.1 是兼容分支的试用批次，不是上游版本号；应用内部仍基于 WizNotePlus 2.12。

源码分支：[Defenceforce1/WizNotePlus · compat/macos-arm64-qt6](https://github.com/Defenceforce1/WizNotePlus/tree/compat/macos-arm64-qt6)。本次只补齐说明、可复制的数学示例、隔离启动脚本、完整 GPL／LGPL／Crypto++ 许可文本及示例回归检查，不加入新编辑器、数学宏、AI、WebDAV、团队或新的打印功能。

## 保留的功能

- Apple Silicon 原生构建，Qt 6.11.3，沿用原始笔记与 Markdown 编辑习惯。
- macOS 原生窗口按钮；浅色搜索框、紧凑搜索栏和内嵌标签筛选。
- 个人笔记／批量选择／含子文件夹的原始数据导入导出，以及通用 HTML 导出。
- 全局正文 CSS、可选分类数学样式、每篇笔记编号与 `\label`／正文 `\ref`。
- 原来的“另存为 PDF”流程，不增加 PDF 重排或后处理。

说明：[数学笔记](math-notes-guide.zh-CN.md)、[搜索](inline-search.md)、[导入导出](native-note-transfer.md)、[构建流程](macos-arm64.md)。

## 试用方式

解压完整试用包，保留 `.app` 与“隔离试用.command”在同一目录。先用 ⌘Q 正常退出正在运行的为知笔记，再运行脚本，只登录专用测试账号。脚本直接启动旁边的确切二进制，不经由应用名称查找旧版。

独立试用数据位于 `~/Library/Application Support/WizNotePlus-AppleSilicon-Trial/`。这是新的试用目录，不会自动复制旧版笔记，也不会接续其他测试目录。脚本不改变 App 的默认资料库位置；直接双击 `.app` 会绕过隔离设置，因此暂不建议。若 macOS 阻止打开，这是未公证试用包的限制；不要关闭系统安全保护或执行不明去隔离命令。

本地隔离不等于云端隔离：使用真实账号，仍可能同步该账号的笔记。原始导入会建立新副本并可能同步到当前账号，不去重、不覆盖旧笔记。试用前备份有价值的资料。

## 已知问题与未验证部分

- PDF 定义／定理引用可到达正确页，但上下定位可能偏移。经隔离多页样本复现；本轮修复试验已撤回，没有把试验性 PDF 重写机制放入本版。
- Safari 旧剪藏器的登录兼容性未解决；8848 端口只能由一个客户端占用。
- `.wiznotes` 不是官方为知通用交换格式。通用 HTML 在官方为知、Notebooks 或其他 App 中的真实导入仍未验证。
- 大规模笔记迁移、崩溃／断电恢复、多显示器与所有 Spaces 操作、复杂用户 CSS／公式和完整账号冲突同步未获全面验证。
- 临时签名，不是 Developer ID 签名或 Apple 公证。macOS 27 本机检查不等于未来 macOS 28 兼容认证，也不等于所有机器都通过测试。
- GPL 与依赖通知随包保留；第三方编辑器、字体、图片等完整资产再分发审计仍未完成，因此不是面向大众的正式公开发行版。

## 验证记录

v14 基线的 GitHub 构建已经成功：[Actions 记录](https://github.com/Defenceforce1/WizNotePlus/actions/runs/38435377626)。该云端任务运行在 macOS 26，不是 macOS 27 的运行时认证。

本次收尾版的本机、打包后与压缩包检查结果见交付目录的“验证记录.md”。本文件不预先声称尚未完成的检查通过。自动测试只使用生成的临时笔记和资料库，没有启动安装的旧版或读取真实笔记。
