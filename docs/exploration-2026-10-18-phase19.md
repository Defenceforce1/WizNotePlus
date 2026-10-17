# 第十九轮：剥离编辑器的 PDF 坐标复现

2026-10-18。只增加独立诊断工程、只读审计和记录；不修改产品实现、SDK／组件、图形默认值或导出路线，不生成或安装新的主 App，不改用户 PDF／账号／资料。主程序仍保留上一轮已备份的源码候选和现用版本。

## 本轮测试工具崩溃及修正

用户收到的崩溃报告指向本轮新建的 `anchor-geometry`，不是为知主程序。崩溃发生在创建 Qt 的临时 profile 时，早于任何笔记／PDF 操作，异常为 SIGTRAP；同时的工具日志明确记录内部扩展清单无法解析。

原因线索与修正：工具最初把相对 SDK 路径直接拼成 WebEngine helper／资源环境变量，当前工作目录中的存在检查虽然通过，但 WebEngine 初始化并未正确解析内部资源。统一为绝对路径后，九组导出全部完成；随后补充在 QApplication／profile 创建前验证 helper、资源包和平台插件的绝对路径及可读性。最终源码重建后再次完成九组；缺少 SDK 的对照明确返回状态 2，没有启动 WebEngine／崩溃。没有关闭系统保护或清理用户缓存。

首次编译缺少 `QWebEngineScriptCollection` 头文件、提前运行审计时尚无输出文件、审计误把 PDF NameObject 语法斜线当成目标名，均有失败记录并分别修正；不把这些开发工具错误算作主程序缺陷，也不丢弃失败证据。用户原始崩溃报告只留本机，不上传 GitHub。

## 真正独立的几何对照

`tests/standalone-qt-pdf/anchor-geometry.cpp` 仅依赖 Qt Widgets／WebEngine，不包含／链接任何 Wiz 文件，不加载 Markdown、MathJax、CSS 预设或主程序桥接。临时 off-the-record profile、零注册脚本、零远程请求；九份输出前后 HTML 完全一致。三页各一个纯色目标与普通内部链接，目标位置通过 PDF 实际填色路径核对，不拿字体墨迹或屏幕 DOM 坐标代替打印位置。

最终九种条件／27 个目标中：

| 条件 | 目标位置结果 |
| --- | --- |
| A4、窄正文、API 零边距 | 横纵偏差小于 0.00002 pt |
| A4、窄正文、API 5 mm | 所有目标向左／上偏约 14.25 pt |
| A4、窄正文、API 20 mm | 所有目标向左／上偏约 57 pt |
| A4、宽正文、API 5 mm | 三个目标纵向偏差约 −1.15、38.14、103.62 pt |
| A4、宽正文、非对称 API 边距 | 纵向偏差约 −16.75、25.25、95.25 pt |
| Letter、宽正文、API 5 mm | 纵向偏差约 −1.99、34.78、96.06 pt |
| A4 横向、宽正文、API 5 mm | 纵向仍偏约 −13.68 至 −9.10 pt |
| CSS 20 mm、API 零边距 | 本夹具的目标误差小于 0.00002 pt，但纸面尺寸改变 |
| CSS 命名混合纸型、API 5 mm | 三种实际纸高不同，目标仍偏约 −14.25 pt |

偏差正值表示目的地在实际目标下方，负值表示上方。九份中**七种条件存在超过 1 pt 的目标误差**，但全部 27 个目标页码正确，且**全部 27 个源链接点击矩形都落在实际绘制的导航条内**。因此不只是所有 PDF 元数据或整个画布一起位移，也不是只有数学环境的标题位置不对。

CSS 命名页实际产生约 594.96×841.92、594.96×420、612×792 pt 三种页面；不能把全篇视为统一 A4。CSS 边距对照的纸高约 841.92 pt，原 API 路线约 840.96 pt，说明“坐标更准”仍不等于原纸面和分页兼容。没有将此对照加入生产。

只读审计严格按 PDF 名类型匹配目标，并从实际填色块取顶边／左边；颜色／目标不唯一、目标缺失或链接不支持时主动失败。它不是通用 PDF 修复器，不输出修改后的 PDF，也没有把当前矩阵假设写回文件。

## 与原生源码的连接线索

检查 Qt 的 Chromium 140 分支和对应上游实现，记录到以下连接；这是源码线索，不是已验证的补丁：

- [`PrintContext::CollectLinkedDestinations`／`OutputLinkedDestinations`](https://github.com/qt/qtwebengine-chromium/blob/bcc09a68b50a40c60d8dc83f0e6b6dc872614812/chromium/third_party/blink/renderer/core/page/print_context.cc#L156)：扫描无命名空间的 href、取目标 layout object 的绝对包围框原点，再减去页矩形原点，单独输出具名位置。这也与上一轮必须给 SVG 补无命名空间 href 的观察相符。
- [`ChromePrintContext::SpoolPage`](https://github.com/qt/qtwebengine-chromium/blob/bcc09a68b50a40c60d8dc83f0e6b6dc872614812/chromium/third_party/blink/renderer/core/frame/web_local_frame_impl.cc#L446)：先打印正文，再用根 layout view 的属性树状态写具名目标。需要进一步确认分页 margin／缩放变换在这两条路径中是否一致。
- [`LocalFrameView::PrintPage`](https://github.com/qt/qtwebengine-chromium/blob/bcc09a68b50a40c60d8dc83f0e6b6dc872614812/chromium/third_party/blink/renderer/core/frame/local_frame_view.cc#L4002)：正文按当前分页状态绘制。原生小程序仍能复现，缩小了应检查的层次。

所读 Qt 分支固定到提交，不能据此证明下载的 SDK 二进制恰好由该提交构建；报告也没有定位并验证某一行引擎修复。Skia 接收到的画布变换、分页属性树和打印路径仍需联合追踪，不能笼统称它“不支持精确坐标”。

## 验证与未完成

最终独立工具九组导出及 HTML／隔离不变量、只读 27 个目标和源链接审计完成，数学库／主程序不是该复现的必要条件。抽查渲染页面确认颜色块与标记正常；没有依赖屏幕截图反推 PDF 内部位置。

上一轮产品源码未改，重跑 **47 项 PDF 生命周期检查通过**；本轮没有再跑完整 665 项，不能借用旧数字当新验收。现用 App 与既有候选／DMG／源码归档校验不变，14.4.16 完整回退清单／签名复核通过。

现在应优先追踪原生目标与正文的打印变换，必要时在隔离 SDK 中做可验证的小范围引擎补丁，而不是继续给定理标题加固定偏移。还没有构建或采用这样的补丁；定理／定义及公式的精确落点、原生窗口整体验收、Metal footprint 问题都仍未完成。本轮不发布新正式版，也不替换用户安装。

源码、脱敏说明仅经 GitHub 插件备份开发分支；完整输出、初始崩溃／失败日志、用户附件和本地证据摘要不上传。
