#!/bin/bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo "Usage: $0 QT_PREFIX BUILD_DIR OUTPUT_DIR" >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
qt_prefix=$1
build_dir=$2
output_dir=$3
app="$output_dir/WizNotePlus.app"
if [[ -e "$app" ]]; then
    echo "Refusing to overwrite existing bundle: $app" >&2
    exit 1
fi
mkdir -p "$output_dir"
ditto "$build_dir/bin/WizNotePlus.app" "$app"
ditto "$source_dir/share" "$app/Contents/Resources"
licenses="$app/Contents/Resources/licenses"
mkdir -p "$licenses"
cp "$source_dir/LICENSE" "$licenses/WizNotePlus-GPL-3.0.txt"
cp "$source_dir/docs/third-party-licenses.md" "$licenses/NOTICE.md"
cp "$source_dir/src/libs/3rdparty/quazip/COPYING" "$licenses/QuaZip-LGPL.txt"
cp "$source_dir/src/libs/3rdparty/gumbo-parser/COPYING" "$licenses/Gumbo-Apache.txt"
if [[ -d "$qt_prefix/licenses" ]]; then
    ditto "$qt_prefix/licenses" "$licenses/Qt"
fi
deploy=("$qt_prefix/bin/macdeployqt")
if [[ ${WIZNOTE_QT_HOST_TOOLS_ROSETTA:-OFF} == ON ]]; then
    deploy=(/usr/bin/arch -x86_64 "${deploy[@]}")
fi
deploy_output=$("${deploy[@]}" "$app" -verbose=1 -codesign=- 2>&1)
printf '%s\n' "$deploy_output"
# macdeployqt can report an error yet exit zero. Never publish that bundle.
if [[ "$deploy_output" == *"ERROR:"* ]]; then
    echo "Deployment reported errors; bundle is incomplete." >&2
    exit 1
fi
test -f "$app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
lipo "$app/Contents/MacOS/WizNotePlus" -verify_arch arm64
codesign --verify --deep --strict "$app"
echo "Experimental ad-hoc signed bundle: $app"
echo "Not Developer ID signed or notarized. Test with isolated data before real use."
