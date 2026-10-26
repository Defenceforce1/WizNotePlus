# Third-party licensing audit (2026-10-13, preliminary)

WizNotePlus is GPL-3.0. Preserve LICENSE, copyright notices, and corresponding
source/build instructions when distributing a modified binary. This is an
engineering inventory, not legal clearance or a complete asset audit.

| Component | License / source of evidence |
| --- | --- |
| Qt 6 modules | LGPL-3.0 / GPL options; inspect exact module and bundled dependency notices |
| Qt WebEngine / Chromium | Qt license plus many third-party notices in QtWebEngineCore resources |
| FramelessHelper | MIT, pinned source headers/LICENSE |
| QuaZip 1.7.2 | LGPL-2.1-or-later; COPYING and static-link exception in pinned upstream commit 3108956647d02547615d4b9aca71b0c517f6dda1; see tests/standalone-quazip-upgrade/README.md |
| MiniZip | zlib, QuaZip zip/unzip source headers |
| Gumbo | Apache-2.0, COPYING |
| Crypto++ 8.9 | Boost Software License 1.0; License.txt, some historical files public domain |
| zlib | zlib license; system zlib used by native build |
| SQLite 3.54.1 | public domain, unmodified official sqlite3.c / sqlite3.h / sqlite3ext.h headers; see tests/standalone-sqlite-upgrade/README.md for source checksum and compatibility settings |
| JsonCpp 1.9.8 | MIT/public-domain options, bundled unmodified official amalgamation headers; commit 8519b8381f3c741ad1421f88237b1deda0b11412; see tests/standalone-jsoncpp-upgrade/README.md |
| MathJax 4.1.3 and TeX font 4.1.3 | Apache-2.0; unchanged reviewed engine/font bytes with local provenance in share/files/wizeditor/dependency/mathjax/inputs.json; full license in v4/LICENSE and tex-font/Apache-2.0.txt. The top-level MathJax.js is the project's compatibility adapter. Historical2.7.8 assets are not part of the active source or default build. |
| Marked 18.1.1 | MIT; exact local browser distribution and full license in share/files/wizeditor/dependency/marked; used only for explicit theorem bodies |
| CodeMirror 5.65.21 | MIT; core and reviewed addons with full license in share/files/wizeditor/dependency/codemirror/LICENSE.txt |
| Welcome jQuery UI 1.14.2 | MIT; official CDN bytes verified against published SRI; full license in share/files/welcomepage/js/jquery-ui-dist-LICENSE.txt |
| Welcome i18next 26.4.2 | MIT; official npm UMD bytes/integrity; full license in share/files/welcomepage/js/i18next-LICENSE.txt; see tests/standalone-reader-opening/welcome-i18n.md |
| Welcome jQuery 4.0.0 | MIT; official npm/CDN full build with published SRI; unchanged full license in share/files/welcomepage/js/jquery-LICENSE.txt; see tests/standalone-reader-opening/welcome-jquery4.md |
| Markdown standalone jQuery 3.7.1 | MIT; unmodified official full distribution at legacy compatibility URL share/files/wizeditor/dependency/jquery-3.3.1.js; complete jquery-LICENSE.txt in the same directory; exact input/integrity in tests/component-editor-jquery/inputs.json. Retained1.11.3 and restricted bundled editor are not replaced. |

The original web editor, templates, fonts, images, plugins and further JS
dependencies retain upstream files and notices. Their full redistribution audit
is still pending. No claim is made that all assets have been cleared. A public
release must include exact-version Qt/Crypto++/submodule source links and notices,
and audit Qt WebEngine's Chromium license resources. Do not relicense the app as
closed source merely because it was forked.
