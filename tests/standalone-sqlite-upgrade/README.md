# SQLite组件更新：隔离兼容验证

更新分支：`improve/component-updates-20261023`，起点为209轮已同步源码记录。
重要回退程序为14.4.25 build207；208／209仅增加诊断，不虚增生产版本。

保持现有逻辑与使用习惯是硬约束：不更改菜单、操作流程、正文格式、数据库业务
schema、同步规则、加密格式及密码解锁；不引入云端迁移，不动现用App或真实资料。
必须改变行为的依赖更新单列说明，不默默采用。每次只推进一个底层组件。

第一阶段官方SQLite3.54.1源文件SHA3-256与官方记录一致：
`e2b869d3003b33c942b7dd7063604fbc53553e1b66609570df39b9b7b558ee81`。
来源：<https://www.sqlite.org/releaselog/3_54_1.html>，归档
<https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip>。

`compat.c`是可单独链接旧3.7.8或新3.54.1的合成C接口夹具，数据库路径必须由
隔离测试运行器提供。仅在任务临时目录创建两个合成库，各1000条，检查UTF-8
标题、含NUL的BLOB、事务提交／回滚、schema版本及integrity_check。顺序为旧
建→新读写→旧回读，以及新建→旧读写→新回读；六个场景均通过，不改变journal
模式或业务逻辑。不是对真实笔记、完整App或所有数据库格式兼容的验收。

## 第二阶段：真实封装与索引

第211轮已将生产`src/database/sqlite3.c`、`sqlite3.h`、`sqlite3ext.h`替换为官方
3.54.1原始字节。仅对该C源文件显式保留`SQLITE_THREADSAFE=1`和原默认页大小
`SQLITE_DEFAULT_PAGE_SIZE=1024`；已有文件沿用自身页大小。不改业务SQL／schema
版本、journal／外键策略、同步规则、笔记正文／加密格式，也不执行真实库迁移。

`wrapper.cpp`链接未改动的CppSQLite3、WizIndexBase、WizThumbIndex与生产辅助
对象，只隔离资源路径、临时路径、日志及schema版本元数据接线。它使用项目建表
脚本和既有0至5版本升级路径，在两个各100条文档的合成库验证标签、附件、
回收站、待同步查询、UTF-8／引号、64位／NULL、预备语句、BLOB、事务回滚、
重复键与忙失败后的可用性、原默认值、完整性，以及缩略图文字／PNG像素。
既有dump/read路径只对健康合成库另存新库并逐表比较，不是损坏库恢复验收。

六个旧建／新建交叉场景450项断言通过，11张业务表逻辑摘要一致且旧3.7.8能
回读；主程序增量构建和非GUI启动检查通过。没有运行正常账号或读改真实资料，
不把这些结果称为完整客户端、真实大库、所有加密组合或长期验收。

独立配置需要Qt 6 Core/Gui/Widgets/Network/Core5Compat、CMake/Ninja，以及同一
工具链生产构建的`share/WizMisc.cpp.o`、`WizQtHelper.cpp.o`、`WizObject.cpp.o`。
旧引擎从重要回退源码独立提取，不能覆盖工作树；下列路径均由本机调用者指定：

```sh
cmake -S tests/standalone-sqlite-upgrade -B <standalone-build> -G Ninja \
  -DCMAKE_PREFIX_PATH=<matching-qt> \
  -DWIZ_SQLITE_OLD_DIR=<isolated-3.7.8-source> \
  -DWIZ_SQLITE_NEW_DIR=<repository>/src/database \
  -DWIZ_SQLITE_SUPPORT_OBJECTS=<production-build>/src/CMakeFiles/WizNotePlus.dir \
  -DCMAKE_BUILD_TYPE=Release
node tests/standalone-sqlite-upgrade/run-cross.cjs \
  <standalone-build> <repository>/share <new-evidence-folder>
```

运行器先构建，失败立即停止，不使用旧程序冒充新结果；运行前后核对测试输入和
程序摘要，只在新证据目录内建合成库并写入隔离标记。日志保留本机，不上传原始
绝对路径。后续单独评估QuaZip；Qt/WebEngine和MathJax不与本阶段混合，不重复
已通过的公式测试，也不承诺更新必然缩小体积或提高查询速度。
