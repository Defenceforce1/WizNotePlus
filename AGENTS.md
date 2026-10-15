# WizNotePlus Apple Silicon — maintenance

Current personal-use main release: **14.4.16**. The user confirmed normal use. This is the Qt main-client repository; develop the independent clipper only in Defenceforce1/WizNotePlus-Clipper.

## Preserve the working application

- Do not rebuild, install, rename or launch an application merely for project cleanup. Keep only the system Applications installation; do not recreate the deleted personal Applications copy or launch the old Intel client.
- Preserve original main icon, bundle IDs, cloud protocol identity, accounts and separate data directories. Main icon SHA256: 367bd83f06671ddb8275f6eb7a6036f8d24990301390b8853a947dfd724ef6c1. Never install icon comparison variants.
- Preserve existing Markdown editing, CSS/math and theorem labels, original Save as PDF, note/folder transfer, personal tag filtering/Chinese IME, native window controls and auto-hiding tab close buttons.
- Keep local-save-first quit, one final bounded sync attempt (five-second network budget), original cleanup progress/cancel and explicit save-failure handling. No new exit preflight popup; no infinite network retries.
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
