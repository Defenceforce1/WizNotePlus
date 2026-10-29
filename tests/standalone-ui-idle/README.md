# UI idle-work regression

Build with the current Qt SDK, then run `ui-idle timer candidate`, `ui-idle combo candidate`
and `ui-idle shortcut candidate` under the normal Cocoa event loop. `WIZ_SOURCE_ROOT` can
point at a frozen pre-change copy for the bounded reference qualification.

The test extracts the actual own toolbar methods and shortcut scheduling/save preamble.
Rendering records use a QComboBox collaborator; toolbar reset and favorite persistence are
recording collaborators, not the full editor/database. The old shortcut reference uses the
same 200ms per-request scheduling semantics with a typed QObject-context connection.
Full native source/database checks are separately recorded in phase336. No account,
clipboard, note store, web connection, forced collection or heap snapshot is used here.
