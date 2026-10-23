# Diagram dependencies: bounded maintenance update

Pinned official npm distributions: Underscore 1.13.8 UMD minified and Raphael
2.3.0 full minified bundle (including its Eve dependency). SHA512 integrity,
registry SHA1, archive SHA256 and extracted file bytes are checked. No npm
installation lifecycle runs; upstream MIT notices are retained. The existing
`underscore.js` and `raphael.js` resource paths remain stable. Inputs and four
retained file hashes are in `inputs.json`.

Only these two distributions change. The restricted editor is loaded unchanged
through public APIs, not inspected or modified. Existing flowchart.js 1.11.3,
js-sequence-diagrams 2.0.1, their grammars, fonts, CSS, jQuery, MathJax, Marked,
CodeMirror and Qt remain unchanged. This is not a new diagram syntax or engine.

`standalone-reader-opening/diagram-components.h` runs the actual public Markdown
reader with synthetic flow and sequence fences and one mixed equation/reference.
It checks loaded versions, SVG labels, common Underscore utility results,
debounce, Raphael Unicode bounds/removal, editing, actual save snapshots and
reading again. The previous packaged build222 passes 31 assertions; staged
updates and final build224 resources each pass 37, including both actual PDF
export modes and original DOM/source/dirty-state restoration. The two diagram
widths, heights and path counts match the baseline exactly in this fixed sample.

Four synthetic PDFs are audited for diagram labels/vector geometry, ordinary
12pt prose, page widths, a precise local equation target and page bounds, and
all four pages are rendered and viewed. Final packaged rasters match the staged
ones. This does not establish every diagram shape/theme, all documents, universal
pagination, real cloud synchronization or long-running memory behavior. No full
old formula/database/archive suite is repeated and no security testing is run.

All synthetic WebEngine profiles/temp files use the project temporary area and
are automatically removed. Qt's separately generated diagnostic pipeline cache
is retired after the checks. No new trial user store is created: build224 uses
the existing fixed store via the shared launcher template, and only the
read-only `--check` startup path is exercised. Actual account login is not
accepted by this test; saved login preferences are neither read nor changed.

Official references:

- [Underscore changelog](https://underscorejs.org/#changelog)
- [Raphael 2.3.0 release](https://github.com/DmitryBaranovskiy/raphael/releases/tag/v2.3.0)
- Raphael's published `history.md` records changes from the old 2.1.4 build.

The first preparation attempt assumed Raphael's license was named `LICENSE`;
its actual `license.txt` is now selected explicitly and rechecked against the
verified archive. Both official minified files have no trailing newline;
apply_patch's generated EOF newline is mechanically removed only after checking
that this is the sole byte difference. The final files match upstream hashes.
