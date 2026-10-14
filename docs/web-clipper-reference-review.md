# 剪藏参考项目与官方资料核查（2026-10-14）

用户提供两个参考项目。本次只下载公开源码作静态分析，没有运行其后台脚本、自测、登录或上传操作，也没有复制代码、图标或依赖到本项目。现有 Intel 客户端和旧剪藏器未启动或替换。

## 参考项目

| 项目／核查快照 | 实际架构 | 有价值的参考 | 不能直接等同于已解决的问题 |
| --- | --- | --- | --- |
| [damoncui668/wiznote-clipper](https://github.com/damoncui668/wiznote-clipper)，`05c2460a6d4a4d556c0543e4182a54274738a340`，2026-04-20 | Manifest V3，Chrome service worker；弹窗使用普通 JavaScript；默认官方服务，也可配置自建服务 | 正文／整页／选中／链接四种模式、页面选区预览、分类与标签、服务端地址配置、保存后验证 | 不包含原生 Safari 宿主或系统分享扩展；Chrome API、权限、跨域图片、Safari 加载／签名仍需移植与实测；它采用 `/ks/note/create/:kbGuid` 和 `X-Wiz-Token`，不证明该路径适用于所有云端／私有服务版本 |
| [cognition9144/WizWebClipperWE](https://github.com/cognition9144/WizWebClipperWE)，`96e1b4876797bc84e6064375dc1f275753cb4c31`，2018-05-02 | Firefox WebExtension，Manifest V2，后台页面，AngularJS／jQuery；自动注入内容脚本 | 正文识别、选区、遮罩预览、计算样式内联、分类标签与保存状态查询的旧实现 | 仍有 MV2、旧库和 `unsafe-eval`；不是新版 Safari 原生 arm64 剪藏器；不能不经审计就打包到新客户端 |

源码位置分别包含 `manifest.json`、`background.js`、`scripts/wiz/Remote.js`、`scripts/wiz/WizConstant.js`、`scripts/PageInfo.js`、`scripts/GlobalUtils.js`、`scripts/ClientClipper.js`。重点核对输入提取、样式保留、登录／令牌存储、保存接口及重试。

## 许可与复用边界

- 第一个项目 README 声称 MIT，但本次快照没有独立 LICENSE／COPYING 文件，且保留了旧官方源码和第三方提取库；不能仅凭 README 徽章就认为所有文件的权利链已核实。暂只参考功能和公开协议，不搬用代码／图片。
- 第二个项目包含 MPL-2.0 LICENSE；README 又说明图标取自 GPLv3 WizQTClient、源码移植自旧官方扩展。第三方库／图标需分别核查，不将整个目录当成单一许可证组件复制。当前原生剪藏代码为独立编写，遵循本仓库 GPL-3.0-or-later。
- 两个社区 README 的“官方停更”“接口废弃”“所有服务端均兼容”等描述属于项目作者的说明，不作为已验证的官方结论。

## 官方资料与实际查询

1. [为知官方 Safari 剪藏帮助](https://www.wiz.cn/webclipper_safari.html) 的搜索缓存标注 2024-08-23，描述安装剪藏 App、登录账号、添加到系统共享菜单，再从 Safari 分享网页。页面仍出现 OS X 10.10 的旧说明。本次直接访问仅返回前端应用外壳；缓存说明可核对历史操作习惯，不能用来证明 macOS 27 的兼容性。
2. 从参考项目给出的官方公开发现地址 [webclipper_openapi_url](https://api.wiz.cn/?p=wiz&c=webclipper_openapi_url&protocol=https) 发出无账号、无令牌的只读 GET，收到 `code:200`，仍列出 `/ks/gather`、`/ks/gather/status`、`/as/user/login`、分类／标签接口。发现服务有响应不等于对应写入端点实际可用；本次没有登录或创建云端笔记。尤其不能据社区 README 就断言旧 gather 已彻底废弃。
3. 官网当前前端源码仍链接 `/downloads-webclipper.html`，但本次该地址响应 404；部分旧下载／手册地址无法访问或超时。没有从这些失败推断整个官网、剪藏服务或旧可用插件停止工作。
4. [Apple 分享扩展说明](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/ExtensionScenarios.html) 说明 Safari 的 JavaScript 预处理与 property-list 输入；匹配现有系统分享入口。当前新工程扩展点为 `com.apple.share-services`，不是 Safari WebExtension。
5. [Apple Safari WebExtension 转换工具](https://developer.apple.com/documentation/safariservices/converting-a-web-extension-for-safari) 为浏览器扩展转 Safari 提供另一条路线，但转换工程不替代 API／权限／加载／签名验证。[发行说明](https://developer.apple.com/documentation/safariservices/distributing-your-safari-web-extension) 的 App Store／Developer ID 路径不能直接套在本次免费系统分享扩展上，也不能据此承诺免费签名包多机无条件加载。

## 对当前实现的影响

- 保留“Safari 分享 → 剪藏”习惯，先完成独立 arm64 系统分享扩展，不改变已有客户端／Markdown／PDF 使用方式。
- 优先交给已登录客户端保存原始 Wiz 笔记，再沿用正常同步；新剪藏器不额外保存云端密码，不读取旧官方 Team ID 下的钥匙串。
- 新 8849 桥接只提供配对、目的账号／文件夹状态及保存；旧 8848 WebChannel 不变，不开放数据库对象、脚本执行或任意文件访问。
- 保留明确的账号／文件夹显示和稳定请求 ID。失败可重试，回执丢失不重复建笔记。现已实测 Swift Foundation 客户端配对、正文保存与重试，合成数据库中只有一份实际 Wiz 容器。
- 图片优先嵌入 Safari 已可读取的资源。未嵌入的远程图片改成可用原图链接；不支持的图形显示占位及提示，不让主客户端自动抓取网页指定地址。
- 原生草稿当前只有“整个网页／选中内容”，没有自动正文识别；不能把整页称作正文。参考项目启发的正文模式、计算样式保留、标签选择列为完成基本剪藏后的候选改进，尚未实现／验收，不自动扩大当前功能范围。

## 当前验证边界

主客户端源构建通过；新增剪藏安全／保存自测 30 项通过，包括账号不匹配、浏览器来源拒绝、内容清理、配对恢复、重复请求和保存中断连。10 项 CTest 全通过。Swift 宿主／扩展 arm64 编译与严格开发签名通过，输入模型 10 项通过，实际 Foundation → C++ → 原始 Wiz 容器的隔离联调通过。源码实际主窗口 72 项回归通过，包括无新增退出预处理进度弹窗、Markdown 本地保存、搜索和红黄绿窗口操作。

草稿测试曾暴露断连时的悬空套接字、缺失 HTML 属性访问和参数名大小写导致的重试失败，均已修复并重跑对应测试；失败记录保留在工作目录，不发布含个人环境信息的系统崩溃报告。主窗口首次回归因合成点击未激活前台窗口而失败；只在测试夹具点击前恢复自身窗口激活，未改搜索生产布局、未降低焦点／几何断言，重跑全部通过。

Safari 真正调用分享扩展、沙盒连接／钥匙串持久化、真实网页图片及样式、多台 Mac 加载／安装，仍未验证。没有覆盖当前用户安装的 rc9，未替换旧剪藏器；这些编译／协议结果不表示新剪藏器已经可日常使用。
