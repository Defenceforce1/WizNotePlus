# QuaZip更新的隔离关联验证

第212轮：原Qt6适配版1.3 → 官方1.7.2。发布说明：
<https://github.com/stachenov/quazip/releases/tag/v1.7.2>。
候选固定提交`3108956647d02547615d4b9aca71b0c517f6dda1`，树
`6b210b5b4ffd4dd298cc047a4dfaf78d6d73aba6`；官方归档SHA256：
`5240f4a3475648773ba10af8b0ebe549cbfc5d6de00470a517330a80068a4f78`。
111份源文件blob和完整目录摘要与上游固定树一致，许可证及静态链接例外保留。

项目仍用同一个QuaZip子模块，不复制另一套归档实现；远端gitlink更新为固定提交。
本地不checkout/reset/stage，保留原Git元数据和旧源码副本，工作文件按已验证上游
字节更新。原1.3的C++17、QLatin1Char、Qt zlib配置适配已检查，新上游覆盖所需
Qt6支持；本项目WizZip.cpp／WizZip.h未改。

保留原QTextCodec/Core5Compat编码后端和系统zlib；BZIP2与依赖自动下载OFF。
不使用新的QuaCompress/QuaExtract接口，不开启其加密或新压缩功能，也不改变
笔记自身加密、正文包格式、附件规则、菜单或工作流程。

测试实际WizZipFile/WizUnzipFile，链接生产路径／字符串辅助对象而非模拟归档器。
每个合成`.ziw`含HTML、中文嵌套资源名、1MiB二进制、含NUL附件、空文件和Markdown
正文（共5个条目；附件属于其中一个条目）。通过实际内存／磁盘压缩、文件和QBuffer
读取、单项／整包提取、HTML与资源枚举、源文件缺失、条目缺失、普通写入失败及
后续可用性，核对全部负载字节和源归档不变。

旧建→新读→旧回读、新建→旧读→新回读，六场景236项断言通过。文件名、CRC、
未压缩大小、method、flags和versionNeeded一致；不要求含当前时间的整个zip
逐字相同，不声称所有元数据／真实大包／全部加密组合或完整客户端已验收。
主程序构建和非GUI启动通过，正常账号和真实笔记未访问，未重新运行公式／PDF。
初次夹具虽执行通过但版本报告为空，不作最终证据；已从实际库目标VERSION取值。

同一Qt6工具链需要Core/Gui/Widgets/Network/Core5Compat、系统zlib及生产构建中的
`share/WizMisc.cpp.o`、`share/WizQtHelper.cpp.o`、`utils/WizMisc.cpp.o`。
分别配置两个独立构建目录，旧源码从本机保留副本提取，不覆盖更新后的工作树：

```sh
cmake -S tests/standalone-quazip-upgrade -B <old-build> -G Ninja \
  -DCMAKE_PREFIX_PATH=<matching-qt> -DWIZ_QUAZIP_SOURCE=<isolated-original-1.3> \
  -DWIZ_QUAZIP_SUPPORT_OBJECTS=<production-build>/src/CMakeFiles/WizNotePlus.dir \
  -DCMAKE_BUILD_TYPE=Release
cmake -S tests/standalone-quazip-upgrade -B <new-build> -G Ninja \
  -DCMAKE_PREFIX_PATH=<matching-qt> -DWIZ_QUAZIP_SOURCE=<repository>/src/libs/3rdparty/quazip \
  -DWIZ_QUAZIP_SUPPORT_OBJECTS=<production-build>/src/CMakeFiles/WizNotePlus.dir \
  -DCMAKE_BUILD_TYPE=Release
node tests/standalone-quazip-upgrade/run-cross.cjs <old-build> <new-build> <new-evidence-folder>
```

运行器先成功构建再测试，输入／程序摘要前后校验，避免陈旧程序冒充新结果。
只在全新证据目录中创建带标记的合成资料；日志包含本机路径，留在本机。
