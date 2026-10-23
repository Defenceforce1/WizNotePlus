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
| MathJax 2.7.8 | Apache-2.0; unmodified retained upstream resources at 85e47d7801b2dea6c15878ac7b3dcf62cfd659b5; full LICENSE in build/osx/licenses/MathJax-2.7.8-Apache-2.0.txt |
| Marked 17.0.5 | MIT; exact local browser distribution and full license in share/files/wizeditor/dependency/marked; used only for explicit theorem bodies |

The original web editor, templates, fonts, images, plugins and further JS
dependencies retain upstream files and notices. Their full redistribution audit
is still pending. No claim is made that all assets have been cleared. A public
release must include exact-version Qt/Crypto++/submodule source links and notices,
and audit Qt WebEngine's Chromium license resources. Do not relicense the app as
closed source merely because it was forked.
