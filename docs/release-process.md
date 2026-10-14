# Apple Silicon 正式发行流程

当前 v14.4-rc1 是发布候选版。自动测试通过不替代真实账号验证、再分发许可核查或 Apple 签名／公证。不要把候选包当作已公证的正式版发布。

## 工程检查

以 GitHub 兼容分支的确切提交构建。保留上游 GPLv3、第三方声明、对应源代码、固定依赖版本和构建步骤；不要打包任何个人资料库。当前使用 Qt 6.11.3、Crypto++ 8.9.0 和固定版本子模块。Qt 动态库随包提供，不要求用户安装开发环境。

构建后跑全部兼容测试，部署后再次跑网络、启动、搜索、导入导出、CSS 和真实主窗口隔离检查，执行 build/osx/check-bundle.py 与严格签名验证。检查 Info.plist 的构建号、独立标识以及无旧系统 URL 注册。最终 ZIP 解压后重新验证，生成 SHA-256。

## 签名与公证

需要 Apple Developer 账户中的 Developer ID Application 证书及对应私钥，以及用户自行准备的 notarytool 钥匙串配置。不要在源码、聊天、环境日志或 GitHub 中提交私钥、密码与公证凭据。

打包脚本第四个参数可填写有效 Developer ID 身份。使用 Qt 官方 macdeployqt 的 sign-for-notarization 选项，启用签名、Hardened Runtime 与时间戳；Qt WebEngine 自带的 helper entitlements 随其部署。不使用上游旧 App Store 沙盒／团队共享 entitlement 来签独立版。

```sh
bash build/osx/package-arm64.sh QT_PREFIX BUILD_DIR OUTPUT_DIR 'Developer ID Application: YOUR_NAME (TEAM_ID)'
bash build/osx/notarize-arm64.sh 'OUTPUT_DIR/WizNotePlus Apple Silicon.app' YOUR_NOTARY_KEYCHAIN_PROFILE
```

上述是配置形式示例，不是本次成功执行记录。脚本会拒绝没有有效身份的签名请求，以及临时签名或没有 Hardened Runtime 的公证输入。公证必须 Accepted，然后 staple、validate 与系统评估通过，再次测试启动、WebEngine、编辑和 PDF；最终归档后重新生成校验码。没有证书时停留在明确标记的候选版，不关闭系统保护来冒充正式签名。

Qt 官方部署依据：[macOS Deployment](https://doc.qt.io/qt-6/macos-deployment.html)。

## 原账号与回退

由用户在备份后完成登录和受控双向同步测试；代理不替用户确认同步删除或发送真实账号凭据。测试正文、附件、标签、加密笔记、修改冲突、断网恢复和退出重开。首次不要批量移动、导入或删除正式笔记。

仅替换 App 不主动改变数据目录和数据库格式。回退到旧候选程序时仍必须退出当前程序并保留资料库备份。本地回退不能撤销已经同步到云端的修改或删除。v14.3 留作工程对照，含关闭证书验证的遗留问题，不推荐回退后用主账号联网。

## 公开发布闸门

所有工程检查、真实同步验证、签名公证和第三方资源／对应源码核查完成后，才创建正式 Release。CI 产物不自动等于正式 Release。发布说明必须列出实际测试系统和已知限制；不承诺尚未发行或未实际测试的系统兼容性。
