#!/usr/bin/env python3
"""Export only a caller-verified Git tree plus pinned local submodule commits.

No Git network, staging, reset, credentials, or blanket working-directory copy.
The input metadata must first be verified through the GitHub connector; this
tool checks local bytes, not server authenticity or redistribution clearance.
"""
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
import sys
import tarfile


def safe_name(name):
    value = PurePosixPath(name)
    if not name or name == "." or value.is_absolute() or value.as_posix() != name or any(
            part in ("..", ".git") for part in value.parts):
        raise ValueError("Unsafe source path")
    return name


def safe_link(name, target):
    joined = posixpath.normpath(posixpath.join(posixpath.dirname(name), target))
    if not target or target.startswith("/") or joined == ".." or joined.startswith("../"):
        raise ValueError("Source symlink escapes archive")


def blob_sha(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def collect(root, metadata):
    if metadata.get("truncated") is not False or not re.fullmatch(r"[0-9a-f]{40}", metadata.get("sha", "")):
        raise ValueError("Complete caller-verified tree required")
    records, modules, seen = [], [], set()
    for entry in metadata["tree"]:
        name = safe_name(entry["path"])
        if name in seen:
            raise ValueError("Duplicate tree entry")
        seen.add(name)
        file = root / name
        if not file.parent.resolve(strict=True).is_relative_to(root):
            raise ValueError("Source parent escapes repository")
        if entry["type"] == "tree" and entry["mode"] == "040000":
            continue
        if not re.fullmatch(r"[0-9a-f]{40}", entry["sha"]):
            raise ValueError("Invalid object identity")
        if entry["type"] == "commit" and entry["mode"] == "160000":
            if file.is_symlink() or not file.is_dir():
                raise ValueError("Physical submodule required")
            # Archive the pinned commit, not dirty submodule working files.
            data = subprocess.check_output(["git", "-C", str(file), "archive", "--format=tar", entry["sha"]])
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                for member in archive:
                    child = name + "/" + safe_name(member.name.rstrip("/"))
                    if member.isdir():
                        continue
                    if member.issym():
                        safe_link(child, member.linkname)
                        records.append((child, "120000", member.linkname.encode()))
                    elif member.isfile():
                        records.append((child, "100755" if member.mode & 0o111 else "100644",
                                        archive.extractfile(member).read()))
                    else:
                        raise ValueError("Unsupported submodule archive entry")
            modules.append({"path": name, "commit": entry["sha"], "dirtyWorkingFilesExcluded": True})
        elif entry["type"] == "blob" and entry["mode"] in ("100644", "100755", "120000"):
            if entry["mode"] == "120000":
                if not file.is_symlink():
                    raise ValueError("Expected source symlink")
                data = file.readlink().as_posix().encode()
                safe_link(name, data.decode())
            else:
                if file.is_symlink() or not file.is_file():
                    raise ValueError("Expected physical source file")
                data = file.read_bytes()
            if blob_sha(data) != entry["sha"]:
                raise ValueError("Local source differs from verified tree: " + name)
            records.append((name, entry["mode"], data))
        else:
            raise ValueError("Unsupported tree entry")
    if len({name for name, _, _ in records}) != len(records):
        raise ValueError("Overlapping source/submodule entries")
    return records, modules


def create(root_arg, tree_arg, commit, output_arg):
    root = Path(root_arg).resolve(strict=True)
    output = Path(output_arg)
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Exact caller-verified commit required")
    if not output.is_absolute() or output.exists() or output.is_symlink() or not output.name.endswith(".tar.gz"):
        raise ValueError("New absolute .tar.gz output required")
    if output.parent.resolve(strict=True).is_relative_to(root):
        raise ValueError("Source archive must be outside the source repository")
    metadata = json.loads(Path(tree_arg).read_text())
    records, modules = collect(root, metadata)
    info = {"schema": 1, "commit": commit, "tree": metadata["sha"], "sourceFiles": len(records),
            "submodules": modules, "localBytesMatchTree": True, "treeAuthenticityVerifiedByThisTool": False,
            "qtSourceBundled": False, "qtSourceInstructions": "docs/engine-build.md",
            "privateWorkingFilesIncluded": False, "redistributionClearanceClaimed": False}
    if any(name == "SOURCE-RECORD.json" for name, _, _ in records):
        raise ValueError("Source record name already exists")
    records.append(("SOURCE-RECORD.json", "100644", (json.dumps(info, indent=2) + "\n").encode()))
    # Normalize tar/gzip metadata; exclusive create protects earlier archives.
    with output.open("xb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w|") as archive:
            for name, mode, data in sorted(records):
                item = tarfile.TarInfo("WizNotePlus-source/" + name)
                item.mode = 0o755 if mode == "100755" else 0o644
                if mode == "120000":
                    item.type = tarfile.SYMTYPE
                    item.linkname = data.decode()
                    archive.addfile(item)
                else:
                    item.size = len(data)
                    archive.addfile(item, io.BytesIO(data))
    print(json.dumps(info))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("Usage: archive-source.py REPOSITORY VERIFIED_TREE_JSON VERIFIED_COMMIT NEW_ARCHIVE.tar.gz")
    create(*sys.argv[1:])
