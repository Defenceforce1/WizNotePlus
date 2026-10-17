# 第二十轮：固定引擎源码与隔离编译

2026-10-18。本轮继续 PDF 目的地偏移的底层探索，不改变主程序实现、原生导出入口、编辑器／MathJax／CSS、笔记格式、账号、图标或资料目录。现用 App、冻结回退和已交付候选保持原样。这里的引擎补丁仍是**未完成实际编译／输出验收的草稿**，不是修复公告或正式版。

## 精确对应现用 SDK 的源码

上一轮固定的公开分支只是线索。本轮从现用 Qt 6.11.3 的 SBOM 追到模块提交 `8c73295524f3ce6a2e10ad2f71141c695376c876`，再由其 `src/3rdparty` gitlink 确定 Chromium 提交 `7927ce61b5a302a3da8172eefbe260ec17484bcd`。

实际下载的官方 QtWebEngine 6.11.3 源码归档 `.tag` 与上述模块相同；两个拟修改文件逐一核对原始 SHA-256。归档来自官方扩展源组件，而不是不含 WebEngine 的普通 Qt Sources 列表：

- [官方组件元数据](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6113/src_doc_examples/Updates.xml)
- 组件 `extensions.qtwebengine.6113.src`，归档 `6.11.3-0-202609240437qtwebengine-everywhere-src-6.11.3.tar.xz`
- 官方 SHA-1 与实际下载一致：`e545e2a47265b873f685a513f2768c259799c623`

归档没有统一顶层文件夹，不能使用 `--strip-components`。最初错误解压的约 1.1 GB 临时副本已停止并移入废纸篓，可恢复；重新检查路径后在新的隔离目录完整解压。源码、归档和编译缓存均不混入正式项目或现用 SDK，也不上传 GitHub。

## 打印变换候选，而非固定偏移

对应源码的关键差别：

- [`PrintContext::OutputLinkedDestinations`](https://github.com/qt/qtwebengine-chromium/blob/7927ce61b5a302a3da8172eefbe260ec17484bcd/chromium/third_party/blink/renderer/core/page/print_context.cc) 记录目标包围框原点，减页矩形原点，在传入的根属性状态下写具名目标。
- [`ChromePrintContext::SpoolPage`](https://github.com/qt/qtwebengine-chromium/blob/7927ce61b5a302a3da8172eefbe260ec17484bcd/chromium/third_party/blink/renderer/core/frame/web_local_frame_impl.cc) 先画正文，再记录这些目标。
- [`PaginationState`](https://github.com/qt/qtwebengine-chromium/blob/7927ce61b5a302a3da8172eefbe260ec17484bcd/chromium/third_party/blink/renderer/core/frame/pagination_state.cc) 已有正文当前页的完整变换：目标页内容区的边距平移、按页缩放、减去拼接页偏移，并带当前页裁剪。

草稿让分页目标采用同一个 `ContentAreaPropertyTreeStateForCurrentPage`，传入未再次减页偏移的拼接坐标；非分页捕获保留原行为。它不加固定像素补偿，不移动 HTML id，不改用户视口、CSS、纸型或导出后 PDF。源码因果关系仍须实际输出验证，不能只凭阅读得出已修复结论。

`tests/standalone-qt-pdf/engine-patches/` 保存小范围补丁、源码校验清单和只读适用性检查，不把整个 Chromium 拷入仓库。检查已对完整官方源码通过，只能证明原始文件匹配与补丁可应用。

补丁另附 Blink 回归设计：同一自链接块的源点击矩形与具名目标原点必须相合，覆盖两页、非对称边距、宽内容收缩和显式打印缩放。**该 Blink 测试尚未编译／运行**；Qt 常规构建不会自动运行 `blink_unittests`。

## 实际编译准备和当前状态

先保持源码不变，构建独立原版对照；之后才可在隔离副本应用补丁并做增量对照。所有构建／临时运行路径必须与现用 SDK、App 和真实资料隔离，不执行向它们的安装。

本机早期配置虽返回成功，实际上跳过了 QtWebEngine，不能算构建成功。逐项处理：

1. 明确设置外层与模块嵌套的 Ninja 路径；构建进程 PATH 也包含任务工具目录，供单独的 GN 引导子项目使用。否则 GN 的子配置仍找不到 Ninja。
2. 使用任务局部 Python 环境准备 HTML／模板与 SPDX 工具，不改系统 Python，不关闭许可证元数据生成。
3. 用户明确允许后，通过 Xcode 官方命令下载 Metal Toolchain **27A266a**；下载完成，实际 `metal --version` 成功。未关闭 Metal 或绕过检测。
4. 显式使用 `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`，与现用 6.11.3 WebEngine arm64 二进制的最低版本一致。空值曾生成无效编译参数，失败日志保留；这不是修改主 App 的最低系统要求。
5. 新配置确认 `QT_FEATURE_qtwebengine_core_build=ON`，实际生成 Chromium 构建图并进入编译。嵌套编译限三任务／负载六，不无界占用整机。

当前检查点：**未修改引擎的基线仍在编译，尚无完整可运行的重编译引擎，也没有补丁 PDF 验收结果**。Xcode SDK 27 新于这版 Qt 官方测试的 SDK 26，警告保持，不静默关闭或据此声称 macOS 27 完全兼容。不能把组件下载成功、配置成功、补丁适用或少量文件编译成功算成最终修复。

## 严格输出验收

独立几何审计增加 `--require-accurate`：所有九种条件的 27 个目的地必须页码正确、坐标误差不超过 1 pt；源点击矩形还要与实际画出的导航条左边／垂直中心对齐。审计只读，不输出改写 PDF。

- 现有原版九组输出完整验收按预期失败，仍有七种条件定位错误；27 个源链接均包含且对齐。
- 两个已知准确条件的显式正对照通过，标为 `subsetOnly`，不能据此接收引擎。
- 增补清单完整性反例：缺失／重复场景、未保存或 HTML 不一致不能通过审计；即便只跑正对照，也要求原始清单保留完整九组。空清单明确失败，不创建验收报告，避免空集合误判成功。源链接还必须覆盖三个不同目标。
- 只读审计不生成新 PDF；本轮尚未用重编译引擎生成 PDF。

重编译的未修改基线须先重现原 SDK 的九组几何、分页与渲染结果；否则先排查编译配置差异，不能把变化归功于补丁。实际引擎候选必须完整通过九组，而后再跑原阅读器／MathJax／科学 CSS 的合成语料，核对方程目的地、分页、渲染一致性、原文与修改状态还原及并发保存安全。还需完整主程序回归、原生界面和签名／包内验收，才可讨论新候选包。

本轮重跑 **47 项 PDF 生命周期检查通过**；没有把上一轮的完整 665 项当作本轮重跑结果。14.4.16 回退的内容清单／签名通过，现用可执行文件、14.4.18 已交付候选、DMG 和源码归档校验不变。

定理／定义及公式定位仍未宣布修复；原生窗口间歇问题、Metal footprint 增长也仍未完成。不发布／安装新版本。GitHub 仅通过插件备份诊断源码、小范围补丁和脱敏记录；用户 PDF、崩溃报告、日志、生成样本、源码归档与本机编译输出不上传。
