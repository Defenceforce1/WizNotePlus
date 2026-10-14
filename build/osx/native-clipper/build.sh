#!/bin/bash
set -euo pipefail
[[ $# -ge 1 && $# -le 2 ]] || { echo 'Usage: build.sh NEW_ABSOLUTE_OUTPUT [APPLE_DEVELOPMENT_IDENTITY]' >&2; exit 2; }
source_dir=$(cd "$(dirname "$0")" && pwd)
output=$1
sign_identity=${2:--}
[[ "$output" = /* && ! -e "$output" ]] || exit 2
if [[ "$sign_identity" != - ]]; then
    [[ "$sign_identity" == 'Apple Development: '* ]] || exit 2
    security find-identity -v -p codesigning | /usr/bin/grep -Fq -- "\"$sign_identity\"" || exit 1
fi
app="$output/WizNotePlus Web Clipper.app"
extension="$app/Contents/PlugIns/WizNotePlusShare.appex"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$extension/Contents/MacOS" "$extension/Contents/Resources"
cp "$source_dir/Host-Info.plist" "$app/Contents/Info.plist"
cp "$source_dir/Extension-Info.plist" "$extension/Contents/Info.plist"
cp "$source_dir/ClipPage.js" "$extension/Contents/Resources/ClipPage.js"
cp "$source_dir/ClipPage.js" "$app/Contents/Resources/ClipPage.js"
cp "$source_dir/../../../LICENSE" "$app/Contents/Resources/LICENSE.txt"
cp "$source_dir/../licenses/GPL-3.0.txt" "$app/Contents/Resources/GPL-3.0.txt"
cp "$source_dir/README.md" "$app/Contents/Resources/README.md"
sdk=$(xcrun --sdk macosx --show-sdk-path)
common=("$source_dir/ClipInput.swift" "$source_dir/ClipCredentialStore.swift" "$source_dir/CloudClipClient.swift" "$source_dir/ClipResources.swift")
xcrun swiftc -sdk "$sdk" -target arm64-apple-macos14.0 -framework AppKit -framework Foundation -framework Security \
    -framework WebKit "${common[@]}" "$source_dir/ClipperSelfTests.swift" "$source_dir/main.swift" -o "$app/Contents/MacOS/WizNotePlusClipper"
xcrun swiftc -sdk "$sdk" -target arm64-apple-macos14.0 -framework AppKit -framework Foundation -framework Security -framework UniformTypeIdentifiers -framework WebKit \
    -application-extension -parse-as-library -emit-executable -module-name WizNotePlusShare \
    -Xlinker -e -Xlinker _NSExtensionMain "${common[@]}" "$source_dir/ShareViewController.swift" \
    -o "$extension/Contents/MacOS/WizNotePlusShare"
codesign --force --sign "$sign_identity" --timestamp=none --entitlements "$source_dir/Extension.entitlements" "$extension"
codesign --force --sign "$sign_identity" --timestamp=none "$app"
codesign --verify --deep --strict "$app"
lipo "$app/Contents/MacOS/WizNotePlusClipper" -verify_arch arm64
lipo "$extension/Contents/MacOS/WizNotePlusShare" -verify_arch arm64
"$app/Contents/MacOS/WizNotePlusClipper" --input-self-test
"$app/Contents/MacOS/WizNotePlusClipper" --cloud-self-test
printf '%s\n' 'Native clipping candidate built. Safari load and multi-Mac signing still require verification.'
