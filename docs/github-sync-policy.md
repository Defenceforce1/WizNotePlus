# GitHub source synchronization policy

Authorized destination: `Defenceforce1/WizNotePlus`, development branch
`improve/compatibility-20261017`. Do not update the stable/default branch,
publish releases, upload installers, or force-push this development branch.

After a meaningful locally verified stage:

1. Read the current branch head and complete tree through the GitHub connector.
2. Review an explicit source/test/build-input allowlist against that tree and the
   accepted local checkpoint. Preserve unrelated files and pinned submodules.
3. Exclude raw local reports/logs, machine paths, user inputs, private data,
   credentials, real certificates/keys, binaries and generated build directories.
   Publish a separate privacy-reviewed progress/test summary instead.
4. Preserve exact source bytes, line endings, executable modes and licenses.
   Do not bulk-stage/reset the local checkout, which may have an older HEAD.
5. Create a tree and a child commit; verify the complete resulting tree against
   the planned overlay and unchanged base, then update the branch with an
   expected-head lease and no force. Re-read the head before retrying a conflict.
6. Read back the branch/tree and record the commit, tree and per-file hashes
   locally. Keep the previous development/test evidence unchanged.

GitHub source backup is the only standing network exception for this workflow;
it does not authorize note-service requests, live-account operations, telemetry
tests, security testing, public binary redistribution, or installation.
On failed verification or changed remote head, retain the local checkpoint and
report the unsynchronized state rather than overwriting another writer's work.
