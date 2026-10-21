# Personal-use development checkpoint: phase 165

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

Phase 165 connects the default-disabled management host to Settings and scoped
open-note retirement, and prevents concurrent management mutations from separate
hosts. The actual Settings constructor and print-margin navigation run offscreen
against an isolated database. Its new page stays disabled; opening/selecting it
does not query Keychain, enable policy or dispatch a management operation.

The actual host snapshots the personal account/server/certificate and rechecks it
before starting and accepting results. Policy persistence is read back only after
provider enrollment success. Account, server or encrypted-key revision changes
reject late results. A process-wide try-lock prevents overlapping enrollment and
forget operations, without queuing an obsolete intent or automatically retrying.
Hiding a dispatched host does not release that mutation until its worker returns.

Management start and successful completion publish only a policy digest. Matching
encrypted open notes retire their old grants; newly opted-in legacy gates become
fresh-authentication gates even with a warm save password. Plain notes and other
scopes are unchanged. DOM, certificate, backups and queued-save credentials stay.
No notification grants access. Destroyed views detach; reentrant host destruction
prevents dispatch. Providers are synthetic and document handlers use the actual
extracted source, not a normal MainWindow instance.

Final verification is **2 associated groups: 1203 PASS, 0 FAIL in 13.56s**
(83 note/host/settings, 1120 routing/quit), once after the final build. All previous
1178 assertions remain unchanged, with 25 new checks. No full-suite or native
authentication rerun. A missing application type include caused one intermediate
build failure. An initial test placed the policy write before its unrelated-scope
assertion; the existing read gate correctly refused access. The test order was
fixed without weakening the product gate. Failed evidence and the earlier 1200
passing checkpoint are retained separately, not added to the final pass count.

### Retained phase 164 evidence

Phase 164 completes three associated isolated stages: an explicit biometric
management component, session-access retirement, and the complete extracted
normal-quit coordinator. Only two affected groups were run after the final build:
**1178 PASS, 0 FAIL in 13.18s** (68 note/management/session, 1110 routing/quit).
There are 43 new checks; all previous 1135 assertions remain unchanged. This is
not a full-suite rerun, native authentication acceptance or a normal-window test.

The new management component starts disabled and has no enabled normal-client
entry. Enrollment requires one-shot consent, locally verified password and a
current account/certificate scope. Policy persistence follows provider success.
Conflicts never overwrite automatically. Forgetting local memory retains the
fresh-authentication policy, certificates, notes, backups and save credentials.
Workers do not access widgets/database objects; retired or destroyed views ignore
late results. A dispatched task remains observed to prevent same-widget overlap.
Already committed native changes cannot be undone by hiding the component.
All test providers are synthetic; real Keychain operations were not exercised.

An initial isolated test crashed when a password text-change receiver destroyed
its widget inside Qt's unfinished clear operation. Internal password clearing
now blocks that notification, while still clearing the password and consent.
The regression checks suppressed internal notification, factory reentrancy and
destruction, post-dispatch destruction and late completion. The failed run is
retained separately, not counted as acceptance. Two intermediate build errors
were corrected and retained in local evidence as well.

The normal document source subscribes to a session monitor: Qt inactivity and
public macOS workspace sleep/session-resign notifications retire opted-in access,
cancel pending output and display the password view without clearing DOM or
canceling saves. Activation alone never grants access. Public SDK subscriptions
compile; deterministic tests disable native subscription. Real sleep, session,
lock-screen and Touch ID interactions remain unaccepted.

The entire actual prepareForQuit body is extracted with only class/collaborator
substitutions. Actual UI rollback/freeze and FIFO encrypted archive writes are
tested for refusal, two save passes, simulated final sync, idempotence and late
external changes. Network, modal UI and note enumeration are collaborators; the
normal MainWindow is not instantiated. This advances the earlier callsite-only
evidence but does not claim whole-client quit acceptance.

### Retained phase 163 evidence

External tasks now retain a SHA-256 comparison of their initial or most recently
admitted converted body. A single successful file read supplies both the body
and comparison; the previous importer's UTF codec/BOM, HTML/text conversion and
empty-file behavior are retained. Read failure is not a successful blank save.
An admitted comparison is not a disk acknowledgement.

Normal quit checks external tasks before local-save preparation and again after
its final local-save pass. Changed, missing, unreadable, invalid or untracked
baseline files refuse quit. Busy or failed external saves also refuse. The check
never submits a save, rewrites either version, removes a task or clears an error.
The existing window, note and external file remain for reconciliation. Unchanged
stale cache cannot overwrite an independently newer note. This is conservative
conflict preservation, not automatic merging or a guarantee for external edits
not yet written to disk. Direct/forced termination is outside normal quit checks.

Phase 163 uses **1 incremental/associated group: 1095 PASS, 0 FAIL in 14.27s**,
one run after the build completes, with 9 new checks and all old 1086 assertions
unchanged. A new locked query reports current unresolved per-note failures from
the saver's existing set. Quit uses that query after checking idle state instead
of the sticky historical transfer-error flag. Only successful real same-note
save removes a current failure; another note's success does not reconcile it.
Historical error state and all other transfer-error consumers remain unchanged.

Tests retain two synthetic encrypted archives behind ordinary output blockers,
observe real failures, restore each exact original, and explicitly retry via
the actual watcher-save method and FIFO saver. Independent success and only one
successful retry still refuse quit; after both retry saves succeed it may pass
while historical error remains set. A new unadmitted file change still refuses.
Original conversion, caches and save credentials remain. No blanket error reset,
automatic retry, conflict merge or user-note conversion is introduced.

Actual Qt, temporary files, per-note failure tracking and archive persistence
participate; process/modal UI remain collaborators. MainWindow's existing two
callsites are unchanged, source-verified/compiled rather than runtime accepted.
Unrelated bytes and prior evidence are verified. No native-async/full-suite rerun
and no old-source negative-control claim for this additive query stage.

The final-file guard is a refusal, not a routine approval or overwrite prompt.
Normally save the external file again and verify return-save before retrying
quit; conflicting versions must be retained separately first. Explicit successful
same-note retry now reconciles current failure without deleting historical error.
Whole-application quit and real external-process/UI acceptance remain separate.
No external process, normal account, native UI or service request participates.

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
   now isolated-tested. Final pending external changes preserve both versions
   through a normal-quit refusal; automatic merging is not intended. Explicit
   external-save retry reconciliation now has actual per-note/FIFO archive tests.
   This bounded external-editor method-level phase is closed; whole-application
   quit and manual conflict reconciliation remain separate.
   Real external processes remain outside isolated verification.
3. The actual Settings management host is wired but disabled. Its scope checks,
   policy broadcast/open-note retirement and cross-host exclusion pass isolated
   tests. User-configured signing and real native enrollment/conflict/forget
   acceptance remain gated; no default enablement or password recovery is added.
4. Public session subscriptions compile; deterministic retirement and the full
   extracted save-first quit method pass. Actual OS events, lock/authentication
   interaction and normal MainWindow end-to-end quit remain unaccepted.
5. User-configured lawful signing for native Touch ID/Keychain acceptance, and an
   authoritative first-certificate-registration service contract.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The reviewed file manifest is [checkpoints/2026-10-21-phase165.json](checkpoints/2026-10-21-phase165.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
