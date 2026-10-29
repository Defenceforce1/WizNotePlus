# 原始HTML快照退役：独立验证

macOS、Qt6.11.3、CMake和Python。测试从当前WizDocumentWebView.cpp提取实际
clear方法，只替换类名；历史方法作为明确的保留引用负对照。
同时提取当前DocumentLoaderSaver.cpp的save交接函数，验证其SAVEDATA独立保留
HTML与笔记身份／路径／标志。线程启动和数据库是禁止执行的协作者，不运行保存工作线程。

使用合成16MiB UTF16快照、真实QObject／QPointer／回调代次和Qt排队QString
传递，核对显式about:blank前释放旧缓存、重入／销毁保护、旧回调退役，以及
排队保存仍独立拥有完整快照。仅枚举自身malloc计数，不改变分配器或强制回收。

这里的load是记录调用的协作者，未启动主程序、WebEngine、账户或笔记数据库，
不作完整界面／磁盘保存／长期内存验收。活动分配释放不能直接等同物理驻留下降。

在项目内新隔离构建目录配置本目录后，只构建cache-probe并运行同名程序或
snapshot_cache_retirement测试即可；不需要重跑公式／PDF或全部兼容矩阵。

第265轮追加path-probe／source_path_index_bound：提取当前加载接纳、加载完成、
保存交接及FIFO取出方法，比较4096篇合成笔记的路径索引范围。核对迟到、错误
请求者／笔记／代次、授权变化、DB失败和重入不覆盖当前索引；已复制的保存路径
及队列任务仍完整。DB查询和页面派发是隔离协作者，无实际数据库、渲染或磁盘保存。
