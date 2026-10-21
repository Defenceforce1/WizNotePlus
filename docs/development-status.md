# Personal-use development checkpoint: HTML export resource URLs

## Phase 178: special destination names in HTML exports

The retained HTML resource-URL defect was reproduced without accounts or real
notes. Eight destination basenames in both archive and rendered modes were loaded
in an actual isolated local WebEngine page. Before the fix, **12 of 16** failed:
ampersands, quotes, fragment/query syntax and literal percent escapes did not
resolve to the on-disk resource directory. No network request was made.

Both the database HTML exporter and rendered-HTML transaction now use one URL
encoder for the existing HTML-escaped on-disk basename. The legacy directory name,
merge behavior, BOM policy and body-last transaction/rollback are unchanged.
Spaces, Unicode and URL metacharacters are encoded as a path segment; a literal
`%20` remains literal on disk rather than becoming a space. Existing exports are
not rewritten. Markdown source rewriting and recovery defaults are unchanged.

After the fix, the actual browser matrix passed **16/0**, checking decoded file
paths, real PNG dimensions and stylesheet application in both modes. Affected
filesystem transactions passed **50/0** with the experimental Markdown gate OFF
and **50/0** with it compiled ON; HTML still does not create persistent recovery
state. Production temporary-database routing **1176/0** passed. The native-transfer
test's historical byte assertion correctly failed at the intentional URL change;
its expected encoding was updated while preserving original archive/image checks,
then the affected group passed **89/0**. Total distinct final checks: **1381/0**.
This is local HTML destination-name acceptance,
not every archive-internal asset spelling or complete real-account acceptance.

No foreground window, screen capture, system authentication, service access,
installation or packaging. Frozen candidates and signed phase177 test App are
unchanged. Native physical-session acceptance and the authoritative compatible
first-certificate registration contract remain external gates.

## Phase 177: native cancellation, first protected load and multi-window acceptance

Two concrete bugs were fixed. Enabling the biometric policy while an encrypted
note was already open now prepares its locked password-page controls immediately;
no reopen, credential read or body grant is implied. The native read now evaluates
the existing biometric constraint asynchronously and uses the authenticated context
for a noninteractive exact-item read. Cancellation releases the worker promptly;
late system replies own completion state only. No password fallback, stored ACL,
enrollment, signing requirement or app-inactive body revocation was weakened.

Affected regressions: document routing **1176**, native keychain **36**, unlock
widget **99**: **1311/0**, 16.41 seconds. Actual signed native authentication passed
**10/0** with a real fingerprint and exact synthetic-item removal. Native cancel
and worker drainage passed in main-window multi-view runs, but those complete runs
did not pass: the app later became inactive during independent-window authorization.
Cocoa confirmed inactive and another frontmost process; the cause is unresolved,
not attributed to user interaction or treated as a product-wide success.

A separate coordination fixture also initially stopped at that boundary
(**22 checks, 1 failure** including cleanup). After the user explicitly kept the
test app foreground, the failure reproduced without app inactivity. A local stack
trace identified first-time Qt Quick-widget binding recreating native ancestors,
hiding the protected view and correctly revoking its fresh grant. Premature test
JavaScript made this happen early, but removing that polling still reproduced
the actual first-load problem. The constructor now initializes only its new blank
page, before it is shown or can hold a note grant. No existing DOM is replaced and
hide/session revocation remains intact. Temporary stack instrumentation is removed.

The corrected workflow then saved the independent note, but the fixture's nested
timer wait deferred WA_DeleteOnClose delivery. It now flushes only that accepted
close's own posted deletion; a refused close is never forcibly deleted. Final
production three-window acceptance passed **33/0**, including separate encrypted
grants, same-scope retirement, plaintext unaffected, both latest dirty DOMs retained,
independent close saved an encrypted archive, no main-view reload/reauthorization,
and normal main quit saved the latest body before eleven-stage cleanup.

Failed tests exit their isolated loop directly so a refused save-first close cannot
strand owned credentials. One older failed fixture had stalled on close; its exact
synthetic item was deleted by a separately signed scoped helper, Missing confirmed,
then only that test PID was terminated. No secret read or broad keychain deletion.
All other owned items were removed by their fixtures. Failed runs remain evidence.

Only new local signed copies and temporary synthetic databases were used. No
installed App, real notes, frozen candidates, physical lock/sleep state or recovery
default was changed. This synthetic three-window scenario is accepted, not every
possible real-data/multi-window combination. Physical-session acceptance and the
authoritative first-certificate service contract remain open. Automation stays
paused; this phase does not claim completion of the full app.

Development version: **14.4.25**. A new personal-use candidate has been built
locally with explicit consent. It is ad-hoc signed, unnotarized, not automatically installed and
not publicly distributed; system-authentication acceptance remains incomplete.

## Main-window native authentication and conditional management

The independently signed main-window workflow exposed a real integration bug:
the system Touch ID panel temporarily deactivated Cocoa, and the host canceled
the pending authentication. Two initial attempts failed and cleaned their owned
synthetic items. The fix preserves an explicit **native pending request only on
an already-locked password page**, not a body grant. The default native widget
delivers success only after the app is active again, with a 15-second bounded
delivery wait. Hard session events, hide/minimize, navigation and context changes
still revoke; manual/injected providers do not get this native-only exception.

After the fix, actual system Touch ID successfully opened the protected renderer
through the production main window and default password-page widget. Enrollment,
duplicate conflict, re-lock, exact deletion, missing-item and password fallback
also passed. That run passed **25 of 26 checks**, not a complete success: its last
synthetic notification lacked the intervening resumed event and was correctly
coalesced. The fixture was corrected; only the no-fingerprint save/quit continuation
was rerun, passing **27/0**, then **29/0** with conditional-entry checks added.
These are separate evidence, not a claim that all 29 used native authentication.
Two workspace notifications are posted only inside the synthetic process, with a
modeled resume between them; the actual Mac session/sleep state is never changed.

Normal source management is now available only with valid self-signing/keychain
prerequisites and an existing compatible certificate. Signed-without-certificate
and unsigned cases remain disabled. The settings tab is simply **Touch ID**.
Presentation performs no authentication/credential reads or policy writes.
Explicit checkbox/button action, a correct certificate password and successful
native enrollment are still required to opt in. No automatic policy enablement,
credential remembering, real-note conversion or password-recovery backdoor.

Only affected regressions ran: unlock widget **99**, document routing **1174**,
note access/management **83**, settings **29**; **1385/0**, 17.67 seconds. The final
synthetic Cocoa main-window continuation **29/0** verifies conditionally available
management, native item enrollment/conflict/deletion, manual fallback, dirty DOM
preserved while locked and latest content saved before normal eleven-stage quit.
The earlier actual fingerprint success remains separately recorded. All owned
synthetic items are confirmed absent. Initial fixture build errors and failed
runs are retained; no unrelated full suite or PDF repetition.

Only new signed test copies were prepared. Forty-six previously audited bundled
components and other inherited bytes/links are unchanged; the current main's
arm64/code/dependencies, strict recursive signature and actual bundled-Qt mapping
are checked incrementally. Existing App, frozen candidates and the previous test
App are untouched. No install/binary/profile/identity/raw-log upload. Broader real
account/multiwindow/physical lock/cancel-panel acceptance and the authoritative
first-certificate service contract remain distinct. Recovery OFF, automation PAUSED.

## Earlier focused system/widget test (phase 175)

With explicit user permission, a separate self-contained Apple Development-signed
main-client test App was prepared locally with the matching development profile.
It uses the current incremental executable/resources and the frozen candidate's
pinned frameworks. Only the new copy is signed/renamed. Minimum main entitlements,
the existing engine-helper rights and system protections are retained. All 47
arm64 components pass dependency and strict recursive-signature checks; main and
pinned-engine code sections match their intended inputs.

The opt-in interactive route runs before account bootstrap. Its uniquely named
executable refuses normal startup even with no arguments or malformed options.
It creates only a random in-memory certificate/password and unique synthetic
scope. The actual production native backend and default asynchronous unlock
widget use the system data-protection keychain: create-only enrollment, duplicate
refusal, native authentication, one-shot credential consumption/context checks,
exact owned-item deletion and absence confirmation pass **10 checks, 0 failures**.
The user was notified before the fingerprint request. The process exits normally;
actual mappings confirm Qt comes from this signed App without SDK overrides.
The related unsigned/refusal self-test passes **36/0**; no unrelated suite ran.

Three initial launch prechecks aborted because the replacement executable still
contained the build SDK rpath, causing a Team-ID mismatch for QtXml. The user's
crash report identifies that same isolated process. Only the new copy's rpath was
corrected to bundled frameworks and it was re-signed; the final prerequisite and
interactive runs pass, and no-argument/malformed-option refusal is verified.
Initial failures remain in local evidence; no signature enforcement was disabled.

The test App/profile/Team details, raw logs and synthetic material remain local.
No existing App, frozen candidate, real notes or normal account was modified or
started. No binary upload, install or new DMG. This accepts the actual signed
main-binary native-backend/widget chain, **not** complete main-window note unlock,
cancel/lock-screen/multiwindow or real-account acceptance. Normal enrollment stays
disabled; the first compatible certificate-registration service contract is still
external. Recovery stays OFF and automation PAUSED.

## Latest source-only UI adjustment

The PDF submenu now uses the parallel titles **Reading Layout…** and **A4 Layout…**
(Simplified Chinese: 阅读版式… and A4 版式…). Visible hover descriptions distinguish
preserving the reading viewport width from fixed A4 paper. The settings tab is
now **PDF Export**, with a wrapping explanation that both modes use the existing
margin settings. Export mode values, action names, dispatch, page layout logic,
default margins and the no-remembered-choice rule are unchanged. English strings
and the actual Simplified Chinese catalog are covered.

Only two directly affected offscreen Qt-widget groups ran: submenu **21/0** and
settings/translations **29/0**, 1.18s total. The real temporary settings window,
compiled translation catalog, original margin-page navigation and stored values
participate. The PDF settings screenshot was visually checked for complete text
and unobstructed controls. No PDF rendering or unrelated full suite was repeated.
This source-only change is **not** in the frozen 14.4.25 candidate delivered below;
no installer was rebuilt or replaced. The user reports basic trial use is normal.

## Development signing setup completed

After explicit authorization for Apple automatic provisioning, an independent
local Xcode helper project was created for the existing application identifier.
Xcode's automatic-signing build succeeded with the verified Apple Development
identity and obtained/installed one matching macOS development profile. The helper
has a valid strict recursive signature, the expected team/application identifiers
and a matching keychain access group. The embedded profile is unexpired, targets
macOS, permits those identifiers/groups and contains the actual signing certificate.

The helper project, team configuration, profile and raw signing logs stay local;
identity names, Team ID, profile contents/device lists and private keys are not
uploaded. No private-key bytes were read/exported by the agent; Xcode used the
existing signing identity as authorized. The helper was not run. The previous
profile-location gate is resolved for this setup; server-side registration details
are not independently inferred from build success.

The installed/main candidate remains untouched and ad-hoc signed. No normal app,
real note, biometric prompt or data-protection keychain credential was opened.
This was signing/provisioning setup, not native Touch ID or complete-client
acceptance. The separately scoped main-binary native-backend/widget test is now
recorded above; broader main-window acceptance remains distinct. Normal biometric
management remains disabled; the authoritative first-certificate registration
service contract is a separate unresolved gate. No main app was repackaged or installed.

## Local candidate delivery

The final bundled application passes three affected synthetic workflows:
actual Cocoa MainWindow/new-note/save/quit **16 checks**, editor runtime **37**,
and actual temporary native-note transfer **89**, all with zero failures. Actual
process mappings/load records confirm bundled Qt frameworks without SDK overrides.
All 47 Mach-O components pass arm64/dependency and strict recursive-signature
checks. Main and pinned-engine code sections match the accepted development
inputs; the original icon and helper signing permissions are unchanged.

The DMG is 158,022,788 bytes and passes image verification. A read-only mount
matches all 1,626 App files/73 internal links and all five auxiliary payloads;
the mounted copy passes the same limited 16-check workflow, recorded separately,
and is normally unmounted. The local source snapshot has 2,741 entries, with 140
explicit overlays and 2,740 source-record entries verified. It is not an exact
remote-tree archive. No normal account, real notes, services, authentication,
desktop capture, unrelated full suite, install or binary upload was used.
See [candidate notes](release-14.4.25-candidate.md) for scope and installation advice.

## Latest focused fix

Notes opened within one second of creation used to force the web editor on while
the host still believed it was reading. A subsequent switch to reading could then
be skipped. The host now chooses the initial mode once, under the existing lock
and permission checks, and the web view follows it. New-note title bookkeeping
remains separate; existing notes and future timestamps retain the reader setting.

The explicitly authorized synthetic Cocoa workflow checks initial host/page
agreement, actual bare-math rendering after a single reader transition, exact
Markdown retention on edit/read/edit, production save, existing/future timestamp
behavior and latest dirty content saved before normal eleven-stage quit cleanup.
Its final run passes **16 checks, 0 failures**. The one affected document-routing
group passes **1174 checks, 0 failures** in 12.31s; its stubbed host initializer is
not evidence for the new mode fix, which is covered by the actual foreground run.
No unrelated full suite or PDF export was repeated. Failed driver attempts and
the pre-fix host-reader/page-editor reproduction remain recorded locally.

No normal account, real notes, desktop capture, authentication, installation or
packaging was used. Remaining signing/service-contract and broader real-client
acceptance gates below are unchanged; this does not complete system acceptance.

## Improvements since the previous GitHub checkpoint

- Personal-note permanent decryption now requires a fresh certificate password
  and explicit confirmation after the save frontier. It preserves the complete
  encrypted archive, stages the original plaintext ZIP and rolls metadata/file
  changes back on ordinary failure. Automatic rollback failure is reported with
  the retained backup path; this is not crash/concurrent-writer atomicity.
- Opted-in protected-note screenshot capture retains pixels in memory across
  intentional hiding. Insertion requires a new authorization on the original
  note after capture; navigation, certificate/page change, policy retirement,
  hard session/sleep boundaries and shutdown discard pending pixels.
- Markdown display-math environments and common LaTeX-style theorem containers,
  retaining source text and cross-references.
- PDF equation numbering and internal destinations; separate electronic-reading
  width and A4 export choices, with no remembered submenu selection.
- Export transaction/rollback work and an experimental persistent-recovery
  implementation. Persistent Markdown recovery remains **OFF** by default.
- Local compatible-certificate generation/backup components and biometric
  components. Normal management is now conditionally available as described
  above; explicit enrollment remains required. First server-side certificate
  registration and broader native acceptance are not accepted as complete.
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

With explicit user authorization, a focused Cocoa run now instantiates the actual
main window, production note views, renderer, Qt dialogs, save worker and exit
handler using only disposable synthetic data. **32 foreground checks pass**:
bare math rendering, both PDF submenu choices, Markdown source preservation,
protected screenshot return/reauthorization/insertion, encrypted image save,
wrong-password and confirmation-No refusal, confirmed permanent decryption via
the real information popup, backup/state refresh and dirty-content save before
the original eleven-stage exit cleanup. UI commands are programmatically driven.

Visual PDF review found a real long-multline issue: an automatically wrapped body
could touch its normal-size equation badge in reading-width export. Supported
row fitting now reserves the first-row tag gutter too. A still-inseparable wide
multline SVG is fitted locally inside that gutter, preserving ordinary text and
number size; print-only styles/aspect ratio are restored afterward. Final PDFs
have four valid internal destinations each, all 16 prose markers, the same 12pt
prose in both modes and exact exported Markdown. Both complete pages were rendered
and visually reviewed with no observed body/number collision or text clipping in
this fixture. Reading width is 553.92pt; the A4 sample is about 594.96pt wide.
One affected native-editor/runtime group also passes **37 checks, 0 failures** in
72.35s, including three edit/read/print restoration cycles. No unrelated full
suite was rerun; old phase-168 counts below are retained, not accumulated here.

The final run follows test-driver corrections for distinct reference markup,
chooser readiness/filename, nonempty synthetic policy identity, duplicate mode
commands, the <=1-second new-note auto-edit branch and Cocoa's empty message-box
window titles. Eight incomplete attempts remain recorded locally; one isolated
failed process stalled in cleanup and was terminated without touching another
application or normal notes. Final results cover existing-note workflows, not
that special just-created-note branch. No result from an incomplete run is
presented as overall acceptance.

No normal account, real notes, desktop capture, native Touch ID/Keychain, service,
installation or packaging was used. The capture payload is a synthetic image,
delivered synchronously before the real helper's 200ms screen-grab timer can run.
Screenshots contain only this synthetic app's own widgets. Save dialogs are real
Qt widgets on Cocoa, not native NS(Save/Open)Panel or physical-pointer acceptance.
The existing isolated-window property disables sync/listeners; remote page
requests are intercepted, the local-only proxy remains in place and system
protection is unchanged. Normal Touch ID remains disabled. The manual-only probe
is not registered for unattended CTest execution.

### Retained phase 168 evidence

The two unfinished local features are implemented in one combined batch. Both
existing cancel-encryption UI entrypoints use the new save-first, fresh-password,
explicit-confirmation workflow. The old password-free database API refuses
conversion. The encrypted backup includes the full original archive and remains
available after success or an ordinary late failure. Certificate and shared
save-password cache are not deleted or changed. Lists and the current note receive
post-commit metadata/body updates; the now-plain note retires its old access gate.
Groups and encrypt-all databases are outside this personal-note conversion scope.

Only three directly affected groups ran: **1287 PASS, 0 FAIL in 15.11s**:
permanent decryption 30, document routing 1174, note access/session 83. Tests use
actual temporary databases, native ZIW encryption/decryption, ZIP/binary resource
bytes, atomic file replacement, SQL rollback, saver FIFO fences and extracted
production routing methods. Prompts, capture, page/editor and session events are
isolated collaborators, not normal-client/native authentication acceptance.

A final backend-only change preserves the latest ancillary metadata instead of
copying pre-prompt read counts/flags. Its same 30 checks passed again, including
an actual database read-count update between preparation and conversion. Only
that affected group was rerun; the 1287 count does not double-count this repeat.

Protected screenshots publish no resource file before a new successful unlock;
wrong passwords retain pending pixels without inserting them. Returning to the
original editor preserves unsaved DOM/save state. Soft inactivity revokes reads
but retains the capture; hard session events escalate even after earlier soft
inactivity and discard it. Normal Touch ID remains disabled.

Intermediate builds exposed new-code include/API mistakes, which were corrected.
The first associated run passed decryption and note-access groups but had three
routing fixture failures after splitting legacy and opted-in capture behavior.
The final run follows correction of those fixture assumptions and post-conversion
reload integration; failing logs remain local. No full-suite rerun, native screen
grab, real account, Keychain, note-service call, installation or packaging.

### Retained phase 167 evidence

The current local implementation batch is closed, not the entire native-client
acceptance plan. Sibling saved notifications now reload only after the actual
modified-state helper confirms clean content with the original live read permit.
Dirty checklist content, unknown state, new edits, relock, navigation and owner
destruction retain DOM and save credentials without an automatic save/overwrite.
The separate-window failure receiver consumes its load result, matching the main
window; this small connection fix is source-checked and compiled, not a native
separate-window acceptance claim.

Only the affected routing group runs: **1160 PASS, 0 FAIL in 12.76s**, with 9 new
checks and all old 1151 assertions retained. The real saved signal reaches the
actual receiver/modified helper. No full/native suite, real account, foreground,
Keychain, service, package or installation. An intermediate test-binding build
error was corrected; no failing runtime is counted as acceptance. One tracked
source absent from the old map was explicitly reconciled with the previous
remote blob before being added to the baseline. The initial snapshot procedure
stopped on that omission; reconstructed mapped snapshots match the old digests.

The backlog below replaces repeated lists of method-level testing subphases.
Do not reopen already accepted local chains merely because normal/native
acceptance remains outstanding. Further changes need a concrete defect, an
affected integration boundary or an explicitly selected unfinished feature.

### Retained phase 166 evidence

Phase 166 fixes two related save-completion risks and verifies three associated
chains. Completion now keeps weak editor/view ownership across saved publication
and checks the compatible main-window cast before requesting sync. Publication
checks the originating note/library, stops after receiver destruction or a note
switch, and emits a stable copy of the current title. Failed saves mark retained
content dirty; successful receipts do not clear newer edits or reauthorize reads.

The exact main-window save connections run with actual Qt queued receipts, actual
publication signals and temporary FIFO encrypted archives. Failure preserves an
exact original archive behind an ordinary output blocker; explicit retry reaches
publication and reconciles the same-note failure. The main HTML command runs
through the actual snapshot producer, queue, archive receipt, read fence and body
transaction, publishing the newly saved content rather than a stale archive.

The full main PDF/HTML/Markdown command and export-controller bodies are compiled
from current source. Reading-width/default and explicit A4 forwarding, no remembered
mode, in-flight coalescing, dialog cancellation/relock, obsolete replies, newer
reservations, invalid data/settings and owner destruction preserve prior output.
Markdown queries retain their original authorization before resource/body commit.
Renderer bytes, active-editor selection, sync requests and modal UI are recording
collaborators. Synthetic PDF headers/end markers test atomic writing, not visual
layout or native printing. The normal MainWindow is not instantiated.

Final verification runs **only 1 affected group: 1151 PASS, 0 FAIL in 12.78s**,
once after the final build, with 31 new checks and all old 1120 routing assertions
unchanged. Note/management and native/full suites were not rerun. The first fixture
omitted the producer's extracted file paths and used the product enum rather than
the existing shell mode convention, causing one failure; required synthetic state
was supplied without weakening product checks. That run and an intermediate enum
qualification build error remain separate evidence, not negative-control claims.
Only the two completion/publication product bodies change; export/layout and
main-window source bytes otherwise remain unchanged.

### Retained phase 165 evidence

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

1. **Implemented and locally isolated-tested:** fresh-password, confirmed,
   backup-preserving personal-note decryption and protected screenshot
   reauthorization, alongside document read/output retirement,
   initialization/resumption, save receipt/frontier/quit coordination, external
   watcher/retry, export controllers and disabled biometric management. Their
   method-level stages are closed; this does not equal whole-client acceptance.
2. **External prerequisite:** an authoritative compatible first-certificate
   registration service contract. Development signing/profile configuration and
   the focused native-backend/widget Touch ID chain are now accepted above;
   source management is conditionally available as described above, with policy
   default-off and explicit native enrollment required. No service probing.
3. **Remaining actual-client acceptance, not repeated source tasks:** the focused
   existing-note main-window/Qt-modal/export/save/quit subset above is accepted;
   normal-account and separate-window/shared-modal combinations,
   OS session/lock, native print/clipboard/
   chooser/capture/search/inspector, external process/manual conflict handling,
   representative attachment/encryption and new-candidate avatar update/reopen.
   Foreground permission covers only synthetic isolated data; normal-account
   operations and actual screen capture remain outside it. The additional explicit
   native-authentication grant was used only for the isolated synthetic test above.
4. **Retained limitations:** historical intermittent offline account-chooser
   fixture failure is not explained (the normal account override stays disabled);
   HTML destination-basename resource URLs are accepted in phase178 above;
   crash/concurrent-writer atomicity is not accepted. No repeated rerun is
   scheduled to hide these gaps.

VoiceOver, App Store/public binary distribution and zero-group non-default
selection persistence are intentionally excluded, not pending development work.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The latest integration manifest is [checkpoints/2026-10-22-phase178.json](checkpoints/2026-10-22-phase178.json).
The synthetic three-window acceptance is [phase177](checkpoints/2026-10-22-phase177.json).
The focused system/widget test is [phase175](checkpoints/2026-10-22-phase175.json).
The signing setup is [phase174](checkpoints/2026-10-21-phase174.json).
The UI change remains [phase172](checkpoints/2026-10-21-phase172.json).
The frozen local candidate is [phase171](checkpoints/2026-10-21-phase171.json),
whose product-source input remains [phase170](checkpoints/2026-10-21-phase170.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
