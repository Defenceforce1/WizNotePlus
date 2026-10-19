# Native titlebar isolation probe

Developer-only Cocoa geometry diagnostic. It opens an explicitly synthetic Qt window and writes a **new** private JSON result, never a note or PDF. No Wiz account, editor, database, WebEngine, normal client identity or profile is linked. Build only in a separate developer directory; do not package/install it or run while the screen is locked or physical input is in use.

CMake extracts the narrow header implementation directly from `src/mac/WizMacHelper.mm` and records that file's SHA256. `current` tests that implementation. `system` never calls any Wiz header function and is the stock-Qt/AppKit negative control. `candidate` and `geometry-only` retain a withdrawn observer experiment in `candidate-observer.inc`, whose separate SHA256 is recorded; that file is never linked into the main client. Three other modes examine public AppKit layout APIs **only in this disposable probe**, not in the main client. None is a production recommendation or accepted replacement on its own.

Use the existing Qt SDK (`CMAKE_PREFIX_PATH`), optionally `WIZ_HEADER_SANITIZE=ON`, and a private new result path:

```sh
native-header-probe --mode current --out /absolute/private/new-result.json --seconds 30
```

The JSON distinguishes actual window state and legacy NSButton-descendant hit testing. A different native hosting view is evidence to investigate, not permission to silently accept a failed hit test. Interactive pointer checks must use only this running synthetic window. Never call an app-discovery method that might accidentally launch a normal client after a diagnostic has exited.

On the observed macOS 27 host, attaching screen inspection changes the public `hasActiveWindowSharingSession` value from false to true. AppKit replaces the visible traffic-light area with its purple sharing control, and the old NSButton-descendant hit predicate becomes false even in `system` mode. Never disable sharing/security or move the system overlay to make that predicate pass. Separate captured font interaction from unshared native-control acceptance; keep failures and environment metadata.
