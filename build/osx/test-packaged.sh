#!/bin/bash
set -euo pipefail
[[ $# -ge 1 && $# -le 2 ]] || { echo "Usage: $0 APP_BUNDLE [OPENSSL]" >&2; exit 2; }
app=$1
exe="$app/Contents/MacOS/WizNotePlus"
[[ -x "$exe" && -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib" ]] || {
    echo "Missing executable or packaged native macOS platform plugin." >&2; exit 1;
}
# Final bundles deploy cocoa, not the SDK's optional offscreen test plugin.
# Exact isolated routes cannot fall through to normal-account startup.
env QT_QPA_PLATFORM=cocoa "$exe" --shutdown-self-test
env QT_QPA_PLATFORM=cocoa "$exe" --native-clipper-self-test
env QT_QPA_PLATFORM=cocoa "$exe" --standalone-startup-self-test
env QT_QPA_PLATFORM=cocoa "$exe" --network-security-self-test "${2:-openssl}"
