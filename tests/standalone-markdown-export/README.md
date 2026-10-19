# Markdown export source-flow diagnostic

Developer-only Qt executable. No App bundle, deployment, signing, accounts,
database engine, archive reader, or main-window initialization is used.

CMake extracts the current `saveAsMarkdown(QString&, bool)` body, the three-argument
UTF-8 writer and filename helpers from product source. Only the export class
identifier and writer function name are renamed. The export method executes
against a real off-record `QWebEnginePage`; a synthetic database collaborator
writes controlled HTML, optionally reporting failure after that write. All output
is under `QTemporaryDir`, never a user-selected destination. External requests
are blocked. Explicit absolute SDK resources are preflighted before WebEngine.

Configure using the existing Qt SDK as `CMAKE_PREFIX_PATH`, build
`markdown-export-probe`, then run with `WIZNOTE_TEST_QT_SDK` set to that SDK root.
The program forces the offscreen platform, without disabling sandbox/security.

`markdown-export-probe --require-reply-safe` now tests the current narrow
reply-first product method. `markdown-export-legacy --require-reply-safe` runs
the frozen extracted pre-guard method and MUST still fail four invalid-result
safety checks. The legacy file and current source hashes are recorded separately.
`--require-safe` additionally requires preservation after a modeled late database
failure, which remains UNSOLVED by the narrow product fix. A pass of ordinary
characterization is not full safety acceptance. Empty Markdown is valid; numeric
results are invalid even though QVariant can stringify them.

This is not an actual `WizDocumentWebView` / database / resource-copy end-to-end
test. A modeled late failure is not proof that a real archive copy failed. Source
hashes and actual callback completion are recorded. Do not infer acceptance of an
incomplete resource transaction redesign from the narrow method's success.

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
The product now has the separately reviewed narrow reply/type/lifetime guard,
retaining the original valid-output deletion policy. This test prototype itself
is never linked into the main target; resource transaction code stays test-only.

Optional `WIZ_MARKDOWN_EXPORT_SANITIZE=ON` instruments the own harness/extracted
code with ASan/UBSan, not Qt itself. Keep leak detection disabled for that test;
do not describe it as whole-application or archive/resource-copy validation.

## Actual resource methods and unaccepted staging prototype

`markdown-resource-probe` uses extracted, unmodified `WizCopyFolder`,
`WizDatabase::exportToHtmlFile`, UTF-8 reader/writer and path helper bodies.
It links no database engine or archive reader: ONLY archive extraction is an
explicit adapter copying a synthetic directory, with an optional unreadable
temporary resource. The shared encoding compatibility header requires Qt Gui
and Core5Compat but this executable creates only QCoreApplication, no windows,
WebEngine, settings or accounts. Source digests identify the extracted files.

Default characterization records the actual copy helper deleting an old resource
before an unreadable copy fails, and the actual export method having already
written HTML and earlier resources. `--require-safe` must fail on that old path.
All permission changes apply only to QTemporaryDir files and are restored.

`--transaction-prototype` separately exercises `transaction-prototype.h`, never
linked into the main App. It snapshots old resources into a same-parent temporary
directory, exports there under the original basename, reserves a QSaveFile, swaps
resource directories and commits Markdown last. An ordinary final-commit failure
rolls resources back. Existing unrelated resources retain the old merge behavior;
valid output path and UTF-8/no-BOM bytes remain identical. Directory/symlink
destinations are refused in this prototype, NOT a changed product policy.

This is NOT power-loss-safe or a multi-file atomic operation. Two controlled
rollback-failure fixtures now verify that a blocked new-directory rollback or
blocked old-directory restore retains the old backup (and returned new directory
where applicable), reports the recovery path, preserves Markdown and refuses to
overwrite an unexpected destination. Test-only callbacks inject these faults;
actual directory renames and permissions still determine the failure. Permission
changes are restored, then the outer QTemporaryDir cleans its synthetic fixtures.

General concurrent-writer recovery, recovery across restart/power loss, a recovery
UI, real archive/password workflows and main-window integration remain unaccepted.
Do not describe controlled backup retention as guaranteed successful rollback.
Never run this developer probe against user destinations or call it a shipped fix.
