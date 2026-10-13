# Experimental note/folder import and export

Scope: personal notes, batch selection and populated folders (including subfolders).
No whole-library backup or new Markdown conversion. The existing export menu is retained.

## UI and data behaviour

In File, choose **导出原始笔记/文件夹…**, check notes or folder subtrees, then save a
new `.wiznotes` file. All selected notes and attachments must already be downloaded.
The exporter refuses an existing destination instead of replacing it. Saving open
editors is requested first, with bounded waits for editor callbacks and disk writes.
Both full and quick sync must be idle and are paused during the operation.

**导入原始笔记包…** imports into the current personal account after confirmation.
It creates `/导入-date-random/`, retains original relative note locations and assigns
fresh document/attachment IDs. Existing notes are not replaced. Reimporting creates
another set of copies. Imported notes are marked as local changes and can subsequently
sync to that account's cloud; use an expendable account first.

The entire archive and note metadata are checked before database writes. Runtime
failure rolls back the current new note and its attachments/parameters; preceding
completed notes remain, and the dialog reports their count. Newly created tag/style
definitions or thumbnails may remain after a late failure. This is not a transaction
over the entire import and does not promise atomicity during a crash or power loss.

## Format version 1

This is a WizNotePlus-specific ZIP envelope, **not an official Wiz interchange format**.
Do not promise direct import by official Wiz clients or arbitrary other note software.
No target-app interoperability has been established for this envelope.

- `manifest.json`: `format=WizNotePlus-native-notes`, `version=1`, note metadata,
  payload paths, sizes and SHA-256 hashes.
- `notes/N/note.ziw`: unchanged original native container bytes.
- `notes/N/attachments/M.bin`: unchanged original attachment bytes; original names
  and metadata are in the manifest. Attachments are not automatically linked into HTML.
- For unencrypted notes, `notes/N/html/index.html` and original resource files are
  included without HTML-to-Markdown conversion or content rewriting. This is a
  migration starting point, not a verified other-app importer.
- Retained fields include title, relative folder location, URL, author, keywords,
  type/file type, timestamps, note parameters (including flags/rating), hierarchical
  tags and style colours/bold. Imported tags/styles use a separate import namespace.
- Encrypted ZIWR notes are copied without decryption and do not get an HTML view.
  Reimport requires the same public certificate fingerprint in the target account.
  No account database, password, private certificate key, cookies or auth token is
  explicitly exported. Note contents and attachments can themselves contain secrets.

Keep the package private. It is not password-protected as a whole: metadata and any
unencrypted notes/attachments are readable. SHA-256/CRC detect corruption; they do not
authenticate the author. Only import trusted packages. Original HTML can contain
scripts and external links, and has not been sanitised by this feature.

## Bounds and exclusions

- At most 10,000 notes; at most 100,000 ZIP entries, 256 MiB per file,
  8 GiB total uncompressed payload and a 16 MiB manifest. The HTML view duplicates
  resources, so export may need substantially more temporary disk space.
- Unsafe relative paths, symlinks, case/Unicode-normalisation duplicate ZIP names,
  unexpected payloads, checksum mismatches and invalid note/attachment metadata are
  rejected. Some unusual existing filenames/locations/parameter names are unsupported.
- Empty folders, deleted items, team/collaborative notes, edit history, sharing and
  server-side state are not exported. This is not a full account/library backup.
- Internal links containing old document/attachment IDs remain unchanged in the
  raw native bytes; their targets are not remapped to the imported copies.
- Imports generate new IDs and container filenames. They do not restore server
  identities or merge/deduplicate notes. Folder-relative paths keep their original
  full hierarchy beneath the new import root, rather than being flattened.
- Large operations run synchronously; progress/cancellation and large-library stress
  coverage are not yet implemented. Begin with a few disposable notes.

## Verification

Automated offscreen tests use only a fresh temporary personal database and synthetic
payloads, bypassing normal login, sockets, cloud sync and real user data. They cover
exact ZIW/HTML/image/attachment bytes, metadata/timestamps, tags, parameters, colours,
fresh IDs, repeated imports, missing attachments, invalid metadata, corrupt ZIP/CRC,
hash mismatches, unsafe paths, duplicate names, links, and injected database failures.
The encryption test uses an ephemeral certificate and the unchanged legacy ZIWR
format; it is not an audit of the legacy cryptography or proof for all real encrypted notes.

Manual menu selection, Cocoa dialogs, dirty-editor export, reopened imported notes,
syncing imported copies, large imports, crash recovery and official/other-client
interoperability still need verification. The app remains experimental and ad-hoc signed.
