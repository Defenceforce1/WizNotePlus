# build234 bounded packaged functional acceptance

The candidate is a new copy of230 with the verified standalone jQuery3.7.1/MIT
and a minimal localized empty-unread-label correction. It is not an installation,
a new account store or a Qt upgrade. Frozen230/220 and installed App are intact.

All WebEngine diagnostics use actual candidate resources, its Qt frameworks and
QtWebEngineProcess with a hidden Cocoa view and synthetic local transport only.
Loader traces positively identify the four packaged Qt framework paths and the
packaged child executable. No sandbox/protection flags are changed. Native
settings/menu/tree checks use the offscreen platform and temporary settings.

The final functional assertion matrix is:

| Diagnostic | Assertions |
| --- | ---: |
| save-state, ordinary-workflow, frontend-components |50 +18 +24|
| jquery-paste, jquery-inventory |18 +22|
| math-references, diagram-components jquery-related |95 +13|
| welcome-components jquery4 / jquery4-en, todo-welcome, todo-storage |30 +30 +34 +26|
| category-selection, pdf-settings-labels, pdf-export-menu |16 +14 +21|
| reader-opening controls / rapid |38 +22|
| mathjax-upgrade4.1.3 |29|

Total500, no final failures. A separate cycle diagnostic also completes three
edit/read round trips with a synthetic edit, exact source retention, visible
states and no persisted hide CSS. It is reported as a scenario, not invented
individual PASS counts. The fixed native launcher has another7 checks.

The initially passing493 matrix is preserved. Visual inspection found the
existing English-only empty unread label. The correction consists of two
dictionary entries and the single assignment using the existing translation
API/textContent; no query or task rules change. The Welcome provider can now
return an empty unread result. Added checks cover Chinese/English empty text,
capturing both states and restoration to populated results. Only the affected
Welcome group is rerun:120 assertions; the unaffected380 remain applicable via
unchanged exact resource/native-code checks. Do not sum historical and final
group results into a bigger acceptance count.

Two actual product PDFs are audited for four exact local destinations, physical
paper width, normal12pt prose, retained code/vector content, no out-of-page text
and source/HTML/DOM/modified-state restoration. Both rendered pages are identical
to the accepted233 source output and visually inspected. Ordinary Welcome images
match230. Todo image differences are bounded to the localized empty-unread DOM
region; other pixels match the initial234 image. New empty states are reviewed.

The script preflight duplicate-comma parse error and an audit output-name
collision occurred outside product runtime checks and are retained locally.
Exclusive output creation refused to overwrite the original PDF runtime record;
the layout audit now uses a distinct filename. These are not product PASS cases.

Main native code/constants and all other resource/framework files retain the
230 identity.48 native components and strict signatures pass. ZIP integrity,
complete extracted1082 App entries, four delivery files and extracted signatures
are checked. No user store, account data, synthetic evidence, PDF or raw log is
included in the source-only GitHub synchronization or the account-free package.

This does not accept a real database save/cloud sync, trusted system clipboard,
all encrypted/attachment combinations, every macro/plugin, whole-application
UI/quit path or long-term memory behavior. Unchanged native modules retain prior
evidence, not an invented fresh full run. No unrelated full database/archive or
security test suite is rerun. No absolute assurance about all real documents is
claimed. The released test launcher reuses the fixed sibling data directory;
it does not create/reset a per-version account store or force login preferences.
