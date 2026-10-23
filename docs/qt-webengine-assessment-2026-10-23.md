# Qt／WebEngine下一阶段评估

日期：2026-10-23，第230轮；只查正式公开资料和安装器元数据，不下载／安装SDK、
源码归档或编译候选，不替换230及现用App的Qt6.11.3，不做网络／安全测试。

## 能确认的升级价值

[Qt6.12官方发布说明](https://www.qt.io/blog/qt-6.12-released)已经宣布基础框架
正式发布，包含图形和模型等更新。但CanvasPainter、QML热重载等新功能不会仅
靠换库自动加速当前Widgets／WebEngine笔记界面，本轮不为使用新API重写交互。

[官方WebEngine独立发布说明](https://www.qt.io/blog/qt-webengine-will-release-separate)
明确：6.12不提供名为WebEngine6.12的二进制，独立产品改用6.140等版本号；首代
仍基于Chromium140，只增加功能／更新，不是一次Chromium大版本跳跃。独立发布
有利后续维护节奏，这是主要潜在价值，不是已经测得的渲染／内存收益。

## 本轮入口核实

- [正式发布根目录](https://download.qt.io/official_releases/)读取成功，当前未列
  qtwebengine独立目录；不能把这一入口未列出当成所有来源均不存在。
- [WebEngine开发目录](https://download.qt.io/development_releases/qtwebengine/)
  仍列6.140.0-rc、beta2、beta1，不作为日常正式候选。
- [源安装器索引](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/)
  当前仍列6120等既有入口；对6120的源码／macOS Updates.xml均读取成功。
- [源码清单](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6120/src_doc_examples/Updates.xml)
  仍为6.12.0-0-202607081027，源码归档明确名为6.12.0-beta2.tar.xz；
  [macOS清单](https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/clang_64/Updates.xml)
  同为该构建，不能以基础Qt6.12正式发布冒充扩展也已验证正式。
- GitHub官方镜像v6.140.0单个标签入口返回404，仅记录该入口结果，不据此推断
  全部发布状态。文档入口还重定向到6.140分支snapshot，亦不等同正式二进制证明。

请求状态／元数据字节摘要和五个包记录保存在本机phase230/qt-assessment.json。
本轮未做旧安装器dry-run复跑，未下载RC／beta归档，也未改变固定Qt工具链。

## 迁移成本与决定

必须核实正式源／匹配macOS arm64 SDK及可重现身份，再分别评估既有PDF目的地、
打印恢复和Metal最终纹理所有权补丁；FramelessHelper还使用Qt私有Widgets／Gui
接口，基础框架更换也有编译／窗口行为成本。不能只混放WebEngine.framework，
也不能为了构建通过丢掉现有修复或关闭系统保护。

新WebEngine若能以当前Qt6.11.3构建，可另评估保留基础Qt的窄迁移；这是官方支持
旧Qt构建的方向，不是已确认本项目6.140组合兼容。通过身份和补丁门槛后，必要
验证限于编辑／保存／切换、精确PDF目标与两模式正文、共享profile关闭后内存；
不重跑SQLite／归档等无关全套，不访问真实笔记。

**决定：评估完成，本轮不升级运行时。** 已核查入口尚不足确认正式源和匹配SDK，
没有可诚实给出的新Qt速度、内存或体积收益数字。先使用230整合候选；未来有可
核实正式组合时才开始独立迁移，不持续下载beta、不无限扩大检查或自动轮询。
