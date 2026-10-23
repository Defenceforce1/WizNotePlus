# Public editor jQuery inventory and bounded assessment

`reader-opening FILES jquery-inventory OUTPUT` requires the exact synthetic marker.
It creates one common rich-text and one Markdown fixture using existing public
editor initialization, on/off and save snapshot APIs. It never reads bundled
editor internals, real note/account data, or the clipboard, and does not write a
normal database or source dependency.

The interceptor counts only two known standalone jQuery asset paths and blocks
all remote requests. Public global version/alias metadata is recorded before
initialization, in reading/editing, and after returning to reading. The view is
not exposed and profiles are temporary. A synthetic text change must survive
both editor and reader capture. No formula/PDF test is implied.

An optional expected-jquery.txt must be3.3.1 or3.7.1. Rich text is explicitly
expected not to expose a global jQuery; Markdown must load the requested version.
One run has22 checks. The baseline230 run and temporary single-resource3.7.1
experiment pass44 total. Only the temporary legacy3.3.1 asset URL is overlaid,
with the unmodified official full3.7.1 bytes; product and old packages are intact.

Initial compilation failure and the incorrect first rich-text-global assumption
are retained locally; only successfully rebuilt final runs count. A global's
absence is not proof that a closed bundle has no internal dependency. Likewise,
no1.11.3 request in these cases does not authorize removing it. See the detailed
scope and remaining checks in docs/editor-jquery-assessment-2026-10-23.md.
