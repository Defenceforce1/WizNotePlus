#!/usr/bin/env python3
"""Portable filesystem guard tests, NOT Qt/GPU/package acceptance."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

tool = Path(__file__).resolve().parents[2] / "build/osx/engine-candidate.py"
spec = importlib.util.spec_from_file_location("engine_candidate", tool)
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


class Guards(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="WizEngineGuard-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def framework(self, name="Example.framework"):
        root = self.root / name
        (root / "Versions/A").mkdir(parents=True)
        (root / "Versions/A/Example").write_bytes(b"synthetic bytes, not executable")
        (root / "Versions/Current").symlink_to("A", target_is_directory=True)
        (root / "Example").symlink_to("Versions/Current/Example")
        return root

    def test_relocatable_inventory(self):
        self.assertEqual(candidate.inventory(self.framework()), candidate.inventory(self.framework("Other.framework")))

    def test_runtime_data_change_detected(self):
        root = self.framework()
        before = candidate.inventory(root)
        (root / "Versions/A/Example").write_bytes(b"different synthetic bytes")
        self.assertNotEqual(before, candidate.inventory(root))

    def test_build_headers_not_deployed(self):
        root = self.framework()
        before = candidate.inventory(root)
        (root / "Headers").mkdir()
        (root / "Headers/build.h").write_text("synthetic metadata")
        (root / "unused.prl").write_text("synthetic metadata")
        self.assertEqual(before, candidate.inventory(root))
        self.assertNotEqual(before, candidate.inventory(root, deployable=False))

    def test_external_alias_rejected(self):
        root = self.framework()
        other = self.root / "outside"
        other.write_text("owned test fixture")
        (root / "External").symlink_to("../outside")
        with self.assertRaises(ValueError):
            candidate.inventory(root)

    def test_absolute_internal_alias_rejected(self):
        root = self.framework()
        (root / "Absolute").symlink_to(root / "Versions/A/Example")
        with self.assertRaises(ValueError):
            candidate.inventory(root)

    def test_alias_to_omitted_header_rejected(self):
        root = self.framework()
        (root / "Headers").mkdir()
        (root / "Headers/file").write_text("fixture")
        (root / "Alias").symlink_to("Headers/file")
        with self.assertRaises(ValueError):
            candidate.inventory(root)

    def test_framework_root_alias_rejected(self):
        root = self.framework()
        alias = self.root / "Alias.framework"
        alias.symlink_to(root, target_is_directory=True)
        with self.assertRaises(ValueError):
            candidate.inventory(alias)

    def test_relative_output_rejected(self):
        with self.assertRaises(ValueError):
            candidate.new_path("relative-output")

    def test_existing_output_unchanged(self):
        file = self.root / "existing"
        file.write_text("preserve me")
        with self.assertRaises(ValueError):
            candidate.new_path(str(file))
        self.assertEqual(file.read_text(), "preserve me")

    def test_dangling_output_alias_rejected(self):
        file = self.root / "alias"
        file.symlink_to("missing")
        with self.assertRaises(ValueError):
            candidate.new_path(str(file))

    def package_rejected(self, output):
        base, runtime = self.root / "base.app", self.root / "runtime"
        base.mkdir(exist_ok=True)
        runtime.mkdir(exist_ok=True)
        with self.assertRaises(ValueError):
            candidate.package(str(base), str(runtime), str(self.root / "unused.json"), str(output))
        self.assertFalse(output.exists())

    def test_applications_output_rejected_before_writes(self):
        output = Path("/Applications") / self.root.name / "WizNotePlus Apple Silicon.app"
        self.package_rejected(output)
        self.assertFalse(output.parent.exists())

    def test_applications_parent_alias_rejected(self):
        alias = self.root / "applications-alias"
        alias.symlink_to("/Applications", target_is_directory=True)
        self.package_rejected(alias / self.root.name / "WizNotePlus Apple Silicon.app")

    def test_nested_input_output_rejected(self):
        output = self.root / "base.app/recursive/WizNotePlus Apple Silicon.app"
        self.package_rejected(output)
        self.assertFalse(output.parent.exists())

    def test_wrong_source_module_rejected_before_output(self):
        source, runtime = self.root / "source", self.root / "runtime"
        source.mkdir()
        runtime.mkdir()
        (source / ".tag").write_text("not-the-reviewed-module")
        output = self.root / "new/report.json"
        with self.assertRaises(ValueError):
            candidate.record(str(source), str(runtime), str(output))
        self.assertFalse(output.parent.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
