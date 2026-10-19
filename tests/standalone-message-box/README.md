# Independent message-box lifetime diagnostic

Developer-only Qt Widgets harness. No main client identity, account, database,
WebEngine, file export or user settings are linked. It refuses non-offscreen
platforms. Do not deploy, sign, install or launch it as the normal App.

`message-box-baseline` compiles the actual product helper and header. The separate
`message-box-candidate` generates a source copy replacing only the central helper
body with `candidate-body.inc`. That prototype is **not accepted or linked into
the main App**. Source/candidate hashes are logged; configure fails if the old
stack-dialog source no longer matches the expected shape.

Default runs compare normal warning/information/question/critical choices,
parentless overload, icon/text/buttons/defaults, translated labels and cleanup.
`--destroy-parent` explicitly destroys a synthetic parent during the actual modal
loop. The baseline is expected to expose the stack-dialog lifetime failure;
run it only as an isolated diagnostic child and retain its private failure log.
The candidate must return NoButton without accessing a destroyed dialog.

`--destroy-warning-parent` tests only the warning path. The narrow production
warning fix can be checked independently without implying that all question,
information or critical caller lifetimes/decisions were accepted. Historical
baseline ASan evidence is pinned to its logged source hash.

`--destroy-critical-ok-parent` isolates the separately audited single-OK critical
path. The actual helper now guards this exact icon/button set, not information,
questions or other critical button sets. All eight direct single-OK critical
call sites were reviewed for returning/ending after the modal, but their complete
workflows (accounts, note combination, passwords and queued work) are NOT accepted
by this helper-only probe. Never run those workflows against real user data.
The normal fixture now also calls the actual implicit-default critical overload:
NoButton remains an absent default button, not an invented automatic OK default.

`--destroy-information-ok-parent` isolates the separately audited single-OK
information path; multi-button information (including purchase prompts) remains
unchanged. Normal cases now include the actual implicit-default information
overload. Post-modal caller guards are required independently of helper ownership.

The two `message-box-email-*` executables extract only the actual network-error
slot into a synthetic QDialog/label shell. No actual email dialog constructor,
network manager, account, database or real error request is used. The actual-helper
target exercises production helper plus extracted current slot; prototype-helper
exists only to expose a caller UAF with the old unguarded slot after experimental
helper ownership bypasses the original stack failure. That broader helper never
links into the App. Normal and `--destroy-parent` runs log separate source hashes.

Configure with the existing Qt SDK and `WIZ_MESSAGE_BOX_SANITIZE=ON` (default).
Use `QT_QPA_PLATFORM=offscreen`, ASan/UBSan with leak checks disabled for the
uninstrumented Qt runtime. Successful compilation is not execution or acceptance.
Before any product integration, inspect callers that access their own state
after a modal returns: fixing helper ownership alone cannot protect those callers.
Also inspect decision semantics: legacy callers that check only `result != No`
can interpret NoButton as approval. Never globally change question return paths
on the strength of this helper-only probe, or exercise real folder/account actions.
On macOS QMessageBox intentionally ignores window titles; the fixture checks that
documented platform behavior rather than requiring a visible title.
