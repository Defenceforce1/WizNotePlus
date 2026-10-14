#!/bin/bash
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: $0 CANDIDATE_DIR NEW_OUTPUT.dmg" >&2
    exit 2
fi
candidate_dir=$(cd "$1" && pwd)
output_dmg=$2
[[ "$output_dmg" = /* && "$output_dmg" = *.dmg && ! -e "$output_dmg" ]] || {
    echo "Provide a new absolute .dmg path; existing files will not be replaced." >&2
    exit 2
}
app="$candidate_dir/WizNotePlus Apple Silicon.app"
[[ -d "$app" && ! -L "$app" ]] || exit 1
codesign --verify --deep --strict "$app"
lipo "$app/Contents/MacOS/WizNotePlus" -verify_arch arm64
dmg_stage=$(mktemp -d /tmp/wiznote-dmg.XXXXXX)
cleanup() {
    # Only the exact, freshly created disposable staging directory.
    case "$dmg_stage" in /tmp/wiznote-dmg.??????) /bin/rm -rf -- "$dmg_stage" ;; esac
}
trap cleanup EXIT
ditto "$app" "$dmg_stage/WizNotePlus Apple Silicon.app"
# Include only the explicitly named companion, never arbitrary candidate apps.
clipper="$candidate_dir/WizNotePlus Web Clipper.app"
if [[ -e "$clipper" ]]; then
    [[ -d "$clipper" && ! -L "$clipper" ]] || exit 1
    [[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$clipper/Contents/Info.plist") == io.github.defenceforce1.wiznoteplus.clipper.mac ]] || exit 1
    extension="$clipper/Contents/PlugIns/WizNotePlusShare.appex"
    [[ -d "$extension" && ! -L "$extension" ]] || exit 1
    codesign --verify --deep --strict "$clipper"
    lipo "$clipper/Contents/MacOS/WizNotePlusClipper" -verify_arch arm64
    lipo "$extension/Contents/MacOS/WizNotePlusShare" -verify_arch arm64
    ditto "$clipper" "$dmg_stage/WizNotePlus Web Clipper.app"
fi
ln -s /Applications "$dmg_stage/Applications"
for name in '开始使用.md' '发布说明.md' '验证记录.md' '安装说明.txt'; do
    [[ -f "$candidate_dir/$name" && ! -L "$candidate_dir/$name" ]] || exit 1
    cp "$candidate_dir/$name" "$dmg_stage/$name"
done
# Do not copy arbitrary files from the candidate directory (notes, keys, logs).
for archive in "$candidate_dir"/WizNotePlus-source-*.tar.gz; do
    if [[ -f "$archive" && ! -L "$archive" ]]; then cp "$archive" "$dmg_stage/"; fi
done
hdiutil create -volname 'WizNotePlus Apple Silicon' -srcfolder "$dmg_stage" \
    -format UDZO "$output_dmg"
hdiutil verify "$output_dmg"
shasum -a 256 "$output_dmg"
