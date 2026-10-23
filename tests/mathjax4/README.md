# MathJax 4 isolated candidate

This is an experimental resource overlay, not the production renderer and not
a general MathJax 2 replacement library. Production resources remain 2.7.8.
Build220 remains the protected daily-use rollback. Do not install this overlay
into an existing App, import real notes, or modify the proprietary editor.

## Public compatibility seam

The unchanged editor loads `mathjax/MathJax.js`. In an isolated candidate copy,
that file is our bridge to real MathJax 4.1.3. It implements the used public
queue/configuration/input-object seam, retains TeX input scripts for the existing
private-paper exporter, and normalizes generated equation links to the existing
document-local namespace. It does not reproduce every MathJax 2 API.

The owned Markdown/theorem extensions, code exclusion, original source getters,
mode-change cancellation, PDF FIFO/restore guards and two export modes stay in
place. Only the test copy receives the overlay. The native executable, frameworks,
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
