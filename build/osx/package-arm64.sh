#!/bin/bash
set -euo pipefail
if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Usage: $0 QT_PREFIX BUILD_DIR OUTPUT_DIR [DEVELOPER_ID_IDENTITY]" >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")/../.." && pwd)
qt_prefix=$1
build_dir=$2
output_dir=$3
sign_identity=${4:--}
cache="$build_dir/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
    echo "A configured CMake build directory is required." >&2
    exit 1
fi
if ! /usr/bin/grep -q '^WIZNOTE_BUILD_COMPAT_TESTS:BOOL=OFF$' "$cache"; then
    if [[ ${WIZNOTE_PACKAGE_DIAGNOSTIC:-0} != 1 ]] ||
       ! /usr/bin/grep -q '^WIZNOTE_BUILD_COMPAT_TESTS:BOOL=ON$' "$cache"; then
        echo "Production packages require WIZNOTE_BUILD_COMPAT_TESTS=OFF; use a separate production build." >&2
        echo "WIZNOTE_PACKAGE_DIAGNOSTIC=1 explicitly permits an ON build for developer-only diagnostics." >&2
        exit 1
    fi
    echo "Developer-only diagnostic package: compatibility probes are included." >&2
fi
if [[ "$sign_identity" != - ]]; then
    if [[ "$sign_identity" != "Developer ID Application: "* ]] ||
       ! security find-identity -v -p codesigning | /usr/bin/grep -Fq -- "\"$sign_identity\""; then
        echo "A valid installed Developer ID Application identity is required; no fallback to ad-hoc signing." >&2
        exit 1
    fi
fi
app="$output_dir/WizNotePlus Apple Silicon.app"
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
cp "$source_dir/src/libs/3rdparty/framelesshelper/LICENSE" "$licenses/FramelessHelper-MIT.txt"
cp "$source_dir/docs/bundled-assets-audit.md" "$licenses/ASSET-AUDIT.md"
ditto "$source_dir/build/osx/licenses" "$licenses/Full-license-texts"
if [[ -d "$qt_prefix/licenses" ]]; then
    ditto "$qt_prefix/licenses" "$licenses/Qt"
fi
python3 "$source_dir/build/osx/collect-qt-notices.py" "$qt_prefix" "$licenses/Qt-SBOM-notices"
deploy=("$qt_prefix/bin/macdeployqt")
if [[ ${WIZNOTE_QT_HOST_TOOLS_ROSETTA:-OFF} == ON ]]; then
    deploy=(/usr/bin/arch -x86_64 "${deploy[@]}")
fi
sign_options=(-codesign=-)
if [[ "$sign_identity" != - ]]; then
    sign_options=("-sign-for-notarization=$sign_identity")
fi
deploy_output=$("${deploy[@]}" "$app" -verbose=1 "${sign_options[@]}" 2>&1)
printf '%s\n' "$deploy_output"
# macdeployqt can report an error yet exit zero. Never publish that bundle.
if [[ "$deploy_output" == *"ERROR:"* ]]; then
    echo "Deployment reported errors; bundle is incomplete." >&2
    exit 1
fi
test -f "$app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore"
lipo "$app/Contents/MacOS/WizNotePlus" -verify_arch arm64
codesign --verify --deep --strict "$app"
if [[ "$sign_identity" == - ]]; then
    echo "Candidate ad-hoc signed bundle: $app"
    echo "Not Developer ID signed or notarized. Test with isolated data before real use."
else
    echo "Developer ID signed candidate: $app"
    echo "Not yet notarized. Complete notarize-arm64.sh and repeat runtime tests before release."
fi
