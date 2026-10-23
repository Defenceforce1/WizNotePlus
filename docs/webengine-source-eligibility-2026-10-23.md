# WebEngine正式来源与独立升级门槛复核

日期：2026-10-23，第231轮。固定对照仍是build230；220重要回退、现用App、固定
测试资料和真实笔记不动。只核实官方来源和构建入口，不做安全测试或启动账号。

## 更正上一轮来源查找范围

230轮只检查6120上层与旧src_doc_examples，漏掉官方安装器的61400子目录。不能
因旧清单是beta2就推断没有新来源。本轮沿官方目录索引进入以下更深层入口，
直接读取Qt官方内容，不把搜索引擎或第三方镜像作为版本验收依据：

- [源索引](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6120/)
  明确链接61400；[新源码清单](https://download.qt.io/online/qtsdkrepository/all_os/extensions/qtwebengine/6120/61400/src_doc_examples/Updates.xml)
  提供extensions.qtwebengine.6120.61400.src，版本6.12.0-0-202609281005，归档
  名qtwebengine-everywhere-src-6.140.0.tar.xz，不带rc后缀。
- [新macOS清单](https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/61400/clang_64/Updates.xml)
  是同一构建的Universal包，提取目标明确为6.12.0/macos，并非当前Qt6.11.3的
  直接替换SDK。文件名／版本前缀不足以证明它适用于当前运行时。
- 关键的[主组件清单](https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6120/61400/main/Updates.xml)
  明确DisplayName为**Qt WebEngine6.140.0-rc**，源提交为
  `d505fc23640d2bd6345e6483eb73daf4e19f5829`。
- [Qt官方RC标签](https://code.qt.io/cgit/qt/qtwebengine.git/tag/?h=v6.140.0-rc)
  的tagged object正是同一提交。这不是仅凭一个404或归档名字作判断。

因此，此次确实找到了更完整的官方源码／macOS包组合，但已核实组合仍为RC，
**不满足用户要求的正式版前提**。不宣称所有可能来源均不存在正式版，也不把
“官方提供”与“正式发布”混为一谈。最初看到无后缀文件名后的初判已纠正。

## 独立升级是否有希望

该提交的[顶层构建文件](https://code.qt.io/cgit/qt/qtwebengine.git/plain/CMakeLists.txt?id=d505fc23640d2bd6345e6483eb73daf4e19f5829)
在入口要求Qt6.8及以上BuildInternals，Core及可选组件按实际BuildInternals版本
查找。故以现有Qt6.11.3重编WebEngine是值得继续评估的方向，不能直接认定必须
先替换整套Qt6.12。但这仅是构建入口条件；尚未配置或构建，不能宣称私有API、
编译器、PDF目的地或Metal最终纹理修复已经兼容。

官方预编译包指定Qt6.12.0，不能直接放进230的Qt6.11.3框架群。本轮不改旧SDK、
不放宽固定引擎打包器的版本／底层UUID检查，也不以CMake最小版本冒充ABI验收。
本地cmake.conf仍有通用alpha1开发字段；现有6.11.3源也有该字段，不单靠它判断
发布状态，采用主组件明确RC和官方tagged object相符的证据链。

## 本轮结果和现场

10项必要来源／构建入口／无候选检查通过，原始HTML/XML、请求状态及SHA256保留
本机phase231。不是10项客户端运行测试。正式版闸门未通过，未移植补丁、未构建
或制作231候选、未做新旧性能／内存A/B；没有可报告的升级收益百分比。

发现主组件RC后，按精确父子进程身份停止本任务下载。只留下约108MiB源码片段
和两个有限镜像字节范围样本，未校验成完整源、未解压、未执行，也未下载SDK。
这些本轮临时输入已整体移入废纸篓，可恢复；校验和／请求证据保留，没有删除
现有构建或回退。镜像均来自Qt官方mirrorlist，只用于普通来源下载，不是安全测试。
下载门控的初始src/rc子串误判、取消后null状态判断及CommonJS绑定名语法问题均
记录并修正；不完整归档仍被完整性闸门拒绝，不将失败尝试计为验收。

下一次有可核实的正式组合时，顺序仍为：保留基础Qt，独立配置／编译WebEngine，
验证PDF与Metal补丁；再从230复制新候选，使用相同合成资源比较加载、公式与
页面关闭后的实际内存，并验保存／切换与PDF双模式。只有条件满足才开始这些
运行验证，不采用RC绕过前提、不无限轮询。当前继续使用未变的230。
