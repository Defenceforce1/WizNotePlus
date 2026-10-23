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
document-async, login-window-recovery, pdf-export-lifecycle and pdf-paper-page.
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
