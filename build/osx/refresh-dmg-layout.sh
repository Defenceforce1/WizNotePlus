#!/bin/bash
# Reuse this project's previous installer layout without controlling Finder.
# The original image and installed App are never edited. Only a new writable
# image clone and a private staging directory are modified.
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: bash refresh-dmg-layout.sh CANDIDATE_DIR PREVIOUS_DMG NEW_ABSOLUTE_DMG' >&2; exit 2; }
candidate=$(cd "$1" && pwd)
template=$2
output=$3
[[ -f "$template" && "$output" = /* && "$output" = *.dmg && ! -e "$output" ]] || exit 2
app="$candidate/WizNotePlus Apple Silicon.app"
bundle_id=io.github.defenceforce1.wiznoteplus.applesilicon
[[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist") == "$bundle_id" ]] || exit 1
version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
codesign --verify --deep --strict "$app"
script_dir=$(cd "$(dirname "$0")" && pwd)
stage=$(mktemp -d /tmp/wiznote-dmg-refresh.XXXXXX)
mounted=0
cleanup() {
    if [[ "$mounted" == 1 ]]; then
        if ! hdiutil detach "$stage/mounted"; then
            echo "Retaining staging directory because detach failed: $stage" >&2
            return
        fi
    fi
    case "$stage" in /tmp/wiznote-dmg-refresh.??????) /bin/rm -rf -- "$stage" ;; esac
}
trap cleanup EXIT
hdiutil convert "$template" -format UDRW -o "$stage/layout.dmg"
mkdir "$stage/mounted" "$stage/previous"
hdiutil attach -nobrowse -noautoopen -mountpoint "$stage/mounted" "$stage/layout.dmg"
mounted=1
root="$stage/mounted"
[[ $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$root/WizNotePlus Apple Silicon.app/Contents/Info.plist") == "$bundle_id" ]] || exit 1
[[ -f "$root/.DS_Store" && -f "$root/.background/install.png" && ! -L "$root/.background/install.png" ]] || exit 1
[[ $(readlink "$root/Applications") == /Applications ]] || exit 1
[[ -d "$root/说明与源码" && ! -L "$root/说明与源码" && -f "$root/安装说明.txt" ]] || exit 1
# Avoid merging old App/resources or retaining the previous source archive.
# Moving clone-only contents out also makes room in the fixed-size image.
mv "$root/WizNotePlus Apple Silicon.app" "$stage/previous/"
mv "$root/说明与源码" "$stage/previous/"
mv "$root/安装说明.txt" "$stage/previous/"
ditto "$app" "$root/WizNotePlus Apple Silicon.app"
mkdir "$root/说明与源码"
for name in '开始使用.md' '发布说明.md' '验证记录.md' '安装说明.txt'; do
    [[ -f "$candidate/$name" && ! -L "$candidate/$name" ]] || exit 1
    if [[ "$name" == '安装说明.txt' ]]; then cp "$candidate/$name" "$root/$name"
    else cp "$candidate/$name" "$root/说明与源码/$name"; fi
done
archives=0
for archive in "$candidate"/WizNotePlus-source-*.tar.gz; do
    if [[ -f "$archive" && ! -L "$archive" ]]; then
        cp "$archive" "$root/说明与源码/"
        archives=$((archives + 1))
    fi
done
[[ "$archives" == 1 ]] || { echo 'Exactly one matching project source archive is required' >&2; exit 1; }
swift "$script_dir/dmg-background.swift" "$stage/install.png" "$version"
# Preserve the background file's identity used by the volume's Finder alias.
cp "$stage/install.png" "$root/.background/install.png"
diskutil renameVolume "$root" "WizNotePlus $version"
sync
hdiutil detach "$root"
mounted=0
hdiutil convert "$stage/layout.dmg" -format UDZO -o "$output"
hdiutil verify "$output"
shasum -a 256 "$output"
