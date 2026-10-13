# Note presentation CSS and mathematical environments (v14)

V14 adds an optional Scientific math environments preset adapted from the user's
Notebooks stylesheet: blue theorem/lemma/proposition/corollary blocks sharing one
counter, independently numbered green definitions and orange examples, gray
remarks/proofs and a proof-end square. No Notebooks-only directive or supplied
JavaScript is executed. The original MathJax 2 engine and original PDF exporter
are unchanged; modern `mjx-container` rules are adapted to `.MathJax_Display`.
The light appearance is kept rather than applying incompatible system-dark colors.

With the preset marker present, read-only Markdown quotes beginning with an isolated
bold math heading are decorated transiently. Example: `> **定理（名称）**
\label{thm:name}` followed by the ordinary quoted body. English headings work too.
Adjacent math headings in one merged Markdown quote are separated in the rendered
DOM only. Original node order and attributes are restored for serialization and
on disabling/canceling CSS. The original public HTML/Markdown getters are guarded
to exclude decoration; editing remains the original editor, not a new parser.

Labels are unique per note, 1–128 ASCII letters/digits or `_ : . / -`.
Prose `\ref{thm:name}` becomes a numbered local link; clicks are intercepted in
the page to avoid the native external-file link forwarder. Duplicate labels warn
and are not linked. Unknown labels are retained. A standalone inline math reference
can be intercepted before the initial MathJax pass, but already-typeset references
are not retroactively integrated into MathJax: prefer prose references without `$`.
Compound formula references and `\eqref` retain original MathJax semantics. Block
labels are not registered in the equation-label table. Cross-note references,
Notebooks format-menu/autocomplete and the attachment's additional macros are not
implemented. Use distinct block/equation label names.

Handwritten headings such as `定理 7` retain that number and continue the following
automatic integer sequence at 8. Dotted manual numbers are displayed verbatim but
do not imply automatic section-aware numbering. The preset's CSS is editable;
quote headings use program-resolved numbers so references agree with displayed
titles, while pre-existing HTML environment classes can use the CSS counters.
Changing numbering conventions in arbitrary custom CSS is not a new numbering API.

V14 local build verification: all five CTest suites pass. The Cocoa CSS fixture
passes 40 checks across five original-reader notes, including typed classes,
merged-quote splitting, manual numbering, forward references, duplicate-label
rejection, local click handling, HTML/Markdown/reader-close serialization guards
and original source retention. The actual native main-window fixture passes 50
checks, including a synthetic Markdown note, typed blocks/label link, entry to
source editing and return to preview with identical Markdown, plus all existing
search/native-control checks. The new-note fixture explicitly initializes the
type-aware reader after asynchronous editor readiness and synchronizes both native
mode holders; this is fixture setup, not a production mode-management change.

Initial CSS tests exposed Markdown's merging of adjacent quoted paragraphs and
temporary decoration in serialized HTML; transient splitting and serialization
guards were implemented. A code-content assertion targeted the renderer's hidden
measurement placeholder instead of the Markdown source; it was corrected to verify
the unchanged complete code source and absence of generated links in code. Initial
native tests entered source/reader modes before initialization or did not synchronize
both mode holders; their failing logs are retained, not counted as passes. Complex
new-style PDF pagination, physical typing and future macOS compatibility remain
unverified; the original PDF function was not modified.

The deployed Cocoa package repeats all 40 CSS and 50 native-main-window checks.
Its 47 Mach-O files pass the arm64/dependency audit, and strict ad-hoc signature
verification passes. Packaged typed-class/quote/formula screenshots were visually
reviewed. This is not Developer ID signing, notarization or future macOS certification.

## Historical v12 changes and verification

V12 keeps the restored original PDF exporter unchanged. Selected tags/folder/attachment
conditions now appear as removable tokens inside the search field. Text/caret and the
IME insertion rectangle follow the tokens; tag labels remain separate from the keyword.
Overflow folds into +N while retaining keyword entry space. The magnifier panel still
edits every condition and offers Clear filters, which preserves the keyword. Selecting
filters returns focus to the text field after searching. Toolbar height stays one line.

The local CSS setting already applies to all opened notes, without per-note setup.
The optional Insert math quote-block style button appends a reusable stylesheet once;
it neither replaces existing CSS nor applies/saves anything until Preview/Save. This
template specifically targets `.wiz-editor-body.markdown-body blockquote`, the bundled
Markdown reader's existing DOM class, so ordinary rich-text quotes and Markdown source
editing are not restyled by this template. Standard `>` blocks and a bold first-line
title are sufficient. All Markdown quote blocks share this style; theorem/definition/
proof semantics, separate typed environments, automatic numbering and new parser syntax
are NOT introduced. Existing math rendering and cross-reference support are unchanged.

Two isolated Markdown fixtures use the same locally saved CSS. Regression checks cover
quote background/border/title rendering, formulas, original source equality, temporary
CSS exclusion from saved HTML, duplicate-free template insertion and cancel recovery.
No remote resources, real notes, old installed app or new PDF rendering path are used.

V12 verification: all five CTest suites pass. The build-tree main-window fixture
passes 46 checks; the deployed Cocoa repeat passes the same 46 checks, including
token containment, caret/IME coordinates after tokens, Chinese keyword composition,
keyword-only clear, filter folding/removal and native window transitions. Both build
and packaged CSS fixtures pass 19 checks, including two Markdown notes using the
same global stylesheet, styled quote/title blocks and preserved source/formulas.
Rendered quote blocks and token/keyword screenshots have been visually inspected.
The bundle passes its 47-Mach-O arm64/dependency audit and strict ad-hoc signature
verification. The original PDF function still matches the pre-v10 implementation.

One initial CTest run used the removed clear-chip selector and failed that UI check;
the test was updated to exercise Clear filters in the existing magnifier panel,
with the same keyword-preservation assertion. Its failing log is retained. One
deployed GUI run stopped at tag suggestions while the app/native key window were
inactive; its log is retained and it is not counted as passing. The subsequent
serialized repeat passed. This does not establish the cause of the inactive run,
or replace manual mouse/IME testing in a real session. Developer ID signing,
notarization and future macOS certification remain outside these local checks.

## Historical v11 changes and validation

At the user's request, v11 removes the new Print / PDF preview action, its Cmd-P
binding, the separate rendering window, native-printer path and `printPreview.js`.
The `saveAsPDF()` function is restored verbatim to the pre-v10 checkout version:
it exports the current WebEngine page directly, using its original save dialog,
A4 pagination and original margin handling. No additional rendering pass, preview,
new pagination CSS or formula conversion is inserted into that function.

Tools > Note body CSS now has just one editor. The former screenCss setting key
is retained, but its CSS is no longer wrapped in `@media screen`, so the same
stylesheet participates naturally in the original PDF export. The retired printCss
setting is ignored, not deleted. User-authored media queries still have their normal
CSS meaning. Print-specific styles already in the original engine are untouched;
physical page width and pagination mean PDF cannot promise pixel-identical screen
geometry. This change does not implement a new PDF or physical printing feature.

Enable/disable, local persistence, live preview, cancel recovery and Restore defaults
are retained. Temporary CSS is not saved into note content. Existing Markdown editing
habits, formula renderer, search, window controls and light-skin refinements remain.

The v10 validation described below is historical and does not validate v11 exports.
V11 tests cover the single CSS settings path and actual main-window CSS preview,
unchanged editor source/mode and canceling the restored PDF save dialog. The former
independent-print-copy tests have been removed with that implementation. Complex
user Markdown/PDF output still requires manual testing; no real account data is used.

The search field has a consistent 30-logical-pixel height, softer border/focus colors,
and reserved text space for both icons. Filter chips are moved into a separate,
collapsible row below the toolbar rather than making the search toolbar item two
lines tall. This keeps Search and New Note vertically centered together with and
without filters. Search semantics, chip removal and the magnifier panel are unchanged.

V11 verification: the native Qt 6.11.3 build passes all five CTest suites. Both the
build-tree and deployed Cocoa main-window fixtures pass 44 checks, including actual
coordinate comparisons (Search/New Note centers within one logical pixel), tag
filters in the separate row, readable long-keyword spacing, CSS preview/cancel,
unchanged note HTML, restored PDF save-dialog cancellation and native window controls.
The packaged empty/filled/filter search screenshots have been visually reviewed.
The bundle passes the 47-Mach-O arm64/dependency audit and strict ad-hoc signature
verification. V11 has not generated or visually validated new user-note PDFs; restoring
the original exporter is verified by source equality, not a claim of resolving every
previously reported Markdown layout issue. No installed old app or real data was used.

## Historical v10 implementation and validation (superseded)

This adds local rendering CSS and a real print route, while retaining the bundled
Markdown/rich-text editor, existing shortcuts and edit/read switching. There is
no new source editor, Markdown conversion, TeX installation or full `.tex`
compilation. Formula numbering/references use the original bundled MathJax engine.
No note schema, encryption, sync protocol or import/export format is changed.

## User-facing changes

- The default light skin has a softer toolbar gradient, blue-tinted hover states
  and more distinct selected/unselected tabs. Existing layout/icons and native
  Cocoa window-button ownership are unchanged; this is not a new dark theme.
- Tools > Note display and print CSS provides separate screen/print fields,
  an enable switch, live current-note preview, Save/Cancel and Restore defaults.
  Cancel restores the previous preview; resetting is persisted only with Save.
  Settings are local `QSettings` keys under `NotePresentation`, not synced DB
  metadata. Each field is limited to 65536 characters. CSS from external sources
  must be trusted: external URLs in CSS can cause network requests.
- The adapter inserts engine-temporary styles, which the original serializer
  excludes from saved note HTML. It does not change Markdown source/shortcuts.
- File > Print / PDF preview (the system Print shortcut, Cmd-P on macOS) renders
  a separate temporary copy using the same bundled reader/MathJax assets. Relative
  resources retain the original note's base URL. The current editor mode/content
  is not changed or saved just to print. Preview is deliberately **not paginated**;
  actual pagination is visible in the generated PDF or final printer output.
- Existing Save as PDF remains: choose a destination, then the independent copy
  renders and generates the PDF. A4 is the default; legacy physical-unit margin
  preferences are converted to millimeters. Invalid margins use a safe default.
  Native printing opens the system printer/paper/orientation dialog.

PDF is an additional fixed-layout reading/printing copy, not a replacement for
editable notes and not a new interchange/import format.

## Safety and failure handling

Formula queues, image loading and fonts must finish before output is enabled.
Missing images or a rendering timeout are reported rather than silently printing
an incomplete copy. Rendering has a 60-second deadline. Built-in print CSS removes
screen-only full-height constraints, avoiding an extra empty final page in the
short Markdown fixture, and requests that headings/formulas/table rows avoid splits.
Arbitrary very large formulas/rows cannot be guaranteed to fit one physical page.

PDF generation is asynchronous. The destination is never pre-deleted; a PDF header
is checked and a `QSaveFile` commit handles successful replacement. The save dialog
uses a default PDF suffix before overwrite confirmation. The printer object is
retained until asynchronous printing reports completion. Busy output windows do
not dismiss themselves mid-job. No automatic opening of the PDF is performed.

Opening the modal print UI is deferred out of WebEngine's JavaScript-result callback,
so that callback cannot block WebEngine IPC needed to initialize the new page.

## Verification and limits

The native Qt 6.11.3 Release build passed all five CTest suites. The new isolated
presentation fixture passed 26 checks in the deployed Cocoa bundle: script-safe CSS
encoding, local persistence/size limits/disable, preview cancel, legacy inch margins,
offline rich-text and Markdown render, completed formulas and resolved `eqref`,
non-empty original source, temporary-style serialization exclusion, unchanged source,
real PDF bytes/pagination/no short-note blank page and an unwritable destination.
No remote resource request was needed for these bundled-renderer fixtures.

The generated short Markdown/formula PDF (one A4 page) and rich-text/image/table
PDF (three A4 pages) were rendered to images and every page visually inspected,
including Chinese glyphs, code, equation labels/references, margins and page endings.
These are generated fixtures, not account notes. No physical printer job was sent.

The actual-main-window Cocoa fixture additionally checks the CSS/print menu routes,
cancel recovery, unchanged editor HTML/mode and all previous search/window controls.
A build-tree run passed all 41 checks. One initial integration run timed out while
opening print UI inside a JavaScript-result callback; its log is retained and the
deferred-opening fix was then tested. One concurrent deployed desktop run failed
an existing search-chip transition; its log is retained and it is not counted as a
pass. GUI regression runs should be serialized to avoid competing desktop focus.
Two subsequent serialized deployed runs passed all 41 checks each; this does not
prove the cause of the concurrent run's failure or replace manual mouse testing.

The bundle passed a 47-Mach-O arm64/dependency audit and strict ad-hoc signature
verification. It is not Developer ID signed/notarized. Physical printing, user-specific
CSS/notes, wide equations, outline/encrypted-note printing, Qt 5, multiple monitors
and future macOS versions remain unverified. Use only a dedicated test account:
an isolated local store does not prevent that account's cloud synchronization.

No installed legacy app, real notes or credentials are used by the regression routes.
There are no new third-party libraries/assets or license changes in this feature.
