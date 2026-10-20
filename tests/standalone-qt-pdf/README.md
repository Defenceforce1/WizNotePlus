# 独立 Qt PDF 版本对照

## 第 119 轮：独立纸宽页面（A4）

A4 模式现在只读快照原页面到私有离屏输出页，按既有纸型／物理边距重新排已识别的数学。
原页面视口、源文、HTML／修改状态保持；电子阅读路径不变。沿用现有受控打印／SVG 编号
和原子提交，不更换数学库版本。宿主只传随包 MathJax；本地资源不完整／未知交互媒体／
最终固定宽度溢出不静默降级成缺内容或整页缩小。不可拆分的显示公式可以局部缩放，
正文和独立编号不随之缩小。不存在每个公式都维持原字号的承诺。

`wiznote_pdf_paper_page` 有 41 项无账号离屏检查，含实际 A4 原子保存、取消／销毁／缺资源
保留旧文件、canvas／惰性本地图及公式原输入／字面美元文本。页面准备不重放笔记脚本。
探针显式 `WIZNOTE_PDF_PAGE_MODE=a4` 调用产品独立页面；源页面 print 事件应为 0，
阅读模式原事件准备／恢复仍为 3，不以源页面事件缺失误报 A4 未打印。

新增 `--paper-assets-base` 固定夹具和只读 `audit-pdf-paper-assets.py`，验证六份 A4 图表／
代码／长式输出的精确 GoTo、普通 12 pt、表格单元正文、图像比例和编号墨迹 <1 pt。
基础两模式继续用 `audit-pdf-page-modes.py`。multline 审计对 A4／20mm 已知长行允许自然
换至三行，不放宽字形／编号间隔／纸边／落点要求。提供文本审计支持正常字号的新跨页块，
同时复核旧九份 PDF 通过；不修改用户源输入或把标题诊断当作忠实导出。

第 119 轮最终 42 PDF 分为 28 忠实／14 标题诊断，结合 17 组 723 项产品回归验收通过，
见阶段报告和 `final-evidence.json`。下方第 117／118 轮 A4 待修／失败结论仅为历史状态，
不覆盖本轮结果。任意宏／CSS、全部附件、真实账号和 iPhone 仍未全面验收。没有打包安装。

## 第 118 轮：独立导出命令（取代模式记忆）

依用户确认，两个 PDF 子菜单不再有勾选，也不记住上次模式。`attachMenu` 的回调直接携带
本次 `Mode`，经 `WizMainWindow::saveAsPDF(Mode)` 转给当前编辑器，之后按值传入异步打印。
不读写 `PDFExport/PageMode`；开发期遗留设置只忽略，不删除真实配置。原有无参数 action
程序入口固定采用电子阅读，不继承前一次 A4 选择；界面正常使用两个独立子命令。

菜单专项改为 20 项：旧 A4 偏好不影响阅读命令、交替分派且无重复触发、重开／重建无勾选、
设置字节不变、禁用／销毁、嵌套命令不改外层模式，以及无效接入拒绝。第 117 轮以下记忆／
单选描述只保留为历史；纸宽算法、PDF helper 与尚未通过的 A4 正常字号门槛均未改动。

## 第 117 轮：PDF 页面宽度二级菜单

产品沿用 `actionSaveAsPDF`，在 `WizPdfExportMenu.h` 附加两个互斥子项，不另加模式弹窗。
`wiznote_pdf_export_menu` 在临时设置中验证分派、记忆、禁用、重入安装和对象销毁；
`wiznote_pdf_file_safety` 验证 CSS 像素／物理页边距的宽度换算，生命周期专项补零尺寸及取消。
正文宽度取当前笔记的 `window.innerWidth`，不是窗口／侧栏宽度或物理像素；先通过与保存共用
的队列只读测量，再受控打印。默认阅读宽度，页高暂保持 A4 长度，不能称为连续无限长页面。

探针新增显式 `WIZNOTE_PDF_PAGE_MODE=reading|a4`，调用实际产品 `printWithMode`；不设此值时
保留历史调用路径。使用合成 `--reader-equations-base`，分别在 850／1200 视口输出到已有的
`reading-850`、`reading-1200`、`a4-850`、`a4-1200` 空目录。随后可执行：

```sh
python3 tests/compat/audit-pdf-page-modes.py /absolute/parent-of-four-output-directories \
  --poppler /absolute/pdftoppm --output /absolute/new-audit.json
```

该检查要求已知夹具的精确链接、页宽、字号及两个编号／目标误差小于 1 pt。第 117 轮电子阅读
六份输出保持正常 12 pt；A4 的既有整页缩小尚未修复，完整双模式检查故意保留失败，不能
调低字号门槛冒称通过。下一步先固定纸张的可用宽度，再在独立输出页面重排数学，不改原笔记。

## 通用探针

开发诊断工程，不是用户客户端。只编译现有 PDF 探针和文件保存 helper，不启动主 App、不读取账号／笔记库、不注册或安装 App。接受本地合成 `native-pdf-fixture.html`，或通过下述显式入口接受用户授权的 Markdown 文本副本；离线 profile 与 cache 随测试结束销毁。

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

macOS 下还在创建 QApplication 前核对实际加载的 QtCore／Gui／Widgets／Quick／QuickWidgets／WebEngineCore／WebEngineWidgets 路径，必须与输入 SDK 的对应框架规范路径一致；同版本号不能代替路径核验。混用其他 SDK 明确退出 2，不启动 WebEngine。自编译运行时若使用隔离覆盖目录，依赖框架的链接也必须指向匹配的底层 SDK；不要把旧 Core 当成新补丁结果。私有生成清单记录这些路径，不能上传含本机信息的原始清单。

九份 PDF 的生成不变量通过，仅说明测试跑完；七种定位错误被成功复现并不等于修复。源点击矩形与目标具名点也必须分开检查；CSS 页边距对照会改变页面尺寸／排版，不能直接代替产品方案。见 [第十九轮](../../docs/exploration-2026-10-18-phase19.md)。

候选引擎必须用 `audit-anchor-geometry.py OUTPUT --require-accurate --output NEW_AUDIT.json` 做全部九组验收；定位／页码或源点击矩形左边及垂直中心对齐失败则返回非零。`--cases` 只用于正负对照，输出明确标记 `subsetOnly`，子集不能接受引擎。现有原版完整验收应失败，不能为得到绿灯而放宽容差。待编译的精确源码补丁及要求见 [engine-patches](engine-patches/README.md) 和 [第二十轮](../../docs/exploration-2026-10-18-phase20.md)；它还不是已验证修复。

验收工具本身可用保留的**未修补九组语料**做 18 项正负检查，不启动 WebEngine、不生成或改写 PDF：

```sh
node tests/standalone-qt-pdf/audit-tool-regression.cjs \
  /absolute/python/with-pypdf-pdfplumber \
  /absolute/unpatched/geometry-output /absolute/new/check-report.json
```

完整原版应检出七组偏移，准确子集的 `acceptancePassed` 可为真但 `engineAccepted` 必须为假。工具还拒绝不完整／重复清单、隔离失败和缺失 PDF，不替换已有报告，最后核对九份 PDF 与输入清单均未变化。只显式完整验收成功才能得到 `engineAccepted: true`；回归工具通过也不代表引擎修复通过。

## SVG／打印变换隔离实验

### 2026-10-18 实际引擎候选验证

隔离原版和补丁引擎已成功编译，严格九组坐标验收通过且 27 页同图；旧阅读器另有九份 PDF／30 页同图与十二个块顶验证，见 [第二十三轮](../../docs/exploration-2026-10-18-phase23.md)。完整主程序回归亦在候选运行时重新通过 665 项，不是原 SDK 的旧结果。未安装或修改正式组件，Blink 上游单测未执行。

旧阅读器探针现在也在创建 QApplication 前核对七个实际加载框架路径。三套旧阅读器输出（普通、边界、普通 eq1）可按如下只读验收；源码／HTML／修改状态、恢复次数与精确链接均须通过。只对原有块 ID 的两种页边距比较彩色块顶，不把可见标题诊断变体误当块顶目标，不用矩阵估算补偿结果。

```sh
python3 tests/standalone-qt-pdf/audit-reader-geometry.py \
  /absolute/new/normal /absolute/new/edge /absolute/new/plain \
  --output /absolute/new/reader-audit.json
```

原版的十二个块顶负对照应全部检出偏差；候选通过不等于已证明公式目的地的精确位置或任意 HTML 排版兼容。

### 公式编号的独立精度检查

仅合成探针额外设置 `WIZNOTE_PDF_EQUATION_GEOMETRY=1`，在编号 SVG 组原有 bbox 中画标记矩形；要求插入前后目标边界完全不变，并在导出后删除标记、核对原文／HTML／修改状态。须同时启用受控 `WIZNOTE_PDF_EVENT_LINKS=1`，不混入其他打印次数／补偿变体。它不是产品效果，也不修改已导出的 PDF。

```sh
python3 tests/standalone-qt-pdf/audit-equation-geometry.py \
  /absolute/new/marked-output --require-accurate \
  --output /absolute/new/equation-audit.json
python3 tests/standalone-qt-pdf/audit-equation-regression.py \
  /absolute/original/marked-output /absolute/candidate/marked-output \
  /absolute/new/auditor-controls.json
```

只读比较实际编号矩形与精确具名 GoTo 目的地，要求正确页且横纵误差均不超过 1 pt。不能用链接存在／解码后相似冒充正确目的地。第二十五轮普通／边界／eq1 九份候选通过，原引擎普通样本纵向偏差约 34.3 pt；十五项审计器正负对照保持原始语料不变。带色页与正常 57 页同图对照分别记录，不声称任意公式都已覆盖。

仅测试进程设置 `WIZNOTE_PDF_SVG_TARGET_PROTOTYPE=1`，在原三份输出外再生成五份：修改 XLink 地址编码、移动目标到 HTML 容器，以及补充无命名空间的 SVG2 `href`（保留编码／原始片段各一份）。每个变体都恢复原属性命名空间、值和目标，并核对原文、HTML、修改状态、容器几何。仅改地址拼写或容器不能视作完整修复。SVG2 使用无命名空间的 `href`，并优先于 XLink 版本，见 [W3C 链接规范](https://www.w3.org/TR/SVG2/linking.html#XLinkRefAttrs)。

生成器可加 `--plain-equation-label`，把合成公式编号从含冒号的 `eq:pdf-eq` 改为 `eq1`；也可加 `--base-equation-label`，在使用 `eq1` 的同时加入与真实阅读页一致的 `<base>` 元素，用来覆盖 MathJax 生成同文档绝对引用的情况。`WIZNOTE_PDF_VIEWPORT_WIDTH` 可设置 400–1200 的初始测试窗口宽度；默认仍为 850，不在打印过程中改用户窗口。

坐标审计同时记录内容流矩阵，并对本夹具的科学样式背景框做**只读**比较。统一 A4 页面的首面打印原点／缩放假设能解释样本，不代表可用于任意 PDF。后续页流的平移含全篇分页偏移，不能再次加到已经页内化的目标上。不要把矩阵估值写回 PDF、用于混合纸型或真实标题变体的背景框补偿。阶段结论见 [第十七轮](../../docs/exploration-2026-10-17-phase17.md)。

## 受控打印与保存隔离候选

2026-10-18 的源码候选可用 `WIZNOTE_PDF_EVENT_LINKS=1` 测试。探针通过与主程序相同的 `WizPdfPageOperations` 调用原生 `printToPdf`，在隔离 world 显式准备／结束临时 SVG2 链接；不加此变量仍走原始对照。不要同时开启其他会增加打印次数的 SVG／锚点变体，当前事件计数验收为三次准备／三次恢复。

主程序所有笔记保存快照必须经同一页面的 FIFO，不能在打印时直接读取 HTML。`wiznote_pdf_export_lifecycle` 保留能读到临时属性的失败机制对照，另外检查实际生产保存脚本、取消输出后仍保持屏障、回调重入、销毁、恢复失败保护和重新载入。不能靠最终 HTML 恢复就推断并发保存安全。原生入口会确保当前文档的 helper，不能只相信上一文档的 DocumentReady 注入。

第 97 轮补充真正的编辑／阅读往返：`WIZNOTE_PDF_READER_CYCLES=1..5` 会先调用原编辑器的
`on`／`off`，再执行三种 PDF 输出。可用新夹具 `--reader-equations-base` 同时覆盖两条 equation、
裸 eqref、定理、例及实际默认 CSS。原阅读器的 `document.open/write` 保留 window 全局对象，却清除
窗口监听器；因此“对象已存在”不能证明打印监听仍在。受控打印现在每次以同一函数身份重新注册，
既修复清除后的监听，也不重复已有监听。旧代码两次往返后三份 PDF 均缺两个公式目的地，准备／恢复
均为零；新代码应每份一次且精确恢复。不要只验证首次打开后的 PDF。

第 97 轮容器输出目视发现宽 `multline` 的末行正文仍可能位于纸张右边界之外；第 99 轮继续
修复这个独立问题。不能把链接存在当作完整排版验收。

### 第 99 轮 multline 逐行适配

`--multline-fit-base`、`--multline-fit-star-base`、`--multline-fit-long-base`、
`--multline-fit-contained-base`、`--multline-fit-ref-base` 生成红／绿／蓝三色合成公式，分别覆盖
普通、星号、真实长行和五位编号、引用块／列表、公式内引用。每份包含可提取的 `Last row:`
结束标记；自动检查必须排除标记后的引用，不能拿引用中同色的编号冒充末行正文。

当前产品 helper 只在已武装、真实、只读打印事件内为已知 MathJax 2 multline SVG 结构建立
临时逐行布局；不改变 TeX、原数学引擎或打印窗口宽度。正常短行不额外缩小，过宽行单独
适配。星号和引擎内部折行可能省略列 wrapper，两种已知形状都须覆盖；显式 shove 和未知
结构保守回退。`lastFitted` 计数不能代替渲染检查。

```sh
python3 tests/compat/check-pdf-multline-fit.py /absolute/new/base-result \
  --output /absolute/new/base-audit.json
python3 tests/compat/check-pdf-multline-fit.py /absolute/new/star-result \
  --star --output /absolute/new/star-audit.json
python3 tests/compat/check-pdf-multline-fit.py /absolute/new/long-result \
  --long-first-row --output /absolute/new/long-audit.json
python3 tests/compat/check-pdf-multline-fit.py /absolute/new/container-result \
  --displays 2 --output /absolute/new/container-audit.json
python3 tests/compat/check-pdf-multline-fit.py /absolute/new/ref-result \
  --inline-reference --output /absolute/new/ref-audit.json
```

检查器只读解析并用 Poppler 渲染临时位图：严格核对实际名称的 GoTo（不通过 URL 解码或
删除真实字符串前导斜杠来放宽），核对三色行真正可见、正常行相对正文的字形高度、可见编号
与末行不重叠、目的地与可见编号纵向误差不超过 1 pt，以及原文／HTML／修改状态和打印恢复。
既有其他公式可能仍触发整页缩放；这里比较行与同页正文的比例，不宣称解决任意绝对字号。
旧裁切及整式缩小对照必须失败。高分辨率位图和绿色抗锯齿容差用于避免小字号采样造成假失败。

`WIZNOTE_PDF_MULTLINE_PROTOTYPE` 仅用于显式诊断，结果记录 `multlinePrototype`；开启时不得
作为产品验收。正式探针记录夹具预期的 `expectedFitted`，覆盖真实编辑／阅读往返后连续三次
打印。生命周期专项另覆盖取消、排队保存及 frame style 恢复失败后拒绝保存，真实 mac 桥仍需
单独验收。无关公式／定理页应在相同视口与分辨率下和已有通过输出逐像素比较。

真实 mac 桥的打印排版夹具须在 `QT_QPA_PLATFORM=offscreen` 下显式显示固定大小的隔离 view，
不激活前台 Cocoa 窗口。未显示 view 的页面 `innerWidth` 和公式容器宽度均为零，产品按规则
不猜测宽度而跳过适配；不能拿这种隐藏页面的打印作为正常阅读排版验收。
`WIZNOTE_NATIVE_PRINT_ONLY=1` 可独立运行原生往返打印；额外设置
`WIZNOTE_NATIVE_PRINT_HIDDEN_CONTROL=1` 会复现零宽度负对照，严格适配断言应失败。
对照用于证明夹具前提，不放宽产品保护或修改真实用户窗口。

见 [第十八轮](../../docs/exploration-2026-10-18-phase18.md)。目的地存在不等于纵向坐标准确；这不是正式包验收，也没有 PDF 后处理。

## 第 115 轮：显式提供的三个独立片段

只有用户明确提供、有权处理的副本才能加 `--provided-markdown ABS_MD EXPECTED_JAX EXPECTED_BLOCKS`。
生成器要求绝对路径、普通文件且不超过 1 MiB，原始文本不做 NBSP 等预处理。正式导出的前两份
不再加合成控制标题／链接，第三份可见标题仍明确只是诊断，不能当作忠实导出。

```sh
node tests/compat/prepare-pdf-reference-fixture.cjs \
  /absolute/source/root /absolute/new/output/native-pdf-fixture.html \
  --provided-markdown /absolute/private/case2.md 6 2
DYLD_FRAMEWORK_PATH=/absolute/verified/matching/runtime/lib \
  QT_QPA_PLATFORM=offscreen WIZNOTE_PDF_EVENT_LINKS=1 \
  WIZNOTE_PDF_READER_CYCLES=2 WIZNOTE_PDF_VIEWPORT_WIDTH=1200 \
  /absolute/build-directory/native-pdf /absolute/verified/matching/runtime \
  /absolute/new/output/native-pdf-fixture.html /absolute/new/output/result
python3 tests/compat/inspect-pdf-reference-integrity.py /absolute/new/output/result
python3 tests/compat/audit-provided-markdown-pdf.py /absolute/new/output/result \
  --case 2 --output /absolute/new/output/paint-audit.json
```

运行时必须核对七个实际加载组件，而非只传入新目录；macOS 忘记设置动态框架搜索路径可能仍加载
原 SDK。第 115 轮使用第 99 轮相同的已验证隔离 runtime，没有修改、部署或下载任何组件。
三个私有片段的 MathJax／块／引用预期分别为 2/0/0、6/2/4、15/6/7；三例分别运行，两种视口均验。
新审计器只适用这三个已知小样本：兼容 `/Dest` 和 `/A /GoTo`，核对精确名称／数量、右侧编号墨迹
及 1 pt 目标误差。显式移到新增标题的 HTML 目标不再与旧块顶比较；两种正常边距仍严格比较块顶。
它不是任意 TeX／多页布局／附件／完整库验收工具。

`WIZNOTE_PRESENTATION_COPY_DIR=/absolute/private/directory` 仅使离屏的
`--note-presentation-self-test` 改测该目录下 `case1.md`／`case2.md`／`case3.md`，通过原保存器和
临时真实数据库保存重开，核对精确可编辑 HTML 正文。未设置开关时原 193 项路线不变。
文本 getter 在原版和兼容入口都可能把 NBSP 映射成普通空格；因此保留 raw input 比较结果，
另核对初始化后的 getter 及实际保存／重开的可编辑正文，不能修改输入来取得通过。

`WIZNOTE_FIXTURE_STOCK_READER=1`、`WIZNOTE_PDF_SOURCE_DIAGNOSTIC=1` 只供 getter 对照；
`WIZNOTE_PDF_GLYPH_PROTOTYPE=1` 对应明确撤回的独立诊断原型，未链接到 App，不可作为产品验收。
第 115 轮曾因批量预览误判编号缺失，但原图／原型 PNG 字节及像素完全相同，所有产品候选已撤回。
详见 [第 115 轮报告](../../docs/exploration-2026-10-20-phase115.md)。

## Qt 6.12 配套版本核对（历史对照）

2026-10-17 官方仓库同时保留旧 `extensions/qtwebengine/6120/clang_64`（7 月试验包）与新 `extensions/qtwebengine/6120/61400/clang_64`。现有 aqt 3.3.0 选到前者，不能直接作为新版 WebEngine 的对照。新路径的 `Updates.xml` 明确用 `Replaces` 替代旧包。

本次对照选择官方新路径中的包：

- 元数据：`https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/61400/clang_64/Updates.xml`
- 包目录：`extensions.qtwebengine.6120.61400.clang_64/`
- 归档：`6.12.0-0-202609281005qtwebengine-MacOS-MacOS_26-Clang-MacOS-MacOS_26-X86_64-ARM64.7z`
- 官方 `.sha1`：`6880cbacce9a8f59a0aa44b456972ffb0757e6cd`

须完成下载并核对官方校验值后，才可解包到**独立测试 SDK**；不要覆盖主开发 SDK 或 Applications。随后重新配置独立工程并核对实际运行版本，不以归档文件名前缀 `6.12.0` 代替 `qWebEngineVersion()`。更换 SDK 期间不运行链接该目录的测试进程。这里只需要 WebEngine，不安装元数据同列的 WebView 插件或调试符号。
