# Offline functional regression targets

This small project compiles an explicit whitelist of existing compatibility
fixtures against the current production helper headers and the matching Qt SDK.
It does not import the main compatibility target list or link/start the normal
client. No network/security, Keychain, certificate creation or foreground probe
is enabled. The async API fixture uses the existing synthetic transport.

Configure with CMake/Ninja and a local `CMAKE_PREFIX_PATH`, then build. Run
Widgets/WebEngine cases with `QT_QPA_PLATFORM=offscreen`, a private temporary
root, and the matching packaged Qt frameworks and WebEngine helper. These tests
use disposable fixtures, never a user account or persistent note directory.

Targets: bounded-cache, thread-pool, document-owner-actions, async-api-lifetime,
document-async, login-window-recovery, pdf-export-lifecycle, pdf-paper-page and
initial-note-load.
The last two produce temporary synthetic PDFs and must follow the PDF artifact
verification workflow. Actual mixed formulas and final PDF page geometry are
also checked separately by the hidden native reader, not inferred from this
project passing. See the phase236 report for scope and incomplete coverage.

In phase236 the historical explicit-shove assertion was updated to distinguish
row reconstruction from the already-existing whole-SVG local fitting fallback.
The stronger check requires unchanged original body, no row copies, bounded
geometry, a number destination and exact restoration. The helper is ensured
before attaching a trusted active-print observer, matching the adjacent control.
No production PDF logic was changed to make that test pass.

The initial-note-load target uses QtCore only. CMake extracts the actual owned
HTML preparation, initial navigation and source-cache getter bodies; small
recording view/style collaborators replace the rest of the client. Its UTF-8
BOM fixture uses real temporary file reads and the production atomic writer.
It checks exact prepared output/source cache, reader/edit opening rules,
notification counts, retained dirty DOM/save ticket, missing/write-failed input,
and stale/destroyed view continuation. This is not a renderer, save-worker,
all-encoding or full-client acceptance. No normal account or database is opened.

An optional WIZ_INITIAL_NOTE_LOAD_BASELINE path may point to the pre-change
owned WizDocumentWebView.cpp snapshot. It builds initial-note-load-before with
the same fixture and a two-read control; the current target requires one read.
Phase253 passed39 current checks and39 old-control checks separately. The four
6000-row cases read each complete input twice before and once after, with exact
expected UTF-8/BOM output. No App RSS, rendering latency or WebEngine high-water
benefit is inferred from this I/O-count change; the native product still needs
a future production build before candidate acceptance.

For the macOS paper-page graphics path, use `QT_QPA_PLATFORM=cocoa` together
with `WIZNOTE_SYNTHETIC_HIDDEN_COCOA=1`. That test-only opt-in sets the Qt plugin
application attribute before QApplication; output views already have
WA_DontShowOnScreen. It does not open the normal client or foreground a window.
Retain and reject offscreen SkSurface errors as normal-graphics acceptance,
even if the functional assertions pass. Phase237 only repeats this affected
case with hidden Cocoa, not the full formula or component suites.
