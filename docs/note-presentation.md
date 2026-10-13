# Note presentation and printing (v10)

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
