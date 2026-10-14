#!/bin/bash
set -euo pipefail

# User-run launcher only. --check validates paths without creating data or
# starting any application. Optional compatibility entry; the App is safe to
# open directly and no longer needs an environment variable for isolation.
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != --check ) ]]; then
    echo "用法：隔离试用.command [--check]" >&2
    exit 2
fi
package_dir=$(cd "$(dirname "$0")" && pwd -P)
app_bundle="$package_dir/WizNotePlus Apple Silicon.app"
app_binary="$app_bundle/Contents/MacOS/WizNotePlus"
for entry in "$app_bundle"; do
    if [[ -L "$entry" || ( -e "$entry" && ! -d "$entry" ) ]]; then
        echo "程序或试用目录不是普通目录，已停止。" >&2
        exit 1
    fi
done
if [[ ! -x "$app_binary" || -L "$app_binary" ]]; then
    echo "未找到旁边的试用程序。请先解压完整试用包，不要单独移动此脚本。" >&2
    exit 1
fi
bundle_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_bundle/Contents/Info.plist")
if [[ "$bundle_id" != io.github.defenceforce1.wiznoteplus.applesilicon ]]; then
    echo "旁边不是独立版程序，已停止，不会启动旧版。" >&2
    exit 1
fi
if [[ ${1:-} == --check ]]; then
    echo "隔离启动路径检查通过；未创建资料库，未启动程序。"
    exit 0
fi
if /usr/sbin/lsof -nP -iTCP:8848 -sTCP:LISTEN -t >/dev/null 2>&1; then
    echo "8848 端口已占用。请先用 ⌘Q 正常退出正在运行的为知笔记，再重试。"
    read -r -p "按回车关闭。" unused_input
    exit 1
fi
echo "WizNotePlus — Apple Silicon 试用版，试用构建 v14.3。"
echo "基础版本 2.12.0；临时签名、未公证。"
echo "默认独立资料库由 App 管理；也可以直接双击 App，无需本脚本。"
unset WIZNOTE_DATA_STORE
exec "$app_binary"
