# Search entry and light appearance (v8)

The top search field searches note content, not tag metadata. In v7 the generic
search field emitted an advanced-search request which the actual main window did
not receive. Dialog/backend tests alone missed that unusable entry point.

Click the magnifier inside the search field, then choose **Advanced search**. The
existing menu now opens the built-in dialog; there is no separate toolbar button,
as requested by the user. Choose Personal notes, select Tag, choose an existing tag,
click Add, then OK. Start with the keyword field empty to test local tag filtering.
Keyword-plus-tag searches still reuse the existing online full-text service with
the documented offline title-only fallback. Typing a tag name alone in the top
field is not newly interpreted as a tag query. The other existing magnifier menu
entry, Add custom search, now also reaches its built-in category action.

The magnifier and clear-icon event targets now use the same widget-local logical
rectangles as painting, including Retina scaling, instead of mixing the widget's
parent coordinates/raw pixmap size with local pointer positions.

## Appearance scope

The bundled default skin has fixed light backgrounds and dark icons. Combining it
with the system's dark palette caused inconsistent title/search fields, including
white text on a white search field. The app now explicitly uses a coherent light
palette, and title/search fields specify both text and background colors. Setting
a title no longer replaces that field's background style. Focus, selection,
placeholder and disabled-text colors remain readable.

On macOS with Qt 6.8 or later, the app requests the Light color scheme for its own
native windows/menus. This is an app-local preference, not a change to macOS's
global appearance setting. See the official
[Qt colorScheme documentation](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop).
This is a readability repair for the existing light skin, not a complete dark
theme or a redesigned interface. Window delegate/control ownership, note/database
formats and online-account semantics are unchanged.

## Regression scope

The isolated actual-main-window Cocoa fixture starts with a simulated dark app
palette/scheme, loads generated temporary notes into the real editor and checks
the fixed light fields and native Aqua appearance. Its synthetic Qt pointer events
click the magnifier, then the menu item, select/add a GUID-backed tag and verify
that the real result list includes the tagged note and excludes an unrelated note.
It reopens/cancels the dialog and retains all previous native window-button checks.
This covers the entry missing from v7, not only a standalone dialog or emitted
signal. These synthetic events are not a substitute for the user's physical mouse
clicks or their real account/server results.

Optional fixture screenshots contain only generated notes. QWidget grabs show
client content, not the native title bar; native appearance is checked separately.
The fixture uses temporary settings/databases, disables sync and clipping, blocks
remote WebEngine requests and routes Qt networking to a non-listening local proxy.
Neither the installed old app nor an existing note database is used.

Full dark-theme adaptation, all skins/OS releases, system-appearance changes during
an editing session, multi-screen/Spaces behaviour and real cloud matching remain
outside this sample-level verification. Use the isolated launcher and a dedicated
test account; data-directory isolation does not prevent that account's cloud sync.

## Verified result and retained failures

The final local Release arm64 build using Qt 6.11.3 passed all four CTest suites.
The deployed Cocoa fixture passed all 26 checks, including the actual magnifier
menu-to-tag-results flow, light fields/Aqua appearance and the previous native
window controls. The packaged advanced-search self-test, a 47-Mach-O arm64/runtime
dependency audit and strict ad-hoc signature verification also passed. The shipped
bundle includes cocoa, not offscreen; packaged tests must use cocoa.

Early fixture failures were retained rather than counted as successes. Triggering
the real menu action in the standalone search test initially crashed because its
local usage logger requires a database manager, which that fixture created only
after the widget checks. The temporary manager is now opened before the action;
normal app initialization and logging code were not changed by this correction.
One earlier full-window run failed its red-close check; an unchanged-app rerun
passed, and the exact cause was not established. The fixture now waits for modal
and popup teardown and return of native main-window focus before the next close
action, with richer failure diagnostics. Native delegate/button implementation
was not changed. This is not a claim that every real-world focus/Spaces transition
has been fixed or validated. Failed local logs are preserved and not published.
