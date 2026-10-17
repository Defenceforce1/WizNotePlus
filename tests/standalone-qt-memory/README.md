# 独立 Qt 网页内存复现

仅用于 macOS 开发诊断，不是笔记客户端或用户启动器。它有自己的 CMake 工程，只编译 `main.cpp` 并链接 Qt；不包含主 App 的源码、资源、桥接、账号或 profile 工厂。不得用于打开用户资料或替代正式 App。

## 构建与运行

需要现成的同一版本 Qt SDK，包含 Widgets、WebEngineWidgets、QuickWidgets 和配套 WebEngine helper／资源。不要混用另一个 App 的 helper 或框架，也不要为此复制、部署一整套 Qt。以下路径由开发者填写：

```sh
qt_memory_sdk=/absolute/path/to/Qt/macos
qt_memory_build=/absolute/path/to/a/new/build-directory
cmake -S tests/standalone-qt-memory -B "$qt_memory_build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$qt_memory_sdk"
cmake --build "$qt_memory_build"
QT_QPA_PLATFORM=cocoa \
QT_PLUGIN_PATH="$qt_memory_sdk/plugins" \
QTWEBENGINEPROCESS_PATH="$qt_memory_sdk/lib/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess" \
  "$qt_memory_build/standalone-qt-memory" --visible-cocoa 10
```

必须显式给出 `--visible-cocoa` 和 1–30 批；会显示 900×600 点合成窗口，完成后自行退出。默认后端实验应确保没有继承 `QT_QUICK_BACKEND`。软件对照只在这一个子进程设置 `QT_QUICK_BACKEND=software`，不要写入正式 App 配置。两组顺序运行，不同时争用可见窗口。

## 工作负载与检查

- 一个普通 QWidget 宿主、一个 QTabWidget，保留一页纯文字。
- 每批依次新建并选中四页，检查可见性、原文、无脚本／桥接，再关闭四页、回到保留页。
- 所有加载、脚本检查、批次和延迟均回到 QApplication 普通事件循环，没有内嵌 QEventLoop。不会清浏览器缓存、强制 GC／GPU 同步、改图形默认值或模拟用户笔记保存。
- QPointer 确认临时视图、页面和内部 Quick 窗口释放；十秒后再次测量，最后关闭保留页。URL 拦截器只允许 about／data／blob，不加载网站或远程页面资源。
- 临时 profile／cache 在私有临时目录；目录在 QApplication 完全析构后移除并明确记录。初版曾过早清理临时 profile，本轮已修正，不对用户目录做缓存清理。

`MEMORY_SAMPLE`／`MEMORY_PROBE` 是机器可读 JSON。只汇总本进程和它自己的后代，不读取其他应用内容；RSS、系统 footprint、内部／压缩／外部内存分别记录。后端记录取自实际 Quick 窗口的 rendererInterface，同时保留 scene graph 后端名和环境选择；静态 `QQuickWindow::graphicsApi()` 不能单独证明软件场景实际使用什么后端。

```sh
node tests/compat/summarize-editor-memory.cjs /absolute/path/to/probe.log
```

汇总器拒绝未完成、失败、缺少 profile 清理、批次不完整或关闭后仍有多余子进程的日志。不把平坦 RSS、对象销毁或某一次短测当作“没有泄漏”；也不把此程序的增长曲线等同于真实 App 的长期使用结果。

本机 Qt 6.11.3／macOS 27.0.1 的阶段结果见 [第十三轮记录](../../docs/exploration-2026-10-17-phase13.md)。这不是跨系统／跨 Qt 版本的结论。
