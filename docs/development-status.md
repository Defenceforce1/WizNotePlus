# Personal-use development checkpoint: phase 161

Development version: **14.4.25**. This is a source checkpoint, not an installer,
release, installation, or completed system-authentication acceptance.

## Improvements since the previous GitHub checkpoint

- Markdown display-math environments and common LaTeX-style theorem containers,
  retaining source text and cross-references.
- PDF equation numbering and internal destinations; separate electronic-reading
  width and A4 export choices, with no remembered submenu selection.
- Export transaction/rollback work and an experimental persistent-recovery
  implementation. Persistent Markdown recovery remains **OFF** by default.
- Local compatible-certificate generation/backup components and biometric
  components. Normal Touch ID enrollment remains **disabled**; first server-side
  certificate registration and native authentication are not accepted as complete.
- Simplified-Chinese resource selection, Settings naming, title-only group sync
  default, and scoped account-page cache configuration.
- Revocable document-read/output permissions across the implemented export,
  share, clipboard-processing, batch, inspector and editor-command paths.
- Guarded queued paste/return, automatic-title replies and password/editor focus.
- Synchronous toolbar formatting/insertion checks, preserving original command
  arguments, encoding and modification markers while refusing stale continuations.
- Image-file chooser returns and plain-text paste now retain their original
  document-read permit across modal/clipboard callbacks. A newer authorization
  cannot revive an obsolete request. Already started file copies are not undone.
- Link-window queries, presentation and acceptance retain the original read
  permit and a separate request epoch. Hide/revoke closes an open link dialog;
  stale continuations cannot consume a newer request. Original URL normalization,
  application modality and one-shot command behavior are preserved.

- Attachment/image paste, attachment insertion and thumbnail continuations now
  retain the original read permit. An obsolete paste is consumed instead of
  falling through to browser default paste. Completed copies/attachments remain
  in the original note; subsequent insertions stop. Image-conversion failure now
  explicitly returns false rather than falling off a non-void function.

- Search-window signals and selected-text publication now retain their original
  read permit/request epoch. Hide/revoke disconnects and hides transient search
  UI without clearing fields or saved state. A guarded presentation overload
  checks each display step; the original overload and find/replace behavior stay.
  Already dispatched searches/replacements are not retroactively undone.

- Screenshot orchestration binds an originating document/request and a read
  permit, coalesces duplicate clicks, and refuses obsolete timers/results.
  Capture helpers are detached and deleted later, not inside their signal stack.
  Restore targets only the originally hidden visible window; shutdown does not
  reopen it. Resource lookup, PNG output and insertion retain the bound permit.
  Intentional hiding must not bypass opt-in authorization. Legacy notes bind a
  permit synchronously after that hide, never when a delayed result arrives.
  The protected opt-in hide path currently stops and restores the password view;
  its future capture/reauthorization UX is not claimed complete.

- Initial HTML caches now record their owning note/knowledge base, including
  the actual editor-save producer. Authorized access to a different note cannot
  return old retained cache bytes. Initial preparation retains its original read
  permit across publication/script/CSS boundaries and atomically replaces the
  temporary decorated file only while authorized. Failure prevents loading.
- Initial load dispatch retains the originating host access, page and loader
  sequence across its own callback invalidation and simulated loadStarted.
  Reentrant retirement returns an expiring ticket. Clear/open/reload stop after
  stale retirement or owner destruction; save work and retained DOM are not
  canceled. Already dispatched navigation is not retroactively retracted.

- Asynchronous editor setup now retains its original read permit, weak page,
  mode and separate attempt epoch through metadata/style preparation, dispatch,
  readiness and initial find/plugin notification. New authorization or a newer
  attempt cannot revive an old result. Successful mode changes retire old setup;
  no-op or failed-save mode changes keep the valid existing attempt alive.
- The readiness helper captures weak owner/page before invoking predicates and
  rechecks liveness after them. Timeout, single in-flight query and one-shot
  delivery remain; renderer destruction still reports failure when permitted.
  Already dispatched JavaScript is not retroactively canceled.

## Latest local verification

External launch requests now bind the launcher's original saver lifetime as well
as the originating document read permit. Closed admission refuses before saving;
closure or replacement during save reply, destination preparation or rendered
HTML reply prevents new output, renderer queries and launch. Existing snapshots,
queued saves and watched tasks are not canceled by this read-side refusal.

The launch method rejects closed admission, missing cache/note, unsupported
format and paths already owned by a task or watch. It registers the origin/watch
before process dispatch so an immediate editor write has an owner. Watch failure
prevents dispatch; launch failure removes only that task/watch and retains cache
bytes and unrelated sessions. The original program, command splitting, quoted
Unicode/spaced paths and working directory remain. An already dispatched process
is not retroactively terminated by closure or owner destruction.

Phase 161 uses **1 incremental/associated group: 1065 PASS, 0 FAIL in 12.85s**,
one final run, with 25 new checks and all old 1040 assertions unchanged. The
original-source 1041-check control fails launch-failure rollback normally; that
probe remains unchanged. Only request and launch method bodies change. Old
extraction rules, unrelated product bytes and encoding are preserved. Compiled
bodies differ only in named class/process/modal collaborators. One fixture-only
missing-origin variable compilation error was corrected before the final run.

Actual Qt lifetimes, temporary files and QFileSystemWatcher registrations
participate. Process startup and modal/bubble UI are recording collaborators;
shutdown and destructive reentry are injected, not whole-application quit. The
associated group also retains the actual watcher/importer/saver chain. No real
external process, account, native UI or network service is used. Engine mapping
verification is not native editor acceptance. No native-async/full-suite rerun.

Launch/request/registration method-level work is complete for this scope. Final
unadmitted file changes and conflict-preserving quit coordination remain; drain
still guarantees queued saves, not external edits not yet written. Do not flush
unchanged stale cache files over newer notes. Actual MainWindow saved-handler,
real process/UI and whole-application quit remain separate acceptance boundaries.

These are isolated local tests, not GitHub CI results or complete native UI /
system-authentication acceptance. Some page, diagnostic, clipboard-operation and
transport collaborators are synthetic. Unchanged/unrelated groups are not rerun
automatically; wider regression requires an affected boundary, failure or other
specific reason. Historical results are not silently counted as current acceptance.

## Remaining bounded work

1. Native screenshot/overlay and opt-in capture/reauthorization integration;
   search/selection, clipboard/paste/drop, link-window/editor and file-chooser
   integration remain distinct from isolated checks of implemented method bodies.
2. Isolated main-window/output integration and external-editor return/save
   behavior. Loader extraction ownership/resumption is implemented at the tested
   method boundary, including failed-new-opening backout. Actual snapshot
   producers through real saver/frontier to text output are now isolated-tested;
   MainWindow saved-handler and worker-to-native-editor integration remain distinct.
   Submitted-setup
   readiness and locked-completion recovery are implemented with current-renderer
   proof. A Bool still has no intrinsic event provenance; it cannot authorize setup
   on its own. The bounded initialization method-level recovery stages are now
   implemented, including pre-submission metadata interruption. Native handwriting
   return, MainWindow host saved-handler/output UI and full initialization
   integration remain separate from this isolated chain. Registered external
   watcher return-save and queued-save drain now have actual local watcher/DB
   checks. Launch registration/rollback and closed-lifetime request admission are
   now isolated-tested. Next is identifying final pending external file changes
   and preserving conflicts during save-first quit, without unconditional stale
   cache writeback. Real external processes remain outside isolated verification.
3. Explicit biometric opt-in/enrollment/conflict/forget-local-memory management.
4. Actual system-session/lock notifications and save-first whole-application quit.
5. User-configured lawful signing for native Touch ID/Keychain acceptance, and an
   authoritative first-certificate-registration service contract.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The reviewed file manifest is [checkpoints/2026-10-21-phase161.json](checkpoints/2026-10-21-phase161.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
