# Apple Silicon compatibility branch

Experimental native port of upstream master
`3cae06fbd0eaa5147e5b307a428af359bceb47f5` (WizNotePlus 2.12).
Target: direct-download macOS app, arm64; macOS 27 validation in progress.
App Store builds, Intel builds and other platforms are not validated by this port.

## Verified local status (2026-10-13)

- Host reports arm64, macOS 27.0.1, SDK 27.0 (Command Line Tools).
- Full native Release compilation/linking succeeded with Qt 6.11.3 and C++17.
- Native build tools, system zlib, static Crypto++ 8.9.0 with the Conan recipe's
  CVE-2023-50980 patch. A fresh build directory used the explicit Crypto++ prefix;
  no generated Conan CMake files or forged compiler profile were needed there.
- Isolated smoke suite passed: UTF-8/BOM, GB18030, UTF-8 output, timestamps,
  recursive locks, INI round-trip, SHA-256 known-answer and synthetic ZIP/ZIW.
  These are dependency/format smoke tests, not complete database/encryption tests.
- User confirmed the isolated experimental bundle opens the login window. The
  initial minimize/restore failure was fixed; the user confirmed restoration.
- Added login-only application activation recovery, a named window and enabled
  minimization. Offscreen regression covers restoration, nested modal dialogs,
  closed/destroyed dialogs and the `QDialog::exec()` loop. The exact Dock versus
  Cmd-Tab path was not recorded separately. Cocoa testing from the sandbox failed before
  opening a window (no screens available); that does not validate or invalidate
  the fix on the user's desktop.
- User-reported manual checks passed for Chinese text, bold formatting, images,
  switching notes, quitting/reopening and restoring an isolated trial-data copy.
- After explicit consent, legacy data was copied to a private local backup and
  a separate test copy, with the original untouched. File comparisons matched;
  the two legacy SQLite databases passed read-only quick checks on the test copy.
  The user reported normal legacy-note/attachment reading and normal results
  for the follow-up offline editing/reopening round. These are sample-level
  reports, not exhaustive migration certification. Formula and encrypted-note
  cases were requested, but individual coverage/results were not recorded.
  Network disconnection was requested, not independently verified.
- No private notes or credentials were uploaded or displayed. Old/new
  interoperability, encrypted-note coverage, online sync, web clipping, search,
  print/export and broader GUI regression remain incomplete.
- First GitHub Actions run compiled and passed the data smoke suite, but failed
  packaging at `lipo -verify_arch`. Place the input filename before that option to
  support the runner's newer tool. The second run completed compilation, both
  smoke suites, deployment and artifact upload successfully:
  https://github.com/Defenceforce1/WizNotePlus/actions/runs/38335163135
  CI ran on macOS 26; it is not macOS 27 runtime certification.

Packaging requires all deployed libraries and signature verification; only a
bundle passing those checks may be handed off. Public release additionally needs
Developer ID signing, notarization, security/asset audit and manual regression.

## Reproducible route

Install Apple command-line developer tools, CMake >= 3.21, Ninja, Qt 6.11.3
with WebEngine, WebChannel, WebSockets, Core5Compat, Svg, Positioning,
StateMachine (qtscxml), image formats and translations. Qt 6.11 deployment also
needs SerialPort and TaskTree for its shipped plugins. Install static Crypto++
with headers under `include/cryptopp` (for example `brew install cryptopp`).
Native macOS builds use system zlib and no longer require Conan 1 or its obsolete
AppleClang settings. Legacy Conan remains an explicit option on other platforms.

```sh
bash build/osx/build-arm64.sh /path/to/Qt/6.11.3/macos /path/to/build /path/to/cryptopp-prefix
bash build/osx/package-arm64.sh /path/to/Qt/6.11.3/macos /path/to/build /path/to/new-output
```

Fresh checkout must initialize the pinned submodules. CMake applies the tracked
FramelessHelper Qt 6 patch idempotently. Qt 5 compatibility guards remain, but
Qt 5 has not been rebuilt. Qt 6.8.3 was a migration bootstrap only: its macOS
package references AGL, removed from the SDK used here, so it is not the release
toolchain. Do not remove framework links to pretend old Qt binaries are modern.

Restricted build sandboxes may deny Qt's CPU detection sysctl. If this causes
universal host tools to abort, set `WIZNOTE_QT_HOST_TOOLS_ROSETTA=ON` for the
scripts. This affects build/deploy tools only, not the arm64 app. Normal builds
leave it OFF. A runtime permission failure is not evidence of app compatibility.
On the tested machine Qt 6.11.3 host tools run natively; Rosetta is not required
for the final build/package. The workaround was needed only with the 6.8 bootstrap.

## Protect data during testing

The native data path remains `~/.wiznote/` unless `WIZNOTE_DATA_STORE` is set.
When that environment variable is set, browser cache/cookies are isolated there too.
Database schema, encryption algorithm and note/attachment filename conventions
are unchanged. Sample-level legacy-copy checks passed, but exhaustive migration
and encrypted-note compatibility have not been established.
Never start the experimental app on the only copy of existing notes. Quit the
old app, back up the entire data directory separately, then test an expendable
copy with an isolated data root. Example using an empty test data directory:

```sh
WIZNOTE_DATA_STORE=/absolute/path/to/empty-test-data/ /path/to/WizNotePlus.app/Contents/MacOS/WizNotePlus
```

Do not run old and new versions against the same data directory. Both use local
port 8848; concurrent copies can prevent startup. Confirmed locally that closing
the old app resolved this conflict. More complete GUI regression is still needed.
An isolated legacy copy still has the original account settings: keep it offline
and quit the test app before reconnecting, or test edits may sync to the real
cloud account. Use a dedicated empty test account for online sync/web clipping.
Data isolation alone does NOT isolate a cloud account.

The existing MathJax/editor/CSS resources are retained. No formula engine
replacement or Markdown-conversion feature is added. A new original-data batch
export feature is not implemented here. Local whole-directory backup/restore was
tested on isolated trial data; the existing HTML/attachment/metadata export is
not a complete native database backup. Original export/import still needs testing.
The build scripts make only ad-hoc signatures, not a Developer ID signature or
notarization. A successful CI run is not full macOS 27 runtime certification.

Licenses and remaining redistribution checks: [third-party-licenses.md](third-party-licenses.md).
Qt's migration note explains persistent profile behavior:
https://doc.qt.io/qt-6.8/qtwebengine-changes-qt6.html

Qt's Cocoa backend explicitly propagates an active-state event when reopening
from the Dock, even if the app was already active:
https://github.com/qt/qtbase/blob/6.11/src/plugins/platforms/cocoa/qcocoaapplicationdelegate.mm
