# v14.4-rc6：网页头像刷新与上传地址

本版仅处理头像，不调整顶栏布局、红黄绿、笔记格式、Markdown/CSS/数学样式或原另存为 PDF。

## 修复

- 当前账号从已有账号资料取得用户 GUID；保留原来以账号 ID 命名的本地缓存，不改账号或笔记数据库结构。
- 下载使用服务发现命令 `avatar` 返回的模板，不再为当前账号拼接旧 `/wizas/a/users/avatar/...` 路径。官方 WizNoteLiteMobile 也使用 `/as/user/avatar/{userGuid}`。
- 启动及成功的手动同步会重新请求当前账号头像；账号菜单增加“刷新头像”。新请求绕过旧缓存，成功后清空界面中的圆形头像缓存。
- HTTP 错误、JSON 错误响应、无效图片、证书失败、超大图片或本地写入失败均保留旧头像。有效图片使用原子写入，避免先删除旧头像再发现返回的不是图片。
- 自动刷新失败以账号悬浮提示显示；手动“刷新头像”失败弹出具体提示。空下载地址也结束请求，不阻塞头像队列。
- 上传使用服务发现命令 `upload_avatar` 返回的地址，并按官方 SDK 添加 `X-Wiz-Token` 认证头，保留原 multipart `token` 和 `avatar` 字段。只在明确成功响应后重新下载，不自动重定向、重传凭据或关闭证书验证。

## 地址依据与验证边界

2026-10-14 无凭据查询官方地址发现服务返回：

- `https://api.wiz.cn/?p=wiz&c=avatar` → `https://as.wiz.cn/as/user/avatar/{userGuid}`
- `https://api.wiz.cn/?p=wiz&c=upload_avatar` → `https://as.wiz.cn/as/users/avatar`

下载地址用虚构身份请求，返回 HTTP 200 和结构化“userGuid is not correct”错误，说明 HTTP 200 不能作为图片下载成功条件。上传地址 HEAD 返回 405/Allow POST，空 POST 返回 HTTP 200/服务码 301 Invalid token；没有发送真实凭据或图片，没有改变用户账号。

参考官方源码：[头像 URL](https://github.com/WizTeam/WizNoteLiteMobile/blob/master/api/index.js)、[认证头及响应判定](https://github.com/WizTeam/wiznote-sdk-js/blob/master/src/common/request.ts)。这些依据确认了路由和通用认证方式，不等于认证后的真实上传已经成功；multipart 接受情况、网页头像实际显示仍需用户试用确认。

本地回归使用临时资料目录、虚构图片、临时 HTTPS 服务和测试证书，不读取真实账号。覆盖地址发现、GUID 编码、认证头、JSON 假成功、有效图片、原头像保留、缓存路径、TLS 及重定向。

测试执行记录：第一次窗口检查误用未识别的参数 `--mac-main-window-controls-self-test`，导致构建中的候选 App 普通启动并尝试自动同步，已立即终止。不能将该次执行计为隔离测试，亦未独立核验其同步是否影响服务端。随后添加未知或参数不正确的 self-test/probe 拒绝检查：在普通启动、账号和资料目录创建之前退出；专门的子进程回归覆盖该误拼参数。正确窗口测试参数为 `--mac-window-controls-self-test`。

仍为发布候选版：第三方素材/许可核查、Developer ID 签名与公证、长期真实同步回归尚未完成。此前 PDF 定理引用的垂直位置偏移仍按用户决定暂缓。
