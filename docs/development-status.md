# Personal-use development checkpoint: phase 154

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

Original-authorization loader acceptance and unfinished-extraction resumption
from phase 153 remain. A failed new-opening save now explicitly retains only the
captured pending extraction belonging to the still-current note/page/navigation.
Its old permit and worker sequence retire before it is assigned to the retained
opening. Protected notes await fresh authorization; legacy notes can reissue
immediately. The refreshed request cannot accept an old worker result.
Overlapping/duplicate stale save notifications cannot adopt a newer request.
Already consumed loads/failures, clear and unrelated navigation are not retried;
dirty DOM, cached save bytes and mandatory save callbacks are not cleared.

Phase 154 uses **2 incremental/associated groups: 1019 PASS, 0 FAIL in 10.80s**,
one final run. Routing has 916 checks (12 new); native document-async retains 103.
Original-source controls fail 3 assertions normally in the initial 912-check
corpus. The final run preserves those assertions and adds duplicate completion,
clear during save, successful replacement and dispatch-time owner destruction.
Old 904 routing assertions and extraction rules are unchanged. The retained
renderer's actual loaded mapping is verified. No whole-suite rerun is claimed;
phase-149 whole-suite results remain historical only.

Current host opening and loader methods, Qt lifetime and temporary database
execute. Save results, worker extraction, navigation and metadata UI use recording
collaborators. This is not real saver-to-host or worker-to-native-editor,
full-main-window or system-authentication acceptance.

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
   method boundary, including failed-new-opening backout. Real saver-to-host and
   worker-to-native-editor integration, and interruptions after a result has
   already been consumed remain distinct checks. Guards do not add origin
   metadata to bare loadFinished or constitute whole-client initialization acceptance.
3. Explicit biometric opt-in/enrollment/conflict/forget-local-memory management.
4. Actual system-session/lock notifications and save-first whole-application quit.
5. User-configured lawful signing for native Touch ID/Keychain acceptance, and an
   authoritative first-certificate-registration service contract.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The reviewed file manifest is [checkpoints/2026-10-21-phase154.json](checkpoints/2026-10-21-phase154.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
