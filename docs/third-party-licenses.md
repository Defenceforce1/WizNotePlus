# Third-party licensing audit (2026-10-13, preliminary)

WizNotePlus is GPL-3.0. Preserve LICENSE, copyright notices, and corresponding
source/build instructions when distributing a modified binary. This is an
engineering inventory, not legal clearance or a complete asset audit.

| Component | License / source of evidence |
| --- | --- |
| Qt 6 modules | LGPL-3.0 / GPL options; inspect exact module and bundled dependency notices |
| Qt WebEngine / Chromium | Qt license plus many third-party notices in QtWebEngineCore resources |
| FramelessHelper | MIT, pinned source headers/LICENSE |
| QuaZip | LGPL-2.1-or-later; COPYING and static-link exception in pinned source |
| MiniZip | zlib, QuaZip zip/unzip source headers |
| Gumbo | Apache-2.0, COPYING |
| Crypto++ 8.9 | Boost Software License 1.0; License.txt, some historical files public domain |
| zlib | zlib license; system zlib used by native build |
| SQLite | public domain, sqlite3.c header |
| JsonCpp | MIT/public-domain options, bundled header |
| MathJax 2 | Apache-2.0, MathJax.js header |

The original web editor, templates, fonts, images, plugins and further JS
dependencies retain upstream files and notices. Their full redistribution audit
is still pending. No claim is made that all assets have been cleared. A public
release must include exact-version Qt/Crypto++/submodule source links and notices,
and audit Qt WebEngine's Chromium license resources. Do not relicense the app as
closed source merely because it was forked.
