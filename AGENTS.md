# WizNotePlus Apple Silicon project handoff

Before the next app build or UI/shutdown change, read `docs/next-build-requests.md` completely. It records the user's latest requirements and overrides older release choices where they conflict.

Preserve user notes, account compatibility, the current Markdown editing workflow, CSS/math rendering and the original Save as PDF. Do not launch the old Intel WizNote client or replace the working old web clipper unless necessary and explicitly within the task. Use synthetic accounts and dedicated temporary stores for tests, with exact supported self-test flags; never let a test fall into normal account startup.

Current scope is personal use on multiple Macs, not App Store or public binary distribution. Do not require paid membership, Developer ID or notarization merely as a default prerequisite for local work; check actual extension signing and capability requirements. Never request passwords or signing private keys in chat. Preserve the user's existing data and unrelated worktree changes.
