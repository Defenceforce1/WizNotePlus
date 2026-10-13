# Inline personal search (v9)

The top search field now combines content keywords with explicit tag, folder and
attachment filters. This replaces the common multi-step tag-selection workflow,
not the existing search engine. The v8 light-appearance and native-window fixes
are retained; no note/database format, sync protocol or account semantics change.

## Everyday interaction

- Typing searches the cached personal tag labels locally and offers matching
  tags below the field. Nested tags show their ancestor path. Typing alone does
  not submit a note search, log in or issue a remote request.
- Return without an explicitly selected suggestion still submits the typed text
  as a content keyword. A tag name is not silently interpreted as a tag filter.
- Choosing a suggestion creates a GUID-backed tag condition and consumes that
  typed tag label. Type a content keyword afterwards to search within those tags.
- The magnifier opens a compact, non-modal personal-filter panel: tag checkboxes,
  a folder selector and an attachment checkbox. Each selection submits the
  combined query; multiple tags must all match, and folder selection includes
  that folder and its descendants.
- Active conditions appear below the field. Click a condition's cross to remove
  only it, or Clear to remove all conditions while keeping the keyword. With more
  than three conditions, two chips and a count are shown; the count reopens the
  panel and its tooltip lists every condition.
- More conditions (dates, etc.) opens the original advanced-search dialog with
  the current query prefilled. Date and other legacy criteria remain active when
  returned to the field or when editing the keyword.
- Save current search opens the original named-search dialog with the current
  conditions/keyword prefilled and uses the existing local saved-search storage.
  Sidebar saved searches restore the visible field/chips when they are personal.

The UI choices are personal-only; no new team feature is added. Existing legacy
all-notes/team saved-query routes are retained, not translated into personal tags.
Resetting category/navigation search state clears both the field and conditions
without automatically sending a new query. Deleted selected tags/folders remain
restrictive rather than silently broadening the user's search.

## Backend and compatibility

Empty-keyword filtering uses locally synced metadata. With keywords, the existing
online full-text results are intersected with local metadata conditions, preserving
body hits/highlights. The explicit offline fallback searches local titles only;
this is not a newly implemented offline full-text engine. Results absent from the
local metadata cannot pass the filter. See [advanced-search.md](advanced-search.md).

Tags retain v7's `tag-guid:` tokens. A new percent-encoded
`folder-path-encoded:` saved-search token stores an arbitrary-depth full personal
folder path, including Unicode, commas and literal SQL LIKE wildcards. It is
validated as a slash-delimited personal path and safely escaped before querying.
Attachment and legacy date tokens keep their existing format and semantics.
Slash-containing keywords use the existing additive encoded-keyword field.

Older clients do not understand the new tag/folder saved-search tokens. This is
saved-search compatibility, not a change to note content, attachments, encryption
or the database schema. Legacy saved folder/date/attachment criteria still parse
in the current UI language; language-neutral legacy-token migration is not added.

## Verification and limits

The local Qt 6.11.3 Release arm64 build passed all four CTest suites. Expanded
temporary-fixture tests cover combined-query SQL, GUID binding, duplicate tags,
deep/escaped folder paths, attachment and date retention, dialog round trips,
keyword-preserving clear/reset and explicit versus implicit tag selection.

The actual-main-window Cocoa fixture passed 37 checks in the build tree. Generated
temporary notes verify the panel, tag AND folder intersection, attachment results,
folded conditions, chip cancellation, ordinary-list restoration, local IME-driven
suggestions, an explicit suggestion click, saved-query prefill/persistence, the
legacy advanced dialog and all previous native window-button transitions.
It blocks remote requests, bypasses login/sync/clipping and uses no existing store.
Qt synthetic pointer events/native button actions are not physical mouse testing
or proof of real account/server matching. Screenshots contain generated notes only.

One initial fixture run tried to click an attachment chip that was correctly
folded behind the condition count; it failed and its log was retained. The test
now checks the overflow panel, removes a visible tag to reveal the remaining chips,
then verifies attachment removal. The failure was not counted as a passing run.

The deployed Cocoa fixture also passed all 37 checks; the packaged advanced-search
self-test passed. The bundle audit found 47 Mach-O files with no arm64/dependency
failures, and strict ad-hoc signature verification passed. These are local runtime
checks, not Developer ID signing or Apple notarization.
The bundle contains the Cocoa platform plugin, not offscreen; packaged GUI tests
must use `QT_QPA_PLATFORM=cocoa`. Real-account queries, long-tag-list ergonomics,
Qt 5, multi-screen/Spaces behaviour and future macOS releases remain unverified.
Use a dedicated test account: local store isolation does not prevent cloud sync.
No installed old app is opened by the fixtures or the supplied isolated launcher.
