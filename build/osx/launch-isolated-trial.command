#!/bin/bash
set -euo pipefail

# User-run launcher only. --check validates paths without creating data or
# starting any application. Keep this beside the packaged WizNotePlus.app.
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != --check ) ]]; then
    echo "用法：隔离试用.command [--check]" >&2
    exit 2
fi
package_dir=$(cd "$(dirname "$0")" && pwd -P)
app_bundle="$package_dir/WizNotePlus.app"
app_binary="$app_bundle/Contents/MacOS/WizNotePlus"
trial_parent="$HOME/Library/Application Support"
trial_data="$trial_parent/WizNotePlus-AppleSilicon-Trial"
for entry in "$HOME" "$HOME/Library" "$trial_parent" "$trial_data" "$app_bundle"; do
    if [[ -L "$entry" || ( -e "$entry" && ! -d "$entry" ) ]]; then
        echo "程序或试用目录不是普通目录，已停止。" >&2
        exit 1
    fi
done
if [[ ! -x "$app_binary" || -L "$app_binary" ]]; then
    echo "未找到旁边的试用程序。请先解压完整试用包，不要单独移动此脚本。" >&2
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
echo "这是 Apple Silicon v14.1 收尾试用版，临时签名、未公证。"
echo "将使用新的独立本地试用目录，不读取或复制旧版资料库。"
echo "请只登录专用测试账号；本地隔离不能阻止该账号的云端同步。"
read -r -p "确认只使用测试账号？输入 y 继续，其他输入取消：" account_confirm
case "$account_confirm" in
    y|Y) ;;
    *) echo "已取消，未启动程序。"; exit 0 ;;
esac
umask 077
mkdir -p "$trial_data"
export WIZNOTE_DATA_STORE="$trial_data/"
exec "$app_binary"
