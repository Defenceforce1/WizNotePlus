#!/usr/bin/env python3
"""Synthetic source-export controls; no network or real project mutation."""
import copy
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

tool = Path(__file__).resolve().parents[2] / "build/osx/archive-source.py"
spec = importlib.util.spec_from_file_location("source_archive", tool)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class ArchiveGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="WizSourceExport-")
        self.root = Path(self.temp.name)
        self.repo = self.root / "repository"
        self.repo.mkdir()
        (self.repo / "source.cpp").write_bytes(b"synthetic source\n")
        (self.repo / "not-in-tree.txt").write_bytes(b"untracked private fixture, exclude me\n")
        self.tree = {"sha": "a" * 40, "truncated": False, "tree": [
            {"path": "source.cpp", "type": "blob", "mode": "100644",
             "sha": export.blob_sha(b"synthetic source\n")} ]}
        self.metadata = self.root / "tree.json"
        self.output = self.root / "source.tar.gz"

    def tearDown(self):
        self.temp.cleanup()

    def create(self, tree=None, output=None):
        self.metadata.write_text(json.dumps(self.tree if tree is None else tree))
        export.create(str(self.repo), str(self.metadata), "b" * 40, str(output or self.output))

    def test_exact_export_excludes_untracked(self):
        self.create()
        with tarfile.open(self.output) as archive:
            self.assertEqual(set(archive.getnames()), {
                "WizNotePlus-source/source.cpp", "WizNotePlus-source/SOURCE-RECORD.json"})
            self.assertEqual(archive.extractfile("WizNotePlus-source/source.cpp").read(), b"synthetic source\n")
        self.assertTrue((self.repo / "not-in-tree.txt").exists())

    def test_deterministic_archive(self):
        self.create()
        second = self.root / "second.tar.gz"
        self.create(output=second)
        self.assertEqual(self.output.read_bytes(), second.read_bytes())

    def test_changed_bytes_rejected_before_writes(self):
        (self.repo / "source.cpp").write_text("modified")
        with self.assertRaises(ValueError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_existing_archive_preserved(self):
        self.output.write_bytes(b"preserve")
        with self.assertRaises(ValueError):
            self.create()
        self.assertEqual(self.output.read_bytes(), b"preserve")

    def test_truncated_tree_rejected(self):
        self.tree["truncated"] = True
        with self.assertRaises(ValueError):
            self.create()

    def test_duplicate_path_rejected(self):
        self.tree["tree"].append(copy.deepcopy(self.tree["tree"][0]))
        with self.assertRaises(ValueError):
            self.create()

    def test_unsafe_paths(self):
        for path in (".", "..", "../escape", "/absolute", "x/../../escape", ".git/config", "x//y"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                export.safe_name(path)

    def test_external_parent_rejected(self):
        (self.repo / "alias").symlink_to(self.root, target_is_directory=True)
        self.tree["tree"][0]["path"] = "alias/tree.json"
        with self.assertRaises(ValueError):
            self.create()

    def test_file_alias_rejected(self):
        (self.repo / "source.cpp").unlink()
        (self.repo / "source.cpp").symlink_to("not-in-tree.txt")
        with self.assertRaises(ValueError):
            self.create()

    def test_safe_source_symlink_preserved(self):
        (self.repo / "alias").symlink_to("source.cpp")
        self.tree["tree"].append({"path": "alias", "type": "blob", "mode": "120000",
                                    "sha": export.blob_sha(b"source.cpp")})
        self.create()
        with tarfile.open(self.output) as archive:
            member = archive.getmember("WizNotePlus-source/alias")
            self.assertTrue(member.issym())
            self.assertEqual(member.linkname, "source.cpp")

    def test_escaping_symlink_rejected(self):
        (self.repo / "alias").symlink_to("../outside")
        self.tree["tree"].append({"path": "alias", "type": "blob", "mode": "120000",
                                    "sha": export.blob_sha(b"../outside")})
        with self.assertRaises(ValueError):
            self.create()

    def test_in_repository_output_rejected(self):
        with self.assertRaises(ValueError):
            self.create(output=self.repo / "source.tar.gz")


if __name__ == "__main__":
    unittest.main(verbosity=2)
