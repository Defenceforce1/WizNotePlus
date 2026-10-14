# 网页剪藏更新评估（2026-10-14）

## 后续核查更新

用户提供的两个公开参考项目及为知／Apple 官方资料现已核查，详见 [参考项目评估](web-clipper-reference-review.md)。新的独立原生分享工程与受限本地保存桥接已编译、签名并通过隔离保存／重试测试；Safari 实际分享调用和多机加载仍未验证。下文“未实现保存”描述早期探针阶段，不能代表最新源码状态。

## 本机已核对

- `/Applications/WizNoteWebClipper.app`，版本 1.0.5，宿主可执行文件仅 x86_64。
- 内含 `MacShareExtension.appex`，扩展点为 `com.apple.share-services`，可执行文件也仅 x86_64。
- 这是系统分享扩展，Safari 可调用它；不能仅因用户称“Safari 插件”就按 Safari Web Extension 改造。
- 只读核对公开安装包元数据与二进制架构，没有启动旧 App、修改注册、读取扩展账号偏好或替换现有剪藏器。用户报告它目前可用。
- Apple 已声明通用 Rosetta 支持到 macOS 27 为止，后续仅保留部分老游戏场景：[官方说明](https://developer.apple.com/news/?id=w5ngl9k2)。当前 Intel-only 分享扩展值得优先迁移，但不能承诺尚未实际测试的未来系统兼容性。

## 推荐顺序

1. 优先保留 Safari“分享 → 剪藏”的工作方式，用原生 arm64 宿主和系统分享扩展替代 Intel 依赖；是否另加 Safari 工具栏按钮，留作后续选择，不自动改变入口。
2. 先验证分享输入的网页 URL、标题、所选文字、HTML/图片及资源保留能力，成功必须落到明确的笔记/文件夹；分享输入不足时说明限制，不把“只存链接”称为完整剪藏。
3. 优先评估将内容交给已登录的新版客户端，减少另存密码和另一次云端登录。现有 8848 是 WebSocket/QWebChannel 接口，不是可以随意 POST HTML 的 HTTP 接口；必须核对实际 API，限制权限、来源、大小、状态反馈及重复提交，不放宽所有来源。
4. 失败保留可重试内容；成功明确显示目的账号/文件夹，避免旧版出现剪藏到另一个账号却没有提示的问题。不要共享或读取官方 Team ID 下的旧钥匙串/App Group，也不要声称新身份可覆盖官方 App Store 扩展。

## 当前边界

2026-10-14 后续状态：用户已安装 Xcode 27 并创建免费 Apple Development 身份。最初由于缺失新的 WWDR G3 中间证书，签名工具无法建立完整链；从 Apple 官方下载并由现有系统根验证后，仅导入该中间证书，现已识别有效身份。新的 arm64 最小宿主和分享扩展已以开发身份签名并通过严格检查；pluginkit 注册/查询可识别独立测试扩展。这仍只是身份与注册验证，不是 Safari 已实际加载，也不是可保存网页的剪藏器。未导出密码/私钥、未改变信任策略、未启用扩展开关、未覆盖旧剪藏器。以下“0 个身份/未注册”记录描述此前的编译证明阶段，不代表最新状态。

主仓库没有找到分享扩展的 Objective-C/Swift 工程；本轮 WizTeam 范围和仓库名称公开检索也未找到对应源码，不能据此断言源码绝不存在。安装包包含 JavaScript 资源，但未确认其独立再利用许可，不将其拷贝到新公开项目，也不能把 Intel 二进制直接改成 arm64。

本机 `xcode-select -p` 为 `/Library/Developer/CommandLineTools`，Safari web extension converter 不可用；签名身份检查为 0。进一步用 CLT 的 swiftc/AppKit/Foundation 编译 arm64 宿主和系统分享扩展，通过 `_NSExtensionMain` 链接入口构建 .appex，并严格验证临时签名。说明完整 Xcode 不是原生编译的绝对前提。

编译证明源码另存于工作目录 clipper-native-probe，产物为 clipper-native-toolchain-probe，界面明确标注“非可用剪藏器”。这个最小包不剪藏、不读取账号、不写入笔记，也没有启动或注册到系统；编译成功不等于 Safari 已接受其身份/能力或剪藏功能完成。正式功能、扩展加载及开发签名仍需准备完整 Xcode 与相应身份。用户已同意自行安装 Xcode、登录 Apple ID 并创建本地开发签名，完成后继续联调。不自动下载大体积 Xcode，不索取聊天中的密码或私钥，不修改 Safari 扩展开关/开发安全设置。

Apple 参考：[分享扩展](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/Share.html)、[App Groups](https://developer.apple.com/documentation/xcode/configuring-app-groups)。新实现需要自己的合法包标识与签名/能力配置，不能借用原官方身份。

本轮完成类型、架构、现有桥接、工具链评估与最小原生宿主/扩展编译证明；尚未实现、安装或测试可实际保存笔记的新剪藏扩展。旧剪藏器保持可用。另有 App 退出卡顿待独立复现，与新剪藏工程分别跟踪。
