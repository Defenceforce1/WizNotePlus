# WizNotePlus 原生网页剪藏（开发候选）

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
