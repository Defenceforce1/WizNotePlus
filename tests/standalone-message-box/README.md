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
