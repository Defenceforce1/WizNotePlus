# WizNotePlus 原生网页剪藏（开发候选）

## 0.2.0 独立云端候选（当前状态，以下旧记录为历史）

仅构建／升级剪藏器，主客户端 rc10 不变。Safari 分享中默认选择“直接上传为知云端”，在本扩展独立登录原为知账号、选择个人文件夹，上传后由服务器完成剪藏任务；以后主客户端正常同步。无需在本机安装／运行主客户端，云端上传需要联网。保留可选“保存到本机客户端”入口，但该入口仍需要 rc10 运行。

保存内容为静态 HTML＋图片／MathML，不转换成 Markdown。旧官方客户端的 HTML 资源收集与 ZIP 容器格式、官方接口发现服务的 gather／status 地址已核对；浏览器参考的 legacy frame 参数独立重写，未复制参考代码。接口发现可用不代表已完成实际登录／写入验证。本候选仅支持官方 HTTPS note.wiz.cn，不支持私有部署／多账号管理／独立离线待上传库。

图片：优先嵌入 Safari 可读取的已加载图片；跨域图片交给独立无登录信息的下载会话，限 48 个资源、单文件 1 MB、PNG 输出 500 KB、总嵌入 4 MB；超时／私网／重定向／受保护图片显示明确占位。下载有类型／尺寸／像素检查、私网 DNS 检查，拒绝隐式重定向；DNS 预检查不是对抗恶意 DNS 重绑定的完整地址固定方案。GIF 仅静态首帧，大图可缩小，不声称无损保存所有媒体。

若系统已经配置本机 HTTPS 转发代理，图片请求显式沿用该代理，由现有代理解析公网域名，避免将 VPN fake-IP 当作真正私网目的地；不创建／修改代理、不读取代理密码、不绕过 HTTPS 验证。无该代理时执行本机公网 DNS 检查。本机首次真实公开图片下载因 fake-IP 拦截失败，修正现有代理适配后，固定上游 PNG 的真实下载／解码／嵌入测试通过；失败日志不抹除。

公式：从 MathJax／KaTeX 隐藏辅助 MathML 中提取语义结构，移除隐藏样式和依赖原站 CSS／字体的显示树；保留分式、求和、上下标和多行结构。没有辅助 MathML 的 SVG／canvas 公式仍可能出现占位，不宣称所有公式引擎已支持。原始 TeX 若在 annotation 内会保留，但这不是完整 TeX 编译／公式编辑器。

“版式预览”在本机显示实际静态 HTML，禁用页面脚本、网络子资源和持久网页存储，不上传到服务器。用户提供的超理论坛页面在 Safari 捕获 644 个静态公式，预览可见头像图片、求和、上下标和多行公式；未将该网页上传到真实账号。不是原网页像素级快照。

登录：密码只在登录请求中使用，不保存、不记录；只有用户勾选“记住登录”才将令牌保存到本扩展独立钥匙串，不读取旧剪藏器的凭据。认证限定官方 HTTPS、拒绝重定向，不给图片会话云端令牌／密码／Safari cookies。传输／响应失败不自动重新上传；记录非正文、非凭据的任务编号，仅查询原任务。取消窗口不等于撤销已提交的服务器任务。确认自己已检查服务器后才可解除未确认任务提示。

验证：arm64／严格开发签名、输入 10 项、模拟云端／资源 28 项、WebKit 静态捕获 9 项通过。实际 Safari 已显示独立登录入口，默认不勾选记住登录；未输入真实账号或提交云端上传。真实云端认证（含服务器是否接受新令牌）、保存任务结果、同步后的 MathML／图片及钥匙串持久化仍待用户验证。未公证，第二台 Mac 的开发签名／扩展加载不保证。

构建仅需 Xcode／系统 Swift SDK，不使用 Qt，也不重建主客户端：

```sh
bash build/osx/native-clipper/build.sh /absolute/new-output-folder 'Apple Development: YOUR_IDENTITY'
'/absolute/new-output-folder/WizNotePlus Web Clipper.app/Contents/MacOS/WizNotePlusClipper' --capture-self-test /absolute/source/tests/compat/fixtures/clipper-cloud-page.html
```

`--cloud-self-test` 不连接真实服务器；`--public-image-self-test` 仅下载一个固定上游公开 PNG，不涉及登录。完整变化和官方来源见 [独立剪藏说明](../../../docs/standalone-web-clipper.md)。以下 0.1.1 本机方案的“客户端必须运行”等限制只适用于本机入口，不适用于新云端入口。

独立 arm64 系统分享扩展。当前用于 Safari 加载联调，不是已验收的发行包；不覆盖旧剪藏器。最低部署目标 macOS 14，实际系统兼容性以测试为准。

## 已实现

- Safari 网页预处理接口：完整页面与选中内容、标题、来源地址，基本 HTML／MathML 结构与可读取图片。
- 原生标题／账号／文件夹界面，纯文字摘要预览。
- 一次性连接码，专用本机凭据，固定 loopback 8849；不另存云端密码，不使用旧剪藏器账号或旧 8848 特权桥接。
- 本地原始 Wiz 容器保存，明确回执，稳定请求 ID 防止失败重试重复创建。
- 尺寸和结构限制、HTML 清理、拒绝网页直接调用，拒绝远程重定向泄露本机凭据。

目前“整个网页”不是自动提取文章正文；不保证原站点外部 CSS、SVG／canvas、跨域／未加载图片完整保留。未嵌入的图片显示原图链接或占位及警告。全部笔记加密模式暂拒绝剪藏，不自动降低加密设置。

## 开发构建

```sh
bash build/osx/native-clipper/build.sh /absolute/new-output-folder 'Apple Development: YOUR_IDENTITY'
```

可省略开发身份作临时签名编译检查，但不能据此保证系统会加载扩展。输出目录必须不存在。脚本编译宿主和扩展、内嵌 GPL 许可、验证签名与 arm64 架构，并运行 10 项纯输入模型测试。密码／私钥不需要导出。

客户端需同时包含 `WizNativeClipperBridge`。本地接口设计与参考核查见 [评估文档](../../../docs/web-clipper-reference-review.md)。

## 隔离验证

启用兼容测试的主客户端源码构建支持：

```sh
QT_QPA_PLATFORM=offscreen /absolute/source-build/WizNotePlus --native-clipper-self-test
QT_QPA_PLATFORM=offscreen /absolute/source-build/WizNotePlus --native-clipper-transport-self-test '/absolute/new-output-folder/WizNotePlus Web Clipper.app/Contents/MacOS/WizNotePlusClipper'
```

这些精确参数在正常登录前进入独立临时数据库。协议测试使用随机 loopback 端口，Swift 客户端拒绝测试连接 8848／8849，不读取真实钥匙串或账号。部署包只有 cocoa 插件时，使用 `QT_QPA_PLATFORM=cocoa`，不要照抄源码构建的 offscreen 设置。

更新：0.1.1 已在本机 Safari 实际调起，扩展沙盒到合成资料库的整页／选区保存通过，保留基础计算样式、表格、嵌入图片和 MathML 上标。测试配对不保存钥匙串；真实配对持久化、多机免费签名加载和配套客户端部署仍需验证。原版复现差异见 [验收记录](../../../docs/native-clipper-parity.md)。不要关闭系统保护、不冒充公证或正式通用签名包。当前源码参考两个社区项目的功能与协议，但没有复制其代码／图标／库；新实现采用 GPL-3.0-or-later。
