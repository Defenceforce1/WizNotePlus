# Isolated macOS foreground acceptance

Run only with explicit user foreground authorization, an unlocked desktop, and no other test of the
same binary. This opens a synthetic parent window, the **actual unmodified recovery prompt**, and
Qt's native save panels. It links Widgets/AppKit only: no recovery executor, account, database,
WebEngine, network transport or normal application identity. Confirming a synthetic prompt records
a decision only; it cannot recover or modify a document. Native save-panel acceptance selects a
path in a QTemporaryDir; no export is written. Cancel is checked against a temporary sentinel.
Acceptance is manual and runs only with the additional `--manual-save` option; the default run
tests cancellation only. With that option, the operator has 60 seconds to click Save in the
visible native panel. Timeout is a failure, not an acceptance, and the panel is canceled.

Build independently with the existing offline Qt SDK. Run with the exact opt-in argument and a new
absolute report path whose parent exists:

```
native-ui-probe --foreground-synthetic-test /absolute/private/new-report.json
```

The probe verifies native key-window focus before dispatching AppKit key events to its **own**
application. It does not inject global input or change keyboard-navigation/system accessibility
preferences. Never invoke `NSSavePanel ok:` or assume `defaultButtonCell` exists: this macOS
service-backed panel throws "not implemented" for the former and exposes no cell for the latter.
The failed first run and diagnostic crash are retained, not product failures or accepted runs.
Native cancellation uses public `cancel:`; acceptance requires a real UI click. It never captures the screen
or launches another app. Results retain failures and the observed system keyboard-navigation mode.
VoiceOver is explicitly outside this personal-use acceptance; generic Qt accessibility metadata is
not removed or counted as VoiceOver acceptance. Main-client menu/export integration is tested by
the separate actual `--mac-window-controls-self-test` route, not by this Widgets-only probe.
