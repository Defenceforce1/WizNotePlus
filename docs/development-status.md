# Personal-use development checkpoint: phase 146

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

## Latest local verification

Phase 146: **26/26 permitted regression groups; 1,687 PASS, 0 FAIL** in 211.51s.
Six focused groups repeated five times: **4,765 PASS, 0 FAIL**. Both runs verify
the retained renderer runtime's actual loaded mapping. The routing corpus has
613 checks; the original-source control failed 8 assertions normally in the
initial 602-check corpus, which passed unchanged after the fix. A separate
default-SDK compatibility run is retained, not substituted for retained-runtime
acceptance. An initial missing helper declaration in the new test fixture was
corrected before execution; the failed build is retained locally.

These are isolated local tests, not GitHub CI results or complete native UI /
system-authentication acceptance. Some page, diagnostic, clipboard-operation and
transport collaborators are synthetic. The current routine excludes spellcheck,
native clipper, shutdown, network-security and standalone-startup groups; historical
results for those groups are not silently counted as current acceptance.

## Remaining bounded work

1. Link-window continuations; attachment clipboard and find/selection lifecycles;
   synthetic-only screenshot orchestration. Native file-chooser and operating-
   system clipboard acceptance remain distinct from the new synthetic checks.
2. Actual navigation, initial read/snapshot/output and isolated main-window
   integration, including external-editor return/save behavior.
3. Explicit biometric opt-in/enrollment/conflict/forget-local-memory management.
4. Actual system-session/lock notifications and save-first whole-application quit.
5. User-configured lawful signing for native Touch ID/Keychain acceptance, and an
   authoritative first-certificate-registration service contract.

No password-recovery backdoor is intended. Forgetting local biometric memory must
not delete certificates, backups or notes. Known-password permanent decryption
requires explicit confirmation and original/backup preservation; real notes are
not converted automatically.

## Synchronization record

The reviewed file manifest is [checkpoints/2026-10-21-phase146.json](checkpoints/2026-10-21-phase146.json).
GitHub commit history records the uploaded checkpoint. Raw logs, private local
phase reports, user documents, credentials, certificates and binaries are excluded.
See [GitHub synchronization policy](github-sync-policy.md).
