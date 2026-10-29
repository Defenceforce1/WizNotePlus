# MathJax 4 isolated candidate

This directory retains the reproducible MathJax 4 assembly fixture and owned
compatibility bridge. MathJax 4.1.3 is the default production renderer since
phase255; this bridge is not a general MathJax 2 replacement library.
Build220 remains the protected daily-use rollback. Do not install this overlay
into an existing App, import real notes, or modify editor internals from this test.

## Public compatibility seam

The existing editor loads `mathjax/MathJax.js`. This default resource is our
bridge to real MathJax 4.1.3; the fixture here is kept byte-identical. It implements the used public
queue/configuration/input-object seam, retains TeX input scripts for the existing
private-paper exporter, and normalizes generated equation links to the existing
document-local namespace. It does not reproduce every MathJax 2 API.

The owned Markdown/theorem extensions, code exclusion, original source getters,
mode-change cancellation, PDF FIFO/restore guards and two export modes stay in
place. Assembly creates only a new isolated overlay. The native executable, frameworks,
database, encryption, accounts, current App and real notes are not changed.

Use the TeX font family to retain the established glyph style. Speech, semantic
enrichment, additional menus and default textmacros are disabled; colorv2 keeps
the older color syntax. The official safe extension is loaded, not bypassed.
Unbreakable wide SVGs are locally fitted without reducing the whole page's prose.
Spacing, local fitting and pagination are not guaranteed pixel-identical to v2.

## Pinned offline assembly

`inputs.json` lists the official archives and exact SHA256 of each retained file.
Verify the archive digests, extract without npm lifecycle scripts, then run:

```sh
node tests/mathjax4/prepare.cjs /path/to/mathjax-4.1.3 /path/to/tex-font-4.1.3 /path/to/new-mathjax
```

The destination must not exist. The script verifies every retained input and
creates only a new overlay. It performs no network access or installation.
The TeX SVG font, MathJax core, TeX extension components and Apache license text
are included. CHTML, speech, newcm fonts and build/example trees are not copied.

## Bounded evidence

Associated tests use the actual unchanged editor/bridge and synthetic source:
mixed and forward references, split loading, source preservation, native save
snapshots, rapid/late mode requests, edit/read cycles, both native PDF modes,
normal prose font size, exact local destinations and print restoration. They
do not establish every TeX macro, all historical documents, real-account use,
long-term memory behavior or universal pagination.

Font-only timings must not be presented as whole-App performance. See the
phase221 checkpoint and MathJax4 candidate report for results and limitations.

## Current lifecycle regression

`lifetime-regression.js` runs only in a synthetic, initialized editor page.
Observe `WizMathLifetimeRegression.done`, then require zero `failures`, not merely
process exit 0. Fourteen actual-engine assertions cover Process/Typeset stability,
scoped Reprocess/Rerender, explicit Remove and reconstruction, disconnected scope
retirement, repeated DOM replacement, original SVG/source preservation, queue
order and the 256-entry recent-operation trace. It creates no production account
and must not be injected into a real note.

Phase267 independently validates this fixture with current locked components.
The bridge keys retained script/placeholder metadata by stable MathItem identity
and retires engine items for fully disconnected source/output pairs. Engine/font
files, source syntax and PDF layout algorithms are unchanged. This bounds specific
owned records; it does not establish that WebEngine resident high-water is resolved.

## Editing transition retirement

`editing-retirement-regression.js` is another synthetic-page-only test. The
independent native driver supplies the expected source and the actual save
producer scripts from `WizDocumentAsync.h`. Require `WizEditing269.done` and zero
failures. Its 23 assertions cover queued retirement after the editor callback,
connected-reader preservation, detached scan roots, current unsaved DOM, capture
failure retention, reader reconstruction, references and delayed cleanup while
switching back to reading.

`Hub.WizRetireDisconnected()` uses the existing math queue. It retires only
records with disconnected input and output, and disconnected completed scan
scopes. The owned note-style callback invokes it for an editable body. It does
not render, reset labels, clear current source or alter connected output.
The native mode-change callbacks already apply note styling after transition.
Known references are retired earlier; this is not measured physical-memory
savings or proof that every old DOM owner has gone away.
