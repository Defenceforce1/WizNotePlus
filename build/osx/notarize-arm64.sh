#!/bin/bash
# Explicit opt-in only. Run against a disposable release candidate, never user data.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 DEVELOPER_ID_SIGNED_APP NOTARY_KEYCHAIN_PROFILE" >&2
    exit 2
fi
candidate_app=$1
notary_profile=$2
if [[ ! -d "$candidate_app/Contents/MacOS" || "$candidate_app" != *.app || -z "$notary_profile" ]]; then
    echo "Expected a signed .app and an existing notarytool keychain profile." >&2
    exit 1
fi
codesign --verify --deep --strict "$candidate_app"
signature=$(codesign -dv --verbose=4 "$candidate_app" 2>&1)
if [[ "$signature" != *"Authority=Developer ID Application:"* || "$signature" != *"runtime"* ]]; then
    echo "Refusing to submit an ad-hoc or non-hardened app." >&2
    exit 1
fi
notary_work=$(mktemp -d "${TMPDIR:-/tmp}/wiznoteplus-notary.XXXXXXXX")
echo "Submission archive and result retained in: $notary_work"
ditto -c -k --keepParent "$candidate_app" "$notary_work/candidate.zip"
xcrun notarytool submit "$notary_work/candidate.zip" --keychain-profile "$notary_profile" \
    --wait --output-format json > "$notary_work/result.json"
if [[ $(plutil -extract status raw -o - "$notary_work/result.json") != Accepted ]]; then
    echo "Notarization was not accepted. Do not publish. Inspect the retained result." >&2
    exit 1
fi
xcrun stapler staple "$candidate_app"
xcrun stapler validate "$candidate_app"
codesign --verify --deep --strict "$candidate_app"
spctl --assess --type execute --verbose=4 "$candidate_app"
echo "Accepted and stapled. Repeat isolated runtime/UI tests and checksum the final archive."
