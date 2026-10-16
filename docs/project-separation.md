# 主 App 与剪藏器项目拆分

2026-10-15：用户确认主 App 14.4.16 目前正常，要求收尾整理并将剪藏器独立；明确批准新仓库名 `Defenceforce1/WizNotePlus-Clipper`。

| 工程 | 当前职责 |
| --- | --- |
| 本仓库 compat/macos-arm64-qt6 | Qt 主 App、笔记／同步、Markdown／CSS／数学、旧 PDF、原始导入导出及主 App 隔离回归 |
| [WizNotePlus-Clipper](https://github.com/Defenceforce1/WizNotePlus-Clipper) 的 main | Swift 原生宿主／系统分享扩展、Safari JavaScript、独立云端、资源、签名／构建／测试／CI 与待办 |

独立剪藏器基线：[7b1e284](https://github.com/Defenceforce1/WizNotePlus-Clipper/commit/7b1e284f42fd75242d142b047d2c659c0b73b0cc)。来自主仓库冻结提交 `4757df7bb8aed65172d6b816f8036c0105b32bb8` 中的 0.2.0 实现。运行源码、配置和合成网页逐字节不变，只调整目录与构建路径；已有历史仍可在原提交中查看，不重写或删除旧提交。

原 `build/osx/native-clipper` 只留迁移 README，移除重复 Swift／JavaScript／配置／构建脚本。剪藏器单独 CI 与 cloud fixture 迁至新仓库；主工程不再触发或托管它的构建。主 App 旧桥接实现与隔离测试属于兼容／历史安全覆盖，保留但不启用正常 8849 服务；本次不重构或删除主程序代码。

独立工程仅使用 Swift／macOS SDK，不需要主程序源码、Qt 或另一份账户资料。本机独立 arm64／严格 ad-hoc 签名、输入 10、模拟云端／资源 28、网络阻断本地 WebKit 捕获 9，总计 47 项通过。[独立 GitHub CI](https://github.com/Defenceforce1/WizNotePlus-Clipper/actions/runs/38681203468) 也已在上述源码提交完成 success，不依赖主仓库或 Qt；不等于系统分享加载、多机开发签名或真实云端验收。没有安装／注册测试包、登录账号或上传网页。

主 App 14.4.16 完整安装包、源码、签名和数据保持；不因本次维护重新编译、改名、更新安装或移动资料库。当前免费签名／未公证方案不变，主程序公开二进制许可审计仍有边界。

剪藏器待办转移到独立项目 ROADMAP：只推送不轮询／不阻塞、HTTP 接收与最终保存区分、重复公式和缺图占位、资源／MathML 排版、实际云端和多机免费加载验收。本次拆分不等于这些功能已完成；旧本机 8849 入口与当前主 App 已不配套，独立文档已明确说明。

清理只涉及被取代的生成 App、重复展开源码与重复归档，约 2.49 GiB 先移至本机废纸篓，可恢复，未清空。当前 14.4.16 与完整上一版 14.4.15、全部唯一历史源码、日志／截图、现用工具链、真实／试验资料、已安装所有剪藏器均保留。旧阶段交付路径仅作历史记录，当前回退以最新交付记录为准。不上传个人报告、账号、网页快照、日志或证书／私钥。
