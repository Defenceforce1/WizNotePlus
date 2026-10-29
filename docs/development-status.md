# 当前状态：build338个人日常包完成，源码同步已恢复

更新：2026-10-29。功能源码保持336；337综合离线验收完成，338按正常Apple Silicon
身份重新构建并制作本机日常DMG。名称、Bundle ID和默认资料路径规则与已安装版本
一致，独立试用固定根已移除。没有替用户安装、迁移资料或删除现用／试用App。

337的18组644断言、9原生组1705断言、公式／UI专项、7次主程序编辑保存读回及双PDF
6份12页通过；338正式身份另7次编辑保存读回、740资源／48原生／严格签名、DMG只读
挂载1083 App项及说明读回通过。完整安装包留本机，不上传GitHub或创建Release。

GitHub项目源码同步已由用户恢复，沿用公开仓库的improve/component-updates-20261023
分支，仅同步已获准公开源码和去敏说明；不上传用户资料、凭据、安装包和原始日志。
本地未提交状态保留，定时任务仍暂停。真实云服务、iPhone、物理会话与长期内存仍未
在本轮验收，WebEngine大文档高水位未解决，不再默认重开无限引擎诊断。

详见[build338日常版](release-14.4.25-build338-daily.md)、
[336综合离线验收](pre-release-comprehensive-qualification-336-2026-10-29.md)与
[当前同步策略](github-source-sync-policy.json)。

以下为第323轮历史状态，不能代替上述当前结论。

## 历史：源码修正至292轮，内存定位至322轮；维护暂停

更新：2026-10-29。本次按用户要求暂停探索并做一次源码同步；不是新版本打包。
既有默认组件保持Qt6.11.3、MathJax4.1.3与TeX字体，语义增强关闭。

已经保留的源码改进包括：必要原文读取的同次快照复用与冗余读取删除、已核
getter排版／转换中的冗余操作减少、公式处理与引用遍历精简、原生旧快照在
明确退役边界释放，以及原文提取异常时临时DOM清理。原文、未保存修改、保存
失败保留、公式写法与交叉引用、读编切换、PDF双模式和密码规则保持。

这些修正有各阶段必要关联证据，不等于整个客户端或全部场景验收。源码与旧
256安装包不同，本次未重编主程序、制作安装包、安装或改动现用App和真实资料。
未来打包仍需重新编译涉及打印的伴随头文件，并完成必要整合验证。

高内存尚未修复：320两组共84控制／16原文校验通过；6000段／1公式累计申请
约177.77MiB，六类容器只覆盖约2.50%，约97.47%类型未识别。这是累计申请量，
不是存活、RSS或精确组件成本。该组峰值约479MiB，切小30秒约74MiB，自然
回落不能证明长期无泄漏。

321准备的805项静态目录在322增量链接中遇到函数局部FlatData符号不可解析。
已按用户约定停止，运行验证0组；独立源／部分构建留本机，新运行时未合格，
不能将旧320映像当成322候选。下一应先完善完整离线目录链接资格。

本次只同步合法项目源码、测试源码及去敏阶段记录。原始日志、诊断构件、安装
包、真实资料和本机路径不上传；自动任务保持暂停，未来GitHub同步仍需另行授权。
详见[本次同步范围](github-sync-2026-10-29.md)、[固定类型实测](native-fixed-type-attribution-2026-10-29.md)
及[目录链接停止](native-catalogue-link-stop-2026-10-29.md)。

以下保留此前历史记录，不能将其中旧状态当作当前验收结论。

# 当前 第255轮默认组件与构建防回退已修正

MathJax4.1.3、字体和既有适配层已成为实际源码／默认构建，旧2.x资源已可
恢复移出。其它已更新组件与242一致；构建不再依赖历史App补拷。15组组件／
80份文件与Qt6.11.3清单接入配置、每次构建和打包，默认关闭自动子模块更新，
生成目录残留会清除，误用旧组件会失败。有意保留的兼容性库在清单中单列。

普通Release arm64生产编译及49项必要检查通过；公式／读编／快照／双PDF
保持，两PDF与254像素和目标一致。旧254临时构建及一次性测试资料可恢复
清理，新构建保留。现用App、真实资料与重要回退未动，GitHub暂停、无自动任务。

尚未解决：大文档renderer高水位、首次必要原文读取的内部开销，以及新包／
正常主窗口验收。没有新增内存收益结论，也没有推广兼容性受限库的新版本。

# 第254轮 生产编译和必要关联通过，尚未形成新包

247／248／253已经在独立目录完成Release arm64生产编译；加载39项及小型
公式／读编／保存快照／双PDF30项，共69项关联通过。两模式PDF与248逐像素、
实际跳转和坐标相同。没有正常客户端／真实资料启动或全量、内存矩阵复测。

确认裸CMake资源仍是MathJax2.7.8，不能直接交付；首轮2项版本失败留证，
29项排除。随后按242既有组件组装，以已验资源为底，仅覆盖当前自有JS再验收；
新生产二进制与合格组件树分别保留，后续候选需沿该组装和完整性流程。只改
诊断空根保护和Metal检查，没有新增产品补丁或内存修复结论。80份一次性
测试文件可恢复清理，现用App／242／重要回退不变，GitHub暂停、无自动任务。
见[整合记录及剩余边界](webengine-memory-attribution-2026-10-26.md)。

# 第253轮 已修正初始加载冗余读取，尚未打包

只改自有WizDocumentWebView的初始加载编排：删除未使用的外层整篇HTML读取，
保留准备函数内原有的一次经校验的读取、原文缓存／属性通知及原子输出。
同一文件读取2→1，避免外层完整副本在准备期间继续存活；保存生产者和缓存
getter函数体保持。QtCore隔离检查当前39项通过、旧39项控制单列，临时输出
逐字节、UTF8/BOM、用户CSS、呈现bootstrap、通知与未保存DOM标志保持。

这是冗余I/O源码修正，没有App RSS／时延收益或高水位修复结论；未启动renderer、
主客户端或真实资料。只编译测试，生产主程序尚需后续构建与关联验收，247／
248／253均未打包。47份自产文件可恢复清理，现用App／242／回退不动，GitHub
暂停，自动任务未重建。见[当前修正与限制](webengine-memory-attribution-2026-10-26.md)。

# 第252轮 确认短组自然回落，尚无新增产品补丁

按用户进一步探索要求，同一诊断二进制做无VM摘要控制／自有renderer摘要两组，
各6000→60段重复两次，60项关联通过。四次小笔记10秒约209～274MiB、30秒
约86～96MiB、60秒基本保持；当前149个DOM节点和自有持有记录不变，所采集合
没有脱离文档的节点。Tag16驻留亦明显回落，但未映射到准确分配器或私有对象。

大笔记打开仍曾约947MiB，没有新增产品内存修复或长期无泄漏结论。新夹具
仅对齐主程序已有样式／拼写调用顺序，不把与251的差异算产品收益。331份
自产文件可恢复退役，证据保留；247／248源码未打包，App／242／重要回退及
真实资料不变，GitHub暂停、自动任务未重建。首次必要getter和大文档高占用
仍列未解决，缺乏安全可改具体持有点时不继续同类重复测试。见[详细记录](webengine-memory-attribution-2026-10-26.md)。

# 第251轮 自行测量，运行时高水位保持未解决

已完成48次混合大小合成笔记开关与同页面阅读24篇，最终390项关联通过。
使用现用242匹配的冻结资源；现用App当时未运行，未启动正常账号或操作真实
资料，不能将本次组件路径称登录主客户端或长期无泄漏验收。

48次关闭后主进程空闲footprint约74→63→63MiB，活跃malloc约20至21MiB，
页面、窗口及renderer归零，本窗口未见关页后持续明显累积。同一页面阅读时，
四个小笔记空闲点renderer约532→101→119→523MiB，自行回落后仍会升高；
主进程100→114→117→118MiB，最后view销毁renderer退出，主进程约78MiB。
两场景工作负载和构建不同，不是产品优化前后对照，也没有新增产品修复。

必要证据与最终CSV已保存，初稿100项排除；407份自产构建／资料可恢复退役。
首次必要getter内部成本和原生驻留尚无安全可改的具体持有点，不继续猜测性
测试或强制回收。247／248源码保持但未打包，现用App／242／回退不变；GitHub
暂停，自动任务未重建。见[测量表和限制](webengine-memory-attribution-2026-10-26.md)。

# 第250轮 完善峰值归因后转入自行测量

完整原生历史一组20项通过，区分约185MiB线程栈虚拟分配、约49.5MiB追踪工具
驻留及夹具约3.5MiB临时HTML字符串；这些不能当主程序泄漏或产品优化收益。
尚无新增产品补丁，249的首次读取参数旁路仍无收益，主进程驻留仍无可安全
修改的产品持有点。247／248已验源码保持，尚未打包，现用App／真实资料不动。

用户要求自行测量；只读进程路径检查现用App未运行，未启动它或正常账号。
下一步只进行正常分配器的混合大小合成正文趋势观察，同profile逐轮返回顶层
事件循环、外部收集样本；不重跑公式／PDF矩阵，不将组件观察称全App日常验收。
GitHub仍暂停，自动任务未重建。见[最新归因记录](webengine-memory-attribution-2026-10-26.md)。

# 第249轮 继续定位，两个残留项尚未修复

按用户新请求，新增三个公开getter参数对照及两个各3次关页的原生组，80项
关联通过。三种参数约0.83至0.89秒，原文长度／摘要相同，无低成本参数路径；
额外实体解码会改原文，getContentHtml阅读输出和原生已保存载荷也不能替代
未保存Markdown原文。不是完整逐字／公式／PDF或长期内存验收。

普通分配器组同profile空闲footprint75.64MiB、活跃malloc18.43MiB；关页对象
和renderer均零。另取得仅合成主进程的原生分配栈，发现Qt／Metal资源及夹具
预先存在的约2MiB源缓冲，但没有可安全删除的自有产品持有点。lite会改用专用
分配区，观察数值不当产品收益。首次必要getter和主进程堆驻留仍列未解决。

本轮只修改测试和记录，没有新增产品补丁。247／248自有JS源码优化保持，均
尚未打包；现用App、真实资料、242及重要回退不动，GitHub暂停，自动任务未
重建。自产构建／合成根及两份原生日志原件可恢复清理，压缩证据保留。见
[最新证据和边界](webengine-memory-attribution-2026-10-26.md)。

# 第248轮 有限内存定位及两处源码优化收口

246确认大文档原文读取成本，247同次转换共用一次快照，248定理同步投影传递
已拿到的原文，避免嵌套正文再读全文。均不跨笔记／回调缓存；原文、直接辅助
接口回退、自定义前置回调及异常恢复保持。两项只在本地源码，尚未进入现用App
或242安装包，没有生产主程序编译、重签或新包。

248最终86项DOM／字节、15项原生及29项小型双PDF关联通过，旧44项和原型
86项单列。定理合成样本内部重复读取3→0，投影约2.67→0.15秒；两种PDF
像素、5个实际链接及4个目标一致。不推广为整App速度或长期无泄漏验收。

有限清单完成后停用自动任务；GitHub仍暂停，仅本地保留进度和证据。513份
自产构建／合成资料可恢复移废纸篓，现用App、真实资料及242／重要回退保持。
首次必要getter内部成本和主进程堆驻留仍未解决，暂无具体新自有分配点；
不自动扩展优化或打包。详见[结果和保留限制](webengine-memory-attribution-2026-10-26.md)。

# 第247轮 同次转换原文复用

247只改自有markdownEnvironments.js：同步customBlockGamut内的原始字节恢复
与普通公式引用共用一次惰性原文读取，函数返回即失效。无跨笔记／回调缓存；
先调用原有自定义回调，保持原文、异常恢复及下一次转换的读取时点。

79项纯函数、两组最终新原生30项、小型双PDF关联29项通过；旧59项控制单列。
同一程序旧／新／新／旧对照中，约1.1MB合成样本的验证前getter 2→1，钩子
旧1.74至1.76秒、新0.95至0.97秒。原文逐字、实际引用、编辑／阅读及关页保持，
两种PDF逐像素和4个真实目标坐标一致。不把局部耗时或footprint推广为整App收益。

现用App、242／重要回退和真实资料未动，无生产编译或新包。首个必要getter
的内部开销与主进程堆驻留未解决。只余定理转换已有原文参数的有限复用检查，
随后收口；自动任务每30分钟继续、GitHub暂停，仅本地保存。396份自产构建／
合成资料可恢复移废纸篓，证据保留。详见[实现和边界](webengine-memory-attribution-2026-10-26.md)。

# 第246轮 原文读取阶段定位

246五组同程序67项通过，确认大文档公开getMarkdownSrc读取有明显开销：额外
调用后renderer相邻增量251.78至287.31MiB。自有裸公式恢复钩子内也调用一次，
约946／970ms，占该样本钩子大部分耗时。245“阅读就绪”已包含原文验证，不能
称为纯排版成本，原始数据和更正均保留。新的JS粗粒度指标为常数，不作阶段差分。

当前只增加独立诊断，没有产品修复、主程序编译、App或安装包改动。下一轮先
验证自有转换轮次内的短生命周期快照复用及失效保护，不能盲删原文恢复。有限
清单与原始记录在本地phase246。用户要求每30分钟自动续作、暂不GitHub同步；
本轮不连接网络，仅本地保存。详见[阶段证据及更正](webengine-memory-attribution-2026-10-26.md)。

# 本次 WebEngine 内存详细定位

按新请求完成独立合成探索，不改产品逻辑、现用App、242安装包或真实资料。
四组同程序96次关页／560项，另两组单次观察16项分列；初始24次漏计helper的
探索排除，规范路径守卫问题留证。profile保留时页面、窗口和renderer均释放，
默认malloc区仍有空闲／碎片驻留；销毁profile不显著降占用。6000段大文档打开
时的renderer成本更大，数学路径JS堆与应用专用脏页增加，但无具体分配调用点。
没有内存修复或新包，本轮未开启自动任务；详见
[定位结果及边界](webengine-memory-attribution-2026-10-26.md)。当前自用正式版仍242。

# 第244轮 项目收尾 当前自用正式版为242

用户已基本确认并将build242暂作为当前自用正式版。原App、DMG和签名不变；
只更新交付记录，不改测试元数据冒充完整验收。项目转为按需维护，不自动
继续开发／测试，定时任务保持PAUSED。见[收尾记录](project-closeout-2026-10-24.md)。

固定试用资料经用户明确同意、无占用核对后移废纸篓，不读取其内容；顶层
试用启动入口停用以防重建。另清理合成库／页面、废弃候选和测试派生资源，
合计17处，已知构件270243845逻辑字节，不含未读取的固定资料。全部可恢复、
未清空废纸篓。源码、上游分发、SDK／生产构建、必要记录、242和重要回退保留。

已安装App只读核对为242，1082项与交付清单一致。本轮不修改现用App或正式
资料目录；旧安装指纹变更单独记录为新基线，不改历史证据。高水位未解决、
正式系统／真实账号等非全部验收的限制保留；以后有具体需求再开启维护。

# 第243轮 高水位复查与242安装镜像及测试构建清理

只更新测试诊断，没有新的产品内存修复，不将诊断工具加入主程序。真正逐阶段
返回顶层事件循环后，最终反向新旧脚本空闲85.67／85.63MiB，旧组另一轮
61.78MiB；此前约25MiB差距不是本次稳定重现的脚本回归。VM摘要显示默认堆
驻留约61MiB、分配约19MiB、其余较多为空闲／碎片。尚未定位具体分配点或
排除长期增长，不宣称高水位解决。见[测试方法与证据边界](webengine-high-water-recheck-2026-10-24.md)。

按此前请求，原样242 App已制作[安装DMG](release-14.4.25-build242-installer.md)，
117146222字节，挂载1082项完整一致、签名／48组件／非GUI路径通过。不安装或
迁移资料，固定试用入口仍不变。36处测试构建与派生资源969039025逻辑字节
可恢复移废纸篓，未清空；源码、上游分发、SDK／签名／生产构建、必要证据和
242／234／230／重要回退保留，未来诊断须重建。现用App及资料不动，二进制
仅本机，源码／去敏记录同步GitHub；自动任务PAUSED。

# 第242轮 性能优化独立测试候选完成

新build242已整合235防抖、237裁剪、238／239转换和DOM／通知局部优化、240
标签缓存及241列表泄漏修补，固定入口沿用原测试资料；没有安装或启动普通
账号，234／230及重要回退保留。详见[使用和验证限制](release-14.4.25-build242-performance-test.md)。

包内113项必要关联、三轮读编场景和7项入口检查通过；48组件、签名、ZIP
完整性、解压1082项App及四份交付文件保持。App逻辑258988229字节，比234
少2258282字节。源码阶段历史矩阵不冒充本轮全量；原生高水位仍未解决。
四套Qt框架路径实际确认，helper完整路径未直接观察，但配置及文件字节保持；
记录这一限制，不重复无关测试。二进制仅本机、去敏记录同步GitHub，自动
任务仍PAUSED，自有解压／缓存可恢复清理。

# 第241轮 列表移除对象泄漏修复

统一修补文档删除、修改后筛出、参数变化后筛出三处takeItem移除的释放责任。
使用deleteLater让当前菜单／信号栈结束后释放，并清理右键引用；整列表清空
也先清菜单引用。不增加每条字段、不改数据库或正文。

实际生产列表＋临时数据库，通过真实manager删除信号连续300次移除，旧302／
新0个对象残留，列表析构后旧303／新0。最终26项覆盖选择信号被阻塞、菜单
快照、分组、无效行与元数据保持，生产增量构建通过；旧19与初次新19不重复
累计。另两条筛出分支只核对共用函数接线，未宣称完整主窗口／真实账号验收。
见[实现、对照与适用范围](list-lifetime-optimization-2026-10-24.md)。

这是独立列表泄漏，不冒称解决239／240的WebEngine原生高水位或测得整App
内存降幅。无新包，234与固定入口、现用App、笔记资料及重要回退不改。约
28.53MiB自有临时构件可恢复移废纸篓、证据保留，仅同步源码／测试／去敏记录；
自动任务仍PAUSED。

# 第240轮 列表空标签优化完成 原生内存归因范围缩小

只在成功查询后缓存空标签，失败继续重试，clear／reload保持失效；不增加
每条字段或全局缓存。真实生产列表对象＋临时数据库的500次读取查询500→1，
重载501→1、返回文件夹500→1、删最后标签501→1。新22项及旧21项单列，
真实manager修改信号到item.reload通过，不冒称整个主窗口或真实账号。
见[优化、对照及未确认项](performance-attribution-2026-10-24.md)。

两组各八次同profile关闭、空闲10／30／60秒的118项诊断通过，但60秒footprint
旧62.16／新87.13MiB仍高，malloc在用均约19.82MiB；差异对应内部／可复用
页状态，压缩0、图形差0.27MiB。分配器驻留仅是线索，未确认具体持有者或
解决风险，不声称RAM下降。无生产强制回收，不重复全公式／PDF／组件矩阵。
唯一产品CPP与增量构建通过，诊断源不进生产；无新包，234仍不含235／237／
238／239／240，现用App、资料与回退保持，自建临时构件可恢复清理；只同步
源码／必要测试和去敏记录，自动任务仍PAUSED。

# 第239轮 第二组十轮局部优化完成 原生内存收益未验收

十项新实现减少按需转换的无效准备、标题临时数组、引用DOM遍历和欢迎信号
重复刷新。样本祖先查询18008→2、嵌套7531→6、通知128→2；逐轮454项含
有意重复样本，最终524项关联、41组定理82次往返及双PDF像素／目标保持通过。
原生程序未变，仅资源构建成功；见[十项对照及风险](performance-followup-ten-2026-10-24.md)。

原生内存按旧→新→新→旧各八次同profile复核；页面／控件／窗口／子对象
每次归零，但两对空闲footprint旧77.13／新90.58、旧67.99／新76.86MiB，
新组均高。临时分配减少不是常驻内存下降，不能排除高水位回归，风险保留。
没有强制GC／清缓存或改生产生命周期；不称完整内存验收。局部源码阶段完成，
无新包，234不含235／237／238／239；现用App、固定资料及重要回退不变，
自有临时资源可恢复清理，必要失败证据保留，仅同步源码／测试／去敏说明。
自动任务仍PAUSED。

# 第238轮 十轮性能与内存优化完成（源码，无新包）

十轮分别削减代码区／TeX扫描、实体数组、重复正文拼接、区间／DOM偏移查找、
引用上下文对象、标题重解析、缩略图重复入队及过期待办读取。每轮保留直接
修改前对照和结果；上下文样本6006→3、标题解析2000→1000、旧待办参数调用
2000→1，源文与最新UI不变。见[十轮明细、测量边界及失败记录](performance-ten-2026-10-24.md)。
最终1976项必要断言及41组定理的82次往返通过，双PDF逐像素同234。联验修正
两处旧夹具API与独立资源布局，并补真实未打开数据库关闭的空指针保护，失败
证据保留。新增同profile编辑器／128公式八次关闭，控件／窗口／子对象归零，
空闲footprint约65.8MiB；不声称整App内存降幅或长期无泄漏。生产增量构建成功，
无新包；234不含235／237／238，现用App及固定资料不动，自动任务PAUSED。

# 第237轮 必要体积清理（源码，无新包）

macOS Release仅主程序启用未引用代码裁剪，相同对象的规范化链接对照减少
2.17MiB／20.56%，折算整个234 App约0.87%，不是新安装包／内存或速度实测。
主程序增量构建与312项必要关联断言通过；纸面测试先有offscreen图形告警，
仅该项补隐藏Cocoa复测，原日志保留。运行组件、许可和受限编辑器不裁删。
旧218／219／221／222／224／225／226七套测试App及四处自有临时构件移入
废纸篓，合计约1.74GiB逻辑文件，可恢复；必要记录另存，不称物理空间已释放。
194／195及重要回退、230／234、现用App、固定入口与笔记资料保持，无新包。
234不含235／237改动；[范围与数值](size-cleanup-2026-10-24.md)。自动任务PAUSED。
本阶段只完成必要体积清理，不冒称完成此前请求的十轮性能／内存优化。

# 第236轮 综合功能回归

最终2144项独立断言通过，另41组定理各两轮往返和三轮Markdown情景通过。
涵盖编辑保存／开页、数学引用／代码图形、欢迎待办／设置菜单、异步队列／
关闭回调、双PDF、三组件交叉读写、8次大公式页和16次共享profile关闭。
旧PDF断言及其观察时序已补强修订，初次失败留证，不改产品逻辑；最终两PDF
正常正文12pt、各四个目标和精确恢复通过，图像同234并查看，六欢迎图保持。
详见[完整范围及未验证边界](functional-regression-2026-10-24.md)。
不是实际账号云同步／加密iPhone联动、系统剪贴板、真实会话和主窗口长期验收，
不承诺绝对无误或无长期内存增长。产品／编译程序不改，无新包；234仍不含235
修复，现用App及真实资料不动，自动任务PAUSED。

# 第235轮 普通笔记首次阅读抖动修复（源码）

确认普通common笔记在初始化前后展示两套布局，已接入Markdown既有的有界
呈现控制，初始编辑／其它类型、原文与保存权限不变。不增加固定延时或重载。
139项最终关联、27项无控制对照及三轮Markdown读编情景通过，主程序arm64
增量构建通过；尚非真实主窗口复验。见[原因与验证](ordinary-opening-2026-10-24.md)。
未制作新App或改固定入口，当前234仍不含修复，下一次打包须用重新编译主程序。
234／230／220、现用App及真实资料保持，自动任务PAUSED；源码和去敏记录同步。

# 第234轮 统一测试包与必要功能验收

[build234免安装测试包](release-14.4.25-build234-integrated-test.md)已整合230＋233
jQuery3.7.1，顺带修旧中文空列表提示；Qt及原生业务保持，不重复Qt评估。固定
入口指向234、沿用同一资料，旧230／220及现用App不改，不启动正常账号。
最终500项合成功能断言、三轮读编场景、7项启动、48组件及ZIP解压清单／签名
通过；两PDF版式／目标／12pt和原文恢复保持，图像同233。测试发现的空态英文
仅局部修订并重测欢迎组，实际云同步／全部笔记／系统剪贴板不冒称通过。
源码与去敏说明同步，安装包只留本机；本阶段完成，自动任务保持PAUSED。

# 第233轮 Markdown独立jQuery源码接入

官方完整3.7.1已替换Markdown活跃独立3.3.1内容，兼容URL、1.11.3和富文本捆绑
组件保持，补完整MIT；欢迎页不动。318项必要关联断言通过，包含两数学引擎的
引用／点击、公开粘贴载荷／撤销／快照、代码／定理／图形及双PDF；四页目标／
字号／宽度和渲染核对，新旧逐像素相同。不是系统剪贴板或完整客户端验收，
无性能收益声明。见[接入与范围](editor-jquery-upgrade-2026-10-23.md)。
未生成新App／安装包，固定入口仍230且不含本轮，重要220、现用App及真实笔记
不动。原生程序保持，只更新资源映射；源码与去敏说明同步，自动任务PAUSED。

# 第232轮 编辑器jQuery可行性评估

确认Markdown实际动态请求独立3.3.1，普通富文本不暴露全局对象；没有读取捆绑
编辑器内部或据此删除1.11.3。临时资源副本只将该URL内容换成官方完整3.7.1，
旧／新各22项基础编辑阅读／保存快照检查通过。建议先3.7.1维护过渡，不直接4.0，
但产品源码／230未升级，无新App。完整公式／PDF／粘贴／插件与性能内存未验收，
详见[公开接口证据与取舍](editor-jquery-assessment-2026-10-23.md)。
初始诊断编译和富文本全局假设失败已留证修正；230／220、现用App、真实笔记、
固定资料均不动。只同步诊断代码／去敏说明，自动任务PAUSED。

# 第231轮 独立WebEngine升级来源复核

230统一候选保持。官方安装器更深层6120/61400确有此前漏掉的源码／macOS包，
但主组件标6.140.0-rc且源提交与官方RC标签相同，不能因归档无后缀就称正式版。
预编译包要求Qt6.12；源码构建入口允许6.8+，故独立构建方向仍可保留，但未配置
或兼容验收。10项来源／入口核对通过，不是客户端测试。正式版闸门未满足，
不制作231候选或给出性能／内存数字；取消的部分输入移废纸篓可恢复，230／220、
现用App、真实笔记、当前SDK和补丁不动。见[更正与证据](webengine-source-eligibility-2026-10-23.md)。
后续只在正式组合可核实时启动独立构建／必要A/B，不自动轮询或扩大到RC。

# 第230轮 整合测试包与升级收益评测

已制作[build230独立测试包](release-14.4.25-build230-integrated-test.md)，合入227～229
待办／欢迎页库／两种语言，保留226保存和目录修订及原MathJax4／PDF，固定入口
转230并沿用原资料，不自动安装／启动。113项包内关联、60项有限性能／归因检查、
7项启动及48原生组件和ZIP解压一致性通过，四图同229；不重复无关公式全套。
可见就绪1032.2→17.5ms主要来自消除旧固定一秒等待；旧库仅去等待也为23.0ms，
不归功于新库或声称整App提速。App相对226只增22676字节，ZIP约109.3MB。
见[收益与局限](component-benefits-2026-10-23.md)和[Qt评估](qt-webengine-assessment-2026-10-23.md)。
Qt正式源码／匹配SDK组合仍未核实，保留6.11.3及PDF／Metal补丁，不采用RC/beta；
220重要回退和现用App不动。真实模板保存、待办客户端创建／同步仍待正常试用，
不冒称完整客户端验收。源码／去敏记录同步，二进制仅本机，自动任务PAUSED。

# 第229轮 欢迎页jQuery4维护

欢迎页独立jQuery已更新到4.0.0正式完整构建，编辑器不替换；官方npm/CDN/SRI
字节与许可核对。唯一自有适配为旧hover事件改on绑定，保留原进入／离开效果。
临时Migrate4.0.2诊断与去掉诊断各87项通过，零迁移警告；中英文欢迎、待办列表
及创建表单四张图与228基线相同。欢迎资源净少8761字节，不宣称整App提速。
见[来源与验证](../tests/standalone-reader-opening/welcome-jquery4.md)。
诊断不进产品，CMake只刷新资源映射；无新App或安装包，226固定入口和现用App／
真实资料不动。后续打包需合入227～229源码，当前226没有这些功能／更新。

# 第228轮 欢迎页组件更新与去除繁体界面

欢迎页UI1.14.2／i18next26.4.2已接入来源固定的官方资源并补许可，不动编辑器、
数学、Qt或业务原生源。按用户要求只留简中／英文，删除欢迎页繁体文案／日历
源与引用；旧偏好仅回退，不删字体或笔记繁体字。英文日历的旧重复初始化问题
改走公开option接口，箭头仍为原样。基线52＋最终75界面／163词典，共290项通过，
三张简中截图与旧版相等；初次输入／夹具／英文和视觉失败保留。
见[验证与体积记录](../tests/standalone-reader-opening/welcome-i18n.md)。
繁体本次清理只再省打包资源412字节，升级和许可整体净增7260字节，不冒称瘦身。
没有新App或安装包，固定入口仍226，不含227待办／228更新；现用App与真实资料
不动。其它旧组件的框架迁移边界保留，自动任务继续PAUSED。

# 第227轮 简洁待办入口与日期管理

欢迎页“待办事宜”旁增加“添加”，只需标题和日期；点击标题打开普通笔记、点击
日期改期、“完成”只移出列表，不删除笔记。新待办无七天期限，逾期和较远日期
都保留；旧日程笔记仍按原七天窗口展示。不新建待办目录或申请通知／日历权限。
日期字段原子提交、失败保留输入／已建笔记GUID重试、迟到刷新保护已接入。
主程序增量构建通过，29项新增界面／26项SQLite存储／23项原欢迎关联检查通过，
两张截图查看；没有重跑旧公式／PDF全量。见[功能与验证范围](todo.md)。
本轮只改源码，未生成新App或改固定入口；现有226不含此功能。未启动正常账号、
读取真实笔记、安装或迁移，实际客户端创建／同步不冒称验收。自动任务仍PAUSED。

# 第226轮 模板保存与启动目录修订，待用户复验

现场模式元数据确认模板的body在编辑／阅读都为false，旧body-only条件误拒绝
编辑。最终改按编辑器互斥模式标记选取原保存接口，保护不取消；初版不足候选
已可恢复退役。另修订树就绪后一次恢复选择，失效ID回退已有全部笔记，迟到
群组通知不抢选择。50项保存关联／16项目录／8项启动检查、主程序必要构建及
48组件签名／依赖通过。[226候选](release-14.4.25-build226-save-state-test.md)
沿用固定资料与入口，当前225进程和现用App不动。未读取正文、改权限或解除
控制台保护；具体用户笔记保存和启动仍待复验，不称完整客户端／DB落盘验收。
详见[检查记录](../tests/standalone-reader-opening/save-state.md)，暂不继续无关组件升级。

# 第225轮 欢迎页维护与流程图视觉门槛

欢迎页专用jQuery3.7.1／jQuery UI1.13.3及官方简体中文日历接入，保留主题、
列表和日期筛选；笔记编辑器jQuery不改。旧／新／最终包各23项，固定入口16项
通过；[build225](release-14.4.25-build225-welcome-test.md)共用固定测试资料。
flowchart1.18.1公开API和双PDF机械通过，但有循环分支斜线触碰标签的视觉回归，
未交付，继续保留1.11.3。最终225编辑器／数学／PDF资源完整保持224，不重复
已验公式套件。现用App、真实笔记、重要回退均不动，仅源码／去敏记录同步。
剩余依赖的迁移边界见[组件记录](component-updates.md)；不以版本数量强行更新。

# 第224轮 图表依赖维护候选

更新Underscore1.13.8／Raphaël2.3.0，保持流程图和时序图写法、字体与操作。
旧包31项、新资源与最终包各37项关联检查，四份双PDF严格检查及4页渲染查看
通过；不重跑旧公式／数据库／归档全量，不宣称整客户端或真实同步验收。
[build224](release-14.4.25-build224-diagrams-test.md)保留222其余资源与原生程序，
固定入口沿用同一测试资料，未创建新用户库，现用App和重要回退不动。自动任务
PAUSED；后续候选继续用固定模板，不顺带jQuery跨代或Qt运行时替换。

# 第223轮 固定测试资料与旧资料清理

按用户要求，后续候选共用固定测试目录与启动入口，当前仍为build222，无App
重编译或组件变更。原222资料原样保留，重复4份测试资料和46份合成配置／缓存
移入废纸篓，可恢复；现用App、正常资料与重要回退不动。不读取笔记／密码或
强行启用记住密码，不把非GUI检查当作真实登录验收。见[固定试用说明](fixed-test-data.md)。

# 第222轮 Marked／CodeMirror5维护候选

已更新自有定理正文Marked18.1.1和CodeMirror5.65.21核心／三个既有扩展，保留旧
语言包、CSS、编辑器和操作规则。两数学引擎82组定理用例／292项原生关联断言，
四PDF严格检查和6页渲染查看通过；独立[222候选](release-14.4.25-build222-frontends-test.md)
另有148项包内复核。保留221 MathJax4，生产源默认仍2.7.8，重要220和现用App不动。
未承诺所有笔记、插件、语言、长期内存或完整客户端验收，不为升级数量顺带重构。
Qt/WebEngine新独立版本体系已记录，后续另案；本轮没有替换运行时。自动任务PAUSED。

用户追加的[关联功能检查](component-functional-check-2026-10-23.md)已补：普通富文本
18项、实际保存快照／重开、快速切换22项、页面初始化／设置／菜单和加载器；
本轮关联范围未检出新增故障。未启动真实账号或数据库，不称全客户端／同步验收。

# 第221轮 MathJax4 独立候选

用户授权后，已准备[build221免安装测试版](release-14.4.25-build221-mathjax4-test.md)。
MathJax4.1.3仅进入隔离测试副本，保留TeX风格、原有公式／引用／PDF双模式；
生产资源默认2.7.8、重要220与现用App不动。178项必要关联断言、3轮模式往返、
两PDF严格字号／页宽／四目标及渲染查看通过，不是所有宏包／真实笔记／长期验收。
字体微测差距小，不以其宣称整App提速；更接近旧风格是采用TeX的主要理由。
源码和去敏记录同步，二进制仅本机；未安装、启动正常账号或访问真实笔记。
自动任务PAUSED；下一步是独立候选试用，不切换默认或重跑旧数据库／归档套件。

# 第220轮 自用安装包发布

用户确认引用恢复后，已生成[build220日常DMG](release-14.4.25-build220-daily.md)。
产品逻辑与219一致，127项新包内资源关联回归通过；未重复数据库／归档／内存或
MathJax4实验。既有两PDF严格证据同代码／资源复核保留，不冒称本轮重新生成。
1702项App及48原生组件／签名／非GUI启动、镜像只读挂载逐项核对通过；正常卸载。
仅个人本机发布，未经公证，不自动安装或改真实笔记，旧包保留。源码和发布记录
同步组件分支，二进制不上GitHub；MathJax仍2.7.8，不自动开始v4／Qt迁移。

用户追加指定220为重要回退，已配套独立源码快照／校验和重要版本记录，排除常规
清理，需用户明确要求才可删除；DMG字节不变，旧重要207也保留。

# 第219轮 交叉引用修订与MathJax4评估

已在隔离分阶段样本用218公开加载器复现标签表丢失导致(???)，修订后为(1)。
复用活跃引擎、统一补充编号，补裸公式／定理正文的普通ref，按新body委托本页
数学点击；原文、编辑阅读和外部链接规则保持。223项关联断言和两模式PDF严格
字号／页宽／四GoTo及恢复通过，保留旧合成样本的灰色标题和空白尾页，不称整库
或全局视觉验收。见[关联说明](../tests/standalone-reader-opening/math-references.md)。

另有[build219免安装修订版](release-14.4.25-build219-reference-fix-test.md)，旧218
及重要回退保留，不安装或访问真实资料。MathJax4.1.3仅作独立微测与可行性评估，
未接入主程序；[报告](mathjax4-feasibility-2026-10-23.md)列性能、迁移和使用习惯边界。
源码与去敏记录同步组件分支，二进制仍仅本机，自动任务PAUSED。

# 第218轮 四组件免安装测试版

已制作[build218独立试用目录](release-14.4.25-build218-components-test.md)，含四项
组件更新及216／217打印修正。App1702项／571处预期变化，48个原生组件零架构／
依赖失败，代码常量段保持、完整签名／非GUI启动、启动器语法／--check通过。
新.command使用单独资料目录，端口占用不干预现用App；未启动GUI／账号，未建
测试资料、未安装／覆盖旧包，未做DMG／ZIP，不重复已验套件。源码及去敏记录同步，
二进制仅本机。下一步转平常试用反馈，Qt正式源／SDK门槛仍单列，不扩展新功能。

# 第217轮 阅读宽度缩字修正与MathJax接入

只在受控可信打印时临时取消CodeMirror负右边距，复用FIFO原样恢复；源码、阅读
编辑和纸宽规则保持。新旧共50项运行时／四PDF严格字号、纸宽、四个GoTo及目标
比较通过，两模式均12pt；原失败留证，不重复数据库／归档／内存全量。
官方MathJax2.7.8的676份保留资源接入，563变化逐字一致，无新主版本／语音包；
许可、主程序增量、非GUI启动及开发资源部署核对。8页新旧同图，原有合成空白尾页
保留，不称完整分页／客户端验收。现有214包未更新，未安装或改真实笔记。
当前源码四项组件完成有限接入，Qt6.11.3补丁引擎保持。见[组件记录](component-updates.md)。

# 第216轮 MathJax隔离候选和A4代码块输出修正

旧2.7.6与候选2.7.8的676份保留资源核对，未替换项目MathJax。对照确认两版
相同的A4代码块交互滚动层溢出；仅静态化私有副本，保留代码文本、源DOM、
保存和裁切保护。新旧共44项运行时通过，四PDF生成，主程序增量／非GUI启动
通过。PDF独立复核仍检出阅读宽度约11.6817pt而非12pt，两版相同；严格门槛未
通过，不把相对一致当完整验收。下一步限于此打印关联问题，再决定候选接入。
现用App／真实笔记／旧包、语法检查和Qt不动，未新打包。见[组件记录](component-updates.md)。

# 第215轮 JsonCpp关联检查与更新

复核SQLite／QuaZip现有686项证据、214源码／包和207重要回退，未重跑套件。
接入官方JsonCpp1.9.8未改动amalgamation，263份上游源和完整树核对，实际业务
响应与文档／标签代码新旧库交叉验证六场景404项通过，主程序增量构建及非GUI
启动通过。新版Unicode转义使输出字节不同，但合成值／JSON类型和跨版本读取
保持；不改业务schema／字段／transport，未实际联网同步，不称完整客户端验收。
语法检查／Qt／MathJax／现用App和真实笔记不动。214包仍不含JsonCpp新更新，
没有覆盖、重新打包或安装。详见[组件记录](component-updates.md)。

# 第214轮 组件更新测试候选

已制作[build214独立测试包](release-14.4.25-build214-components-test.md)，纳入SQLite
3.54.1／QuaZip1.7.2，保留Qt6.11.3补丁引擎、菜单、编辑器、公式和资料规则。
沿用686项合成关联断言，仅新增必要打包／依赖／签名／只读镜像／非GUI检查；
重要207包不动，没有自动安装、真实账号／笔记或资料迁移。此为测试候选，不把
独立文件输出当作自动资料隔离，也不声称完整日常验收已完成。
Qt6.12当前WebEngine来源仍指向beta2，本轮不替换。详见[组件记录](component-updates.md)。

# 第209轮：共享profile原生内存增长的诊断归因修正

拆开外层测试回调返回与profile销毁边界后，主要累积被单回调自动释放作用域
解释：profile仍在，footprint已从176.8降到69.0MiB，再销毁只变约0.1MiB。
逐轮返回事件循环、仅诊断池控制均未重现同等幅度；具名持久profile另16次，
最终临时路径隔离复测后8次范围1.5MiB、空闲67.6MiB。四组208项原生断言、7项汇总器控制通过，
没有重跑公式。修正诊断入口／旧汇总归因护栏，不改生产App或profile策略。
208的“尚待定位”由本轮接续，不再当作已确认产品泄漏；不是整个客户端长期
无泄漏保证。详见[209轮记录](maintenance-2026-10-23-phase209.md)。

# 第208轮：合成大文档／512公式性能与内存检查

6000段约1.1MB正文／同规模512公式／无编辑器纯HTML，每组8次；另有不抓图
纯HTML对照。四组284项功能与生命周期断言通过，引用确实检查SVG xlink及目标，
5项汇总器拒绝路径通过。原文／编辑阅读／关闭销毁保持。主进程在共享profile
存活期仍抬升，无编辑器对照也有；profile及夹具释放后显著下降，尚未定位具体
持有者，不宣称内存问题已修复。产品代码／安装包保持207，不盲改引擎、授权或
清缓存，不打开真实资料。详见[208轮记录](maintenance-2026-10-23-phase208.md)。

# 第207轮：综合审查清单收尾

按授权的六类有限清单，合并优化裸公式原文索引、数学加载期间最新模式编排、
已是阅读状态的no-op完成和依赖闭包／页面退出清理。26项自有控制、22项真实
公开接口快速切换、38项呈现、顺序往返及2／2／66对象SVG对照通过。缓存和
体积只读复核无新确认缺陷，不盲删引擎／字体／公式，不重跑无关联全量。
新207资源包保留旧包，不自动安装或改真实笔记。更新过时交接头部，停止扩展
本轮清单，按实际使用反馈维护；不声称穷尽全部软件优化空间。详见[207轮记录](maintenance-2026-10-23-phase207.md)。

# 第206轮：数学阶段间暂停优化

通过MathJax标准临时配置，仅把默认section暂停50ms调为8ms，保留非默认配置、
processing预算和SVG分批／让出执行时间，不改第三方引擎或数学解析。三类一对
本地合成样本数学阶段258→65、265→70、373→263ms，SVG规范化逐项一致，
编号引用、编辑原文及六次模式切换保持，38项关联控制通过。不是所有笔记的性能
保证。新206仅资源更新封装，不自动安装或改现用App／真实笔记，详见[206轮](maintenance-2026-10-23-phase206.md)。

# 第205轮：Markdown就绪后呈现等待优化

增加setup就绪通知，临时呈现控制下一帧确认当前ready后显示，不再等轮询加两帧。
保留25ms轮询／30秒兜底及204的CSSOM不序列化保护。三组微小合成样本setup后
显示等待55／59／62ms降至24／29／26ms，非整App提速比例；公式本身排版成本
保持。32项控制与六次模式切换／编辑原文保持通过，生产增量构建通过。新205
自用包保留旧包、不自动安装，详见[205轮记录](maintenance-2026-10-23-phase205.md)。

# 第204轮：Markdown编辑／阅读空白修正

203呈现隐藏样式被初始化HTML快照克隆，恢复模式后持续透明。真实公开on／off
合成往返复现六状态空白；改用仅活跃CSSOM规则、序列化style为空后六状态正常，
编辑后源文保持。三个初次打开场景和21项控制通过，生产增量构建通过。新204
修正版替代203日常推荐，不覆盖旧包或现用App。详见[204轮记录](maintenance-2026-10-23-phase204.md)。

# 第202轮：PDF设置文字修订与Markdown打开呈现修正

PDF设置页三个空单位翻译补齐，底部提示缩为“两种 PDF 版式共用此页边距。”；
原单位索引／数值不动，实际Ui＋新QM独立检查14项通过。仅源码修改，未重打包。
Markdown分阶段观察到原始HTML→阅读转换→异步公式／重复样式应用，文档容器
在最终就绪前已显示。三组合成页面各加载一次，不支持重复文件加载的说法。
随后用户授权修正，已在Markdown初次阅读临时HTML增加最终ready后的呈现控制；
原始／转换／数学中间帧不再提前显示，不reload、不改捆绑编辑器、保存或锁定。
三组修改后合成场景均只在ready可见，19项缓存重开／取消／失败／超时／迟到token控制
通过，生产主程序增量编译通过。未打新包，不改200或现用App；不是完整客户端
屏幕视觉验收。详见[202轮记录](maintenance-2026-10-23-phase202.md)。

# 第200轮：日常版DMG与阶段收尾

199生产主代码不变，封装build200自用日常版，保持密码解锁与正常功能，移除
测试负载和本地符号，不删字体／数学／PDF／媒体／许可。无剩余可直接去掉的
Headers、静态库、CMake或调试包。DMG不再重复捆源码与多份文档，源码／校验
在镜像旁保留。包清单、48个arm64组件、严格ad-hoc签名、非GUI路径及只读挂载
逐项核对；不称完整日常验收，不安装／改现用App、笔记、194／195备份。

必要记录和源码／可复现输入保留；重复候选与退役临时构建可恢复清理。自动任务
暂停，项目转日常反馈维护，不继续额外检查循环。仅源码及去敏记录同步GitHub。
详见[build200收尾](release-14.4.25-build200-daily.md)。以下为历史阶段。

# 第198／199轮：内存对照纠正与独立生产候选

此前offscreen数学／大图内存曲线受图形上下文丢失污染，已保留并拒收为性能
比较。新增隐藏Cocoa实际Metal后端观察：公式＋大图40次、公式20次、纯大图
20次，正常事件循环、无额外池／缓存清理／后端切换，无图形错误；关闭后没有
重现持续累积，延迟footprint约78–84MiB。不称整个客户端长期无泄漏或新产品
内存修复。9项报告解析回归通过，原引擎／正文保存／锁定逻辑不改。

199独立生产构建排除兼容测试，打包入口默认要求OFF，8项配置拒绝检查通过。
新副本移除主exe本地／调试符号且代码／常量段不变；整个App逻辑字节减少
6.45MiB（2.50%），不是主exe缩减比例。除主exe／Info，全部文件链接与195
一致，保留公式／PDF／字体／媒体／许可，48组件依赖及严格签名通过。仅本机
候选App，未生成新DMG或安装、未公证，不称完整客户端试用通过。

194／195与现用App／笔记不动。当前可执行的清理、缓存热点、有效内存复核和
安全体积精简已收尾，后续以具体使用反馈为准，不扩展盲目引擎裁剪／检查轮次。
详见[性能记录](performance-memory-plan.zh-CN.md)和[生产打包](production-package.zh-CN.md)。

# Personal-use development checkpoint: existing-certificate usage only

## Historical phase196/197 (superseded by phase198/199 above)

User installed password-only195 and now requests workspace cleanup,then measured
performance/memory work,and package size last. Installed main build/hash matches
195;metadata/executable only,not account or note contents. Retire95 explicitly
obsolete project artifact targets to recoverable Trash (~21.75GiB removed from
workspace,not disk space reclaimed). Keep194/195 DMGs/source/inventories/evidence
and active reproducible toolchain/patched Qt source/build/runtime. Historical
artifact references resolve through a local relocation manifest;do not delete
verification records or the user's data/installed Apps.

First source optimization replaces10000-entry clear-all thumbnail cache with
16MiB estimated-cost LRU,minimum32KiB/entry,single-flight pending loads and scoped
invalidation/reload. It synchronizes GUI/worker state and returns completion on
GUI with independent closable state rather than a dangling cache QObject. No
body/save/encryption/lock-policy change. Oversized single preview is admitted
alone at full accounting budget to preserve quality;not a strict RSS cap.

12 component plus10 actual temporary-thumbnail-index/GUI integration checks
pass. One initial fixture failure compared original64px input against existing
50px database conversion;corrected fixture compares stored thumbnail,not product
index logic.500 repaint requests create one actual DB load.2000 standard50px
images retain20,000,000→5,120,000 image bytes in isolated legacy/LRU workloads;
this is not whole-app memory or a claim based on a tiny timing delta.

Offscreen editor baseline is separate:empty closed pages steady,but formula+
large-image batches still grow main footprint after views/pages/windows/children
close. A test-only autorelease-pool control reduces but does not eliminate it.
No real-client leak fix is claimed. Next isolate image/formula/graphics ownership
before modifying fixed engine;package slimming follows measured memory work.
No new installer or actual-note/foreground operation. See performance-memory-plan.zh-CN.md.

## Current phase195: password-only use without weekly provisioning maintenance

The user has only a free personal team and explicitly chose to retain the current
installer as backup, then remove Touch ID and accept certificate-password unlock.
The final instruction is backup/package only: do not install or replace any App.

Pre-removal build194 records the accepted192 product code/constants with the same
optimized App name, bundle ID and default data-root rules. Its local DMG and full
source snapshot are frozen; the personal-team profile expires2026-10-28 15:54UTC.
Packaged routing1236/0; session142 checks had4 failures because old management
fixtures assumed unsigned main. A signed fixture may dispatch synthetic native
enrollment,so do not claim no native keychain calls or all package tests passed.
No real account/note/secret or physical fingerprint operation was requested.

The password-only client removes the Settings tab and does not construct a
biometric widget in the default password form. Certificate password, Unlock and
the existing optional password hint remain. The native biometric backend is no
longer compiled/linked; compatibility APIs return unsupported without keychain
reads/writes/deletes or LocalAuthentication. Supplied fake providers remain only
for isolated request/session regression fixtures; they are not a user feature.
Existing biometric policy flags/records are ignored for UI availability,not
deleted,and no secret is moved into a weaker password store. The historical
native implementation stays in source history but is not part of the new target.

Per-note verified Session/Gate and native screen-boundary monitoring stay active
for password unlock: A does not unlock B,navigation can reuse A proof,five-minute
background grace and immediate sleep/screen retirement preserve dirty DOM/save
cache/queues. No certificate/ciphertext/iPhone protocol or permanent-decryption
change. Ordinary Markdown-save investigation remains stopped; automationPAUSED,
recoveryOFF. No extra full suite or hardware acceptance round is added.

New local-only password package uses ad-hoc signing as the earlier local package
did,without the main's restricted keychain entitlements or embedded provisioning
profile. It has no seven-day personal-team provisioning dependency; this is not
Developer ID/notarization and does not promise Gatekeeper acceptance on other
machines. Do not disable OS protections to launch it. Never modify installed App
or data; no migration/merge of the independent186 store is performed.

## Current closeout: user reports basic trial acceptance of192

The user's latest feedback is "我已基本检查通过" after receiving trial192.
Record general basic trial acceptance and close the agreed executable source
work. Move to normal use and concrete feedback-driven maintenance; do not add
another audit, rebuild, package or mandatory hardware/lock-screen test round.

This feedback does not separately enumerate physical screen-lock notification
delivery or every hardware/client scenario. Keep those limits explicit instead
of claiming itemized physical or full-client acceptance. The former pending
screen-delivery observation is no longer a mandatory blocker or active task.

This update is documentation only; reuse the existing independently signed192.
No program changes, new builds, functional tests, signing, installation, account
launch or real-note actions. Source/sanitized records only to GitHub; binaries
remain local. Ordinary Markdown-save investigation stays stopped, certificate
creation and VoiceOver stay excluded, automationPAUSED/recoveryOFF unchanged.

## Implementation phase192: connect screen-lock notifications, not just user-session resign

The user reports that a physical macOS screen-lock round trip left a note open.
The native bridge subscribed only to workspace will-sleep and session-resign;
locking the screen need not resign the logged-in user session. Earlier model
tests invoked Monitor directly and did not establish delivery of this boundary.

The bridge now also listens for macOS distributed screen-lock and screen-unlock
notifications, with explicit immediate delivery while AppKit is inactive. The
names are observed macOS notification strings, not an SDK-guaranteed lock-state
API. Only their occurrence is used to revoke access; no payload is trusted, no
private lock-state API is called, and unlock never grants note access. Workspace
wake/session-resume also revoke as fallback if departure delivery was missed.
Ordinary app activation is not a hard event and keeps its five-minute grace.

Native callbacks marshal to the GUI thread before accessing Qt state; teardown
removes both centers and invalidates already queued callbacks. Existing Session
retirement clears individually verified note grants, including hidden notes;
fresh A verification cannot revive B. Save cache, dirty DOM, queues, ciphertext,
password UI and automatic/manual Touch ID policy are unchanged.

An in-process distributed-center fixture exercises the production selector
registration and immediate-delivery option through Monitor and Session, rather
than directly calling the hard-boundary method. It never posts a global lock
notification or changes the real screen/session. Necessary session and routing
tests only; physical screen-lock delivery still needs normal user observation
of the new trial, not a claim inferred from these isolated tests.

Incremental acceptance: unchanged document-routing group1236/0 in the first
combined run; native/session group142/0 after its offscreen fixture was corrected
to pump the native main run loop as well as Qt. The first combined run had one
fixture failure and is retained, not relabeled as passed. Only the changed group
was rerun; total1378 distinct checks, not a full-suite or physical-system claim.

## Historical phase191: retain unlock per note, never blanket-unlock a certificate

The user corrected phase190: opening one encrypted note must not unlock every
other note using the same certificate. Certificate-wide proof reuse was too broad.
Session authorization is now a set of individually verified note-identity digests
inside each current server/account/KB/certificate generation. Shared password
cache alone is not authorization for another note. This changes access policy,
not certificate format, ciphertext or permanent-decryption behavior.

Unlock A, switch away and return: A stays usable within the valid app session.
Unverified B still requires its own password or Touch ID even when its certificate
is identical. Other windows displaying A may reuse A's proof; notifications include
the exact note GUID and the host checks it before presenting content. Unlocking B
independently adds only B, not a wildcard. Wrong B credentials cannot borrow A's
proof or revoke A. Every page still gets fresh callback tickets; retained per-note
proof never revives an obsolete navigation or output continuation.

Five-minute continuous app-background expiry, native sleep/session lock, actual
shutdown and account/server/certificate/record-management boundaries remain as
previously implemented. Expiry clears all note grants; freshly unlocking A does
not resurrect B, even if B was unlocked before expiry. Only opaque note digests
are retained in memory, no new password/certificate persistence. Scope generation
retirement still cancels queued same-note notifications. Save/dirty DOM and
unencrypted notes remain unchanged.

Affected session/management and document-routing tests only, with temporary
synthetic notes and fake authentication. No native auth/account or real-note
actions. New independent local191 preserves190/installed/frozen Apps; source and
sanitized records only to GitHub. Ordinary Markdown save investigation remains
excluded; recoveryOFF/automationPAUSED. Prior user feedback on hardware/iPhone is
not presented as physical acceptance of this new policy correction.

Both affected groups pass in the same incremental run: session/management113/0,
document routing1236/0,total1349/0. No unrelated full suite or physical tests run.

## Historical phase190: certificate-session reuse (scope corrected above)

The user reports that fingerprint use, hardware combinations and official iPhone
use all worked in the prior trial. Record this as user feedback, not newly run
agent/system acceptance. They reopened the lock-policy work because switching
notes and returning unnecessarily prompted again. Ordinary Markdown save-failure
investigation remains excluded.

Audited the encrypted-access retirement paths: navigation, hide/show, window
close/minimize, background grace, native sleep/session retirement, certificate/
account/server changes, local-record policy events, screenshot restoration and
shutdown. Normal navigation/window closure no longer clears verified session
proof or the password cache needed for saves. A process-local Session holds only
digests, authorization state and opaque generations scoped to server/account/KB/
current certificate, never another password or a persistent bypass. It is shared
by production views and renewed only after existing local password verification.

Normal encrypted/plain-note round trips, reopening, matching windows and internal
screenshot hide/restore reuse proof within the same valid app session. Every page
still receives a fresh local generation: navigation/hide retires its old requests
and reads. Unlocking a new generation cannot revive a delayed old clipboard,
export or editing result. Unsaved DOM/save queues remain independent. No changes
to certificate formats, official iPhone behavior, edit-ownership/file locks or
system protection.

Five continuous background minutes and native sleep/session lock retire shared
proof; actual shutdown forbids new grants. Account/server changes drop previous
session state, certificate replacement destroys its revision token, and local
record management retires only its matching scope. Restoration of old bytes does
not resurrect proof. One authoritative session listener retires first, then each
window retires requests and prepares its new locked context, avoiding multi-window
double retirement and second-click unlock. Queued grant notifications check the
original weak generation before reaching other visible locked views.

Affected session-management and routing groups only; older unbound request-only
fixtures remain explicit strict retirement tests, while new policy cases inject
the real product Session with a non-native monitor. No actual native fingerprint,
screen capture, account launch or real-note access. A new independently signed190
is local-only; source/sanitized records go to GitHub. RecoveryOFF/automationPAUSED.

Incremental accepted unique checks: session/management110/0 and final routing
1224/0, total1334. The earlier combined invocation failed because routing exposed
double retirement; its session component passed. Later changes touched routing
only, so the passed session group is not repeated merely to produce a green
aggregate. Initial const-argument fixture build error and failed routing logs
remain preserved. This is neither a fresh full-suite nor physical-system claim.

## Historical phase189: three-part unlock and plain settings

Closeout feedback (2026-10-22): the user reported no issue and explicitly deferred
further testing. Record general trial feedback as accepted, not as itemized proof
of native fingerprint/hardware combinations or official iPhone round trips.
Those checks remain unverified and deferred, not a blocker or a new mandatory
test request. The agreed executable source checklist is closed; move to ordinary
feedback-driven maintenance without adding feature/test rounds. This follow-up
changes documentation only: no build, test, signing, installer or App modification.
Ordinary Markdown save-failure investigation remains excluded. Automation stays
paused and recovery OFF; no user account or real-note action is authorized here.

The user explicitly reduced the unlock body to exactly three primary controls:
masked certificate-password input, Unlock, and Touch ID. The ordinary title,
redundant password caption/mode-switch button and decorative card were removed.
Certificate hints, exceptional action context, progress and errors appear only
when needed. An existing certificate hint appears below the password field as
one subdued theme-colored plain-text label, prefixed with "密码提示："; long hints
wrap, and absent hints hide and clear the line. No password-derived hint, extra
button or modal is added. If biometric capability is unavailable, only password unlock is
shown. Embedded authentication has no extra application Cancel button: native
panel cancellation or typing a password retires the request; manual Touch ID
retry remains after cancellation without an automatic loop. Standalone widget
consumers keep their original password/cancel choices.

Touch ID settings now has a plain password field, explicit one-shot local-record
consent and Enable/Remove record actions. Removal still needs consent but no
password; enabling needs both. No new confirmation loop, automatic enrollment,
policy/keychain mutation, session timing or save-path change was introduced.

Only affected UI/session-management/form-routing checks are run. After the user's
three-control refinement, only the two changed groups are rerun; the final hint
refinement reruns only form routing, not management or unrelated/full/save tests.
New local trial189 preserves188 and installed/frozen
Apps; no autonomous real-account/store launch, installation or native fingerprint
test. Ordinary Markdown saving-failure investigation remains explicitly stopped
and outside the plan, not declared fixed. RecoveryOFF, automationPAUSED. Remaining
native hardware/fingerprint and account/iPhone checks are deferred by the user's
closeout decision above, not required now. Do not expand the source checklist or
restart unattended idle checks.

## Historical phase188: shared lock policy, capability-aware unlock and minimal UI

Password-only and explicitly enrolled Touch ID now share the same encrypted-note
access gate: five continuous minutes away from the application, canceled on early
return, with immediate retirement on sleep/session lock. Plain notes are unchanged.
Navigation/hiding still retire note-specific access. No dirty DOM, save credential
or queued write is cleared. Password verification retains the existing local
certificate verifier; the gate itself never verifies or recovers a password.

The native provider queries current biometric capability and Touch ID type
without authenticating or reading a stored credential, then rechecks immediately
before starting. Display presence is not a proxy for hardware availability. An
unavailable context shows password input directly and creates no read operation.
If capability returns on activation before the first attempt, the still-current
visible context may start once. Actual cancellation/failure never automatically
loops: the Touch ID button retries explicitly. Typing a password retires automatic
intent; availability restoration may reveal a manual choice but cannot revive it.

Settings and password form now use native password inputs and normal-sized
buttons. Persistent explanatory/lock-policy paragraphs and cancellation warnings
were removed at the user's request. Only concise errors/progress and explicit
local-record management consent remain. Offscreen synthetic snapshots were
visually inspected for clipped buttons, not claimed as native Cocoa acceptance.

Affected acceptance only: unlock UI116/0, session/management91/0 and document
routing1189/0, total1396/0. Earlier test-constructor and three legacy screenshot
fixture failures are retained. Plain screenshot insertion uses a plain-note
fixture; protected cases retain post-capture authorization and stale-result
checks. Fake providers perform no system authentication or screen capture.

Clipboard behavior is unchanged: while unlocked, copy may publish plaintext;
relocking rejects new requests and stale-permit delayed publication. Already
published clipboard contents are not erased/recalled. Clipboard history/OS sync
is not controlled by note encryption. This is not a secure-memory-erasure claim.

Local signed trial188 is a new copy, preserving187 and frozen/installed Apps. It
uses the existing independent186 store only on user manual launch, never by the
agent. Source/docs only go to GitHub. The user explicitly stopped investigation
of the ordinary probably-Markdown save failure: removed from the forward plan,
not declared fixed, with existing save protections retained. Actual native availability changes,
fingerprint use, real-account workflows and iPhone round-trip remain user gates.
RecoveryOFF, automationPAUSED; no first-certificate creation or VoiceOver work.

## Historical phase187: editing grace, automatic unlock and save diagnostics

User feedback reopened the reduced checklist: a non-encrypted, probably Markdown
note reported saving failure and blocked closing/quit; the user already force-quit.
Do not attribute that issue to encryption or claim the unsaved changes recovered.
Read-only inspection of the trial application log found14 ambiguous snapshot/
extracted-HTML-write failures and no final archive-write failure message. No note
body/database or credentials were opened; no normal account was launched.

Brief application switching no longer retires an opted-in encrypted editing
session immediately. A five-minute continuous-background grace period applies;
returning early cancels it, repeated background states do not extend it, and
overdue activation handles delayed timer delivery. Existing hard sleep/session
boundaries still retire immediately; this changes app policy, not OS protection.

The ordinary native-provider password form now opts into one automatic Touch ID
attempt for its valid remembered context, only when visible in the active window
and active application. Queued context changes, manual input/cancel, hiding and
shutdown retire pending requests. Cancel/failure never loops; explicit retry and
certificate password remain available. No automatic enrollment or policy opt-in.
Synthetic collaborators validate the flow, not actual system authentication.

An independent-window FIFO acknowledgment now resolves an older failed write
when a newer write for the exact requester/note succeeds. Pending/latest failures
still block close; unsolicited completions cannot poison the state. Snapshot
failure reasons are now static content-free codes with concrete UI explanations,
and duplicate modal warnings are suppressed until a successful write or new
document. No preview-as-source fallback, dirty-state discard or forced exit.

Incremental accepted results: document async119/0, note/session access89/0,
document routing1181/0, automatic-unlock UI107/0, and a selected actual-engine
ordinary common/Markdown save probe13/0:1509checks across five affected groups.
The first UI run exposed a manual-cancellation reentrancy regression, corrected
before acceptance; later re-show fixture failures were Qt offscreen activation
state, resolved by driving only the synthetic active-window state. Failed logs
are retained. Already-passing affected groups were not repeatedly rerun after
the final fixture-only correction; no unrelated full suite or physical OS test.

Local signed trial187 (14425.187) preserves186 and uses the same independent186
test store only when manually launched; it does not switch to the installed App
store, migrate data or auto-start an account. Signed inherited components and
minimum rights remain unchanged. Source/docs only go to GitHub; binaries,
signing data and raw logs stay local. RecoveryOFF, automationPAUSED.

The reported particular Markdown save failure remains **not reproduced/root
cause unconfirmed**. Basic actual-engine save success and the acknowledgment fix
do not close it. The new precise prompt or a minimal non-sensitive reproduction
is required; real account/iPhone and native automatic Touch ID also await user
acceptance. Do not resume idle autonomous tests while waiting for that evidence.

## Historical phase186 delivery: independently signed trial

The newly prepared local **14.4.25, build14425.186** trial contains the current
phase183/184 fixes, not the older frozen installer. The user authorized this
next delivery step after phase185. Existing installed App, old signed test copies
and frozen candidates are preserved; no installation or normal account launch.

Only the separately named `WizNoteUserTrial` entry gets a fixed fresh standalone
root using the existing storage validation/consent flow. Finder and inherited
data-store overrides cannot fall back to the installed App store. The ordinary
executable path is unchanged. Source BOM/CRLF and current executable code/constant
sections are verified. Manual first launch prepares the new directory before the
existing default-cancel consent; cancellation does not reach account bootstrap.
Local storage isolation does not isolate cloud data after the user logs in.

The new copy reuses the previously accepted development-signing identity,
matching unexpired profile and minimum main entitlements. Its original46 signed
components/rights are byte-preserved. Initial packaged offscreen testing aborted
at Qt platform initialization because the compact bundle only contained cocoa;
the supplied crash report matches that test executable/stack, before database
opening. The failed log is retained, not a passing encryption test. A matching
Qt6.11.3 arm64 offscreen test plugin was added and signed in this new copy only;
strict recursive signing and existing main permissions remain unchanged, with
48 Mach-O components total. No library-validation/system-protection weakening.

Final package checks: **permanent decryption43/0 and routing1181/0, total1224/0**,
plus two read-only startup-root checks, malformed probe code2 and a read-only
native signing-prerequisite check. Actual Qt maps from the signed bundle, without
developer framework overrides. No SecItem/authentication calls, fingerprint,
normal account, trial account-store creation, real note, native physical-session
or unrelated full-suite repetition. The trial's launcher validates the exact
bundle/signature and refuses when an existing client/clipper port is in use;
its check-only mode does not launch the App. No automated user session was run.

The local App/launcher and concise trial instructions are delivered separately;
only source and sanitized summaries go to GitHub, not binaries/profiles/raw crash
reports. The phase185 delivery gap is now closed. The [acceptance steps](user-acceptance-existing-certificate.zh-CN.md)
remain user-operated and unaccepted. Recovery OFF, automation PAUSED; no new
routine approval loop or automatic restart while waiting for actual feedback.

## Historical phase185 handoff (delivery gap superseded by phase186 above)

Phase185 reconciles the reduced checklist with the latest evidence. No additional
concrete source defect remains identified in that checklist; this is not complete
App acceptance. The existing-certificate use fixes are in source and the developer
executable: permanent-decryption feedback43/0 in phase183 and opening/review,
relocking/encrypted-save routing1181/0 in phase184. Neither group is rerun here.

There is an explicit **delivery gap** before the next user test. The frozen
14.4.25 installer is the phase171 build, and the latest accepted signed test copy
is phase181. Neither includes the phase183/184 fixes, despite sharing the same
development version label. Do not ask the user to validate these fixes in either
old artifact. A new independently signed test build matching the current source
must be prepared before that session, preserving the installed App and frozen
candidates. It has not been prepared or installed; the user is not expected to
build it themselves. Do not equate a source commit or incremental build with a
new delivered installer, or silently expand continuation into packaging/install.

The consolidated [user acceptance checklist](user-acceptance-existing-certificate.zh-CN.md)
covers prerequisites, existing-certificate Mac use, official iPhone round trip,
representative resources and optional plaintext conversion on a disposable note.
It distinguishes required account/device actions from optional specialized native
workflows and retained historical limitations; no passwords/keys need to be sent.
Actual normal-account/iPhone/native-control outcomes remain unaccepted.

The user requested pausing once only delivery/user-dependent acceptance remains.
The existing automation is now **PAUSED**, with its 20-minute cadence and saved
instructions retained. No product/test/build-input changes or functional tests,
signing, packaging, installation, real-note/account access or authentication took
place in this documentation-only handoff. Recovery stays OFF. Resume source fixes
only for concrete findings or an explicit next delivery/test step, not idle loops.

## Phase 184: existing-certificate opening, relock and encrypted-save routing

The initial affected group reproduced **1181 checks, 2 failures**: opening
ignored certificate-initialization failure and review silently returned after
certificate reload failure. The encrypted opening now conceals the old body
before the existing acquisition boundary, checks its result and rechecks owner,
opening epoch and closing state after that potentially modal call. New navigation
or close cannot continue the old certificate load or password-page presentation.

Failure retires existing read requests, shows a blank page with an explicit
existing-certificate-unavailable message, and retains dirty DOM and save cache.
The blank page refuses reads even on the legacy route; stale password submissions
outside the password page are ignored. After the existing certificate becomes
available, review plus fresh authorization resumes retained content without a
body reload. There is no automatic certificate creation/replacement or retry.

Only incremental main-client compilation and the **one affected routing group**
ran, finally **1181/0**. Existing wrong-password/relock cases and real temporary-
database FIFO encrypted-write/save-first-quit assertions remain in that group.
The exact production routing bodies are compiled with explicit synthetic
certificate-acquisition, prompt and renderer collaborators; no real account or
certificate service is contacted. Database/sync/password-algorithm/save source
is unchanged. No permanent-decryption/PDF/native-auth/physical-session/full-suite
rerun, signing, installer or installation. Normal App, real notes, accepted signed
copies and frozen candidates are unchanged. This is not normal-account/iPhone or
real native-dialog acceptance.

Recovery remains OFF. The user explicitly reactivated the existing 20-minute
automation after phase183; it is now ACTIVE, not the historical PAUSED state
recorded below. It follows the current existing-certificate-only scope, avoids
repeating closed stages, and pauses with a precise list if only user-dependent
real-data/foreground prerequisites remain.

## Phase 183: existing-certificate permanent-decryption failure feedback

Two concrete use-path defects reproduced before the fix: a failed save frontier
returned silently, and an unavailable local certificate was reported as a generic
unsupported note. The focused group initially had **32 checks, 2 failures**.

Preparation now preserves a specific failure category and honors certificate-read
failure; certificate availability rules and encryption/archive/rollback algorithms
are unchanged. Missing/unusable local certificates stop before password or consent
prompts, with no automatic creation/replacement or server request. Save-frontier
failure produces one report and preserves the unresolved save failure, pending
work and original content. Intentional cancellation still produces no error popup.

Messages distinguish certificate password from account login password, changed
scope, read/decrypt failure, backup failure, write/metadata failure and incomplete
rollback. Any retained backup path stays visible; password/key bytes are excluded.
The incomplete-rollback instruction still stops editing/sync rather than claiming
success. Successful conversion, backup retention and original resource bytes stay
covered by the same affected group.

Only incremental main-client compilation and **one directly related test group**
ran: **43/0, 0.35s**. It uses actual temporary database/reader/archive/saver code
with synthetic prompt/report callbacks, not a real Cocoa dialog, account or iPhone
round trip. No PDF/native-auth/old-matrix rerun, signing, installer or installation.
Normal App, real notes, accepted signed copies and frozen candidates are unchanged;
recovery OFF, automation PAUSED. First-certificate creation remains out of scope.

## Current scope decision: phase 182, 2026-10-22

The user has removed Mac first-certificate creation/registration from the current
plan. Continue only the use of an existing compatible account certificate:
password unlock, encrypted save, explicit biometric enrollment/forgetting,
relocking/read-output retirement and confirmed permanent decryption with backup.
This is a workflow prerequisite, NOT evidence that a real account has a valid
certificate. Missing certificates and read/authentication failures must still
prevent certificate-dependent operations; never infer absence from a failure or
automatically create, upload, replace or reset a certificate.

The official open-source client exposes `getCert`/`setCert` and the certificate
fields, but the first-create atomic no-replacement contract, authoritative absent
response and uncertain-submit reconciliation remain unconfirmed. That issue is
retained, not solved; it is no longer a prerequisite for this reduced scope.
Local generation/backup prototypes and their historical tests remain dormant,
not an enabled product feature. Do not continue their native-panel/registration
acceptance or remove shared certificate-verification helpers used by decryption.

See [existing-certificate usage and recorded implementation](encryption-existing-certificate.zh-CN.md)
for the consolidated feature/evidence list and bounded next steps. Previous phase
records below are historical; references there to first-certificate prerequisites
do not reinstate that canceled task. Phase 182 changes documentation only, with
source/evidence continuity checks and no new functional-test or build claims.
Normal App, real notes, signed test copies and frozen candidates remain unchanged;
recovery stays OFF and automation PAUSED. Actual account/iPhone interoperability
and representative real attachment combinations remain unaccepted.

## Phase 181: user-operated sleep/wake, actual workspace notifications

Reusing the isolated main-window flow, the new explicit sleep fixture asks the
user to choose Apple-menu Sleep, wake/unlock normally and confirm. The agent
never sleeps or locks the Mac. No normal account, real note, test fingerprint or
screen capture is involved. The retained production session/auth rules are not
modified; only the guarded test dispatch/fixture changes.

All **23 checks passed**, with actual native notifications separately observed:
**WillSleep1, DidWake1, workspace retirement1, production Monitor hard event1
before quit**, and one app-inactive event. No local notification was posted.
The user's confirmation and actual native sleep/wake events both participate;
mere app deactivation is insufficient for this mode. Protected body access and
old read permits stay retired, dirty DOM/original body/certificate/save cache
survive, archive format stays encrypted, exact owned memory is removed/Missing
confirmed, and normal eleven-stage quit saves original plus latest content.

The pre-quit observation file is authoritative for hard-event acceptance. The
final aggregate counter includes the subsequent normal shutdown hard event
(2 total); that second event is NOT claimed as another real OS notification.
Runtime is bundled Qt; strict signature/current code/arm64/dependencies and the
46 inherited signed components/permissions are incrementally verified. No-arg
and malformed launches still refuse account bootstrap with code2. No other suite,
PDF, lock-screen, multi-window, fingerprint or recovery-matrix repetition.

Installed App, phase180 signed test App and frozen candidates are unchanged;
only a new signed local sleep-test copy is made, no installer or installation.
Recovery stays OFF and automation PAUSED. This closes the focused physical
sleep/wake scenario, not switch-user or all system-event combinations. Remaining
real-data/native-control acceptance and the authoritative compatible first-
certificate service contract stay separate; no service probing or new routine
approvals. No keys, profile/identity, raw logs, synthetic data or binary uploaded.

## Phase 180: actual user-operated lock/unlock with synthetic notes

A new independent signed main-client copy offers an explicit live-session fixture
before account bootstrap. It generates only temporary synthetic credentials/notes,
opens through the production manual-password control and leaves a dirty renderer.
The agent never locks or sleeps the Mac. The user operates Control+Command+Q,
unlocks normally and confirms in the isolated instruction window. The fixture
times out after three minutes if unconfirmed; failure still cleans its owned item.
No test fingerprint, normal account, real note or screen capture is requested.

The user confirmed the round trip and the actual Cocoa process observed **one
inactive event and return to active**. All **22 checks passed**: no body access
or old read permit restored on unlock; latest DOM, certificate/save cache and
encrypted archive format retained; exact synthetic memory deleted and Missing
confirmed; normal eleven-stage quit saved latest plus original body. The event
loop exited normally and runtime Qt came from this signed App. No-argument and
malformed launches refused normal startup (code2). No unrelated suite rerun.

Important distinction: **zero native NSWorkspace retirement notifications** were
observed in this exercise. The physical lock/unlock is user-confirmed and its
actual application-inactive route is accepted, not independent proof that public
workspace sleep/session-resign events fire for lock screen. Actual sleep and
switch-user hard-event delivery remain separate unaccepted combinations. No
local notification was injected or automatically posted; this is distinct from
the earlier simulated workspace tests. Production session/lock logic was not
changed to make acceptance pass.

Only the guarded fixture dispatch and acceptance test changed. Existing installed
App, real notes, phase177 signed App and frozen candidates remain unchanged; no
installer or installation. The new signed test uses the same matching profile,
minimum main entitlements and 46 inherited signed components/permissions. No keys
exported, no identity/profile/binary or raw log uploaded. Recovery stays OFF and
automation PAUSED. First compatible certificate service registration still needs
an authoritative contract; no server probing is authorized or performed.

## Phase 179: account-file chooser fixture readiness

Only the offline test driver changed; every existing product source byte is
unchanged. The ordinary account chooser override remains disabled. This is not
a real-account upload, avatar replacement or server-success verification.

The retained intermittent fixture failure reproduced immediately: **23 checks,
2 failures**, despite Chromium reaching its chooser. Temporary observation of
the dialog selection then passed once and three bounded repeats; this timing
sensitivity does NOT establish a complete root cause. Failed logs remain.

The old driver accepted after a fixed 30ms and treated merely observing the modal
as completion. It now waits for visibility, the exact readable path and filename,
and an enabled Open button; it revalidates in the queued accept and waits for
dialog completion before polling DOM/FileReader. A controlled filename-reset
case proves acceptance is deferred and the selection restored. Reselecting the
same model row alone did not restore cleared text in the initial new fixture;
the driver now fills its own filename editor explicitly, with that failure kept.
No file is injected into the page; real Chromium chooser delivery remains used.

Final affected group: **25/0** in each of three bounded runs (**75 passes**, 2.18s
total), including exact PNG/name/MIME/FileReader bytes, cancel, owner/parent
destruction, and private-profile cache policy checks. No full suite, repeated
PDF/native-auth workflow, new signing, installation or packaging. The historical
failure's complete cause and real-avatar update remain unaccepted; no further
repetition is scheduled just to turn that limitation into a passing claim.

## Phase 178: special destination names in HTML exports

The retained HTML resource-URL defect was reproduced without accounts or real
notes. Eight destination basenames in both archive and rendered modes were loaded
in an actual isolated local WebEngine page. Before the fix, **12 of 16** failed:
ampersands, quotes, fragment/query syntax and literal percent escapes did not
resolve to the on-disk resource directory. No network request was made.

Both the database HTML exporter and rendered-HTML transaction now use one URL
encoder for the existing HTML-escaped on-disk basename. The legacy directory name,
merge behavior, BOM policy and body-last transaction/rollback are unchanged.
Spaces, Unicode and URL metacharacters are encoded as a path segment; a literal
`%20` remains literal on disk rather than becoming a space. Existing exports are
not rewritten. Markdown source rewriting and recovery defaults are unchanged.

After the fix, the actual browser matrix passed **16/0**, checking decoded file
paths, real PNG dimensions and stylesheet application in both modes. Affected
filesystem transactions passed **50/0** with the experimental Markdown gate OFF
and **50/0** with it compiled ON; HTML still does not create persistent recovery
state. Production temporary-database routing **1176/0** passed. The native-transfer
test's historical byte assertion correctly failed at the intentional URL change;
its expected encoding was updated while preserving original archive/image checks,
then the affected group passed **89/0**. Total distinct final checks: **1381/0**.
This is local HTML destination-name acceptance,
not every archive-internal asset spelling or complete real-account acceptance.

No foreground window, screen capture, system authentication, service access,
installation or packaging. Frozen candidates and signed phase177 test App are
unchanged. Native physical-session acceptance and the authoritative compatible
first-certificate registration contract remain external gates.

## Phase 177: native cancellation, first protected load and multi-window acceptance

Two concrete bugs were fixed. Enabling the biometric policy while an encrypted
note was already open now prepares its locked password-page controls immediately;
no reopen, credential read or body grant is implied. The native read now evaluates
the existing biometric constraint asynchronously and uses the authenticated context
for a noninteractive exact-item read. Cancellation releases the worker promptly;
late system replies own completion state only. No password fallback, stored ACL,
enrollment, signing requirement or app-inactive body revocation was weakened.

Affected regressions: document routing **1176**, native keychain **36**, unlock
widget **99**: **1311/0**, 16.41 seconds. Actual signed native authentication passed
**10/0** with a real fingerprint and exact synthetic-item removal. Native cancel
and worker drainage passed in main-window multi-view runs, but those complete runs
did not pass: the app later became inactive during independent-window authorization.
Cocoa confirmed inactive and another frontmost process; the cause is unresolved,
not attributed to user interaction or treated as a product-wide success.

A separate coordination fixture also initially stopped at that boundary
(**22 checks, 1 failure** including cleanup). After the user explicitly kept the
test app foreground, the failure reproduced without app inactivity. A local stack
trace identified first-time Qt Quick-widget binding recreating native ancestors,
hiding the protected view and correctly revoking its fresh grant. Premature test
JavaScript made this happen early, but removing that polling still reproduced
the actual first-load problem. The constructor now initializes only its new blank
page, before it is shown or can hold a note grant. No existing DOM is replaced and
hide/session revocation remains intact. Temporary stack instrumentation is removed.

The corrected workflow then saved the independent note, but the fixture's nested
timer wait deferred WA_DeleteOnClose delivery. It now flushes only that accepted
close's own posted deletion; a refused close is never forcibly deleted. Final
production three-window acceptance passed **33/0**, including separate encrypted
grants, same-scope retirement, plaintext unaffected, both latest dirty DOMs retained,
independent close saved an encrypted archive, no main-view reload/reauthorization,
and normal main quit saved the latest body before eleven-stage cleanup.

Failed tests exit their isolated loop directly so a refused save-first close cannot
strand owned credentials. One older failed fixture had stalled on close; its exact
synthetic item was deleted by a separately signed scoped helper, Missing confirmed,
then only that test PID was terminated. No secret read or broad keychain deletion.
All other owned items were removed by their fixtures. Failed runs remain evidence.

Only new local signed copies and temporary synthetic databases were used. No
installed App, real notes, frozen candidates, physical lock/sleep state or recovery
default was changed. This synthetic three-window scenario is accepted, not every
possible real-data/multi-window combination. Physical-session acceptance and the
authoritative first-certificate service contract remain open. Automation stays
paused; this phase does not claim completion of the full app.

Development version: **14.4.25**. A new personal-use candidate has been built
locally with explicit consent. It is ad-hoc signed, unnotarized, not automatically installed and
not publicly distributed; system-authentication acceptance remains incomplete.

## Main-window native authentication and conditional management

The independently signed main-window workflow exposed a real integration bug:
the system Touch ID panel temporarily deactivated Cocoa, and the host canceled
the pending authentication. Two initial attempts failed and cleaned their owned
synthetic items. The fix preserves an explicit **native pending request only on
an already-locked password page**, not a body grant. The default native widget
delivers success only after the app is active again, with a 15-second bounded
delivery wait. Hard session events, hide/minimize, navigation and context changes
still revoke; manual/injected providers do not get this native-only exception.

After the fix, actual system Touch ID successfully opened the protected renderer
through the production main window and default password-page widget. Enrollment,
duplicate conflict, re-lock, exact deletion, missing-item and password fallback
also passed. That run passed **25 of 26 checks**, not a complete success: its last
synthetic notification lacked the intervening resumed event and was correctly
coalesced. The fixture was corrected; only the no-fingerprint save/quit continuation
was rerun, passing **27/0**, then **29/0** with conditional-entry checks added.
These are separate evidence, not a claim that all 29 used native authentication.
Two workspace notifications are posted only inside the synthetic process, with a
modeled resume between them; the actual Mac session/sleep state is never changed.

Normal source management is now available only with valid self-signing/keychain
prerequisites and an existing compatible certificate. Signed-without-certificate
and unsigned cases remain disabled. The settings tab is simply **Touch ID**.
Presentation performs no authentication/credential reads or policy writes.
Explicit checkbox/button action, a correct certificate password and successful
native enrollment are still required to opt in. No automatic policy enablement,
credential remembering, real-note conversion or password-recovery backdoor.

Only affected regressions ran: unlock widget **99**, document routing **1174**,
note access/management **83**, settings **29**; **1385/0**, 17.67 seconds. The final
synthetic Cocoa main-window continuation **29/0** verifies conditionally available
management, native item enrollment/conflict/deletion, manual fallback, dirty DOM
preserved while locked and latest content saved before normal eleven-stage quit.
The earlier actual fingerprint success remains separately recorded. All owned
synthetic items are confirmed absent. Initial fixture build errors and failed
runs are retained; no unrelated full suite or PDF repetition.

Only new signed test copies were prepared. Forty-six previously audited bundled
components and other inherited bytes/links are unchanged; the current main's
arm64/code/dependencies, strict recursive signature and actual bundled-Qt mapping
are checked incrementally. Existing App, frozen candidates and the previous test
App are untouched. No install/binary/profile/identity/raw-log upload. Broader real
account/multiwindow/physical lock/cancel-panel acceptance and the authoritative
first-certificate service contract remain distinct. Recovery OFF, automation PAUSED.

## Earlier focused system/widget test (phase 175)

With explicit user permission, a separate self-contained Apple Development-signed
main-client test App was prepared locally with the matching development profile.
It uses the current incremental executable/resources and the frozen candidate's
pinned frameworks. Only the new copy is signed/renamed. Minimum main entitlements,
the existing engine-helper rights and system protections are retained. All 47
arm64 components pass dependency and strict recursive-signature checks; main and
pinned-engine code sections match their intended inputs.

The opt-in interactive route runs before account bootstrap. Its uniquely named
executable refuses normal startup even with no arguments or malformed options.
It creates only a random in-memory certificate/password and unique synthetic
scope. The actual production native backend and default asynchronous unlock
widget use the system data-protection keychain: create-only enrollment, duplicate
refusal, native authentication, one-shot credential consumption/context checks,
exact owned-item deletion and absence confirmation pass **10 checks, 0 failures**.
The user was notified before the fingerprint request. The process exits normally;
actual mappings confirm Qt comes from this signed App without SDK overrides.
The related unsigned/refusal self-test passes **36/0**; no unrelated suite ran.

Three initial launch prechecks aborted because the replacement executable still
contained the build SDK rpath, causing a Team-ID mismatch for QtXml. The user's
crash report identifies that same isolated process. Only the new copy's rpath was
corrected to bundled frameworks and it was re-signed; the final prerequisite and
interactive runs pass, and no-argument/malformed-option refusal is verified.
Initial failures remain in local evidence; no signature enforcement was disabled.

The test App/profile/Team details, raw logs and synthetic material remain local.
No existing App, frozen candidate, real notes or normal account was modified or
started. No binary upload, install or new DMG. This accepts the actual signed
main-binary native-backend/widget chain, **not** complete main-window note unlock,
cancel/lock-screen/multiwindow or real-account acceptance. Normal enrollment stays
disabled; the first compatible certificate-registration service contract is still
external. Recovery stays OFF and automation PAUSED.

## Latest source-only UI adjustment

The PDF submenu now uses the parallel titles **Reading Layout…** and **A4 Layout…**
(Simplified Chinese: 阅读版式… and A4 版式…). Visible hover descriptions distinguish
preserving the reading viewport width from fixed A4 paper. The settings tab is
now **PDF Export**, with a wrapping explanation that both modes use the existing
margin settings. Export mode values, action names, dispatch, page layout logic,
default margins and the no-remembered-choice rule are unchanged. English strings
and the actual Simplified Chinese catalog are covered.

Only two directly affected offscreen Qt-widget groups ran: submenu **21/0** and
settings/translations **29/0**, 1.18s total. The real temporary settings window,
compiled translation catalog, original margin-page navigation and stored values
participate. The PDF settings screenshot was visually checked for complete text
and unobstructed controls. No PDF rendering or unrelated full suite was repeated.
This source-only change is **not** in the frozen 14.4.25 candidate delivered below;
no installer was rebuilt or replaced. The user reports basic trial use is normal.

## Development signing setup completed

After explicit authorization for Apple automatic provisioning, an independent
local Xcode helper project was created for the existing application identifier.
Xcode's automatic-signing build succeeded with the verified Apple Development
identity and obtained/installed one matching macOS development profile. The helper
has a valid strict recursive signature, the expected team/application identifiers
and a matching keychain access group. The embedded profile is unexpired, targets
macOS, permits those identifiers/groups and contains the actual signing certificate.

The helper project, team configuration, profile and raw signing logs stay local;
identity names, Team ID, profile contents/device lists and private keys are not
uploaded. No private-key bytes were read/exported by the agent; Xcode used the
existing signing identity as authorized. The helper was not run. The previous
profile-location gate is resolved for this setup; server-side registration details
are not independently inferred from build success.

The installed/main candidate remains untouched and ad-hoc signed. No normal app,
real note, biometric prompt or data-protection keychain credential was opened.
This was signing/provisioning setup, not native Touch ID or complete-client
acceptance. The separately scoped main-binary native-backend/widget test is now
recorded above; broader main-window acceptance remains distinct. Normal biometric
management remains disabled; the authoritative first-certificate registration
service contract is a separate unresolved gate. No main app was repackaged or installed.

## Local candidate delivery

The final bundled application passes three affected synthetic workflows:
actual Cocoa MainWindow/new-note/save/quit **16 checks**, editor runtime **37**,
and actual temporary native-note transfer **89**, all with zero failures. Actual
process mappings/load records confirm bundled Qt frameworks without SDK overrides.
All 47 Mach-O components pass arm64/dependency and strict recursive-signature
checks. Main and pinned-engine code sections match the accepted development
inputs; the original icon and helper signing permissions are unchanged.

The DMG is 158,022,788 bytes and passes image verification. A read-only mount
matches all 1,626 App files/73 internal links and all five auxiliary payloads;
the mounted copy passes the same limited 16-check workflow, recorded separately,
and is normally unmounted. The local source snapshot has 2,741 entries, with 140
explicit overlays and 2,740 source-record entries verified. It is not an exact
remote-tree archive. No normal account, real notes, services, authentication,
desktop capture, unrelated full suite, install or binary upload was used.
See [candidate notes](release-14.4.25-candidate.md) for scope and installation advice.

## Latest focused fix

Notes opened within one second of creation used to force the web editor on while
the host still believed it was reading. A subsequent switch to reading could then
be skipped. The host now chooses the initial mode once, under the existing lock
and permission checks, and the web view follows it. New-note title bookkeeping
remains separate; existing notes and future timestamps retain the reader setting.

The explicitly authorized synthetic Cocoa workflow checks initial host/page
agreement, actual bare-math rendering after a single reader transition, exact
Markdown retention on edit/read/edit, production save, existing/future timestamp
behavior and latest dirty content saved before normal eleven-stage quit cleanup.
Its final run passes **16 checks, 0 failures**. The one affected document-routing
group passes **1174 checks, 0 failures** in 12.31s; its stubbed host initializer is
not evidence for the new mode fix, which is covered by the actual foreground run.
No unrelated full suite or PDF export was repeated. Failed driver attempts and
the pre-fix host-reader/page-editor reproduction remain recorded locally.

No normal account, real notes, desktop capture, authentication, installation or
packaging was used. Remaining signing/service-contract and broader real-client
acceptance gates below are unchanged; this does not complete system acceptance.

## Improvements since the previous GitHub checkpoint

- Personal-note permanent decryption now requires a fresh certificate password
  and explicit confirmation after the save frontier. It preserves the complete
  encrypted archive, stages the original plaintext ZIP and rolls metadata/file
  changes back on ordinary failure. Automatic rollback failure is reported with
  the retained backup path; this is not crash/concurrent-writer atomicity.
- Opted-in protected-note screenshot capture retains pixels in memory across
  intentional hiding. Insertion requires a new authorization on the original
  note after capture; navigation, certificate/page change, policy retirement,
  hard session/sleep boundaries and shutdown discard pending pixels.
- Markdown display-math environments and common LaTeX-style theorem containers,
  retaining source text and cross-references.
- PDF equation numbering and internal destinations; separate electronic-reading
  width and A4 export choices, with no remembered submenu selection.
- Export transaction/rollback work and an experimental persistent-recovery
  implementation. Persistent Markdown recovery remains **OFF** by default.
- Local compatible-certificate generation/backup components and biometric
  components. Normal management is now conditionally available as described
  above; explicit enrollment remains required. First server-side certificate
  registration and broader native acceptance are not accepted as complete.
- Simplified-Chinese resource selection, Settings naming, title-only group sync
  default, and scoped account-page cache configuration.
- Revocable document-read/output permissions across the implemented export,
  share, clipboard-processing, batch, inspector and editor-command paths.
- Guarded queued paste/return, automatic-title replies and password/editor focus.
- Synchronous toolbar formatting/insertion checks, preserving original command
  arguments, encoding and modification markers while refusing stale continuations.
- Image-file chooser returns and plain-text paste now retain their original
  document-read permit across modal/clipboard callbacks. A newer authorization
  cannot revive an obsolete request. Already started file copies are not undone.
- Link-window queries, presentation and acceptance retain the original read
  permit and a separate request epoch. Hide/revoke closes an open link dialog;
  stale continuations cannot consume a newer request. Original URL normalization,
  application modality and one-shot command behavior are preserved.

- Attachment/image paste, attachment insertion and thumbnail continuations now
  retain the original read permit. An obsolete paste is consumed instead of
  falling through to browser default paste. Completed copies/attachments remain
  in the original note; subsequent insertions stop. Image-conversion failure now
  explicitly returns false rather than falling off a non-void function.

- Search-window signals and selected-text publication now retain their original
  read permit/request epoch. Hide/revoke disconnects and hides transient search
  UI without clearing fields or saved state. A guarded presentation overload
  checks each display step; the original overload and find/replace behavior stay.
  Already dispatched searches/replacements are not retroactively undone.

- Screenshot orchestration binds an originating document/request and a read
  permit, coalesces duplicate clicks, and refuses obsolete timers/results.
  Capture helpers are detached and deleted later, not inside their signal stack.
  Restore targets only the originally hidden visible window; shutdown does not
  reopen it. Resource lookup, PNG output and insertion retain the bound permit.
  Intentional hiding must not bypass opt-in authorization. Legacy notes bind a
  permit synchronously after that hide, never when a delayed result arrives.
  The protected opt-in hide path currently stops and restores the password view;
  its future capture/reauthorization UX is not claimed complete.

- Initial HTML caches now record their owning note/knowledge base, including
  the actual editor-save producer. Authorized access to a different note cannot
  return old retained cache bytes. Initial preparation retains its original read
  permit across publication/script/CSS boundaries and atomically replaces the
  temporary decorated file only while authorized. Failure prevents loading.
- Initial load dispatch retains the originating host access, page and loader
  sequence across its own callback invalidation and simulated loadStarted.
  Reentrant retirement returns an expiring ticket. Clear/open/reload stop after
  stale retirement or owner destruction; save work and retained DOM are not
  canceled. Already dispatched navigation is not retroactively retracted.

- Asynchronous editor setup now retains its original read permit, weak page,
  mode and separate attempt epoch through metadata/style preparation, dispatch,
  readiness and initial find/plugin notification. New authorization or a newer
  attempt cannot revive an old result. Successful mode changes retire old setup;
  no-op or failed-save mode changes keep the valid existing attempt alive.
- The readiness helper captures weak owner/page before invoking predicates and
  rechecks liveness after them. Timeout, single in-flight query and one-shot
  delivery remain; renderer destruction still reports failure when permitted.
  Already dispatched JavaScript is not retroactively canceled.

## Latest local verification

With explicit user authorization, a focused Cocoa run now instantiates the actual
main window, production note views, renderer, Qt dialogs, save worker and exit
handler using only disposable synthetic data. **32 foreground checks pass**:
bare math rendering, both PDF submenu choices, Markdown source preservation,
protected screenshot return/reauthorization/insertion, encrypted image save,
wrong-password and confirmation-No refusal, confirmed permanent decryption via
the real information popup, backup/state refresh and dirty-content save before
the original eleven-stage exit cleanup. UI commands are programmatically driven.

Visual PDF review found a real long-multline issue: an automatically wrapped body
could touch its normal-size equation badge in reading-width export. Supported
row fitting now reserves the first-row tag gutter too. A still-inseparable wide
multline SVG is fitted locally inside that gutter, preserving ordinary text and
number size; print-only styles/aspect ratio are restored afterward. Final PDFs
have four valid internal destinations each, all 16 prose markers, the same 12pt
prose in both modes and exact exported Markdown. Both complete pages were rendered
and visually reviewed with no observed body/number collision or text clipping in
this fixture. Reading width is 553.92pt; the A4 sample is about 594.96pt wide.
One affected native-editor/runtime group also passes **37 checks, 0 failures** in
72.35s, including three edit/read/print restoration cycles. No unrelated full
suite was rerun; old phase-168 counts below are retained, not accumulated here.

The final run follows test-driver corrections for distinct reference markup,
chooser readiness/filename, nonempty synthetic policy identity, duplicate mode
commands, the <=1-second new-note auto-edit branch and Cocoa's empty message-box
window titles. Eight incomplete attempts remain recorded locally; one isolated
failed process stalled in cleanup and was terminated without touching another
application or normal notes. Final results cover existing-note workflows, not
that special just-created-note branch. No result from an incomplete run is
presented as overall acceptance.

No normal account, real notes, desktop capture, native Touch ID/Keychain, service,
installation or packaging was used. The capture payload is a synthetic image,
delivered synchronously before the real helper's 200ms screen-grab timer can run.
Screenshots contain only this synthetic app's own widgets. Save dialogs are real
Qt widgets on Cocoa, not native NS(Save/Open)Panel or physical-pointer acceptance.
The existing isolated-window property disables sync/listeners; remote page
requests are intercepted, the local-only proxy remains in place and system
protection is unchanged. Normal Touch ID remains disabled. The manual-only probe
is not registered for unattended CTest execution.

### Retained phase 168 evidence

The two unfinished local features are implemented in one combined batch. Both
existing cancel-encryption UI entrypoints use the new save-first, fresh-password,
explicit-confirmation workflow. The old password-free database API refuses
conversion. The encrypted backup includes the full original archive and remains
available after success or an ordinary late failure. Certificate and shared
save-password cache are not deleted or changed. Lists and the current note receive
post-commit metadata/body updates; the now-plain note retires its old access gate.
Groups and encrypt-all databases are outside this personal-note conversion scope.

Only three directly affected groups ran: **1287 PASS, 0 FAIL in 15.11s**:
permanent decryption 30, document routing 1174, note access/session 83. Tests use
actual temporary databases, native ZIW encryption/decryption, ZIP/binary resource
bytes, atomic file replacement, SQL rollback, saver FIFO fences and extracted
production routing methods. Prompts, capture, page/editor and session events are
isolated collaborators, not normal-client/native authentication acceptance.

A final backend-only change preserves the latest ancillary metadata instead of
copying pre-prompt read counts/flags. Its same 30 checks passed again, including
an actual database read-count update between preparation and conversion. Only
that affected group was rerun; the 1287 count does not double-count this repeat.

Protected screenshots publish no resource file before a new successful unlock;
wrong passwords retain pending pixels without inserting them. Returning to the
original editor preserves unsaved DOM/save state. Soft inactivity revokes reads
but retains the capture; hard session events escalate even after earlier soft
inactivity and discard it. Normal Touch ID remains disabled.

Intermediate builds exposed new-code include/API mistakes, which were corrected.
The first associated run passed decryption and note-access groups but had three
routing fixture failures after splitting legacy and opted-in capture behavior.
The final run follows correction of those fixture assumptions and post-conversion
reload integration; failing logs remain local. No full-suite rerun, native screen
grab, real account, Keychain, note-service call, installation or packaging.

### Retained phase 167 evidence

The current local implementation batch is closed, not the entire native-client
acceptance plan. Sibling saved notifications now reload only after the actual
modified-state helper confirms clean content with the original live read permit.
Dirty checklist content, unknown state, new edits, relock, navigation and owner
destruction retain DOM and save credentials without an automatic save/overwrite.
The separate-window failure receiver consumes its load result, matching the main
window; this small connection fix is source-checked and compiled, not a native
separate-window acceptance claim.

Only the affected routing group runs: **1160 PASS, 0 FAIL in 12.76s**, with 9 new
checks and all old 1151 assertions retained. The real saved signal reaches the
actual receiver/modified helper. No full/native suite, real account, foreground,
Keychain, service, package or installation. An intermediate test-binding build
error was corrected; no failing runtime is counted as acceptance. One tracked
source absent from the old map was explicitly reconciled with the previous
remote blob before being added to the baseline. The initial snapshot procedure
stopped on that omission; reconstructed mapped snapshots match the old digests.

The backlog below replaces repeated lists of method-level testing subphases.
Do not reopen already accepted local chains merely because normal/native
acceptance remains outstanding. Further changes need a concrete defect, an
affected integration boundary or an explicitly selected unfinished feature.

### Retained phase 166 evidence

Phase 166 fixes two related save-completion risks and verifies three associated
chains. Completion now keeps weak editor/view ownership across saved publication
and checks the compatible main-window cast before requesting sync. Publication
checks the originating note/library, stops after receiver destruction or a note
switch, and emits a stable copy of the current title. Failed saves mark retained
content dirty; successful receipts do not clear newer edits or reauthorize reads.

The exact main-window save connections run with actual Qt queued receipts, actual
publication signals and temporary FIFO encrypted archives. Failure preserves an
exact original archive behind an ordinary output blocker; explicit retry reaches
publication and reconciles the same-note failure. The main HTML command runs
through the actual snapshot producer, queue, archive receipt, read fence and body
transaction, publishing the newly saved content rather than a stale archive.

The full main PDF/HTML/Markdown command and export-controller bodies are compiled
from current source. Reading-width/default and explicit A4 forwarding, no remembered
mode, in-flight coalescing, dialog cancellation/relock, obsolete replies, newer
reservations, invalid data/settings and owner destruction preserve prior output.
Markdown queries retain their original authorization before resource/body commit.
Renderer bytes, active-editor selection, sync requests and modal UI are recording
collaborators. Synthetic PDF headers/end markers test atomic writing, not visual
layout or native printing. The normal MainWindow is not instantiated.

Final verification runs **only 1 affected group: 1151 PASS, 0 FAIL in 12.78s**,
once after the final build, with 31 new checks and all old 1120 routing assertions
unchanged. Note/management and native/full suites were not rerun. The first fixture
omitted the producer's extracted file paths and used the product enum rather than
the existing shell mode convention, causing one failure; required synthetic state
was supplied without weakening product checks. That run and an intermediate enum
qualification build error remain separate evidence, not negative-control claims.
Only the two completion/publication product bodies change; export/layout and
main-window source bytes otherwise remain unchanged.

### Retained phase 165 evidence

Phase 165 connects the default-disabled management host to Settings and scoped
open-note retirement, and prevents concurrent management mutations from separate
hosts. The actual Settings constructor and print-margin navigation run offscreen
against an isolated database. Its new page stays disabled; opening/selecting it
does not query Keychain, enable policy or dispatch a management operation.

The actual host snapshots the personal account/server/certificate and rechecks it
before starting and accepting results. Policy persistence is read back only after
provider enrollment success. Account, server or encrypted-key revision changes
reject late results. A process-wide try-lock prevents overlapping enrollment and
forget operations, without queuing an obsolete intent or automatically retrying.
Hiding a dispatched host does not release that mutation until its worker returns.

Management start and successful completion publish only a policy digest. Matching
encrypted open notes retire their old grants; newly opted-in legacy gates become
fresh-authentication gates even with a warm save password. Plain notes and other
scopes are unchanged. DOM, certificate, backups and queued-save credentials stay.
No notification grants access. Destroyed views detach; reentrant host destruction
prevents dispatch. Providers are synthetic and document handlers use the actual
extracted source, not a normal MainWindow instance.

Final verification is **2 associated groups: 1203 PASS, 0 FAIL in 13.56s**
(83 note/host/settings, 1120 routing/quit), once after the final build. All previous
1178 assertions remain unchanged, with 25 new checks. No full-suite or native
authentication rerun. A missing application type include caused one intermediate
build failure. An initial test placed the policy write before its unrelated-scope
assertion; the existing read gate correctly refused access. The test order was
fixed without weakening the product gate. Failed evidence and the earlier 1200
passing checkpoint are retained separately, not added to the final pass count.

### Retained phase 164 evidence

Phase 164 completes three associated isolated stages: an explicit biometric
management component, session-access retirement, and the complete extracted
normal-quit coordinator. Only two affected groups were run after the final build:
**1178 PASS, 0 FAIL in 13.18s** (68 note/management/session, 1110 routing/quit).
There are 43 new checks; all previous 1135 assertions remain unchanged. This is
not a full-suite rerun, native authentication acceptance or a normal-window test.

The new management component starts disabled and has no enabled normal-client
entry. Enrollment requires one-shot consent, locally verified password and a
current account/certificate scope. Policy persistence follows provider success.
Conflicts never overwrite automatically. Forgetting local memory retains the
fresh-authentication policy, certificates, notes, backups and save credentials.
Workers do not access widgets/database objects; retired or destroyed views ignore
late results. A dispatched task remains observed to prevent same-widget overlap.
Already committed native changes cannot be undone by hiding the component.
All test providers are synthetic; real Keychain operations were not exercised.

An initial isolated test crashed when a password text-change receiver destroyed
its widget inside Qt's unfinished clear operation. Internal password clearing
now blocks that notification, while still clearing the password and consent.
The regression checks suppressed internal notification, factory reentrancy and
destruction, post-dispatch destruction and late completion. The failed run is
retained separately, not counted as acceptance. Two intermediate build errors
were corrected and retained in local evidence as well.

The normal document source subscribes to a session monitor: Qt inactivity and
public macOS workspace sleep/session-resign notifications retire opted-in access,
cancel pending output and display the password view without clearing DOM or
canceling saves. Activation alone never grants access. Public SDK subscriptions
compile; deterministic tests disable native subscription. Real sleep, session,
lock-screen and Touch ID interactions remain unaccepted.

The entire actual prepareForQuit body is extracted with only class/collaborator
substitutions. Actual UI rollback/freeze and FIFO encrypted archive writes are
tested for refusal, two save passes, simulated final sync, idempotence and late
external changes. Network, modal UI and note enumeration are collaborators; the
normal MainWindow is not instantiated. This advances the earlier callsite-only
evidence but does not claim whole-client quit acceptance.

### Retained phase 163 evidence

External tasks now retain a SHA-256 comparison of their initial or most recently
admitted converted body. A single successful file read supplies both the body
and comparison; the previous importer's UTF codec/BOM, HTML/text conversion and
empty-file behavior are retained. Read failure is not a successful blank save.
An admitted comparison is not a disk acknowledgement.

Normal quit checks external tasks before local-save preparation and again after
its final local-save pass. Changed, missing, unreadable, invalid or untracked
baseline files refuse quit. Busy or failed external saves also refuse. The check
never submits a save, rewrites either version, removes a task or clears an error.
The existing window, note and external file remain for reconciliation. Unchanged
stale cache cannot overwrite an independently newer note. This is conservative
conflict preservation, not automatic merging or a guarantee for external edits
not yet written to disk. Direct/forced termination is outside normal quit checks.

Phase 163 uses **1 incremental/associated group: 1095 PASS, 0 FAIL in 14.27s**,
one run after the build completes, with 9 new checks and all old 1086 assertions
unchanged. A new locked query reports current unresolved per-note failures from
the saver's existing set. Quit uses that query after checking idle state instead
of the sticky historical transfer-error flag. Only successful real same-note
save removes a current failure; another note's success does not reconcile it.
Historical error state and all other transfer-error consumers remain unchanged.

Tests retain two synthetic encrypted archives behind ordinary output blockers,
observe real failures, restore each exact original, and explicitly retry via
the actual watcher-save method and FIFO saver. Independent success and only one
successful retry still refuse quit; after both retry saves succeed it may pass
while historical error remains set. A new unadmitted file change still refuses.
Original conversion, caches and save credentials remain. No blanket error reset,
automatic retry, conflict merge or user-note conversion is introduced.

Actual Qt, temporary files, per-note failure tracking and archive persistence
participate; process/modal UI remain collaborators. MainWindow's existing two
callsites are unchanged, source-verified/compiled rather than runtime accepted.
Unrelated bytes and prior evidence are verified. No native-async/full-suite rerun
and no old-source negative-control claim for this additive query stage.

The final-file guard is a refusal, not a routine approval or overwrite prompt.
Normally save the external file again and verify return-save before retrying
quit; conflicting versions must be retained separately first. Explicit successful
same-note retry now reconciles current failure without deleting historical error.
Whole-application quit and real external-process/UI acceptance remain separate.
No external process, normal account, native UI or service request participates.

These are isolated local tests, not GitHub CI results or complete native UI /
system-authentication acceptance. Some page, diagnostic, clipboard-operation and
transport collaborators are synthetic. Unchanged/unrelated groups are not rerun
automatically; wider regression requires an affected boundary, failure or other
specific reason. Historical results are not silently counted as current acceptance.

## Remaining bounded work

1. **Implemented and locally isolated-tested:** fresh-password, confirmed,
   backup-preserving personal-note decryption and protected screenshot
   reauthorization, alongside document read/output retirement,
   initialization/resumption, save receipt/frontier/quit coordination, external
   watcher/retry, export controllers and conditional opt-in biometric management. Their
   method-level stages are closed; this does not equal whole-client acceptance.
2. **Existing-certificate prerequisite, not first registration:** a compatible
   account certificate and its known password are required for the relevant
   operations. Do not assume real-account availability from synthetic tests.
   Development signing/profile configuration and the focused native-backend/widget
   Touch ID chain are accepted above; source management remains conditional on
   valid runtime signing/authorization and an existing certificate, with policy
   default-off and explicit native enrollment required. First-certificate service
   registration and its unresolved contract are outside the current user scope.
   No service probing, automatic certificate creation or replacement.
3. **Delivery and actual-client acceptance, not repeated source tasks:** a new
   source-matched signed test build is needed before user acceptance; frozen
   installer171 and signed test181 do not contain fixes183/184. The focused
   existing-note main-window/Qt-modal/export/save/quit subset above is accepted;
   normal-account and separate-window/shared-modal combinations,
   switch-user and broader system-event combinations (user-operated lock accepted
   in phase180; actual sleep/wake hard-notification route accepted in phase181),
   native print/clipboard/
   chooser/capture/search/inspector, external process/manual conflict handling,
   representative attachment/encryption and new-candidate avatar update/reopen.
   Foreground permission covers only synthetic isolated data; normal-account
   operations and actual screen capture remain outside it. The additional explicit
   native-authentication grant was used only for the isolated synthetic test above.
4. **Retained limitations:** the offline account-chooser driver now checks actual
   readiness/completion and passes bounded verification in phase179, but the
   historical intermittent failure's complete cause is not established
   (the normal account override stays disabled);
   HTML destination-basename resource URLs are accepted in phase178 above;
   crash/concurrent-writer atomicity is not accepted. No repeated rerun is
   scheduled to hide these gaps.

VoiceOver, App Store/public binary distribution and zero-group non-default
selection persistence are intentionally excluded, not pending development work.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The latest local-delivery manifest is [checkpoints/2026-10-22-phase186.json](checkpoints/2026-10-22-phase186.json).
The preceding delivery-gap handoff is [phase185](checkpoints/2026-10-22-phase185.json).
The latest focused functional integration is [phase184](checkpoints/2026-10-22-phase184.json).
Permanent-decryption feedback is recorded in [phase183](checkpoints/2026-10-22-phase183.json).
The existing-certificate scope decision is [phase182](checkpoints/2026-10-22-phase182.json).
The latest signed physical-session acceptance is [phase181](checkpoints/2026-10-22-phase181.json).
The manual lock/application-inactive acceptance is [phase180](checkpoints/2026-10-22-phase180.json).
The bounded chooser-fixture improvement is [phase179](checkpoints/2026-10-22-phase179.json).
The HTML resource URL correction is [phase178](checkpoints/2026-10-22-phase178.json).
The synthetic three-window acceptance is [phase177](checkpoints/2026-10-22-phase177.json).
The focused system/widget test is [phase175](checkpoints/2026-10-22-phase175.json).
The signing setup is [phase174](checkpoints/2026-10-21-phase174.json).
The UI change remains [phase172](checkpoints/2026-10-21-phase172.json).
The frozen local candidate is [phase171](checkpoints/2026-10-21-phase171.json),
whose product-source input remains [phase170](checkpoints/2026-10-21-phase170.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).


# 2026-10-23 第203轮最终包与外部测试资料整理

第202轮修正进入新build203，不覆盖194／195／200；48组件／严格签名、包内14项
中文设置检查通过。生产主程序与翻译一起更新，归档工具将i18n纳入明确覆盖与
未覆盖输入检查，防止源码备份保留旧翻译。必要关联核验后制作只含App／入口／
说明的DMG，源码与详细校验在旁。未自动安装或改现用App、原有资料。

按新增授权清理22处额外测试配置／缓存等至废纸篓；固定186测试资料目录已不存在。
四个旧剪藏／图标探针容器被系统拒绝移动，只有系统元数据和目录／链接，记录路径
待用户处理；不绕过保护。生产剪藏器、原有资料与钥匙串保留，自动任务继续暂停。

