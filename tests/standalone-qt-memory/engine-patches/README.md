# Metal final-texture ownership: isolated candidates, NOT integrated

## Clean packaged candidate

The later `release` source state uses only
`offscreen-final-texture-owner.patch` against original source; do NOT apply the
three historical diagnostic/prototype/activation patches first. The Mac texture
bridge is byte-identical to upstream, with no lifetime logging or experimental
environment gate in this candidate. The ownership scope and both-lifetime rule
below are unchanged. `release` means this clean source state, not a claim that
an installed product or public release has been accepted.

`check-source.cjs SOURCE release` checks three hashes and reverse applicability;
`engine-candidate.py` records/packages this clean state with the separate PDF
patch. The earlier four-patch provenance belongs only to the prior diagnostic
candidate. See [phase 26](../../../docs/exploration-2026-10-18-phase26.md): fresh
120-open stress, original-editor math, PDF geometry, main/package and unlocked
native window checks all ran again. No raw create/release counter is claimed
for the clean binary: those diagnostic callbacks were deliberately removed.

These files are not included in the main App build. Never apply them to the
installed SDK or an application. Source module, three-file hashes and separate
diagnostic/prototype patch hashes are fixed in `source-manifest.json`.

The diagnostic only records native texture creation, existing release calls,
QSG wrapper destruction and the compositor's last texture. It captures raw
pointer values, not Objective-C objects or content. Logs remain private.

Apply the diagnostic patch, then the owner prototype, only after preserving
separate complete unmodified/PDF-only runtimes. The PDF patch is independent;
this checker verifies only these three files, not other engine source changes.

```sh
node tests/standalone-qt-memory/engine-patches/check-source.cjs \
  /absolute/pinned/qtwebengine-source prototype
```

`original` checks original hashes and diagnostic forward applicability;
`diagnostic` checks instrumented hashes and prototype forward applicability;
`prototype` checks prototype hashes and reverse applicability. Nothing is
applied, downloaded or built by the checker.

## Narrow ownership hypothesis

The pinned engine delays release of the previous imported Metal texture via a
buffer callback, but its final texture has no such next-buffer callback. In
the logging-only standalone run, 90 creations/90 wrapper destructions/41 view
destructions left 41 final native textures unreleased; no new ownership was
introduced by that diagnostic. RSS stayed relatively flat while footprint grew.

The explicit `WIZNOTE_QT_METAL_FINAL_OWNER_PROTOTYPE=1` test path retains the
existing final +1 ownership until BOTH compositor and QSG wrapper lifetimes end.
Replaced textures keep their existing delayed release and cannot double-free.
Unbind the observer before accessing derived members; the base's later unbind
is harmless. The shared token holds no wrapper/window pointer and uses an atomic
activation flag so the two destruction threads cannot race that flag.

This path is deliberately restricted to `QQuickWidgetOffscreenWindow` in this
exact Qt revision. [Pinned QQuickWidget](https://github.com/qt/qtdeclarative/blob/95549d7db3344829d4e89c5533a7bd98dbaa1dd7/src/quickwidgets/qquickwidget.cpp)
uses render-control offscreen frames, and
[pinned Metal RHI](https://github.com/qt/qtbase/blob/5a1194b2d368e2d72c230205c464b04a2f23eccd/src/gui/rhi/qrhimetal.mm)
waits for completion at `endOffscreenFrame`. It must NOT be generalized to
onscreen/threaded Qt Quick or arbitrary render-control consumers: those may
have GPU commands in flight, with native resources not automatically retained.
Do not free a last raw pointer blindly in a compositor destructor.

## Required evidence, still not a release gate

Keep logging-only, prototype-enabled and same-binary prototype-disabled runs
separate. Require complete view/page/window/profile cleanup, accurate source,
actual Metal backend and matching parent/helper libraries. Use
`audit-metal-lifetime.cjs LOG NEW_REPORT --expect-balanced` for enabled runs,
or `--expect-final-retention` for the explicit negative controls. These prove
release calls, NOT GPU safety. Keep Metal API validation, resize/hide/switch/
close stress, ordinary product rendering/export/lifetime/full/native-window
tests, and package verification separate. Do not claim an integrated fix or
ship this experimental gate based only on a lower footprint.

## Matched-feature narrow candidate (continuing)

After the guarded positive/negative controls, the separate
`activate-final-owner-candidate.patch` removes only the experimental environment
condition. It still requires this exact offscreen-widget identity, both object
lifetimes and the pinned completion semantics; it does not change a graphics
backend or extend to ordinary Qt Quick. The installed SDK/App is untouched.
The continuing isolated build also turns WebDriver off to match both original
public/private SDK feature headers. Do not use an earlier prototype's binary
or tests as evidence that this fresh candidate has finished compilation.

The checker's `candidate` state validates all three candidate hashes and reverse
applicability of the activation patch. Keep earlier logging/prototype snapshots
and results independent. That earlier candidate's foreground acceptance waited
for an unlocked session. Phase 26 subsequently tested the clean candidate after
unlocking; never bypass the lock or change system protection to finish a run.
