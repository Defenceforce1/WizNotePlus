# Markdown export source-flow diagnostic

Phase113 current status: the App has an experimental compile-time recovery gate,
default OFF. Its ON writer now prepares persistent evidence, installs resources,
commits Markdown last and handles ordinary failures using a one-attempt proof.
Direct quit now retains before cleanup as well as prepared quit. The complete
authorized offline regression set and final integration tests passed; native
and real-note acceptance remain separate, unperformed gates. Earlier dormant
and transitional writer notes below describe historical phase boundaries.

Developer-only Qt executable. No App bundle, deployment, signing, accounts,
database engine, archive reader, or main-window initialization is used.

CMake extracts the current `saveAsMarkdown(QString&, bool)` body, the three-argument
UTF-8 writer and filename helpers from product source. Only the export class
identifier and writer function name are renamed. The export method executes
against a real off-record `QWebEnginePage`; a synthetic database collaborator
writes controlled HTML, optionally reporting failure after that write. All output
is under `QTemporaryDir`, never a user-selected destination. External requests
are blocked. Explicit absolute SDK resources are preflighted before WebEngine.

Configure using the existing Qt SDK as `CMAKE_PREFIX_PATH`, build
`markdown-export-probe`, then run with `WIZNOTE_TEST_QT_SDK` set to that SDK root.
The program forces the offscreen platform, without disabling sandbox/security.

`markdown-export-probe --require-reply-safe` now tests the current narrow
reply-first product method. `markdown-export-legacy --require-reply-safe` runs
the frozen extracted pre-guard method and MUST still fail four invalid-result
safety checks. The legacy file and current source hashes are recorded separately.
`--require-safe` additionally requires preservation after a modeled late database
failure, which remains UNSOLVED by the narrow product fix. A pass of ordinary
characterization is not full safety acceptance. Empty Markdown is valid; numeric
results are invalid even though QVariant can stringify them.

This is not an actual `WizDocumentWebView` / database / resource-copy end-to-end
test. A modeled late failure is not proof that a real archive copy failed. Source
hashes and actual callback completion are recorded. Do not infer acceptance of an
incomplete resource transaction redesign from the narrow method's success.

## Unaccepted reply-first prototype

`markdown-export-candidate` compiles `candidate-body.inc`, NOT the main export
method. It permits only a genuine QString reply (including empty), then runs
the same modeled database export and actual writer/path helpers. Existing epoch,
weak owner/page and note identity checks are exercised through the adapter.
Destroying the adapter, invalidating its epoch, changing its synthetic note, or
detaching its page before delivery must preserve the temporary sentinel.
This does not prove actual main-window/navigation/destruction end-to-end safety.

`--require-reply-safe` checks only invalid JavaScript results; `--require-safe`
also checks the modeled late database failure. The latter must STILL FAIL for
this prototype: successful validation is not an atomic resource/file transaction.
The product now has the separately reviewed reply/type/lifetime guard and, since
Phase67, the separate staged `WizMarkdownExportTransaction` helper. This older
candidate remains a historical comparison and is never linked into the main target.

Optional `WIZ_MARKDOWN_EXPORT_SANITIZE=ON` instruments the own harness/extracted
code with ASan/UBSan, not Qt itself. Keep leak detection disabled for that test;
do not describe it as whole-application or archive/resource-copy validation.

## Actual resource methods and unaccepted staging prototype

`markdown-resource-probe` uses extracted, unmodified `WizCopyFolder`,
`WizDatabase::exportToHtmlFile`, UTF-8 reader/writer and path helper bodies.
It links no database engine or archive reader: ONLY archive extraction is an
explicit adapter copying a synthetic directory, with an optional unreadable
temporary resource. The shared encoding compatibility header requires Qt Gui
and Core5Compat but this executable creates only QCoreApplication, no windows,
WebEngine, settings or accounts. Source digests identify the extracted files.

Default characterization records the actual copy helper deleting an old resource
before an unreadable copy fails, and the actual export method having already
written HTML and earlier resources. `--require-safe` must fail on that old path.
All permission changes apply only to QTemporaryDir files and are restored.

`--transaction-prototype` separately exercises `transaction-prototype.h`, never
linked into the main App. It snapshots old resources into a same-parent temporary
directory, exports there under the original basename, reserves a QSaveFile, swaps
resource directories and commits Markdown last. An ordinary final-commit failure
rolls resources back. Existing unrelated resources retain the old merge behavior;
valid output path and UTF-8/no-BOM bytes remain identical. Directory/symlink
destinations are refused here and by the separately tested Phase67 product helper.

This is NOT power-loss-safe or a multi-file atomic operation. Two controlled
rollback-failure fixtures now verify that a blocked new-directory rollback or
blocked old-directory restore retains the old backup (and returned new directory
where applicable), reports the recovery path, preserves Markdown and refuses to
overwrite an unexpected destination. Test-only callbacks inject these faults;
actual directory renames and permissions still determine the failure. Permission
changes are restored, then the outer QTemporaryDir cleans its synthetic fixtures.

General concurrent-writer recovery, recovery across restart/power loss, a recovery
UI, real archive/password workflows and main-window integration remain unaccepted.
Do not describe controlled backup retention as guaranteed successful rollback.
Never run this developer probe against user destinations or call it a shipped fix.

`--transaction-compatibility` checks reserved-looking basenames (also without an
extension), dot-prefixed names, Unicode/ampersand/angle brackets, hidden old files
and nested hidden directories. User output is now staged under a separate content
subdirectory so it cannot collide with internal backup/returned paths. A private
snapshot helper preserves hidden regular old resources instead of inheriting
the broad legacy copy helper's hidden-file filter. Archive export itself STILL
uses the unmodified actual helper; this does not globally change archive rules.
Unknown old-resource symlinks and nonregular entries are refused before output
mutation. The product helper uses the same safe refusal policy. Phase69 separately
checks macOS `0600` mode plus one file and one directory extended attribute; other
metadata combinations and the crash/concurrency/end-to-end boundaries remain open.

## Current product helper and large-resource check

`markdown-product-transaction` compiles the actual
`src/share/WizMarkdownExportTransaction.cpp` against the extracted database export
path and a synthetic archive adapter. Ordinary mode covers returned failures,
successful and empty output, first export, unsupported destination shapes, exact
UTF-8/resource paths and staging cleanup. `--stress` creates 2,048 existing files
across 32 directories (8 MiB total) and checks preservation of every file, a
restrictive mode, file/directory xattrs and private staging cleanup. ASan/UBSan
instrument this harness and product helper, not Qt itself. This remains synthetic;
it is not a full native picker or real-user-note benchmark.

Phase135 adds the ordinary HTML entry of the same product helper. Both the OFF
and recovery-gated ON transaction targets run the HTML corpus, including actual
late recursive-copy failure, permission expiry before staging/after extraction/
after resource installation, missing staged body, exact BOM/archive fallback,
Unicode naming, first output, hidden/unrelated resources and macOS mode/xattrs.
Their compile-only HTML checkpoint cancels the actual QSaveFile before commit;
it is absent from normal App builds. A separate failed rollback retains the only
complete previous resource directory and reports its path. No HTML recovery
manifest/lock is created in either build. Legacy special-name rewrite differences
are recorded, not presented as URL compatibility acceptance. This is still not
power-loss/concurrent-writer atomicity or normal-client/authentication acceptance.

## Restart recovery and concurrent-writer protocol prototype

`markdown-recovery-prototype` is test-only and never linked into the App. It
builds explicit filesystem states for prepared output, old-resource reservation,
new-resource installation, final Markdown commit, interrupted rollback and first
exports with or without resources. A compact manifest records old/new content
digests; recovery finalizes only an exact new pair, rolls back only recognized old
and new states, and otherwise retains evidence without overwriting unknown changes.
Malformed manifests, parent traversal, replacement symlinks and unexpected live or
returned bytes are refused.

A deterministic lock derived from the canonical parent and destination filename is
held by a separate writer process in the concurrency check. A second writer and
recovery both refuse the busy destination, while another destination remains
independent. These checks establish a minimum protocol shape, not product approval.

The Phase72 extension uses an exact destination-derived recovery sibling instead
of scanning a parent directory. It explicitly full-syncs regular files with
`F_FULLFSYNC` on macOS, syncs containing directories after manifest commits,
renames and cleanup, and refuses persistence calls through symlinks. A child
writer is then killed with `SIGKILL` after three completed persistence barriers:
old resources reserved, new resources installed and Markdown committed. The
recognized old/new pair is recovered idempotently at each boundary.

Phase73 adds three more real child-process termination points immediately after
the corresponding rename but before the following parent-directory sync: moving
old resources, installing new resources and replacing Markdown. The readiness
signal lives outside the export parent so observation cannot itself sync the
directory being tested. These checks exercise process termination while the live
filesystem still exposes each rename; they are not a simulation or proof of
storage-device power loss or cache survival.

The same phase measures both content-only and metadata-aware digests over a
synthetic 2,048-file, 8 MiB tree. The metadata digest covers file type/path,
content, permission mode, uid, gid, macOS file flags and all visible extended
attributes for files and directories. A permission/xattr-only mutation must
leave the old content digest unchanged and change the metadata digest. This is a
diagnostic full-tree cost, not an incremental implementation or an application
benchmark. ACL entries and specifically seeded resource-fork/FinderInfo cases
remain unverified even though visible extended attributes are enumerated.

Phase74 adds a decision/audit facade around the same test-only recovery model.
Inspect, cancel and explicit retain write only a privacy-minimized event in a
separate control directory and leave the recovery directory, dead lock, manifest
and live/backup output unchanged. Confirm first persists a request event, then may
invoke the existing same-host dead-owner recovery, followed by a result event.
The log stores a derived destination identity, decision and outcome; it excludes
the destination path and note/resource content. A replacement audit-log symlink
is refused before recovery starts. This models required ordering and choices, not
a shipped dialog, wording, accessibility treatment or production log location.

The phase also adds an in-memory incremental content-digest cache while retaining
a complete sorted directory enumeration and metadata read on every pass. Cache
reuse requires the same relative path plus device, inode, size, mtime, ctime,
mode, uid, gid, macOS flags and extended attributes. Same-size content replacement
with restored mtime, permissions, xattrs and path renames each invalidate the
affected file; additions create and removals prune entries. This conservative
model avoids trusting size/mtime alone, but it is not a persistent cache format
and does not yet address cross-process validation or cache-file corruption.

Phase75 gives that cache a test-only persistent JSON envelope. Schema version,
root identity and exact top-level/per-entry field sets are mandatory; truncated
JSON, unknown versions or fields, malformed digests, duplicate/out-of-root paths
and a mismatched root are rejected without retaining partially loaded entries.
The file is replaced with QSaveFile in the same directory, then file and parent
directory persistence barriers are applied. Read and write both refuse a cache
path symlink.

A deterministic exclusive cache lock covers both readers and writers. Active
cross-process ownership blocks both. A child writer killed after syncing its
uncommitted QSaveFile temporary but before commit leaves the prior target bytes
unchanged and a real lock file. Default load refuses that dead lock; only the same
explicit same-host/same-app/dead-PID confirmation used by recovery removes it.
An initial test run exposed Qt silently clearing the dead lock when the cache
helper called `tryLock()` directly; the model now refuses any pre-existing lock
before attempting acquisition. This was a test-prototype defect, not product code.

Phase76 makes the cache trust boundary explicit. Entries loaded from persistent
storage are structurally valid hints but are marked non-reusable; every file is
read and hashed once before the entry can be reused inside the current process.
A well-formed cache containing a deliberately wrong content digest therefore
cannot poison the computed tree digest. This means the safe model currently gains
no cross-process content-I/O reduction; persistence records diagnostics and future
design evidence only.

For in-process reuse, the scan records each observed fingerprint and performs a
second fingerprint pass. A controlled same-size rewrite immediately after the
first fingerprint (with mtime restored) changes ctime, fails validation and causes
one retry that rehashes the changed file. A stable follow-up reuses all entries.
This narrows a tested race window but does not make an unlocked directory snapshot
atomic. Recovery continues to ignore cache results and performs its existing live
content checks; a valid stale cache beside changed resources cannot authorize any
rollback or finalization.

Phase77 bounds the test audit lifecycle. The audit directory is forced to owner
`0700` and current/rotated files to owner `0600`. Exact JSON fields and allowed
identity/decision/event values are revalidated before every append. Entry and byte
limits rotate the live file through two complete archives; the oldest segment is
removed under the explicit finite-retention rule. A symlink in any archive slot or
corruption in any retained segment blocks further appends.

A corrupt live log is never overwritten automatically. An explicit quarantine
operation renames it to a SHA-derived sibling, preserves its bytes with private
permissions, and only then permits creation of a new log. Quarantine refuses a
valid current log, and a single event larger than the configured bound is refused.
The rotation sequence is individually persisted but is not a multi-file atomic
transaction; product log location, retention duration and user-visible controls
remain undefined.

Phase78 terminates a real child process at all six visible rotation boundaries:
after oldest removal, archive shift, live-to-archive rename, atomic live commit,
live-file sync and audit-directory sync. Every interrupted layout remains a valid
exact event prefix with private permissions. The next append recognizes either a
normal contiguous archive prefix or the single gap made by an interrupted
descending shift, completes only the missing moves, and retains the expected
bounded order without duplicating a committed event. `QSaveFile` permissions are
set before commit so a newly visible live segment is already owner-only.

This process-termination matrix does not make the multi-file rotation atomic and
does not prove storage power-loss durability. The test logger still has no
cross-process writer lock, product-owned location, retention-duration policy,
view/delete controls or uninstall policy. It therefore remains diagnostic-only;
recovery correctness must never depend on audit append success or audit history.

Phase79 adds a deterministic cross-process writer lock for each audit path. The
lock is owner-only, synced before any log mutation, distinct from every export
destination recovery lock, and explicitly removed and directory-synced after a
successful append. An active writer blocks the same log without blocking a
different log. A lock symlink, malformed record, foreign host or live same-host
owner is preserved and refused.

The termination matrix now also kills a child after the audit lock is persisted
but before retention or append work, in addition to the six Phase78 rotation
boundaries. Every kill leaves a real lock. Ordinary append never silently removes
it; explicit takeover succeeds only when QLockFile identifies this executable on
this host and the recorded PID is dead, after which the exact interrupted layout
is revalidated and resumed. The writer lock closes the tested process-concurrency
gap, but the logger remains diagnostic until product location, retention, user
controls, uninstall behavior and UI/exit coordination are specified.

Lock takeover is deliberately not automatic. Active same-host owners, malformed
or foreign-host lock records remain busy. A stale file may be removed only through
the explicit confirmed-recovery path when QLockFile identifies this executable on
this host and the recorded PID no longer exists; the manifest and live state must
still pass all digest checks. This is a protocol decision for a future prompt, not
a product UI or authorization to delete arbitrary lock files.

Phase80 freezes those conclusions in
`recovery-interface-contract.json` and the human-readable
`docs/markdown-export-recovery-interface.md`. The JSON contract is intentionally
marked `design-frozen-not-product-integrated`: it specifies strict manifest fields,
targeted discovery, non-nested destination/audit locks, a privacy-minimized bounded
audit lifecycle, safe UI defaults and quit coordination, but does not make those
features available in the App.

Run `validate-recovery-interface-contract.py` to check the product-integration
boundary. The validator rejects unknown contract top-level fields and changes that
enable automatic recovery, nest locks, weaken the manifest whitelist, change the
default from retain, or alter the established three-second quit synchronization
policy. It uses only the Python standard library and reads no user data. This is a
design-contract check, not a substitute for product core, UI, termination or full
application regression tests.

Phase81 adds the first product-source recovery core in
`src/share/WizMarkdownExportRecovery.{h,cpp}`. The standalone
`markdown-recovery-core` target compiles those files directly, but neither file is
listed in the App build nor called by the current export transaction. The core is
therefore dormant: it derives same-parent destination identities and paths and
performs read-only, same-destination inspection only.

Inspection accepts only the exact v1 manifest field set and matching destination
and HTML-escaped resource basenames. It rejects unknown/missing fields, wrong
types, unknown schema, invalid digest/existence pairs, malformed or oversized JSON,
and link/non-regular shapes. A pre-existing regular destination lock is returned
as busy and is never removed; link or non-file lock shapes are refused. Ordinary
summaries omit full paths while diagnostic details retain them for a future
user-opened details view. No live digest comparison, lock takeover, mutation,
recovery, audit append, prompt or quit integration is implemented yet.

Phase82 extends the dormant core with a non-mutating confirmation preflight.
It classifies destination locks without removing them, then—only when no lock
exists—takes a conservative same-target lock, rereads the strict manifest and
rehashes live Markdown plus resource trees. The lock is created atomically with
owner-only permissions and never uses automatic stale-owner cleanup; release
checks that the path still names the same filesystem object before unlinking it.

Preflight recognizes the six evidence states frozen by the Phase80 contract and
returns only `RollBack`, `Finalize`, `Busy` or `Refused` plans. It never performs
the planned rename, cleanup or overwrite. An active owner remains busy; malformed,
foreign or link-shaped locks are refused; a real same-host/same-application dead
PID is classified `ConfirmedDead` but remains present and ordinary preflight still
returns busy. Exact external Markdown/resource changes and a changed staged new
Markdown copy are refused with all evidence preserved. Confirmed takeover and all
recovery mutations remain unimplemented.

Phase83 adds a still-dormant durable preparation API. With a free destination
lock and no existing recovery site, it snapshots the live old-state digests,
creates the deterministic sibling directory with owner-only permissions, writes
the new Markdown bytes, invokes a caller-supplied HTML/resource export beneath a
private `content` directory, syncs the staged tree, rechecks that the live old
state did not change, and atomically commits the exact v1 manifest last. The live
Markdown and resources are never renamed or replaced by preparation.

Ordinary callback, shape, sync or digest failures remove only the directory this
call created, after verifying the directory identity; cleanup never follows a
link. An existing site is retained and refused. Active and confirmed-dead locks
are retained before the export callback is invoked. A real child killed while
holding inside the export callback leaves a private partial site with no committed
manifest plus a classifiable dead lock; ordinary retry preserves both. Another
destination remains independent. This tests one real pre-manifest termination
boundary, not every manifest-commit or directory-sync boundary, and still does not
enable product export or recovery mutation.

Phase84 narrows the committed-site ownership boundary. The HTML file used only to
extract resources is removed and its containing directory is synced before the
manifest is committed, so every retained content entry is covered by the strict
manifest. A committed recovery root now accepts only `transaction.json`,
`new-markdown`, `content`, `previous-resources` and `returned-resources`; the first
three are mandatory, links and wrong types are refused, and `content` may contain
only the declared resource directory. That directory may be absent after a known
rename step, but any unrelated root file or leftover staged HTML causes inspection
and preflight to refuse while preserving all evidence.

A separate child now completes preparation, releases its destination lock, writes
a readiness signal outside the export parent and is then forcibly terminated.
The already committed site remains inspectable, preflight classifies it as
`prepared`, and the old live Markdown/resource pair is unchanged. Together with
the Phase83 pre-manifest kill this distinguishes partial no-manifest evidence from
a complete committed recovery site. It still does not cover every instruction
between manifest commit and directory sync, simulate storage power loss, or execute
rollback/finalization.

Phase85 adds the first still-dormant mutation primitive, `executePlan`. Its caller
must supply the previously observed `RollBack` or `Finalize` plan; any other or
stale plan is refused. The executor acquires the conservative destination lock,
captures the recovery-directory identity, then rereads the strict manifest and
rehashes the complete live state before every rename or cleanup step. Directory
renames sync both source and destination parents. Before deleting a recognized
terminal site it syncs the selected old/new live pair, rereads and reclassifies
the evidence again, and requires the same terminal state.

All six frozen evidence states are exercised. Rollback can start from prepared,
old-resources-reserved, new-resources-installed, new-resources-returned or
old-resources-restored layouts; finalization accepts only markdown-committed.
First exports with or without resources, stale plans, external Markdown/resource
changes, unknown layout entries, active/dead locks and repeated completed calls
are covered. The returned audit outcome remains `NotRequested`: this primitive is
not the future `Confirm` orchestration and must not be wired to UI until durable
audit intent/result ordering exists. Executor force-termination checkpoints and
partial cleanup failures remain future work.

Phase86 compiles fault checkpoints only into `markdown-recovery-core`; the product
source path without `WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING` contains no waits or
readiness files. Real child processes are killed after the new-resource return
rename, after the old-resource restore rename, after terminal revalidation but
before cleanup, and after cleanup but before the recovery-parent sync. Readiness
signals are placed outside each export parent so observing a checkpoint does not
sync the directory whose boundary is under test.

The first three terminations retain a same-host/same-application dead lock and a
recognized recovery state. After the test explicitly removes only its own proven
dead lock, preflight reports returned-new, restored-old or markdown-committed and
`executePlan` resumes idempotently. The post-cleanup termination has no recovery
site and keeps the complete old live pair, but its dead lock conservatively blocks
a new export until explicit takeover exists. These are process-termination tests,
not storage power-loss proofs; default product logic still never removes a dead
lock.

Phase87 adds an explicit `takeOverConfirmedDeadDestinationLock` building block.
No ordinary inspect, preflight, preparation or execution path calls it. The
operation first classifies the lock, accepts only same-host/same-application
records whose PID is no longer running, opens the regular file without following
links, rechecks the owner, verifies the current device/inode still matches the
opened file, unlinks it and syncs the parent directory. Active, malformed,
foreign-host, link-shaped or changed locks are retained and refused.

Removing a confirmed-dead lock does not report a recovery outcome and does not
authorize file mutation. Tests rerun strict preflight after takeover before
calling `executePlan`. Both a retained recovery site and the Phase86-style orphan
lock with no recovery directory are covered. The latter can start a new export
only after explicit takeover, and that export still follows normal preparation
and rollback validation. Audit intent and actual user-confirmation orchestration
remain unimplemented, so the building block is still dormant.

Phase88 directly compiles the separate dormant `WizMarkdownExportRecoveryAudit`
intent writer. It uses the app-local `recovery-audit/v1` directory, 0700/0600
permissions, its own conservative cross-process lock, exact canonical three-field
confirm/requested JSONL, and whole-file atomic replacement plus file/directory
sync. Existing malformed, truncated, linked, over-limit or duplicate-field logs
are retained. Active and dead audit locks are never silently removed. Only this
test target redirects app-local data into a temporary root. The writer reports
Recorded only after explicit lock release and directory sync; a test-only release
failure verifies that already committed bytes are retained while Failed is
reported. No result events or rotation are implemented yet.

Phase90 fixes the older dormant destination-lock release result found during
review. Preparation, preflight and execution now report an independent
LockReleaseOutcome. Failed release prevents an actionable preflight plan or
Prepared result; already completed rollback/finalization remains accurately
reported together with the release failure. Test-only fault cases cover all
three operations and subsequent revalidation. Future orchestration must inspect
this field before reporting overall completion or entering another lock path.

Phase98 adds only the dormant result writer. `appendConfirmResult` accepts a
typed recovery result and records a finite none/rolled-back/finalized/busy/refused
event with the same exact three fields. Diagnostics, paths and note content are
never serialized. Failed or unknown destination unlock refuses before audit I/O;
completed recovery requires explicitly Released. Tests include an actual temporary
rollback followed by busy audit, result-only retry, audit-release failure and a
full log, without changing or rerunning the completed file operation. Existing
intent records remain valid; subsequent intents accept all finite result events.
The caller still owns sequencing: no confirmation orchestrator, rotation, UI,
accessibility, exit integration or App linkage is provided by this slice.

Phase100 adds bounded rotation to that still-dormant product audit writer. The
fixed live log plus .1/.2 archives each enforce 64 KiB/256 entries; every retained
segment is canonical, owned, regular, nonempty for archives and private. Unknown
same-prefix files and invalid segments refuse before history moves. Bytes and
file identity are rechecked before moves/removal. A full live segment with only
the .1 slot missing is the single known interrupted descending shift; retry must
not discard the already moved .2 segment again. Directory sync follows every
visible move/removal; private QSaveFile replacement, file/directory sync and
explicit unlock precede Recorded. These steps are not multi-file atomic.

The actual product code is exercised at 256-entry boundaries, exact chronological
retention, finite result rotation, privacy/permissions, invalid archived segments,
unknown files and explainable/unexplained gaps. Eight ordinary checkpoint failures
and seven real child-termination boundaries retain exact expected bounded event
prefixes. Active/dead locks block append throughout; in that phase only the test removed its own
identified terminated child's lock before validating continuation. There is no
product audit-lock takeover API or confirmation UI in the Phase100 slice. A post-commit Failed may
already have written an event: no exactly-once request or power-loss claim is made.
Own code is ASan/UBSan instrumented, Qt is not; leak detection remains disabled.
The separate `markdown-recovery-audit-production-compile` object target compiles
without any testing definition and is never App-linked; its object must contain
none of the test fault/hold/path-override environment-variable strings.

Phase101 adds the dormant `takeOverConfirmedDeadAuditLock` building block, never
called by ordinary append. Missing directories/locks remain absent; unsupported
directories or unknown/active/foreign/application-mismatched/linked locks remain
untouched. A same-host, same-app dead writer is revalidated with bounded lock
bytes, directory/file identity and ownership; an opened directory anchors the
unlink, and a nonblocking flock serializes cooperating explicit takeovers on the
old inode. Removed requires successful directory sync and is not recovery success
or a reservation for the next append. Failed after unlink may leave no lock.

The seven rotation kill cases now use this product primitive rather than Qt's
test-only stale-lock removal. Fifty added checks cover absent roots/no creation,
active/dead/idempotent behavior, corrupt-log refusal after removal, unknown locks,
three ordinary takeover failure boundaries and four real child snapshot windows
(replacement inode, changed bytes, newly active PID, renamed audit root). Each
snapshot window also rejects a concurrent takeover nonblockingly. Final465/465
passes five times under ASan/UBSan and contract46 passes. Test-only resume/failure
knobs must remain absent from the production-shaped object. This is not an atomic
compare-and-unlink guarantee against arbitrary external writers, a full system-call
fault matrix, or a power-loss/UI/App integration acceptance.

Phase102 directly compiles the dormant recovery coordinator. Only explicit
Confirm may enter audit takeover, durable intent, destination takeover, fresh
preflight, reacquired execution, and final audit in that order. Inspect is read
only; Cancel/Retain do no I/O and unknown decisions refuse. Every acquired target
operation, including dead-lock takeover, propagates its release state. An
unconfirmed intent prevents all target-lock changes; unconfirmed target release
prevents both execution (if not yet started) and final audit lock acquisition.
Completed recovery is never retried/reverted because final audit failed. The
final append is single-attempt and never takes over another audit writer's lock.

Target takeover now uses nonblocking flock on the old inode and explicit
LockTakeover.lockRelease, preventing overlapping cooperating removers and hiding
neither close nor removal-sync uncertainty. A competing full coordinator is Busy
while the first holds the old inode. Tests exercise all six recognized states,
repeat-none, nonconfirm/no-I/O, both dead locks, five intent-blocking cases,
takeover/preflight/execute release failures, and stale preflight after an external
edit. Actual child windows cover intent persistence, preflight, completed recovery,
execution cleanup, and target takeover. Three children are killed at intent,
execution, or takeover windows; only a fresh explicit confirmation resumes them.
During actual execution the target lock excludes preflight while an independent
audit append succeeds. Between coordinator operations both locks are absent.

Initial545/545 (and its five repeats) predates the additional takeover-release
review; final acceptance is **verified-1.log through verified-5.log**, each548/548,
ASan/UBSan, not the earlier final-*.log filenames. Contract46 passes; three
production-shaped objects (audit, core, coordinator) contain no test environment
knobs. There is no App linkage, UI, mid-operation cancel/exit integration, real
power-loss, complete syscall/partial-cleanup failure matrix, or exactly-once event
claim. This is an explicit same-target facade, not filesystem scanning or an
automatic startup recovery feature.

Phase103 adds terminal-cleanup failure coverage to the same dormant executor.
Testing-only operation gates cover terminal live-pair sync, before cleanup, after
one recovery entry has been removed, and before cleanup-parent sync after the root
is gone. Only execution cleanup enables per-entry checkpoints; preparation keeps
its existing behavior. Actual non-writable staged-directory unlink failure and
real child kills after a removed entry cover ordinary and interrupted cleanup.
Terminal old/new Markdown and resources remain exact; intact sites can revalidate,
partial sites strictly refuse and preserve their remainder, and absent sites retry
as None without claiming the earlier durability failure was repaired.

The original cleanup failure summary could claim retention after the root was
already deleted. It now distinguishes incomplete/unconfirmed cleanup and reports
current existence only in diagnostics, without claiming overall completion.
Refused does not mean an executing recovery made no prior authorized moves:
resource restoration may already have occurred before a later barrier fails.
No missing v1 evidence is reconstructed or silently deleted after partial cleanup.

Initial602 had9 failures: seven cleanup-status requirements plus two fixture
fingerprints that missed an empty-directory deletion (the old content-only digest
did not include directories). A structure+content snapshot fixes those two fixture
assertions; corrected reproduction602 still has7 status failures. The narrow
summary fix and two already-restored-resource cases yield final608/608 x5, with
contract46 passing. Use phase103 verified-*.log, not retained reproduction logs.
Production-shaped objects contain no test environment knobs. This is only the
first terminal-cleanup/sync group, not preparation-barrier, full syscall, power-loss,
UI or App acceptance; temporary test directory permissions are explicitly restored.

Phase104 closes the predefined finite preparation operation matrix: 12 nodes
cover recovery/content directory creation, Markdown commit/file-sync/dir-sync,
staged HTML removal, resource sync, manifest commit/file-sync/dir-sync, final root
sync and final parent sync. Each runs with an existing old pair and a first export;
failure cannot become Prepared, successful cleanup allows fresh strict preparation,
and the fresh site rolls back to the exact original present/absent pair. Three
paired preparation+cleanup failures distinguish intact, partial and absent sites;
FailedRetained means staging/cleanup/release is unconfirmed, not that a directory
necessarily still exists. Cleanup details and unlock warnings now state that
distinction. No public enum/field/layout or on-disk format is changed.

Actual staged-directory unlink denial restores the synthetic permissions exactly.
Six actual child-kill windows cover early creation, Markdown visibility, HTML
removal, manifest visibility, manifest directory sync and final parent sync.
Default paths keep their actual dead locks; explicit takeover still refuses
pre-manifest incompleteness, while a visible valid manifest can be strictly
revalidated after process termination without asserting true power-loss safety.
Four further ordinary failure points cover source/destination parent sync after
returning new resources or restoring old resources; exact bytes remain in their
recognized intermediate locations and fresh execution completes rollback.

Initial775 had8 failures: four new-fixture path errors derived identity before
creating a canonicalizable macOS temporary parent, plus four status requirements.
Create the parent first (as production already requires); corrected775 retains
only4 status failures. Narrow status fix plus16 rename-sync checks gives final791
x5 ASan/UBSan and contract46. Use phase104 verified-*.log; retain both negatives.
Three production-shaped objects contain no test environment knobs. Test-only
comma-separated failure names enable paired failures; there is no production
switch. These are bounded operation gates, not every syscall errno, controller
cache loss, power loss, all-metadata, UI, async cancel, exit or App acceptance.

Phase105 adds the actual dormant `WizMarkdownExportRecoveryPrompt` widget in a
separate `markdown-recovery-prompt` target. Its only application link dependencies
are Qt Widgets/Test (plus their Qt/platform dependencies), not recovery, audit,
coordinator, database or WebEngine objects. `markdown-recovery-prompt-production-compile`
also compiles the widget and moc without testing definitions or sanitizers. Neither
target adds anything to the App build. The harness forces `QT_QPA_PLATFORM=offscreen`
before QApplication construction and never opens a native Cocoa/account window.

The single-use prompt defaults to Retain, including Return/keypad Enter, Escape,
close, external accept/done and pending exit. Only the enabled explicit confirmation
button (mouse or focused Space) publishes Confirm, once. Focus does not transfer
the default. Twenty-five availability/lock combinations include unknown enums,
active/refused locks and explicitly checkable dead locks; the latter permit a
request, not a recovery success. Initial labels are fixed plain text. Diagnostic
paths are absent from the initially empty hidden read-only details field; explicit
view shows literal text, and hide clears it. Qt accessibility interface names,
roles, descriptions, read-only/disabled states and detail values are checked.

An initial owner-deletion observer on details textChanged crashed inside Qt's
text setter before any QPointer guard could regain control. This negative log is
retained. The private read-only update now suppresses its unneeded value-change
signal (no public editing signal contract); this prevents that reentry rather
than claiming arbitrary synchronous deletion inside Qt setters is supported.
Pending parent destruction discards context-bound queued work; finished handlers
can destroy the owner. Focus/hide exit reentry before publication downgrades to
Retain. A previously published Confirm is not revoked by the prompt: future host
coordination must handle cancellation/generation and completed file outcomes.

Final222/222 x5 ASan/UBSan and contract46 pass. Qt is uninstrumented, leak detection
is disabled, and offscreen propagateSizeHints capability diagnostics are retained.
The corrected build has no warnings. No backend file operation, durable decision
audit, actual worker cancellation, application quit policy or native VoiceOver
acceptance is implied. Phase104 core source and791-check executable hashes remain
unchanged; the last full product baseline is still Phase99 (19 groups/856 PASS).

Phase106 adds the actual dormant `WizMarkdownExportRecoveryRequest` lifetime gate,
not a background executor or application quit controller. GUI methods reuse the
existing weak document epoch plus QPointer owner. Only explicit current-context
Confirm produces one immutable-destination Work packet. Inspect stays pending;
cancel/retain/unknown decisions cannot dispatch. The host must invalidate before
navigation/derived teardown; decision and UI receipt also recheck the document
ticket. Do not rely solely on QPointer during derived destruction. Wrong-thread
GUI methods reject before reading owner/document state. Only Work crosses threads.

Queued cancellation and worker start compete through one atomic state. A worker
must successfully call tryStart immediately before the strict backend (not claim
then enqueue/wait again). Cancellation before that point retains; after it, no
forced stop, undo or false Retained result is manufactured. Completion is stored
once with release/acquire publication, independent of UI eligibility. Request or
owner destruction, stale generations and quit drop only the UI receipt; a retained
Work can still read exact recovery/audit/unlock fields after request destruction.
No local-note-save persistence acknowledgment uses this UI receipt.

The direct QtCore-only test target uses real QThreads and finite semaphore gates:
exit before/after start, GUI queue delivery while a started worker is held,
competing starts, wrong-thread rejection, old same-note generation, owner/request
destruction and actual context-bound queued receipts. Initial99 and first repeats
pass; after explicit header includes/comments the final build is verified by
**accepted-1.log through accepted-5.log**, each99/99 ASan/UBSan, contract46. The
production-shaped object has no testing definition/sanitizer; the executable has
no recovery/audit/coordinator/DB/Widgets/WebEngine symbols/dependencies. No build
warning or sanitizer error; Qt is uninstrumented and leaks are disabled.

The outcome used here is a synthetic Result value, not evidence of a filesystem
recovery. There is no thread scheduler/join owner, prompt adapter, durable retain
audit or App quit integration yet; beginQuit is only a nonblocking one-way request
gate. Real save/sync/three-second quit behavior is unchanged, not newly accepted.
Existing791-check core and222-check prompt binaries/sources remain unchanged.

Phase107 adds the actual dormant `WizMarkdownExportRecoverySession` adapter. It is
application-parented, uses the existing WizThreads DEFAULT/MAIN queues and startup
WizQueuedThreadsSession lifetime, and never creates another pool or performs note
saving/sync. Only the prompt's explicit decision can generate a request Work. A
single runnable claims immediately before the existing strict coordinator; its
destructor abandons only unstarted Work, including closed-queue rejection/discard.
Five added request checks give104/104 x5. The queue and core/audit implementations
are unchanged, and no App target is altered.

UI resultReady is current-context only. A separate settled notification lets an
application owner release the session even after navigation/quit suppresses the UI
receipt. The session lifetime ticket is canceled before derived destruction, not
on mere navigation; a main-queue callback may thus collect application cleanup
without updating a stale view. Result slots may destroy the session. If queue
shutdown discards delivery, the existing startup session still joins actual worker
execution, and a retained Work preserves the independent completed outcome.

The direct integration target includes actual prompt/request/coordinator/core/
audit/threads sources, QtWidgets/Test/Core5Compat, and forces offscreen before
QApplication. Fourteen isolated synthetic interrupted-resource fixtures exercise
seven no-backend cases and seven actual strict confirmations (six real rollbacks,
one external-edit refusal). It covers pending/queued/started quit, context change,
session deletion, a real competing final-audit lock, started recovery during queue
cleanup, queued-action discard, closed-queue rejection and exact private audit
intent/result events. The UI-deletion fixture clicks the real QPushButton signal
without a QTest delay event pump; native mouse/keyboard behavior remains Phase105.

Initial123 passes predate the additional application cleanup notification and
started-recovery shutdown case; final acceptance is **phase107 verified-{1..5}.log**,
each138/138, plus **request-verified-{1..5}.log**, each104/104; contract46 passes.
ASan/UBSan instrument own sources, not Qt; leaks are disabled. Existing eight
missing-override warnings per compilation of WizThreads_p.h and offscreen platform
diagnostics are retained, not fixed/suppressed or called new session warnings.
Separate production-shaped session/request objects compile without test macros or
sanitizers; no DB/WebEngine, account, original note or installed App is involved.

This is not main-window quit/local-save/final-sync/VoiceOver acceptance. beginQuit
does not join or reset a timeout; the existing final pool cleanup joins workers
and may wait for filesystem I/O, without asserting a new three-second I/O limit.
Same-target discovery and best-effort retain audit still need their host adapters;
then the default-disabled App wiring and full regression remain separate gates.

Phase108 adds the same-target asynchronous read-only discovery entry to the
dormant session. Construction does no target I/O; the first show submits exactly
one existing-queue action for inspect and inspectDestinationLock. The worker
uses immutable destination/shared snapshot state, never GUI owner/document state,
and never preflights, acquires locks, audits, prepares or executes recovery. An
abandoned queued action releases its claim; an already-running read may drain.
GUI delivery rechecks context both before and after discovered receivers run.
No-site+available emits advisory metadata and settles without popup or export;
other states use the existing conservative prompt. Lock reasons join the hidden
plain-text details only. Neither discovered nor settled authorizes a write.

Sixteen added synthetic cases cover queued responsiveness/repeated show, another
destination's site ignored, no-site no-write, incomplete metadata, actual active
lock, quit-before-show, queued cancellation/destruction, completed-read/stale-GUI
delivery, reentrant discovered receivers and a full discovery/confirm/rollback
round trip, plus closed-queue rejection. All30 cases and269 checks include the
earlier138 checks. Initial268/1 failed because stock Qt recorded executable name
markdown-recovery-session while the fixture expected custom display name
WizRecoverySessionOffscreen. The product correctly refused the mismatched writer.
Diagnostic evidence is retained; only the standalone process name is aligned,
with an explicit matching-writer assertion. No product identity/check is changed.

Final **phase108 accepted-{1..5}.log** each269/269,30 fixtures,0 failures;
contract46 passes. Earlier fixed/verified positives precede final comment-only
contract clarification and remain as evidence, not the final baseline. ASan/UBSan
own code, Qt uninstrumented/leaks off; existing thread-header override/offscreen
diagnostics remain. The request104, core791 and prompt222 sources/binaries remain
unchanged. No App hookup, retain audit, full-product regression or native
accessibility acceptance is claimed; no-site discovery is not a reservation for
later export. Do not scan startup/history or infer permission for another target.

The prototype still does not simulate storage-device power removal, controller
cache loss, unplug or filesystem corruption; define a shipped prompt/log/cancel
flow; fingerprint ACLs or every macOS metadata combination; or define a durable,
production-owned cache location, lifecycle and trust boundary. The current
product continues to use a random temporary directory and has no manifest/lock/
restart entry point. Do not integrate this model until those remaining durability,
metadata, performance and user-interaction questions are resolved.

Phase109 extends only the dormant audit/session with optional retention logging.
appendRetain emits the finite canonical retain/retained pair; confirm retains its
six events. All decisions share strict validation of all bounded retained segments,
private permissions, conservative locking and durable writes. No target I/O,
takeover, recovery or automatic retry is added. Old strict readers may refuse the
new event without changing history; no backward writer migration is asserted.

An ordinary current-context retain (including Escape/close) may enqueue one
identity-only audit action in the original pool and immediately emits settled.
Logging has no UI receipt and does not gate cleanup. beginQuit/invalidate never
start logging; queued logging is cancelled on invalidation/session destruction or
queue rejection/discard. Already-running audit I/O drains without owner access in
the original pool. beginQuit itself never joins; final existing pool shutdown can
wait on running filesystem I/O. There is no new hard deadline, save/sync call,
retry, timer or failure popup, and no guarantee of a persisted quit event.

Final **phase109 session-verified-{1..5}.log** each418/418,44 synthetic fixtures,
0 failures. The409/43 initial pass precedes actual retain-button and reentrant
settled-receiver destruction coverage. All prior discovery/confirmation cases
remain; ordinary Escape now expects only retain audit, not zero audit. Added
cases cover finite event pairing, mixed history rotation, private permissions,
invalid identity/history, active/malformed audit locks, four operation-level
append failures and unlock uncertainty, immediate retention on success/failure,
queued quit/navigation/destruction, reentrant cleanup, started logging after
owner destruction, and actual queue rejection/discard. Snapshots of recovery site
structure/bytes/Qt permissions remain exact; no full metadata/power-loss claim.

Shared writer changes also recompile/run the full existing **core791 x5**, all
passing, including finite failure/rotation/termination/coordinator matrices.
Contract adds four guardrails (finite event pairs and optional exit scheduling),
now50/50. ASan/UBSan own source; Qt uninstrumented/leaks disabled. Production-shaped
audit/session/moc compile without test macros or sanitizer; old thread-header
override and offscreen diagnostics remain. Prompt222/request104 sources and
binaries are unchanged, not rerun. No App hookup or whole-App save/sync/native
accessibility acceptance yet; next is default-disabled App/quit wiring and full
regression, without installing or replacing a candidate.

Phase110 compiles the App facade into the direct session target (now486 checks,
51 synthetic fixtures). It covers actual same-target discovery/confirmation,
no-site-only continuation, owner/document expiry, nested/cancelled/committed quit
scopes, and fresh locked ordinary-export validation. Recovery never automatically
retries the original export; settled only cleans up. MainWindow adds exactly three
conditional insertions, preserving original BOM/CRLF/save/sync bytes. No timers,
new sync, save deadline, queue/persistence-ack redesign or default enablement.

markdown-recovery-app-export extracts the very same real export method with the
switch ON and links actual facade/session/queues/coordinator/transaction. Only the
database extraction and result presentation are collaborators, not an actual
WebView-class/database end-to-end test. The real off-record WebEngine cases cover
late quit after no-site delivery but before JS reply, and refusing overwrite of
incomplete recovery evidence. An explicit discovery barrier avoids relying on
read/JS response timing. Final83 x5; original OFF source-flow67 x5 also passes.

markdown-recovery-gated-transaction runs the unchanged actual transaction corpus
with the target reservation ON. Both it and ordinary markdown-product-transaction
pass35 x5 with --stress (2048 files/8MiB,0600/file+directory xattrs preserved).
The initial accidentally supplied --stress-large-resources was unrecognized and
ran only the normal27 checks; that log is not stress acceptance. Existing core791
passes once; excluding the newly inserted gate, its source bytes are identical to
phase109, so the old finite matrix is not needlessly repeated five times.

Main App ON builds and passes temporary-DB/synthetic-ZIW89. The first OFF link
failed because AUTOMOC scanned experimental headers inside conditional includes;
explicit OFF SKIP_AUTOMOC fixes it. Final OFF builds, contains no recovery/moc
symbols, and passes the same89. Negative build evidence is retained. No test uses
normal startup/accounts, installed Apps, remote access or security test routes.
Own-source ASan/UBSan on standalone targets, Qt uninstrumented/leaks off; main App
is not sanitized. Existing compiler/offscreen diagnostics remain. Contract52 is
experimental-default-disabled. This is NOT whole-App quit/accessibility or complete
product regression; ordinary output still uses random staging, so persistent
installation/commit and all quit entrances remain in the existing integration
stage before full regression. No package, installation or automatic enablement.

## Phase111: default-disabled persistent publication

The ON transaction now calls the actual prepare/publish APIs. Preparation merges
old/unrelated resources using the existing snapshot helper under its target lock.
Only successful preparation with confirmed unlock returns an opaque process-local
one-attempt proof. Publication takes a fresh conservative lock and validates root
identity, frozen manifest and live hashes, installs resources, then commits body
last. Ordinary pre-commit failure cancels its QSaveFile and releases the lock before
proof-bound rollback; uncertain release stops. Post-commit failure never undoes
new content. No automatic lock takeover, audit, adoption or retry is added.

`markdown-recovery-publication` compiles actual transaction/core/coordinator/audit
with test-only fault hooks. Its final **117 checks x5** include the original27,
first exports with/without resources, private mode, one-attempt/stale/replaced
proof refusal, six pre-commit failure points with different AND identical body,
five post-commit sync/cleanup failures, release uncertainty, failed rollback sync,
and nine actual owned child interruption windows. Seven are the finite normal
publication boundaries; two additionally cover identical-body preparation and
installed resources. Explicit recovery may finalize a complete semantic new pair
even if identical body bytes alone cannot identify a separate rewrite. Known
pre-commit ordinary failure instead uses its proof to restore the old resources.

The core originally misclassified identical-body preparation with changed
resources as unknown; `same-body-repro.log` records794/1 before the narrow fix.
Final unmodified **core-serial-verified-{1..5}.log each794/0**. Concurrent run2
with build and other fsync tests missed three preexisting3-second readiness
windows (794/3); retained, not counted as a pass. No product timeout was changed.
Session486, ON/OFF extracted methods83/67 and actual stress transactions35/35 each
pass five times. Stress preserves2048 files/8MiB/0600/file+directory xattrs; ON
durability has additional cost, not a speedup or all-metadata/power-loss claim.

`PUBLICATION_CHILD_REMAINING` logs show Qt QSaveFile can leave random sibling
body temporaries after pre-commit SIGKILL. The v1 manifest does not own these
names; explicit recovery restores/finalizes the known body/resources and cleans
its recovery directory only. It deliberately never guesses which similarly named
files to delete. Synthetic cases and any temporary remnants are QTemporaryDir-owned.
OFF keeps the legacy transaction. The one-attempt proof and failure hooks are
not authority to recover another operation's site. All quit entrances and full
product/native acceptance remain pending; the feature is not enabled or packaged.

Main ON/OFF builds after the final CMake fix each pass89 isolated temporary-DB
checks; final OFF has no recovery moc/symbols. SKIP_AUTOMOC alone did not fix
incremental toggling: Ninja reused a previous gate's generated compilation.
An initial AUTOGEN_TARGET_DEPENDS filename experiment was only order-only here
and failed the pure ON transition; both failures remain in logs. The final public
AUTOGEN_BUILD_DIR setting separates ON/OFF output and passes actual complete
builds in both directions, without clearing caches or touching generated files.

## Phase112: actual quit entrances with isolated collaborators

`markdown-recovery-quit` extracts the complete current MainWindow event filter,
aboutToQuit slot, prepareForQuit and exit action. Only the owning class name,
critical-message collaborator name and synthetic sync signal reference change.
The sync substitute emits QThread's stock finished signal; no real sync/network,
database, account or actual MainWindow construction occurs. Save/saver/sync and
cleanup are explicit collaborators, while recovery/session/queues/core/audit are
actual source. This is entry-flow acceptance, not native Cocoa or real note-save
end-to-end acceptance. Original `cleanOnQuit` stays byte-identical and is not
executed by this target; the collaborator uses the existing real queue shutdown.

The old direct slot produces **12 checks/3 failures**: the pending prompt and App
gate were still open when cleanup began. The product fix is exactly five lines in
the experimental branch: create/commit the existing QuitScope before cleanOnQuit.
Removing that insertion recovers the Phase111 file SHA exactly, including original
BOM and all CRLF. No save/final-sync/input/queue logic was rewritten.

Final **138 checks x5**, nine fresh owned child processes per run, cover direct
QApplication exit, actual menu action, actual application Quit/Close filters,
first/late save failure, queued confirmation, started confirmation and queued
optional retain logging. Actual save-preparation event pumping delivers synthetic
GUI save receipts even while recovery exports are blocked. Both save barriers and
exactly one synthetic final sync remain; failed save reopens future explicit
requests without reviving old prompts or auto-retrying. Direct fallback adds no
save/sync/audit. Queued work is retained before start, running recovery finishes
with independent file/audit result during the existing worker join, no late UI.

Session486 and actual extracted export83 receive a single regression run because
their product modules are unchanged; the Phase111 core794/publication117 stress
matrices remain hash-bound evidence rather than needlessly repeated here.
Contract55 adds direct-exit ordering/no-extra-I/O requirements. Main ON/OFF build
and temporary-DB89 checks are recorded in the phase112 report. The next existing
stage is the full in-scope regression/final record, with network/security and
foreground-only routes still excluded or separately gated. No package/install.

Phase113 closes the executable offline plan: current product CTest15/15,
648 named PASS records plus the separate bootstrap-cancel boolean, with five
network/security/foreground routes explicitly excluded (not historical19-group
equivalence). Final current-source core794/publication117/prompt222/request104/
session486/quit138/App-export83/OFF-export67/gated35/ordinary35 each pass once;
contract55 passes. Prior repeated matrices remain separately identified.
Frozen PDF27 exact-link and18 raster checks pass read-only, with four visual
page inspections and current native bridge print cycles. No product change,
default enablement, package or install. See phase113 for outstanding native
Cocoa/VoiceOver and real-note gates; do not continue inventing new phases while
waiting for those prerequisites.
