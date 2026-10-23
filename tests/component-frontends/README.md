# Marked and CodeMirror5 maintenance update

Pinned packages: Marked18.1.1 and CodeMirror5.65.21. See `inputs.json` for official
archive identity/integrity and exact retained file hashes. Package archives are
verified before extraction; no npm install or lifecycle script is executed.

Only the existing standalone theorem-body Marked parser and CodeMirror core plus
three existing addons are replaced. The proprietary editor, legacy Markdown
converter, bundled language modes, stylesheet, toolbar, source/serialization
rules and PDF menu are not replaced or reverse-engineered. This is not a
CodeMirror6 migration and does not claim updated language-mode behavior.

Every promoted file matches its official upstream bytes. CodeMirror's unminified
core adds a small size cost versus the old minified file; no unverified minifier
transformation or performance claim is introduced. MIT notices are retained.

## Associated evidence

The existing public-editor theorem suite accepts an explicit local resource root
through `WIZ_THEOREM_ASSETS` and a project-owned fixture root through
`WIZ_THEOREM_TEMP_ROOT`. The production path remains the default. The same41
synthetic cases run with both MathJax2.7.8 and the isolated MathJax4.1.3 adapter,
including numbering, common/starred environments, nesting, Markdown body,
code exclusion, exact source and edit/read cycles. No normal account is opened.

`frontend-components.h` in the hidden native reader checks real CodeMirror5
version, legacy language/addon integration, code values, selection, undo/redo,
Unicode, literal tabs via its public API, theorem markup, source and native save
snapshots. Each engine has24 assertions. The existing mixed-reference fixture
adds95 and actual PDF entry point adds25(v2)/29(v4). Both PDF modes retain12pt
prose, exact local destinations, complete output and print restoration.

A literal HTML tab in the original stock editor is expanded to four spaces
before its source getter. This was reproduced with protected build220 as well as
the candidate. It is retained as a known pre-existing behavior, not fixed or
claimed as full literal-tab source preservation in this update. The canonical
note fixture uses four-space indentation; literal-tab preservation is tested
separately at the CodeMirror public value interface. Failure evidence is kept.

The source resource default remains MathJax2.7.8. Build222 is an independent
candidate based on221 with MathJax4 retained; it does not install into the current
App or merge test/real data. Synthetic checks do not establish all plugins,
languages, documents, real synchronization, memory usage or full-client acceptance.

Further Qt/WebEngine updates remain separate work, including the newly separate
WebEngine release/version scheme; no runtime or project-specific Qt patch is
changed by this maintenance update.
