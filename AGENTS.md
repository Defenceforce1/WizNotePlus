# WizNotePlus Apple Silicon — maintenance

User-confirmed frozen main baseline: **14.4.16**. Read-only inspection on 2026-10-17 now finds **14.4.17** in system Applications; the agent did not install it and this observation is not a claim of complete user acceptance. Develop the new 14.4.18 candidate separately, without replacing that installed copy. This is the Qt main-client repository; develop the independent clipper only in Defenceforce1/WizNotePlus-Clipper.

Phase 11 (same date) changes only memory diagnostics/tests and documentation, not product defaults or the delivered 14.4.18 binary. Real Cocoa shared-host tests release page/view objects and renderer processes, but Metal footprint still grows despite nearly flat RSS; a software-backend diagnostic control does not show the same growth. See docs/exploration-2026-10-17-phase11.md. Do not call this a fixed leak, equate RSS with footprint, clear user caches, or disable production hardware acceleration on this evidence alone. Preserve source/editing compatibility while investigating native graphics lifecycle.

Phase 12 adds retained-tab and stock Qt empty-view controls (400 transient opens, 346 checks). Stock views/pages without an app channel/editor also reproduce Metal footprint growth; software control does not. Quick resource release, native batch pools, current-context finish/cache controls do not stop it and must remain test-only. Do not infer a specific Qt/driver leak or a production fix; next isolate a fully standalone Qt reproducer. See docs/exploration-2026-10-17-phase12.md. Normal App defaults, installed app and delivered candidates remain unchanged.

2026-10-17 superseding scope: user authorized incremental reference navigation (including PDF export), Markdown/math rendering, performance/memory and stability improvements, strictly preserving editing habits and data compatibility. First freeze the working 14.4.16 as the rollback baseline; see docs/rollback-14.4.16.md and docs/compatibility-improvement-plan.md. Do not overwrite/delete the frozen App, DMG or source, migrate data, replace the installed App before candidate verification, or infer approval for WebDAV/AI/search/editor replacement. No product code was changed when recording this baseline. Older “paused” wording below describes the preceding housekeeping phase; clipper is now 1.1.0 and out of scope here.

Latest follow-up (2026-10-16): after the user eventually deletes the old Intel App, the deferred cleanup includes directory naming/data-storage migration as well as visible App naming. The user has NOT said that deletion or migration is already complete. Confirm target paths, back up and validate local unsynced notes before any future migration; do not silently reuse/merge stores or change bundle IDs. Project remains paused; this is a record only. User clarified that restarting the Mac fixed only the Safari share NAME; its ICON remains a placeholder. Keep icon display open in the independent clipper project, without changing the main app or repeating name/reboot experiments.

## Preserve the working application

- Do not rebuild, install, rename or launch an application merely for project cleanup. Keep only the system Applications installation; do not recreate the deleted personal Applications copy or launch the old Intel client.
- Preserve original main icon, bundle IDs, cloud protocol identity, accounts and separate data directories. Main icon SHA256: 367bd83f06671ddb8275f6eb7a6036f8d24990301390b8853a947dfd724ef6c1. Never install icon comparison variants.
- Preserve existing Markdown editing, CSS/math and theorem labels, original Save as PDF, note/folder transfer, personal tag filtering/Chinese IME, native window controls and auto-hiding tab close buttons.
- Keep local-save-first quit and one final sync attempt. Latest user clarification on 2026-10-17 supersedes the 14.4.17 ten-second rule: allow three seconds to connect/start, and stop the quit-time attempt after THREE continuous seconds without response/progress, with no retry. Once started, continue to completion while genuine progress keeps arriving within that window. Three seconds is NOT an overall upload deadline or a local-save deadline; unsent updates remain local for next launch. Keep original cleanup progress/cancel and explicit save-failure handling; no new exit preflight popup or automatic retry. Do not permanently change global network availability based on this quit-only cutoff.
- Keep obsolete 8849 pairing disabled and legacy 8848 compatibility. Do not recreate embedded clipper runtime/CI or remove existing group notes/databases/sync. Unneeded group-management web UI stays disabled.
- Main remains free ad-hoc signed and unnotarized. Do not imply public redistribution/license audit, future macOS or all-Mac compatibility is complete.

## Cleanup and backups

User's latest decision (2026-10-16): directly delete verified unused generated builds, obsolete icon variants and disposable build dependencies; prefer GitHub source/history over redundant local outputs. Preserve original artwork/user inputs, source and unique source snapshots, formal packages, immediate rollback and private data. Do not empty Trash or bypass protected directories. Private stores/logs/reports, accounts, keys and brand assets must not be published.

Use the GitHub connector, not browser or Git network commands. Never stage/reset all main-repository changes: this local development checkout deliberately has an earlier HEAD. Before cleanup, 2,207 remote entries matched and the only remote-tracked differences were two maintenance instruction files; product source was already backed up. Preserve unrelated/local/submodule changes.

Build SDKs/caches may be removed under this cleanup authorization after checking packaged apps do not depend on them; future main builds then require dependency setup again. Retain reproducible versions/workflows and original dependency source.

## Verification

No live-account login/upload, credential reading, note changes, forced quits or runtime stress tests during housekeeping. Use read-only hashes/signatures/bundle audits. Only use synthetic isolated stores for future authorized functional tests. Keep failure evidence honest.

Current index: [docs/project-maintenance.md](docs/project-maintenance.md).
Before a future build/UI/shutdown change read [docs/next-build-requests.md](docs/next-build-requests.md). Earlier instructions are historical, not new permissions.
