#!/usr/bin/env python3
"""Auditor controls against retained synthetic PDFs; never launch or rewrite PDFs."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if len(sys.argv) != 4:
    raise SystemExit("Usage: audit-equation-regression.py ORIGINAL_MARKED CANDIDATE_MARKED NEW_REPORT")
original, candidate = (Path(value).resolve(strict=True) for value in sys.argv[1:3])
output = Path(sys.argv[3])
if output.exists() or output.is_symlink():
    raise ValueError("Never replace a report")
tool = Path(__file__).with_name("audit-equation-geometry.py")
names = ("native-pdf-results.json", "references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf")


def inventory():
    return {(str(folder), name): hashlib.sha256((folder / name).read_bytes()).hexdigest()
            for folder in (original, candidate) for name in names}


before = inventory()
results = []
with tempfile.TemporaryDirectory(prefix="wiz-equation-auditor-") as temporary:
    root = Path(temporary)

    def check(label, source, change=None, expected=1, remove=None, existing=False):
        folder = root / str(len(results))
        folder.mkdir()
        for name in names:
            shutil.copyfile(source / name, folder / name)
        manifest = json.loads((folder / names[0]).read_text())
        if change:
            change(manifest)
        (folder / names[0]).write_text(json.dumps(manifest))
        if remove:
            (folder / remove).unlink()
        report = folder / "audit.json"
        if existing:
            report.write_text("preserve existing audit\n")
        process = subprocess.run([sys.executable, str(tool), str(folder), "--require-accurate", "--output", str(report)],
                                 capture_output=True, text=True)
        passed = (process.returncode == 0) if expected == 0 else (process.returncode != 0)
        if expected == 0:
            passed = passed and json.loads(report.read_text())["positionChecksPassed"] is True
        if existing:
            passed = passed and report.read_text() == "preserve existing audit\n"
        results.append({"case": label, "passed": passed, "exitCode": process.returncode})
        if not passed:
            raise AssertionError(label + ": " + process.stdout + process.stderr)

    check("candidate exact coordinates", candidate, expected=0)
    check("original offset negative", original)
    check("source changed", candidate, lambda m: m.update(sourceAfter=False))
    check("HTML changed", candidate, lambda m: m.update(htmlSame=False))
    check("remote request", candidate, lambda m: m.update(blockedRequests=1))
    check("missing explicit marker", candidate, lambda m: m.pop("equationGeometryMarker"))
    check("moved geometry", candidate, lambda m: m["equationGeometryMarker"]["rectAfter"].update(x=0))
    check("nonfinite geometry", candidate, lambda m: m["equationGeometryMarker"]["rectAfter"].update(x=float("nan")))
    check("degenerate bbox", candidate, lambda m: m["equationGeometryMarker"]["bbox"].update(width=0))
    check("absent target", candidate, lambda m: m["equationGeometryMarker"].update(id="mjx-eqn-absent"))
    check("coordinate compensation rejected", candidate, lambda m: m.update(transformVariant=True))
    check("restore incomplete", candidate, lambda m: m["eventLinks"].update(restores=2))
    check("active print", candidate, lambda m: m["eventLinks"].update(active=True))
    check("missing PDF", candidate, remove="references-20mm.pdf")
    check("never overwrite audit", candidate, existing=True)

unchanged = before == inventory()
if not unchanged:
    raise AssertionError("Original corpus changed")
with output.open("x") as stream:
    json.dump({"checks": results, "count": len(results), "inputsUnchanged": unchanged,
               "pdfsRewritten": False, "engineAcceptance": False}, stream, indent=2)
    stream.write("\n")
print(f"{len(results)} auditor controls passed; retained PDFs/manifests unchanged; not engine acceptance")
