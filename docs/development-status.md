# Personal-use development checkpoint: existing-certificate usage only

## Current phase189: three-part unlock and plain settings

Closeout feedback (2026-10-22): the user reported no issue and explicitly deferred
further testing. Record general trial feedback as accepted, not as itemized proof
of native fingerprint/hardware combinations or official iPhone round trips.
Those checks remain unverified and deferred, not a blocker or a new mandatory
test request. The agreed executable source checklist is closed; move to ordinary
feedback-driven maintenance without adding feature/test rounds. This follow-up
changes documentation only: no build, test, signing, installer or App modification.
Ordinary Markdown save-failure investigation remains excluded. Automation stays
paused and recovery OFF; no user account or real-note action is authorized here.

The user explicitly reduced the unlock body to exactly three primary controls:
masked certificate-password input, Unlock, and Touch ID. The ordinary title,
redundant password caption/mode-switch button and decorative card were removed.
Certificate hints, exceptional action context, progress and errors appear only
when needed. An existing certificate hint appears below the password field as
one subdued theme-colored plain-text label, prefixed with "密码提示："; long hints
wrap, and absent hints hide and clear the line. No password-derived hint, extra
button or modal is added. If biometric capability is unavailable, only password unlock is
shown. Embedded authentication has no extra application Cancel button: native
panel cancellation or typing a password retires the request; manual Touch ID
retry remains after cancellation without an automatic loop. Standalone widget
consumers keep their original password/cancel choices.

Touch ID settings now has a plain password field, explicit one-shot local-record
consent and Enable/Remove record actions. Removal still needs consent but no
password; enabling needs both. No new confirmation loop, automatic enrollment,
policy/keychain mutation, session timing or save-path change was introduced.

Only affected UI/session-management/form-routing checks are run. After the user's
three-control refinement, only the two changed groups are rerun; the final hint
refinement reruns only form routing, not management or unrelated/full/save tests.
New local trial189 preserves188 and installed/frozen
Apps; no autonomous real-account/store launch, installation or native fingerprint
test. Ordinary Markdown saving-failure investigation remains explicitly stopped
and outside the plan, not declared fixed. RecoveryOFF, automationPAUSED. Remaining
native hardware/fingerprint and account/iPhone checks are deferred by the user's
closeout decision above, not required now. Do not expand the source checklist or
restart unattended idle checks.

## Historical phase188: shared lock policy, capability-aware unlock and minimal UI

Password-only and explicitly enrolled Touch ID now share the same encrypted-note
access gate: five continuous minutes away from the application, canceled on early
return, with immediate retirement on sleep/session lock. Plain notes are unchanged.
Navigation/hiding still retire note-specific access. No dirty DOM, save credential
or queued write is cleared. Password verification retains the existing local
certificate verifier; the gate itself never verifies or recovers a password.

The native provider queries current biometric capability and Touch ID type
without authenticating or reading a stored credential, then rechecks immediately
before starting. Display presence is not a proxy for hardware availability. An
unavailable context shows password input directly and creates no read operation.
If capability returns on activation before the first attempt, the still-current
visible context may start once. Actual cancellation/failure never automatically
loops: the Touch ID button retries explicitly. Typing a password retires automatic
intent; availability restoration may reveal a manual choice but cannot revive it.

Settings and password form now use native password inputs and normal-sized
buttons. Persistent explanatory/lock-policy paragraphs and cancellation warnings
were removed at the user's request. Only concise errors/progress and explicit
local-record management consent remain. Offscreen synthetic snapshots were
visually inspected for clipped buttons, not claimed as native Cocoa acceptance.

Affected acceptance only: unlock UI116/0, session/management91/0 and document
routing1189/0, total1396/0. Earlier test-constructor and three legacy screenshot
fixture failures are retained. Plain screenshot insertion uses a plain-note
fixture; protected cases retain post-capture authorization and stale-result
checks. Fake providers perform no system authentication or screen capture.

Clipboard behavior is unchanged: while unlocked, copy may publish plaintext;
relocking rejects new requests and stale-permit delayed publication. Already
published clipboard contents are not erased/recalled. Clipboard history/OS sync
is not controlled by note encryption. This is not a secure-memory-erasure claim.

Local signed trial188 is a new copy, preserving187 and frozen/installed Apps. It
uses the existing independent186 store only on user manual launch, never by the
agent. Source/docs only go to GitHub. The user explicitly stopped investigation
of the ordinary probably-Markdown save failure: removed from the forward plan,
not declared fixed, with existing save protections retained. Actual native availability changes,
fingerprint use, real-account workflows and iPhone round-trip remain user gates.
RecoveryOFF, automationPAUSED; no first-certificate creation or VoiceOver work.

## Historical phase187: editing grace, automatic unlock and save diagnostics

User feedback reopened the reduced checklist: a non-encrypted, probably Markdown
note reported saving failure and blocked closing/quit; the user already force-quit.
Do not attribute that issue to encryption or claim the unsaved changes recovered.
Read-only inspection of the trial application log found14 ambiguous snapshot/
extracted-HTML-write failures and no final archive-write failure message. No note
body/database or credentials were opened; no normal account was launched.

Brief application switching no longer retires an opted-in encrypted editing
session immediately. A five-minute continuous-background grace period applies;
returning early cancels it, repeated background states do not extend it, and
overdue activation handles delayed timer delivery. Existing hard sleep/session
boundaries still retire immediately; this changes app policy, not OS protection.

The ordinary native-provider password form now opts into one automatic Touch ID
attempt for its valid remembered context, only when visible in the active window
and active application. Queued context changes, manual input/cancel, hiding and
shutdown retire pending requests. Cancel/failure never loops; explicit retry and
certificate password remain available. No automatic enrollment or policy opt-in.
Synthetic collaborators validate the flow, not actual system authentication.

An independent-window FIFO acknowledgment now resolves an older failed write
when a newer write for the exact requester/note succeeds. Pending/latest failures
still block close; unsolicited completions cannot poison the state. Snapshot
failure reasons are now static content-free codes with concrete UI explanations,
and duplicate modal warnings are suppressed until a successful write or new
document. No preview-as-source fallback, dirty-state discard or forced exit.

Incremental accepted results: document async119/0, note/session access89/0,
document routing1181/0, automatic-unlock UI107/0, and a selected actual-engine
ordinary common/Markdown save probe13/0:1509checks across five affected groups.
The first UI run exposed a manual-cancellation reentrancy regression, corrected
before acceptance; later re-show fixture failures were Qt offscreen activation
state, resolved by driving only the synthetic active-window state. Failed logs
are retained. Already-passing affected groups were not repeatedly rerun after
the final fixture-only correction; no unrelated full suite or physical OS test.

Local signed trial187 (14425.187) preserves186 and uses the same independent186
test store only when manually launched; it does not switch to the installed App
store, migrate data or auto-start an account. Signed inherited components and
minimum rights remain unchanged. Source/docs only go to GitHub; binaries,
signing data and raw logs stay local. RecoveryOFF, automationPAUSED.

The reported particular Markdown save failure remains **not reproduced/root
cause unconfirmed**. Basic actual-engine save success and the acknowledgment fix
do not close it. The new precise prompt or a minimal non-sensitive reproduction
is required; real account/iPhone and native automatic Touch ID also await user
acceptance. Do not resume idle autonomous tests while waiting for that evidence.

## Historical phase186 delivery: independently signed trial

The newly prepared local **14.4.25, build14425.186** trial contains the current
phase183/184 fixes, not the older frozen installer. The user authorized this
next delivery step after phase185. Existing installed App, old signed test copies
and frozen candidates are preserved; no installation or normal account launch.

Only the separately named `WizNoteUserTrial` entry gets a fixed fresh standalone
root using the existing storage validation/consent flow. Finder and inherited
data-store overrides cannot fall back to the installed App store. The ordinary
executable path is unchanged. Source BOM/CRLF and current executable code/constant
sections are verified. Manual first launch prepares the new directory before the
existing default-cancel consent; cancellation does not reach account bootstrap.
Local storage isolation does not isolate cloud data after the user logs in.

The new copy reuses the previously accepted development-signing identity,
matching unexpired profile and minimum main entitlements. Its original46 signed
components/rights are byte-preserved. Initial packaged offscreen testing aborted
at Qt platform initialization because the compact bundle only contained cocoa;
the supplied crash report matches that test executable/stack, before database
opening. The failed log is retained, not a passing encryption test. A matching
Qt6.11.3 arm64 offscreen test plugin was added and signed in this new copy only;
strict recursive signing and existing main permissions remain unchanged, with
48 Mach-O components total. No library-validation/system-protection weakening.

Final package checks: **permanent decryption43/0 and routing1181/0, total1224/0**,
plus two read-only startup-root checks, malformed probe code2 and a read-only
native signing-prerequisite check. Actual Qt maps from the signed bundle, without
developer framework overrides. No SecItem/authentication calls, fingerprint,
normal account, trial account-store creation, real note, native physical-session
or unrelated full-suite repetition. The trial's launcher validates the exact
bundle/signature and refuses when an existing client/clipper port is in use;
its check-only mode does not launch the App. No automated user session was run.

The local App/launcher and concise trial instructions are delivered separately;
only source and sanitized summaries go to GitHub, not binaries/profiles/raw crash
reports. The phase185 delivery gap is now closed. The [acceptance steps](user-acceptance-existing-certificate.zh-CN.md)
remain user-operated and unaccepted. Recovery OFF, automation PAUSED; no new
routine approval loop or automatic restart while waiting for actual feedback.

## Historical phase185 handoff (delivery gap superseded by phase186 above)

Phase185 reconciles the reduced checklist with the latest evidence. No additional
concrete source defect remains identified in that checklist; this is not complete
App acceptance. The existing-certificate use fixes are in source and the developer
executable: permanent-decryption feedback43/0 in phase183 and opening/review,
relocking/encrypted-save routing1181/0 in phase184. Neither group is rerun here.

There is an explicit **delivery gap** before the next user test. The frozen
14.4.25 installer is the phase171 build, and the latest accepted signed test copy
is phase181. Neither includes the phase183/184 fixes, despite sharing the same
development version label. Do not ask the user to validate these fixes in either
old artifact. A new independently signed test build matching the current source
must be prepared before that session, preserving the installed App and frozen
candidates. It has not been prepared or installed; the user is not expected to
build it themselves. Do not equate a source commit or incremental build with a
new delivered installer, or silently expand continuation into packaging/install.

The consolidated [user acceptance checklist](user-acceptance-existing-certificate.zh-CN.md)
covers prerequisites, existing-certificate Mac use, official iPhone round trip,
representative resources and optional plaintext conversion on a disposable note.
It distinguishes required account/device actions from optional specialized native
workflows and retained historical limitations; no passwords/keys need to be sent.
Actual normal-account/iPhone/native-control outcomes remain unaccepted.

The user requested pausing once only delivery/user-dependent acceptance remains.
The existing automation is now **PAUSED**, with its 20-minute cadence and saved
instructions retained. No product/test/build-input changes or functional tests,
signing, packaging, installation, real-note/account access or authentication took
place in this documentation-only handoff. Recovery stays OFF. Resume source fixes
only for concrete findings or an explicit next delivery/test step, not idle loops.

## Phase 184: existing-certificate opening, relock and encrypted-save routing

The initial affected group reproduced **1181 checks, 2 failures**: opening
ignored certificate-initialization failure and review silently returned after
certificate reload failure. The encrypted opening now conceals the old body
before the existing acquisition boundary, checks its result and rechecks owner,
opening epoch and closing state after that potentially modal call. New navigation
or close cannot continue the old certificate load or password-page presentation.

Failure retires existing read requests, shows a blank page with an explicit
existing-certificate-unavailable message, and retains dirty DOM and save cache.
The blank page refuses reads even on the legacy route; stale password submissions
outside the password page are ignored. After the existing certificate becomes
available, review plus fresh authorization resumes retained content without a
body reload. There is no automatic certificate creation/replacement or retry.

Only incremental main-client compilation and the **one affected routing group**
ran, finally **1181/0**. Existing wrong-password/relock cases and real temporary-
database FIFO encrypted-write/save-first-quit assertions remain in that group.
The exact production routing bodies are compiled with explicit synthetic
certificate-acquisition, prompt and renderer collaborators; no real account or
certificate service is contacted. Database/sync/password-algorithm/save source
is unchanged. No permanent-decryption/PDF/native-auth/physical-session/full-suite
rerun, signing, installer or installation. Normal App, real notes, accepted signed
copies and frozen candidates are unchanged. This is not normal-account/iPhone or
real native-dialog acceptance.

Recovery remains OFF. The user explicitly reactivated the existing 20-minute
automation after phase183; it is now ACTIVE, not the historical PAUSED state
recorded below. It follows the current existing-certificate-only scope, avoids
repeating closed stages, and pauses with a precise list if only user-dependent
real-data/foreground prerequisites remain.

## Phase 183: existing-certificate permanent-decryption failure feedback

Two concrete use-path defects reproduced before the fix: a failed save frontier
returned silently, and an unavailable local certificate was reported as a generic
unsupported note. The focused group initially had **32 checks, 2 failures**.

Preparation now preserves a specific failure category and honors certificate-read
failure; certificate availability rules and encryption/archive/rollback algorithms
are unchanged. Missing/unusable local certificates stop before password or consent
prompts, with no automatic creation/replacement or server request. Save-frontier
failure produces one report and preserves the unresolved save failure, pending
work and original content. Intentional cancellation still produces no error popup.

Messages distinguish certificate password from account login password, changed
scope, read/decrypt failure, backup failure, write/metadata failure and incomplete
rollback. Any retained backup path stays visible; password/key bytes are excluded.
The incomplete-rollback instruction still stops editing/sync rather than claiming
success. Successful conversion, backup retention and original resource bytes stay
covered by the same affected group.

Only incremental main-client compilation and **one directly related test group**
ran: **43/0, 0.35s**. It uses actual temporary database/reader/archive/saver code
with synthetic prompt/report callbacks, not a real Cocoa dialog, account or iPhone
round trip. No PDF/native-auth/old-matrix rerun, signing, installer or installation.
Normal App, real notes, accepted signed copies and frozen candidates are unchanged;
recovery OFF, automation PAUSED. First-certificate creation remains out of scope.

## Current scope decision: phase 182, 2026-10-22

The user has removed Mac first-certificate creation/registration from the current
plan. Continue only the use of an existing compatible account certificate:
password unlock, encrypted save, explicit biometric enrollment/forgetting,
relocking/read-output retirement and confirmed permanent decryption with backup.
This is a workflow prerequisite, NOT evidence that a real account has a valid
certificate. Missing certificates and read/authentication failures must still
prevent certificate-dependent operations; never infer absence from a failure or
automatically create, upload, replace or reset a certificate.

The official open-source client exposes `getCert`/`setCert` and the certificate
fields, but the first-create atomic no-replacement contract, authoritative absent
response and uncertain-submit reconciliation remain unconfirmed. That issue is
retained, not solved; it is no longer a prerequisite for this reduced scope.
Local generation/backup prototypes and their historical tests remain dormant,
not an enabled product feature. Do not continue their native-panel/registration
acceptance or remove shared certificate-verification helpers used by decryption.

See [existing-certificate usage and recorded implementation](encryption-existing-certificate.zh-CN.md)
for the consolidated feature/evidence list and bounded next steps. Previous phase
records below are historical; references there to first-certificate prerequisites
do not reinstate that canceled task. Phase 182 changes documentation only, with
source/evidence continuity checks and no new functional-test or build claims.
Normal App, real notes, signed test copies and frozen candidates remain unchanged;
recovery stays OFF and automation PAUSED. Actual account/iPhone interoperability
and representative real attachment combinations remain unaccepted.

## Phase 181: user-operated sleep/wake, actual workspace notifications

Reusing the isolated main-window flow, the new explicit sleep fixture asks the
user to choose Apple-menu Sleep, wake/unlock normally and confirm. The agent
never sleeps or locks the Mac. No normal account, real note, test fingerprint or
screen capture is involved. The retained production session/auth rules are not
modified; only the guarded test dispatch/fixture changes.

All **23 checks passed**, with actual native notifications separately observed:
**WillSleep1, DidWake1, workspace retirement1, production Monitor hard event1
before quit**, and one app-inactive event. No local notification was posted.
The user's confirmation and actual native sleep/wake events both participate;
mere app deactivation is insufficient for this mode. Protected body access and
old read permits stay retired, dirty DOM/original body/certificate/save cache
survive, archive format stays encrypted, exact owned memory is removed/Missing
confirmed, and normal eleven-stage quit saves original plus latest content.

The pre-quit observation file is authoritative for hard-event acceptance. The
final aggregate counter includes the subsequent normal shutdown hard event
(2 total); that second event is NOT claimed as another real OS notification.
Runtime is bundled Qt; strict signature/current code/arm64/dependencies and the
46 inherited signed components/permissions are incrementally verified. No-arg
and malformed launches still refuse account bootstrap with code2. No other suite,
PDF, lock-screen, multi-window, fingerprint or recovery-matrix repetition.

Installed App, phase180 signed test App and frozen candidates are unchanged;
only a new signed local sleep-test copy is made, no installer or installation.
Recovery stays OFF and automation PAUSED. This closes the focused physical
sleep/wake scenario, not switch-user or all system-event combinations. Remaining
real-data/native-control acceptance and the authoritative compatible first-
certificate service contract stay separate; no service probing or new routine
approvals. No keys, profile/identity, raw logs, synthetic data or binary uploaded.

## Phase 180: actual user-operated lock/unlock with synthetic notes

A new independent signed main-client copy offers an explicit live-session fixture
before account bootstrap. It generates only temporary synthetic credentials/notes,
opens through the production manual-password control and leaves a dirty renderer.
The agent never locks or sleeps the Mac. The user operates Control+Command+Q,
unlocks normally and confirms in the isolated instruction window. The fixture
times out after three minutes if unconfirmed; failure still cleans its owned item.
No test fingerprint, normal account, real note or screen capture is requested.

The user confirmed the round trip and the actual Cocoa process observed **one
inactive event and return to active**. All **22 checks passed**: no body access
or old read permit restored on unlock; latest DOM, certificate/save cache and
encrypted archive format retained; exact synthetic memory deleted and Missing
confirmed; normal eleven-stage quit saved latest plus original body. The event
loop exited normally and runtime Qt came from this signed App. No-argument and
malformed launches refused normal startup (code2). No unrelated suite rerun.

Important distinction: **zero native NSWorkspace retirement notifications** were
observed in this exercise. The physical lock/unlock is user-confirmed and its
actual application-inactive route is accepted, not independent proof that public
workspace sleep/session-resign events fire for lock screen. Actual sleep and
switch-user hard-event delivery remain separate unaccepted combinations. No
local notification was injected or automatically posted; this is distinct from
the earlier simulated workspace tests. Production session/lock logic was not
changed to make acceptance pass.

Only the guarded fixture dispatch and acceptance test changed. Existing installed
App, real notes, phase177 signed App and frozen candidates remain unchanged; no
installer or installation. The new signed test uses the same matching profile,
minimum main entitlements and 46 inherited signed components/permissions. No keys
exported, no identity/profile/binary or raw log uploaded. Recovery stays OFF and
automation PAUSED. First compatible certificate service registration still needs
an authoritative contract; no server probing is authorized or performed.

## Phase 179: account-file chooser fixture readiness

Only the offline test driver changed; every existing product source byte is
unchanged. The ordinary account chooser override remains disabled. This is not
a real-account upload, avatar replacement or server-success verification.

The retained intermittent fixture failure reproduced immediately: **23 checks,
2 failures**, despite Chromium reaching its chooser. Temporary observation of
the dialog selection then passed once and three bounded repeats; this timing
sensitivity does NOT establish a complete root cause. Failed logs remain.

The old driver accepted after a fixed 30ms and treated merely observing the modal
as completion. It now waits for visibility, the exact readable path and filename,
and an enabled Open button; it revalidates in the queued accept and waits for
dialog completion before polling DOM/FileReader. A controlled filename-reset
case proves acceptance is deferred and the selection restored. Reselecting the
same model row alone did not restore cleared text in the initial new fixture;
the driver now fills its own filename editor explicitly, with that failure kept.
No file is injected into the page; real Chromium chooser delivery remains used.

Final affected group: **25/0** in each of three bounded runs (**75 passes**, 2.18s
total), including exact PNG/name/MIME/FileReader bytes, cancel, owner/parent
destruction, and private-profile cache policy checks. No full suite, repeated
PDF/native-auth workflow, new signing, installation or packaging. The historical
failure's complete cause and real-avatar update remain unaccepted; no further
repetition is scheduled just to turn that limitation into a passing claim.

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
   watcher/retry, export controllers and conditional opt-in biometric management. Their
   method-level stages are closed; this does not equal whole-client acceptance.
2. **Existing-certificate prerequisite, not first registration:** a compatible
   account certificate and its known password are required for the relevant
   operations. Do not assume real-account availability from synthetic tests.
   Development signing/profile configuration and the focused native-backend/widget
   Touch ID chain are accepted above; source management remains conditional on
   valid runtime signing/authorization and an existing certificate, with policy
   default-off and explicit native enrollment required. First-certificate service
   registration and its unresolved contract are outside the current user scope.
   No service probing, automatic certificate creation or replacement.
3. **Delivery and actual-client acceptance, not repeated source tasks:** a new
   source-matched signed test build is needed before user acceptance; frozen
   installer171 and signed test181 do not contain fixes183/184. The focused
   existing-note main-window/Qt-modal/export/save/quit subset above is accepted;
   normal-account and separate-window/shared-modal combinations,
   switch-user and broader system-event combinations (user-operated lock accepted
   in phase180; actual sleep/wake hard-notification route accepted in phase181),
   native print/clipboard/
   chooser/capture/search/inspector, external process/manual conflict handling,
   representative attachment/encryption and new-candidate avatar update/reopen.
   Foreground permission covers only synthetic isolated data; normal-account
   operations and actual screen capture remain outside it. The additional explicit
   native-authentication grant was used only for the isolated synthetic test above.
4. **Retained limitations:** the offline account-chooser driver now checks actual
   readiness/completion and passes bounded verification in phase179, but the
   historical intermittent failure's complete cause is not established
   (the normal account override stays disabled);
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

The latest local-delivery manifest is [checkpoints/2026-10-22-phase186.json](checkpoints/2026-10-22-phase186.json).
The preceding delivery-gap handoff is [phase185](checkpoints/2026-10-22-phase185.json).
The latest focused functional integration is [phase184](checkpoints/2026-10-22-phase184.json).
Permanent-decryption feedback is recorded in [phase183](checkpoints/2026-10-22-phase183.json).
The existing-certificate scope decision is [phase182](checkpoints/2026-10-22-phase182.json).
The latest signed physical-session acceptance is [phase181](checkpoints/2026-10-22-phase181.json).
The manual lock/application-inactive acceptance is [phase180](checkpoints/2026-10-22-phase180.json).
The bounded chooser-fixture improvement is [phase179](checkpoints/2026-10-22-phase179.json).
The HTML resource URL correction is [phase178](checkpoints/2026-10-22-phase178.json).
The synthetic three-window acceptance is [phase177](checkpoints/2026-10-22-phase177.json).
The focused system/widget test is [phase175](checkpoints/2026-10-22-phase175.json).
The signing setup is [phase174](checkpoints/2026-10-21-phase174.json).
The UI change remains [phase172](checkpoints/2026-10-21-phase172.json).
The frozen local candidate is [phase171](checkpoints/2026-10-21-phase171.json),
whose product-source input remains [phase170](checkpoints/2026-10-21-phase170.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
