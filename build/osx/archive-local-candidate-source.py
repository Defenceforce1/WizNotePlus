#!/usr/bin/env python3
"""Create a local candidate source snapshot from a verified prior archive plus
an explicit reviewed overlay. No network, Git staging, or recursive worktree copy.
This never claims that local changes have been published or remotely verified.
"""
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import sys
import tarfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe(name):
    value = PurePosixPath(name)
    if value.is_absolute() or value.as_posix() != name or any(
            part in ("..", ".git", ".DS_Store") for part in value.parts):
        raise ValueError("Unsafe source path: " + name)
    return name


def create(base_arg, repo_arg, overlay_arg, output_arg):
    base, root = Path(base_arg).resolve(strict=True), Path(repo_arg).resolve(strict=True)
    metadata = json.loads(Path(overlay_arg).read_text())
    output = Path(output_arg)
    if not output.is_absolute() or output.exists() or output.is_symlink():
        raise ValueError("New absolute output required")
    if sha(base.read_bytes()) != metadata["baseArchiveSha256"]:
        raise ValueError("Prior verified source archive changed")
    records = {}
    with tarfile.open(base) as archive:
        for member in archive:
            parts = member.name.split("/", 1)
            if parts[0] != "WizNotePlus-source" or len(parts) != 2:
                raise ValueError("Unexpected baseline archive root")
            name = safe(parts[1])
            if member.isdir() or name in ("AGENTS.md", "SOURCE-RECORD.json"):
                continue
            if member.isfile():
                records[name] = (member.mode & 0o777, archive.extractfile(member).read(), None)
            elif member.issym():
                target = (PurePosixPath(name).parent / member.linkname).as_posix()
                if member.linkname.startswith("/") or ".." in PurePosixPath(target).parts:
                    raise ValueError("Unsafe baseline source symlink")
                records[name] = (0o777, None, member.linkname)
            else:
                raise ValueError("Unsupported baseline entry")
    overlay = metadata["files"]
    if len(overlay) != len(set(overlay)):
        raise ValueError("Duplicate overlay source")
    for name in overlay:
        safe(name)
        if not name.startswith(("src/", "share/", "tests/", "docs/", "build/", "cmake/")) and name != "CMakeLists.txt":
            raise ValueError("Overlay outside source allowlist")
        file = root / name
        if file.is_symlink() or not file.is_file() or not file.resolve().is_relative_to(root):
            raise ValueError("Physical reviewed source required")
        data = file.read_bytes()
        records[name] = (0o755 if file.stat().st_mode & 0o111 else 0o644, data, None)
    # Existing product inputs outside the explicit overlay must still match.
    # Pinned submodules are from the baseline; their tracked CMake patch is
    # applied at configure time, as in that release's source instructions.
    for name, (_, data, link) in records.items():
        if name.startswith("src/libs/3rdparty/") or name in overlay:
            continue
        if name.startswith(("src/", "share/", "cmake/", "build/")):
            file = root / name
            if link is None and (not file.is_file() or file.read_bytes() != data):
                raise ValueError("Unreviewed product source difference: " + name)
    inventory = [{"path": name, "sha256": sha(data) if data is not None else None,
                  "symlink": link, "mode": oct(mode)}
                 for name, (mode, data, link) in sorted(records.items())]
    source_record = {"schema": 1, "kind": "local-candidate-snapshot", "version": metadata["version"],
                     "baseArchiveSha256": metadata["baseArchiveSha256"],
                     "remoteCommitClaimed": False, "reviewedOverlay": overlay,
                     "privateWorktreeCopied": False, "maintenanceInstructionsExcluded": True,
                     "files": inventory}
    records["SOURCE-RECORD.json"] = (0o644, (json.dumps(source_record, indent=2) + "\n").encode(), None)
    with output.open("xb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w|") as archive:
            for name, (mode, data, link) in sorted(records.items()):
                item = tarfile.TarInfo("WizNotePlus-source/" + name)
                item.mode = mode
                if link is not None:
                    item.type, item.linkname = tarfile.SYMTYPE, link
                    archive.addfile(item)
                else:
                    item.size = len(data)
                    archive.addfile(item, io.BytesIO(data))
    with tarfile.open(output) as archive:
        for member in archive:
            name = member.name.split("/", 1)[1]
            if name in records and member.isfile() and archive.extractfile(member).read() != records[name][1]:
                raise ValueError("Archive verification failed")
    print(json.dumps({"version": metadata["version"], "files": len(records), "overlayFiles": len(overlay),
                      "sha256": sha(output.read_bytes()), "bytes": output.stat().st_size,
                      "kind": "local-candidate-snapshot", "remoteCommitClaimed": False}))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("Usage: archive-local-candidate-source.py BASE_ARCHIVE REPO OVERLAY_JSON NEW_ARCHIVE")
    create(*sys.argv[1:])
