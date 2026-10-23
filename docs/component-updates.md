# 自用组件更新记录

更新：2026-10-23；分支`improve/component-updates-20261023`。

## 不变的边界

保持现有逻辑和使用习惯，不改菜单、工作流程、正文／加密格式、业务数据库
schema或同步规则。仅在项目临时目录用合成资料测试；不启动正常账号、不动
现用App或真实笔记。Touch ID不恢复，自建证书和VoiceOver专项不重启。

更新前重要回退包是14.4.25 build207，DMG SHA256：
`2d37edbdea0651cb893a52c5eab467e6f8587bdc2b74f7985b9c84f624b04e65`。
包和对应源码留在本机，受重要版本保留规则保护，不覆盖、不例行删除。
该安装包仍使用旧组件；本分支的新依赖不能被说成已经装入重要回退包。

## 已接入：SQLite 3.7.8 → 3.54.1（第211轮）

官方三个amalgamation文件按原始字节替换，发布页与SHA3见
[隔离验证说明](../tests/standalone-sqlite-upgrade/README.md)。库文件仍为公共领域，
原始声明保留。不改CppSQLite3、索引、缩略图实现或业务SQL。

仅对sqlite3.c明确指定原线程安全和1024字节默认页大小。已有文件保留自身页大小；
未改journal/synchronous/foreign_keys策略，未启用WAL、STRICT或新schema特性，
未执行迁移、VACUUM或真实库修复。业务schema版本仍为5。

两个各100条文档的合成业务库，六个新旧创建／读写／回读场景450项断言通过。
实际封装、索引查询、标签／附件／回收站／待同步、UTF-8／BLOB／64位／NULL、
事务／失败后可用性、缩略图文字及PNG像素、健康库dump/read逐表一致均有证据。
11张业务表的逻辑摘要六次一致，旧3.7.8可以回读本次合成库。

主程序增量构建通过，兼容测试和恢复开关继续OFF，非GUI启动检查通过。构建
仍有既有Qt废弃API／override等警告，未为组件更新扩展无关重构。没有重跑公式
或PDF套件，没有制作新的安装包，也不把构建或合成结果当作完整客户端验收。

测试代码初版的schema版本旁路不能证明业务升级路径；扩展版本的编译类型错误
和其后误跑旧程序的结果明确不计验收。现使用先构建、失败停止、输入／程序
摘要前后校验的独立运行器，最终450项来自成功新构建，原日志留证。

SQLite源文件比旧版更大，本次不声称体积缩小、查询必然变快、真实库或所有历史
数据库都兼容。真实日常规模、完整客户端／同步／全部加密组合仍未重新验收。

## 已接入：QuaZip Qt6适配1.3 → 1.7.2（第212轮）

上游固定提交的111份源文件和目录摘要核对后，更新原QuaZip子模块gitlink。
本地保留旧源码、Git HEAD／暂存条目和.git指针，不执行checkout/reset/stage。
后续git status触发子模块索引stat缓存刷新，索引文件摘要改变，但暂存条目无差异；
这项元数据变化留证，不恢复或重写用户索引以掩盖它。根仓库HEAD／索引仍保持。
原Qt6适配已核查，新上游覆盖所需支持；项目WizZip包装实现保持。

保持QTextCodec/Core5Compat和系统zlib，BZIP2与依赖自动下载OFF，不调用新的
压缩／提取／加密接口。原版正文包和附件流程不改，许可证和静态链接例外保留。
六个新旧交叉场景236项通过，核对中文名、HTML／Markdown、1MiB二进制、NUL
附件、空文件、QBuffer／磁盘读取、提取、普通失败后可用性及源归档不变。指定的
文件名／CRC／大小／method／flags／versionNeeded一致，不声称所有zip元数据相同。
见[关联验证说明](../tests/standalone-quazip-upgrade/README.md)。

主程序必要构建及非GUI启动通过，仍不访问正常账号或真实笔记，不重复公式／PDF。
初次测试版本报告为空，已修正为库目标VERSION；最终236项来自新成功构建，非
早期空版本记录。不是完整客户端、真实大库或全部加密组合验收，不承诺减小体积。

## 当前边界与下一步

SQLite和QuaZip的首批兼容更新均已源码接入，并在第214轮制作
[build214独立测试包](release-14.4.25-build214-components-test.md)。重要207包仍保留
旧组件作回退；214配套源码展开核对新QuaZip的111份源文件，不沿用旧归档冒充
更新后版本。安装包与源码仅留本机，不自动替换现用App或访问真实资料。

Qt含本项目WebEngine补丁，MathJax存在大版本API差异，均保留现有实现，另案做
独立候选及关联验证；不直接替换生产运行时或第三方编辑器。不会为了更新数量
强行改用新API、改变资料格式或扩大到真实资料测试。

## Qt 6.12候选暂不采用

第213轮核对的[Qt 6.12发布记录](https://www.qt.io/blog/qt-6.12-released)和安装器
列表已提供6.12.0基础组件，但不能据此推断WebEngine扩展也已正式发布。
当前[WebEngine源码元数据](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6120/src_doc_examples/Updates.xml)
的源码归档是`qtwebengine-everywhere-src-6.12.0-beta2.tar.xz`，版本构建号
`6.12.0-0-202607081027`。[macOS扩展元数据](https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/clang_64/Updates.xml)
也指向同一7月构建；安装器dry-run会把它与9月28日构建的6.12.0基础包放入同一
SDK。本轮不据此安装或拼接运行时。

已核对的[6.12.0正式源码目录](https://download.qt.io/official_releases/qt/6.12/6.12.0/submodules/)
列出41个tar.xz模块，但没有qtwebengine。GitHub镜像的v6.12.0标签和code.qt.io
对应源码路径返回404。这些结果限定于本次已查官方入口，不代表所有来源均不存在，
也不把不同日期本身当作二进制不兼容证明；明确的beta2源码才是当前候选不采用的
依据。

SDK／源码／二进制均未安装或下载，未尝试将原PDF目的地补丁和Metal最终纹理
所有权补丁移植到该beta候选。现有打包工具继续固定6.11.3、五个源码摘要和四个
底层Qt构建身份，不放宽版本检查。正式WebEngine源与匹配SDK可核对后，下一阶段
才做补丁适用性、独立构建和受影响PDF／内存路径的必要回归；不以Qt基础版发布
代替这项接入条件。
