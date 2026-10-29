# xctrace附加失败重试：Allocations与Time Profiler分离

2026-10-27，第273轮。用户询问首选工具为何附加失败并要求再试。本轮只复核
首选工具，不重复272的WebEngine内存组，不处理真实App、账号或笔记。
承接272的775份源码，AGENTS.md不访问，HEAD／索引／分支及既有修改保持。

## 环境与控制

本机xctrace27.1(27A9275)，Xcode路径有效；DevToolsSecurity -status只读输出
Developer mode is currently disabled。当前用户属于admin及_developer组。
本机DevToolsSecurity(8)说明，该开关管理Apple签名调试／性能工具的开发者
授权策略，启用后指定组用户不需额外密码；它不等同于关闭SIP、V8隔离或
整个系统保护。开关状态是一条线索，不能单凭它认定本次错误原因。

本轮没有执行-enable／-disable、sudo或更改授权数据库，没有重新签名目标、
修改描述文件或启用get-task-allow。仅只读查看自有校准二进制签名状态。

沿用272的已知8MiB／16MiB申请释放小程序，延长尾部等待以避免短命进程。
在项目内独立目录编译一次，两组使用同一二进制。先从普通子进程启动，
确认输出CAL273 ready及实际PID／完整路径，再由xctrace显式--attach该PID。
两次录制结束时目标均仍存活，之后核对路径，仅正常终止自有校准进程。
因此“程序过快退出”及“目标本身无法独立启动”可在这两组控制中排除。

xctrace自身沿用正常调用环境，不覆盖工具HOME；校准目标使用本项目新建的
合成HOME／TMPDIR，不读取真实资料。只录制指定PID，不使用--all-processes。

## 实际结果

| 10秒定向录制 | xctrace退出码 | 录制内容验收 |
| --- | ---: | --- |
| Allocations，显式附加存活程序 | 2 | Failed to attach to target process；失败 |
| Time Profiler，同一二进制独立进程 | 0 | 录制完成；导出time-profile表5条采样 |

8项工具控制核对通过。Time Profiler不只是生成trace文件或退出0，还实际导出
了采样行；目标以等待为主，因此仅5行并非高频繁忙采样覆盖。这些是CPU调用
采样，不能代替malloc申请／释放事件或解决WebEngine物理高水位。

此次对照说明“所有分析工具都无法附加”不成立。问题范围缩小至Allocations
这一采集路径或它特有的条件，但尚未证明具体是开发工具授权、分配跟踪服务、
目标可检查条件还是工具运行故障。Time Profiler成功也不能证明其它工具需要
的授权全部完备。不能为了追求结论把任一可能性写成已经确认的根因。

## 进一步日志及限制

只查询本次确切目标PID／工具PID相关的诊断日志，没有无条件导出系统日志。
Allocations记录“unable to obtain a symbol provider”及“Tracing Error: Failed
to attach to target process”，另一个详细Tracing Error被系统标成<private>。
符号提供者错误可能是附加失败后的结果，不能反向当作已确认原因；AppKit的
退出信息同样不当作主要根因。没有尝试解除日志隐私或关闭系统保护。

失败Allocations trace的目录导出第一次15秒超时；录制全部结束后单独再导出
一次仍在20秒超时，原trace和超时记录保留。没有把文件存在、提示ready to be
viewed或超时输出当作可用分配数据。成功Time Profiler导出的目录包含工具自动
记录的环境元数据，证据版已删去environment及设备唯一标识，只保留相关表和
目标信息；原trace仅保留项目本地独立临时根，不进入源码同步清单。

前后DevToolsSecurity状态相同，测试结束无自有目标或xctrace运行残留。既有
272的malloc_history已知申请／释放控制与6份原生图仍有效，没有因此被否定。

## 结论与后续门槛

重试已经完成，首选Allocations仍未修复；这次明确排除了短命目标，并用同一
二进制证明CPU采样可以成功。开发工具授权策略未启用是一条可检查的线索，
但现有错误没有给出足够证据直接归因或自动更改授权策略。

若要继续诊断这条路径，下一步更有针对性的是在用户可见的Instruments中，
仅选本轮合成小程序，观察Allocations是否出现授权提示或更完整错误；涉及
系统授权／管理员交互时由用户自行处理，不在无人值守脚本里放宽策略。
这不是当前源码功能缺陷的修补，也不需要改现用App或真实笔记。

本轮无产品／仓库测试代码改变，无新增内存收益或完整客户端验收；271产品
优化保持。源码说明、检查点及证据仅本地保存，不打包、联网、GitHub同步、
安装、改签名、改设置或重建自动任务。现用App、256及重要回退、266／255／199
原生产构建、正式资料根和Git状态保持。整体WebEngine高水位仍未解决。

证据位于outputs/exploration-20261027/phase273，新合成根为该轮preflight所记
项目tmp目录。有限对照已收口，不重复运行同样模板或扩展无关测试。
