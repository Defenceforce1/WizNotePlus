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
- A dedicated test account's bidirectional sync, title/body search, small attachment
  persistence/sync, undo/redo and Chinese copy/paste received user-reported sample
  confirmation. Safari clipping to that same account remains unverified: an earlier
  clip was found in a different account and the user deferred clipper-login testing.
  No private notes or credentials were uploaded or displayed. These are not complete
  server, encryption, conflict, large-attachment or broader GUI regression results.
- Added built-in personal note/folder native export/import, with original ZIW and
  attachment bytes, metadata and unencrypted HTML/resource views. Three local test
  suites pass, including synthetic database round trips, path/checksum/CRC rejection,
  repeated imports, injected write failures and ephemeral-certificate encrypted ZIWR.
  This is a new WizNotePlus envelope, not verified official/other-app interchange.
  Manual menu/editor/imported-note/cloud tests remain pending. See
  [native-note-transfer.md](native-note-transfer.md) for scope, limits and safety.
- First GitHub Actions run compiled and passed the data smoke suite, but failed
  packaging at `lipo -verify_arch`. Place the input filename before that option to
  support the runner's newer tool. The second run completed compilation, both
  smoke suites, deployment and artifact upload successfully:
  https://github.com/Defenceforce1/WizNotePlus/actions/runs/38335163135
  CI ran on macOS 26; it is not macOS 27 runtime certification.
- The native-note transfer commit's CI also succeeded:
  https://github.com/Defenceforce1/WizNotePlus/actions/runs/38346437328
- Fixed the old export wizard's automatic directory opening to use a local-file URL,
  preserving Unicode/spaces/hash/percent. User reported a LaunchServices -50 dialog
  at completion; the faulty bare-path URL was identified, but the repaired workflow
  still needs user confirmation on that machine.
- Added portable HTML/folder export alongside the native package. Synthetic tests
  cover unchanged bodies/images/attachments, title/URL escaping, duplicate titles,
  destination refusal and encrypted-selection rejection. Target-app import remains
  unverified; see [native-note-transfer.md](native-note-transfer.md).
- Reapply native traffic-light enablement after activation, window-state and modal
  transitions, respecting modal blocking; preserve collection behaviour and route
  fullscreen through Qt. Export uses a non-dismissible modal progress window instead
  of disabling the top-level main window. A Cocoa-only synthetic window test passed
  native pointer hit tests, actual yellow/green button actions, restore/fullscreen
  transitions and modal-close recovery, including a hidden prior modal login window.
  It reads no account or real notes and is not a full WizMainWindow test. The user's
  report of permanently ineffective main-window buttons is not yet confirmed fixed.

Packaging requires all deployed libraries and signature verification; only a
bundle passing those checks may be handed off. Public release additionally needs
Developer ID signing, notarization, security/asset audit and manual regression.

### Follow-up: native main-window button actions

The user confirmed the v4 export workflow and other sampled behaviour were normal,
but the main yellow/green buttons still highlighted without changing the window.
Both corresponding menu actions worked. Thus the window can change state; v4's
enablement restoration alone did not solve the button-action path. The exact native
responder-chain failure has not been observed in the user's live process.

The follow-up binds those two native buttons to a target retained by their own NSWindow,
which dispatches directly to a QPointer of the owning QWidget. It never guesses
`QApplication::activeWindow()`. Binding is reapplied after show, activation, resize and
window-state transitions; it neither changes the red close button nor forces buttons
enabled during a modal dialog. Option-click green keeps zoom behaviour, while normal
green toggles fullscreen through Qt. Fullscreen transition notifications temporarily
suppress overlapping actions, with a 10-second stale-notification fallback.

The Cocoa test now loads only a synthetic off-the-record WebEngine editor, deliberately
injects no-op targets (clicks dispatch but do not move the window), then verifies the
direct binding repairs them. It checks owning-window versus other-active-window actions,
modal blocking and repeated binding. Checks wait for native animation completion and
native/Qt states, not just Qt's requested state. Early timing-sensitive failures are
retained in local verification logs; the revised build-environment run passed. This
does not prove the user's live main-window failure is fixed. User retesting, Option-click,
hover/tile menus and broader multi-screen/Spaces behaviour remain pending.

The v4 commit's full CI succeeded:
https://github.com/Defenceforce1/WizNotePlus/actions/runs/38354044557

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
replacement or Markdown-conversion feature is added. The new original-note batch
export is limited to notes/populated folders; no built-in whole-library backup was
added. Local whole-directory backup/restore was previously tested on isolated trial
data. Neither the new native note envelope nor existing HTML export is a complete
native database backup. The existing export/import path still needs separate testing.
The build scripts make only ad-hoc signatures, not a Developer ID signature or
notarization. A successful CI run is not full macOS 27 runtime certification.

When compatibility tests are enabled, the app also accepts
`--mac-window-controls-self-test` on macOS. It opens isolated synthetic Cocoa windows
and bypasses normal login/network/data cleanup. Run it in a desktop session separately
from offscreen CTest; it is not part of the headless CI suite.

Licenses and remaining redistribution checks: [third-party-licenses.md](third-party-licenses.md).
Qt's migration note explains persistent profile behavior:
https://doc.qt.io/qt-6.8/qtwebengine-changes-qt6.html

Qt's Cocoa backend explicitly propagates an active-state event when reopening
from the Dock, even if the app was already active:
https://github.com/qt/qtbase/blob/6.11/src/plugins/platforms/cocoa/qcocoaapplicationdelegate.mm
