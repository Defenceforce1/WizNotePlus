# MathJax 2.7系列有限关联检查

第219轮补[分阶段标签与阅读点击检查](math-references.md)：217轮的链接存在／PDF
检查未覆盖补充引擎丢失标签及body替换后的点击处理，不能据此否定用户的??反馈。
当前已修订自有兼容层并补223项必要关联；原50项和失败历史保留，不称全库验收。

第217轮更新：已修正受控打印时CodeMirror滚动层负右边距导致的阅读宽度微缩小；
新旧各25项、共50项运行时通过。四PDF严格字号／纸宽／四个GoTo和目标比较通过，
两模式均12pt。8页渲染新旧同图，保留合成样本原有空白尾页，不声称全局分页验收。
676份已验候选资源已接入源码，其中563份更新；214包未重制，真实资料不动。
以下216轮失败历史保留，不能当作当前字号结果。

只操作本地合成资料，不打开账号、笔记库或前台窗口。读取原有编辑器／桥／
自有扩展，不改受限编辑器，不启用远程资源，不关闭系统保护。窗口为隐藏Cocoa／
原Metal后端，Qt保持已验6.11.3补丁引擎。

2.7.9主要升级数学语音依赖并更换102份旧映射。项目已跳过VoiceOver专项，
因此先隔离评估[2.7.8排版修复](https://github.com/mathjax/MathJax/releases/tag/2.7.8)：
固定提交85e47d7801b2dea6c15878ac7b3dcf62cfd659b5，树
6436a2b2915cd62f5849a236e1f5563d9fd6c423。676份现有资源匹配官方2.7.6；
候选保留这些路径，563份变化匹配官方blob，不是整个上游分发的覆盖，未接入项目。

## 入口与覆盖

reader-opening目标增加显式mathjax-upgrade入口。输出目录必须含标记文件
.wiz-synthetic-mathjax-upgrade，内容为synthetic-only加换行，文件拒绝覆盖：

    reader-opening <absolute-isolated-files-root> mathjax-upgrade \
      <absolute-new-marked-output-directory> <expected-mathjax-version>
    python3 audit-mathjax-upgrade.py <parent-with-old-runtime-and-new-runtime> \
      --output <absolute-new-audit.json>

两个只读依赖叠加目录仅MathJax指向旧快照／候选，其余项目文件保持；Qt SDK、
框架和helper匹配既有验证引擎。先成功构建，再检查运行时MathJax.version，
不把旧程序或目录名当候选版本。

合成笔记含裸equation／align／gather／multline、cases、定理内公式、vphantom／
积分、代码围栏和四个引用，共12个数学对象。覆盖实际编辑阅读往返、原文、非空
序列化、错误和代码排除、SVG／HTML目标、两个产品PDF模式及打印恢复。新旧
各22项、共44项运行时断言通过，无远程请求。

## 发现和修正

两版均有A4空结果，非新组件回归：纸面700px而CodeMirror克隆滚动层宽730px，
防裁切保护拒绝输出。WizPdfPaperPage现仅在私有副本中，通过公开getValue将
实际CodeMirror静态化为代码文本，保留基本字体和换行，不把代码字符串当HTML
插入，不改阅读DOM／编辑器实例。无法识别的同名CSS节点不替换；任意交互widget
未验收，不放宽原溢出／资源／恢复失败门槛。

## 保留的失败

四PDF具备四个精确GoTo／目标、完整代码／正文、正确纸宽，跨版本分页和目标
误差不超过1pt。A4普通文字12pt；阅读宽度仍约11.6817pt，两版相同。审计保持
accepted:false，另报相对比较通过，不能据此接受整个PDF或MathJax更新。
第一页渲染人工查看只是局部视觉检查，不是全部页面验收。

下一步只处理阅读宽度代码块滚动宽度导致的微缩小，保留12pt门槛及原文，再考虑
接入候选。不重跑无关数据库／归档／历史公式／内存全量，不混入语音扩展、MathJax
新主版本或Qt beta；现有安装包不含本轮修正。
