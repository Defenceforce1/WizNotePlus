# Markdown export source-flow diagnostic

Developer-only Qt executable. No App bundle, deployment, signing, accounts,
database engine, archive reader, or main-window initialization is used.

CMake extracts the current `saveAsMarkdown(QString&, bool)` body, the three-argument
UTF-8 writer and filename helpers from product source. Only the export class
qualifier and writer function name are renamed. The export method executes
against a real off-record `QWebEnginePage`; a synthetic database collaborator
writes controlled HTML, optionally reporting failure after that write. All output
is under `QTemporaryDir`, never a user-selected destination. External requests
are blocked. Explicit absolute SDK resources are preflighted before WebEngine.

Configure using the existing Qt SDK as `CMAKE_PREFIX_PATH`, build
`markdown-export-probe`, then run with `WIZNOTE_TEST_QT_SDK` set to that SDK root.
The program forces the offscreen platform, without disabling sandbox/security.

The default mode CHARACTERIZES the legacy ordering and exact bytes; a pass is
not product safety acceptance. `--require-safe` additionally requires existing
files to survive invalid replies and a modeled late database failure. It is
expected to fail against the legacy flow. An empty Markdown string is valid and
must not be rejected merely for being empty. The numeric reply is invalid as
Markdown source even though `QVariant::toString()` produces text.

This is not an actual `WizDocumentWebView` / database / resource-copy end-to-end
test. A modeled late failure is not proof that a real archive copy failed. Source
hashes and actual callback completion are recorded. This project does not fix
the export path or authorize integrating an incomplete transaction redesign.

## Unaccepted reply-first prototype

`markdown-export-candidate` compiles `candidate-body.inc`, NOT the main export
method. It permits only a genuine QString reply (including empty), then runs
the same modeled database export and actual writer/path helpers. Existing epoch,
weak owner/page and note identity checks are exercised through the adapter.
Destroying the adapter, invalidating its epoch, changing its synthetic note, or
detaching its page before delivery must preserve the temporary sentinel.
This does not prove actual main-window/navigation/destruction end-to-end safety.

`--require-reply-safe` checks only invalid JavaScript results; `--require-safe`
also checks the modeled late database failure. The latter must STILL FAIL for
this prototype: successful validation is not an atomic resource/file transaction.
No product source is changed or main target linked to this prototype.

Optional `WIZ_MARKDOWN_EXPORT_SANITIZE=ON` instruments the own harness/extracted
code with ASan/UBSan, not Qt itself. Keep leak detection disabled for that test;
do not describe it as whole-application or archive/resource-copy validation.
