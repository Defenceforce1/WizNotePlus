# Standalone Markdown jQuery association checks

The phase233 update promotes only the public standalone asset used by Markdown:
full jQuery3.3.1 to full3.7.1. The legacy URL remains jquery-3.3.1.js because the
loader is in the unchanged restricted editor. Actual runtime versions and the
input manifest, not the compatibility filename, identify the installed library.
The independent1.11.3 file is retained and no global is injected into rich text.

`reader-opening FILES jquery-paste OUTPUT` requires the exact
`.wiz-synthetic-jquery-paste` marker (`synthetic-only` plus LF) and
`expected-jquery.txt` containing3.3.1 or3.7.1. It has18 checks: actual initialized
version, full/legacy API presence, Deferred, editing, synthetic DataTransfer and
jQuery originalEvent handling/unbinding, actual public pasteB64 text/HTML
payloads, Unicode/literal angle brackets, public undo/redo, current-page snapshot
and the subsequent reading transition. No native clipboard is read or changed.
The synthetic event is explicitly untrusted and its receiver is a fixture;
payload insertion separately calls the same public editor API as native plain
paste. This is not a trusted Cocoa paste or a real database save acceptance.

Associated old/new resource runs are bounded to the changed dependency:

| Group | Old/new checks | Scope |
| --- | ---: | --- |
| jquery-paste |18/18|Public paste payloads and event/API compatibility|
| frontend-components |24/24|CodeMirror public edits/undo/addons, theorem Markdown and source snapshots|
| diagram-components jquery-related |13/13|Existing flow/sequence SVG, one math reference, source-preserving edit/read cycle|
| math-references on source2.7.8 |32/32|Two refs, supplemental split-pass label retention, real click targets and mode cycle|
| math-references on candidate4.1.3 |32/32|Same bounded reference and navigation checks|
| mathjax-upgrade4.1.3 |29/29|Existing environments/theorem/code and actual product A4/reading PDF paths|

These total296 successful runtime assertions. A further22 checks on the actual
promoted source confirm Markdown3.7.1, rich-text global absence, both save
snapshots and the mode cycle. Four synthetic PDFs have correct destinations,
normal12pt prose and expected paper width; candidate rasters match baseline
pixel-for-pixel in both modes, with both distinct pages visually reviewed.
Original source/HTML/DOM/modified state is restored after each export.

The jquery-related diagram variant only skips unrelated full utility tests and
duplicate diagram PDFs; it keeps the current1.13.8/2.3.0 version and SVG/source
assertions. Original diagram variants are unchanged. No formula/database/archive
full matrix, system clipboard, real notes/cloud sync, all plugins/macros, or
performance/memory claims are implied. No new App/package is produced.
