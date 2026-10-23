# JsonCpp更新：本地关联验证

组件分支把JsonCpp 1.8.0更新为官方1.9.8，固定提交
`8519b8381f3c741ad1421f88237b1deda0b11412`，源码树
`b2b54358d63493c3ea3d3e3c32719e8172c7035a`。
[官方发布记录](https://github.com/open-source-parsers/jsoncpp/releases/tag/1.9.8)，
[源码归档](https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.8.tar.gz)。
下载归档SHA256：
`51828cf3574281d2b79ec2a1c56a9e4c20cc1103711321ea96384cffb8d2d904`。
263份源码按Git blob／完整Merkle树核对；两个devtools Windows样例仅在验证
Git blob时按上游.gitattributes转换CRLF，不改原始归档或生成输入。

使用上游`amalgamate.py`生成现有三个文件，不改官方代码或改用新的业务API。
生成源／主头／前向头SHA256依次为：

- `fef22501f7be036556abe04c12ce6bd38e92135449e7dd5e505687dfab772e62`
- `dd534a7d4cd8b4165b94d09f893d02fd011e3c55d9d97b3c24f5b27371ff45a6`
- `c11b43c29e5d9e99aba3f7a99774c4d29458367463ebff9148dffc29b06d97eb`

MIT／public-domain原声明随三个文件保留。旧1.8.0从更新前本地检查点独立复制，
不能覆盖工作树；重要207和测试214安装包／源码不动。

## 验证范围和已知差异

新旧库分别重新编译未改动的生产`WizObject.cpp`和`WizRequest.cpp`，覆盖文档／
标签字段与时间、保护标记、实际标准响应成功／普通失败解析。仅日志替代；
未调用transport、登录、数据库、正常账号或真实笔记。非JSON辅助对象必须来自
匹配Qt工具链；业务源码逐字核对，隔离包含目录避免误用工作树的另一版本头。

六个旧写／新读／旧回读和新写／旧读／新回读场景，404项断言通过。含Reader、
FastWriter、StyledWriter、tab缩进StreamWriter，中文／emoji／引号／换行／NUL、
64位整数精度、NULL／数组／对象、普通截断输入、1MiB字段和1000项列表。
JSON不区分正整数的有符号存储：两个版本均可能将小的UInt64回读为Int64，
夹具比较数值和JSON类型，另外显式核对64位整数，而非误要求内部signedness相同。

**不是逐字节无差异更新**：新版默认把非ASCII字符写成`\u…`（emoji用代理对），
旧版保留UTF-8。测试确认跨版本解析后的值、字段及JSON类型保持；不新增schema、
同步请求字段或传输逻辑，不修改密码／笔记格式。不会为了掩盖差异修改官方源码。
未联网调用同步或证书服务，不把本地解析通过当作实际服务或全部客户端验收。

## 运行

```sh
cmake -S tests/standalone-jsoncpp-upgrade -B <isolated-build> -G Ninja \
  -DCMAKE_PREFIX_PATH=<matching-qt> \
  -DWIZ_JSONCPP_OLD_DIR=<isolated-1.8.0-amalgamation> \
  -DWIZ_JSONCPP_NEW_DIR=<repository>/src/share/jsoncpp \
  -DWIZ_JSONCPP_SUPPORT_OBJECTS=<production-build>/src/CMakeFiles/WizNotePlus.dir \
  -DCMAKE_BUILD_TYPE=Release
node tests/standalone-jsoncpp-upgrade/run-cross.cjs <isolated-build> <new-evidence-directory>
```

运行器先构建，失败立即停止；核对库版本、实际源码副本、输入和程序摘要，
只在有专用标记的全新目录写合成JSON。原始日志留本机，不上传私有路径。
不重复SQLite／QuaZip、公式／PDF／内存或无关全量测试。语法检查功能不改。
