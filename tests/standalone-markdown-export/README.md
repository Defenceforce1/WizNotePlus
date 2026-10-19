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
