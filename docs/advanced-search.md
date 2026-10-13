# Personal advanced search (v7)

The existing advanced-search dialog now enables personal tag conditions. Tag choices
carry tag GUIDs, not SQL built from names, and show ancestor labels to distinguish
nested tags. Unicode, commas, slashes and quotes in tag names are supported. Multiple
tag conditions use AND, match the exact tag (not all descendants), and remain bound
to the same GUID after a rename. Removing a tag yields no matches rather than dropping
the condition. Qt 5/6 use the shared activated(int) signal for parameter selectors.

Personal advanced searches with keywords reuse the existing online full-text service.
The locally synced personal metadata is filtered by tag, folder, date and attachment
conditions. Body-only server hits and highlights survive filtering; duplicates and
trash are excluded; the 500-result limit is applied after filtering. A successful
empty cloud result is not replaced by title matches. Keyword-only personal advanced
search also uses this route, bypassing the old non-existent searchKeyword invocation.

With no keyword, filters run entirely locally. If online search is unavailable, an
explicit message says the results only search local titles. Multiple title terms use
AND and escape apostrophes and literal LIKE wildcards. This is not a new offline
full-text engine, does not index encrypted note bodies and cannot return notes whose
metadata is absent locally. Cloud semantics and server result limits remain those
of the existing service. No new team functionality, WebDAV or AI was introduced.

The existing document/tag/database formats are unchanged. Existing saved folder,
date and attachment filters remain supported in the current UI language. New saved
tag criteria use tag-guid: tokens; slash-containing name/keyword values use additive
percent-encoded fields. Older clients do not understand those new saved-search tokens,
even though they can still read the notes. Do not treat saved-search compatibility
as note-content incompatibility. Original legacy date interval semantics are retained.

## Verification

A new --advanced-search-self-test route creates only temporary databases/settings and
uses a stub online-result provider; it never logs in or reads an existing account.
Tests cover body-only hits/highlights, post-filter limits, explicit fallback, multiple
tag intersection, trash exclusion, malformed criteria, legacy conditions, special
characters, actual Qt widgets/signals, saving/restoring and tag rename.

Local Release arm64 build with Qt 6.11.3 passed all four CTest suites. The current
build's actual-main-window Cocoa fixture passed all 21 button regression checks.
The deployed bundle passed the search self-test, a 47-Mach-O arm64/dependency audit,
and strict ad-hoc signature validation. The v6 CI finished successfully:
https://github.com/Defenceforce1/WizNotePlus/actions/runs/38379792071

The deployed bundle includes the cocoa platform plugin, not offscreen. An attempted
packaged offscreen self-test aborted before QApplication finished initialization;
this was a test invocation error, not normal startup or database access. Running
the same packaged test with QT_QPA_PLATFORM=cocoa passed. Build-tree CTest can use
offscreen because the full Qt installation is present. Do not repeat an offscreen
test against this deployed bundle.

Real account/server matching and daily UI behaviour still need user confirmation.
No existing old installed app was opened for these tests. Developer ID signing,
notarization, multi-screen/Spaces coverage and cross-app import remain unverified.
