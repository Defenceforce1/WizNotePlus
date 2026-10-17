#!/bin/bash
# Explicit synthetic route only. Never falls back to normal-account startup.
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
app=${1:-"$script_dir/WizNotePlus Apple Silicon.app"}
[[ -d "$app" && ! -L "$app" ]] || { echo '找不到同目录的候选 App；未启动任何程序。'; exit 1; }
app=$(cd "$app" && pwd)
[[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist") == io.github.defenceforce1.wiznoteplus.applesilicon ]] || exit 1
case $(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist") in
    14.4.17|14.4.18) ;;
    *) echo '不是此诊断脚本已验证的候选版本；未启动应用。'; exit 1 ;;
esac
codesign --verify --deep --strict "$app"
artifacts=$(mktemp -d "${TMPDIR:-/tmp}/wiznote-window-check.XXXXXX")
echo '这只验证临时合成笔记，不登录账号，不打开现有资料。'
echo '出现“筛选个人笔记”窗口时，请点击一次“找标签…”输入框，然后暂时不要切换其他 App。'
echo '之后窗口会自动测试最小化、全屏及关闭，并正常退出。'
echo "记录目录：$artifacts"
open -n -W -a "$app" --stdout "$artifacts/stdout.log" --stderr "$artifacts/result.log" \
    --env QT_QPA_PLATFORM=cocoa --env "WIZNOTE_TEST_ARTIFACT_DIR=$artifacts" \
    --args --mac-window-controls-self-test
count=$(/usr/bin/grep -c '^PASS ' "$artifacts/result.log" || true)
if /usr/bin/grep -q '^FAIL ' "$artifacts/result.log" || [[ "$count" != 93 ]]; then
    echo "验证未全部通过（PASS：$count），请保留记录；不要据此安装或替换稳定版。"
    exit 1
fi
echo '93 项检查通过。请把结果告诉维护者；本脚本没有安装应用。'
