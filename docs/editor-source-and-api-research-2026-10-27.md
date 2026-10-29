# 核心编辑器官方来源与API资料核查

更新（第261轮）：用户已新增条件授权；同版文件的语法级声明核对完成，整包
GPLv3-or-later、buffer注释MIT、elliptic6.5.2元数据MIT已分开记录。完整SBOM
仍缺失，不改内嵌依赖。已在下载的独立同版副本定位并修正getter临时1px排版，
详情见[文件级核对及读取修正](editor-getter-layout-2026-10-27.md)。下文为260轮
只读查找时的历史范围，不能据此继续一概阻止已获授权的本轮有限内部分析。

日期：2026-10-27，第260轮。用户明确允许联网查找官方源码和API资料，仍暂停
GitHub同步。本轮仅公开网页／仓库／包元数据的只读查询与本地整件哈希，不运行
下载代码、不安装依赖、不启动账号、不展开本地编辑器内部，不改App或真实笔记。

## 当前旧编辑器：确认官方分发文件与本地完全一致

官方[WizTeam/WizQTClient](https://github.com/WizTeam/WizQTClient)当前默认分支提交
为`7067bcfc421edce932aa3144ef2d1d631f27595a`，树为
`00a58abc3c29f71ea22ab489b064f97ea5d8b626`。

[官方编辑器文件](https://github.com/WizTeam/WizQTClient/blob/7067bcfc421edce932aa3144ef2d1d631f27595a/share/files/wizeditor/wizEditorForMac.js)
与本地同路径文件均为1196494字节，Git blob SHA-1均为
`6cb7a9927020f562b5749464cc28c68f16f7ed71`。本地仅用git hash-object进行整件
哈希，未展开源码。这个值是含Git blob头的对象哈希，不是普通文件SHA-1。

[官方LICENSE](https://github.com/WizTeam/WizQTClient/blob/7067bcfc421edce932aa3144ef2d1d631f27595a/LICENSE)
声明Mac/Linux客户端整包采用GPLv3；[README](https://github.com/WizTeam/WizQTClient/blob/7067bcfc421edce932aa3144ef2d1d631f27595a/README.md)
也声明面向个人用户的GPLv3路线。

因此不能将“未找到独立开发工程”说成“没有任何公开源码”或直接认定整份文件
都是专有代码。此前“受限”是执行中的处理范围，尚不是完整文件级许可核查的
结论。现已获得精确上游来源和整包许可证据；仍应分开核对文件级声明、内嵌依赖
范围与后续处理授权，不能据仓库根许可证自动宣称每个内嵌依赖均已核清。

在本次检查的默认分支完整文件树中，未发现该编辑器独立可重建的开发工程或
对应source map。官方客户端源码中能找到getMarkdownSrc({unEscapeHtml:true})
调用及导出用途说明，但不是关于缓存、修改代次、失效和未保存内容的完整契约。

## 新编辑器：有官方文档，尚不能直接用于当前接口

官方[WizTeam/wiz-editor](https://github.com/WizTeam/wiz-editor)默认提交
`bbe0787308acf8e667404dca322492b0ef941f69`，树
`34f7d00a61c65580d0cc67cd97a475e7ae6d97cf`，仓库MIT。README明确说明重构版和
Lite使用该编辑器；其文件树主要是中文文档、示例和对npm包的引用，不是已经
确认与旧Mac合并文件对应的核心开发工程。

已实际读取的官方资料包括：

- [编辑器结构](https://github.com/WizTeam/wiz-editor/blob/bbe0787308acf8e667404dca322492b0ef941f69/docs/zh-CN/editor-structure.md)：Container／Block／Box和扩展接口。
- [事件回调](https://github.com/WizTeam/wiz-editor/blob/bbe0787308acf8e667404dca322492b0ef941f69/docs/zh-CN/handle-message.md)：onLoad、onChange、onSave、onStatusChanged等。
- [本地模式示例](https://github.com/WizTeam/wiz-editor/blob/bbe0787308acf8e667404dca322492b0ef941f69/local/src/local.ts)：createEditorPromise、EditorDoc、editor.data()等。
- [示例依赖清单](https://github.com/WizTeam/wiz-editor/blob/bbe0787308acf8e667404dca322492b0ef941f69/h5/package.json)：依赖wiz-editor ^v0.0.647。

直接HTTPS只读访问[npm版本元数据](https://registry.npmjs.org/wiz-editor/0.0.647)
及公开包文件清单，确认0.0.647指向live-editor/live-editor，包metadata的license
字段为ISC，不能把文档仓库MIT直接扩展到包内全部代码。未下载tarball、安装或
执行包。包中的[Editor.d.ts](https://unpkg.com/wiz-editor@0.0.647/client/src/editor/Editor.d.ts)
为28243字节，SHA256
`363f329132ed343f2546b10184ece1af0d007890c0ab7a50caf0a35430a5160e`；存在data()、
isDirty()、destroy()、toMarkdown()等类型声明，未包含getMarkdownSrc。

依包声明继续检查的[live-editor/live-editor](https://github.com/live-editor/live-editor)
公开仓库也是样例／集成说明，根MIT，不构成旧Mac编辑器版本对应证明。
WizTeam/wiz-editor-react是React包装层，依赖npm编辑器，不能当成底层完整实现。

这些资料可作为新编辑器扩展和将来迁移的参考，但不能直接替换当前WizEditor.init／
on／off／getMarkdownSrc协议，也没有证明现有公式、原文、保存、引用和PDF兼容。

## 其它入口和查找范围

- WizTeam/wiznote-manual的公开树请求返回409，原因是仓库为空。
- WizQTClient Wiki只观察到一个编译问题首页，没有旧编辑器API参考。
- WizNoteClientSDK主要是移动SDK示例，所带wizEditor.js大小／入口不同，不是本地Mac文件。
- WizTeam范围的默认分支代码检索找到旧客户端调用；新编辑器／React／相关Markdown
  仓库范围未返回getMarkdownSrc契约。这不证明历史分支或非公开资料也不存在。
- wiz-editor.com首页本次通过网页工具访问未成功；不据此断言站点已关闭。
  npm网页工具失败后，用普通HTTPS GET读取公开元数据成功，没有服务试探或安全测试。

## 对进一步定位的判断

版本来源条件明显改善：现在能以官方同版分发文件为基线，不需要先替换整个编辑器。
下一步应先核对其文件级版权／依赖声明及允许的内部处理范围，再在独立副本上研究
当前getter；本轮未实施内部分析。未找到可重建的完整旧版工程，也未找到当前接口
关于未保存原文、修改代次和缓存失效的正式契约，不能把新版文档当作该契约。

本轮只补充来源证据和更正此前过宽的专有分类，不宣称已定位或修复首次必要读取、
WebEngine高水位；不重启自动任务、不打包或安装，GitHub无写入／同步。
