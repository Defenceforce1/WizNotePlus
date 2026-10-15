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
script_dir=$(cd "$(dirname "$0")" && pwd)
[[ -d "$app" && ! -L "$app" ]] || exit 1
[[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist") == io.github.defenceforce1.wiznoteplus.applesilicon ]] || exit 1
codesign --verify --deep --strict "$app"
lipo "$app/Contents/MacOS/WizNotePlus" -verify_arch arm64
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
dmg_stage=$(mktemp -d /tmp/wiznote-dmg.XXXXXX)
image_mounted=0
cleanup() {
    if [[ "$image_mounted" == 1 ]]; then
        if ! hdiutil detach "$dmg_stage/mounted"; then
            echo "Retaining temporary image directory because detach failed: $dmg_stage" >&2
            return
        fi
    fi
    # Only the exact, freshly created disposable staging directory.
    case "$dmg_stage" in /tmp/wiznote-dmg.??????) /bin/rm -rf -- "$dmg_stage" ;; esac
}
trap cleanup EXIT
content="$dmg_stage/content"
mkdir -p "$content/说明与源码"
ditto "$app" "$content/WizNotePlus Apple Silicon.app"
# Main-client releases intentionally never include the separate clipper, even
# when an older candidate directory still contains its companion App.
ln -s /Applications "$content/Applications"
for name in '开始使用.md' '发布说明.md' '验证记录.md' '安装说明.txt'; do
    [[ -f "$candidate_dir/$name" && ! -L "$candidate_dir/$name" ]] || exit 1
    if [[ "$name" == '安装说明.txt' ]]; then
        cp "$candidate_dir/$name" "$content/$name"
    else
        cp "$candidate_dir/$name" "$content/说明与源码/$name"
    fi
done
# Do not copy arbitrary files from the candidate directory (notes, keys, logs).
for archive in "$candidate_dir"/WizNotePlus-source-*.tar.gz; do
    if [[ -f "$archive" && ! -L "$archive" ]]; then cp "$archive" "$content/说明与源码/"; fi
done
# Optional local Finder presentation. CI/headless builds keep the same contents
# without requesting Finder automation or changing global Finder preferences.
if [[ ${WIZNOTE_DMG_FINDER_LAYOUT:-0} == 1 ]]; then
    mkdir "$content/.background"
    swift "$script_dir/dmg-background.swift" "$content/.background/install.png" "$version"
    hdiutil create -volname "WizNotePlus $version" -srcfolder "$content" \
        -fs HFS+ -format UDRW "$dmg_stage/layout.dmg"
    mkdir "$dmg_stage/mounted"
    hdiutil attach -nobrowse -noautoopen -mountpoint "$dmg_stage/mounted" "$dmg_stage/layout.dmg"
    image_mounted=1
    osascript "$script_dir/dmg-layout.applescript" "$dmg_stage/mounted"
    # Finder writes per-volume settings on close; never modify global defaults.
    for attempt in {1..20}; do
        [[ -f "$dmg_stage/mounted/.DS_Store" ]] && break
        sleep 0.2
    done
    [[ -f "$dmg_stage/mounted/.DS_Store" ]] || { echo 'Finder did not save the layout' >&2; exit 1; }
    sync
    hdiutil detach "$dmg_stage/mounted"
    image_mounted=0
    hdiutil convert "$dmg_stage/layout.dmg" -format UDZO -o "$output_dmg"
else
    hdiutil create -volname "WizNotePlus $version" -srcfolder "$content" \
        -format UDZO "$output_dmg"
fi
hdiutil verify "$output_dmg"
shasum -a 256 "$output_dmg"
