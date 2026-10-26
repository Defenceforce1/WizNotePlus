#!/usr/bin/env python3
"""Read-only arm64/dependency audit. This is not a GUI or notarization test."""
import os
import pathlib
import json
import re
import runpy
import subprocess
import sys

if len(sys.argv) != 2:
    print("Usage: check-bundle.py APP_BUNDLE", file=sys.stderr)
    sys.exit(2)
app = pathlib.Path(sys.argv[1]).resolve()
frameworks = app / "Contents/Frameworks"
main_dir = app / "Contents/MacOS"
if not app.is_dir() or not frameworks.is_dir() or not (main_dir / "WizNotePlus").is_file():
    print(f"Invalid or incomplete application bundle: {app}", file=sys.stderr)
    sys.exit(1)
component_build = pathlib.Path(__file__).resolve().parents[1]
component_lock = json.loads((component_build / 'components.lock.json').read_text())
component_api = runpy.run_path(str(component_build / 'verify-components.py'))
try:
    qt_version = component_api['qt_framework_version'](frameworks / 'QtCore.framework')
    component_errors, _ = component_api['verify'](
        component_lock, resource_root=app / 'Contents/Resources/files', qt_version=qt_version)
except (OSError, ValueError) as error:
    component_errors = ['Component metadata unavailable: {}'.format(error)]
if component_errors:
    for error in component_errors:
        print(error, file=sys.stderr)
    sys.exit(1)
magic = {bytes.fromhex(v) for v in ("feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
seen = set()
failures = []
for directory, _, names in os.walk(app):
    for name in names:
        binary = pathlib.Path(directory, name).resolve()
        if binary in seen or not binary.is_file():
            continue
        with binary.open("rb") as file:
            if file.read(4) not in magic:
                continue
        seen.add(binary)
        if subprocess.run(["lipo", str(binary), "-verify_arch", "arm64"], capture_output=True).returncode:
            failures.append(f"No arm64 slice: {binary.relative_to(app)}")
            continue
        listing = subprocess.check_output(["otool", "-L", "-arch", "arm64", str(binary)], text=True)
        for line in listing.splitlines():
            match = re.match(r"\s+(.+?) \(compatibility version", line)
            if not match:
                continue
            dependency = match[1]
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            candidates = []
            if dependency.startswith("@rpath/"):
                candidates = [frameworks / dependency[7:], app / "Contents/lib" / dependency[7:]]
            elif dependency.startswith("@loader_path/"):
                candidates = [binary.parent / dependency[13:]]
            elif dependency.startswith("@executable_path/"):
                candidates = [main_dir / dependency[17:], binary.parent / dependency[17:]]
            if not any(path.exists() for path in candidates):
                failures.append(f"Unresolved/nonportable: {binary.relative_to(app)} -> {dependency}")
if not seen:
    failures.append("No Mach-O executable was inspected; refusing an empty audit")
print(f"Checked {len(seen)} Mach-O files; {len(failures)} architecture/dependency failures")
for failure in failures:
    print(failure)
sys.exit(bool(failures))
