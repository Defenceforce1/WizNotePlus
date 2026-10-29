# 14.4.25 build338：Apple Silicon个人日常版

2026-10-29。功能源码保持336，337完成综合离线检查；338重新使用正常Apple Silicon
身份编译，移除独立试用固定资料根，保留笔记格式及现有默认资料路径规则。

- App：WizNotePlus Apple Silicon.app。
- Bundle ID：io.github.defenceforce1.wiznoteplus.applesilicon。
- 默认资料根：~/Library/Application Support/WizNotePlus-AppleSilicon；尊重显式覆盖。
- CFBundleVersion：14425.338。名称、身份和URL策略与现用build256相同。
- 安装：先正常退出原Apple Silicon版，再将镜像中的同名App拖到Applications选择替换。
  不迁移笔记，不自动采用试用资料；制作阶段未安装、删App或读取账号／笔记。

## 验证与内容

337轮18组644断言、9原生组1705断言，以及公式引用、首次显示、评论、工具栏与切换
检查通过。主程序7次完整编辑保存／ZIW回读和双PDF6份12页检查通过。
338轮正式身份另做7次合成编辑／保存／ZIW回读／返读／正常关闭；实际包内Core映像
核对通过。默认资料及显式覆盖仅在合成HOME执行路径检查，没有枚举真实资料根。
740源码资源、48原生构件、严格本地签名及包内搜索路径通过，无诊断引擎和测试计数。
密码解锁与锁定规则保持，不含需要续签维护的Touch ID。

镜像：WizNotePlus-14.4.25-build338-macOS-arm64.dmg

SHA256：4ab636e987b1fd9f1af10f1e4c27f4f125a398c45adc06f70bd5077db2d5f39d

镜像校验、只读挂载1083 App条目及4说明文件逐项读回、严格签名通过并卸载。
完整App／DMG只留本机，不创建GitHub Release或上传安装包。现用App、重要回退、
既有试用版和试用资料根保持，未自动替换或删除。

## 保留的验收边界

真实云服务、iPhone、物理系统会话未在本轮执行。WebEngine大文档高水位尚未修复，
不宣称全局内存降低或所有场景无误。本地ad-hoc签名，不作已公证或上架的声明。

## 源码同步

用户恢复项目源码同步。沿用既有公开仓库的improve/component-updates-20261023分支，
只同步已获准公开源码与去敏说明；用户资料、签名、密码、缓存、原始诊断及安装包排除。
本地HEAD、索引和分支保持，不自动恢复定时任务。提交／回读／恢复校验见本机phase338。
