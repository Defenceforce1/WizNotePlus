# 独立 Qt PDF 版本对照

开发诊断工程，不是用户客户端。只编译现有 PDF 探针和文件保存 helper，不启动主 App、不读取账号／笔记库、不注册或安装 App。只接受本地合成 `native-pdf-fixture.html`，离线 profile 与 cache 随测试结束销毁。

每个 Qt SDK 使用独立构建目录；传给探针的 SDK 必须与链接的 SDK 相同。不得将另一个 App 的框架、helper、资源或插件混入。运行结果记录实际 Qt／WebEngine／Chromium／安全补丁版本；不能只凭下载目录或 UserAgent 判断版本。

```sh
cmake -S tests/standalone-qt-pdf -B /absolute/new/build-directory \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH=/absolute/Qt/macos
cmake --build /absolute/new/build-directory
node tests/compat/prepare-pdf-reference-fixture.cjs \
  /absolute/source/root /absolute/new/output/native-pdf-fixture.html
QT_QPA_PLATFORM=cocoa /absolute/new/build-directory/native-pdf \
  /absolute/Qt/macos /absolute/new/output/native-pdf-fixture.html /absolute/new/output
python3 tests/compat/inspect-pdf-reference-integrity.py /absolute/new/output
python3 tests/compat/explore-reference-pdf.py /absolute/new/output --engine qt
```

生成器要求输出目录已经存在，HTML／PDF／JSON 不覆盖旧文件。分页边界对照仅在该测试进程设置 `WIZNOTE_PDF_EDGE_PROTOTYPE=1`。两种布局各生成三份 PDF：5 mm、20 mm、真实可见标题锚点；第三种只是诊断变体，不是产品修复。

同一夹具跨版本比较时，须保留相同 Markdown／编辑器／MathJax／CSS，逐项比较：原文和 HTML／修改状态不变、公式无错误、无远程请求、目标是否存在、页码和可见标题坐标，以及整页视觉效果。SVG 引用使用 `href`／`xlink:href` 读取，不能用只适合普通 HTML anchor 的 `.hash` 漏掉它们。网页中存在 SVG 目标并不保证 PDF 含同名目的地。

用户文件及其截图、内容、路径和诊断结果只留本机，不加入源码备份。不要将测试 PDF 后处理成“通过”，或因一个样本变好就更新正式运行库。

## 不依赖主程序的坐标复现

同一工程另有 `anchor-geometry`，不包含／链接任何 Wiz 代码、编辑器、数学库或页面脚本。它用合成三页彩色目标和普通内部链接，改变纸型、方向、API 页边距、正文宽度和 CSS 命名页。运行前要求一个新建的空输出目录：

```sh
cmake --build /absolute/build-directory --target anchor-geometry
QT_QPA_PLATFORM=cocoa /absolute/build-directory/anchor-geometry \
  /absolute/matching/Qt/macos /absolute/new/empty/output
python3 tests/standalone-qt-pdf/audit-anchor-geometry.py \
  /absolute/new/empty/output --output /absolute/new/empty/output/audit.json
```

SDK／helper／资源路径必须在 WebEngine 创建前规范为绝对路径并验证可读。首次诊断曾因相对资源路径崩溃，不能再以当前工作目录中“文件存在”作为运行时路径有效的证明。不注册 App、不用正常用户 profile、不启动主程序。审计只读，区分 PDF `/Dests` 字典的 NameObject 与 Names tree 的字符串，不把 `/` 语法误报为目标缺失。

九份 PDF 的生成不变量通过，仅说明测试跑完；七种定位错误被成功复现并不等于修复。源点击矩形与目标具名点也必须分开检查；CSS 页边距对照会改变页面尺寸／排版，不能直接代替产品方案。见 [第十九轮](../../docs/exploration-2026-10-18-phase19.md)。

## SVG／打印变换隔离实验

仅测试进程设置 `WIZNOTE_PDF_SVG_TARGET_PROTOTYPE=1`，在原三份输出外再生成五份：修改 XLink 地址编码、移动目标到 HTML 容器，以及补充无命名空间的 SVG2 `href`（保留编码／原始片段各一份）。每个变体都恢复原属性命名空间、值和目标，并核对原文、HTML、修改状态、容器几何。仅改地址拼写或容器不能视作完整修复。SVG2 使用无命名空间的 `href`，并优先于 XLink 版本，见 [W3C 链接规范](https://www.w3.org/TR/SVG2/linking.html#XLinkRefAttrs)。

生成器可加 `--plain-equation-label`，把合成公式编号从含冒号的 `eq:pdf-eq` 改为 `eq1`。`WIZNOTE_PDF_VIEWPORT_WIDTH` 可设置 400–1200 的初始测试窗口宽度；默认仍为 850，不在打印过程中改用户窗口。

坐标审计同时记录内容流矩阵，并对本夹具的科学样式背景框做**只读**比较。统一 A4 页面的首面打印原点／缩放假设能解释样本，不代表可用于任意 PDF。后续页流的平移含全篇分页偏移，不能再次加到已经页内化的目标上。不要把矩阵估值写回 PDF、用于混合纸型或真实标题变体的背景框补偿。阶段结论见 [第十七轮](../../docs/exploration-2026-10-17-phase17.md)。

## 受控打印与保存隔离候选

2026-10-18 的源码候选可用 `WIZNOTE_PDF_EVENT_LINKS=1` 测试。探针通过与主程序相同的 `WizPdfPageOperations` 调用原生 `printToPdf`，在隔离 world 显式准备／结束临时 SVG2 链接；不加此变量仍走原始对照。不要同时开启其他会增加打印次数的 SVG／锚点变体，当前事件计数验收为三次准备／三次恢复。

主程序所有笔记保存快照必须经同一页面的 FIFO，不能在打印时直接读取 HTML。`wiznote_pdf_export_lifecycle` 保留能读到临时属性的失败机制对照，另外检查实际生产保存脚本、取消输出后仍保持屏障、回调重入、销毁、恢复失败保护和重新载入。不能靠最终 HTML 恢复就推断并发保存安全。原生入口会确保当前文档的 helper，不能只相信上一文档的 DocumentReady 注入。

见 [第十八轮](../../docs/exploration-2026-10-18-phase18.md)。目的地存在不等于纵向坐标准确；这不是正式包验收，也没有 PDF 后处理。

## Qt 6.12 配套版本核对（历史对照）

2026-10-17 官方仓库同时保留旧 `extensions/qtwebengine/6120/clang_64`（7 月试验包）与新 `extensions/qtwebengine/6120/61400/clang_64`。现有 aqt 3.3.0 选到前者，不能直接作为新版 WebEngine 的对照。新路径的 `Updates.xml` 明确用 `Replaces` 替代旧包。

本次对照选择官方新路径中的包：

- 元数据：`https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/61400/clang_64/Updates.xml`
- 包目录：`extensions.qtwebengine.6120.61400.clang_64/`
- 归档：`6.12.0-0-202609281005qtwebengine-MacOS-MacOS_26-Clang-MacOS-MacOS_26-X86_64-ARM64.7z`
- 官方 `.sha1`：`6880cbacce9a8f59a0aa44b456972ffb0757e6cd`

须完成下载并核对官方校验值后，才可解包到**独立测试 SDK**；不要覆盖主开发 SDK 或 Applications。随后重新配置独立工程并核对实际运行版本，不以归档文件名前缀 `6.12.0` 代替 `qWebEngineVersion()`。更换 SDK 期间不运行链接该目录的测试进程。这里只需要 WebEngine，不安装元数据同列的 WebView 插件或调试符号。
