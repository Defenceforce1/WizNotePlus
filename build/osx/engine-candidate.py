#!/usr/bin/env python3
"""Record pinned engine inputs or create a NEW ad-hoc candidate, never install.

Neither provenance nor a successful package operation is functional acceptance.
Only the two selected engine frameworks are replaced in the NEW copied App.
"""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
MODULE = "8c73295524f3ce6a2e10ad2f71141c695376c876"
VERSION = "6.11.3"
FRAMEWORKS = ("QtWebEngineCore", "QtWebEngineWidgets")
# Guard the exact underlying build whose offscreen completion behavior was
# reviewed. UUIDs survive deployment/thinning; this is identity, not attestation.
UNDERLYING_UUIDS = {
    "QtCore": "1EB0E3D3-C943-3D7B-AB8E-B1B8A41E8E5E",
    "QtGui": "5AA5CC09-BF93-3413-BDB4-43BA79FABC3E",
    "QtQuick": "2115BA0A-6D4C-34AB-8C1C-09D11FDC43C9",
    "QtQuickWidgets": "F29751B9-136C-3557-B658-83465A23BE30",
}
HEADER_HASHES = {
    "Headers/qtwebenginecore-config.h": "428824e2f8ca719cf30e2d742b35d49c81600ba8d7d316e14ca9f4c34bf7a5ab",
    "Headers/6.11.3/QtWebEngineCore/private/qtwebenginecore-config_p.h": "cfc59a2310bdeaed6f9eb26d3a31438d7fbb102aacaa426692984d4c6489cd98",
}


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def digest(file):
    value = hashlib.sha256()
    with file.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def ignored(name):
    return name == "Headers" or name.endswith(".prl")


def verify_underlying(folder):
    for name, expected in UNDERLYING_UUIDS.items():
        value = run("dwarfdump", "--uuid", str(folder / (name + ".framework") / "Versions/A" / name))
        match = re.search(r"UUID: ([0-9A-F-]+) \(arm64\)", value)
        if not match or match[1] != expected:
            raise ValueError("Unreviewed underlying Qt arm64 build: " + name)


def inventory(root, deployable=True):
    """Only deployable entries; reject external aliases, never follow them."""
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Engine framework root must be a physical directory")
    canonical = root.resolve(strict=True)
    entries = []

    def walk(file):
        relative = file.relative_to(root).as_posix()
        if file.is_symlink():
            target = os.readlink(file)
            resolved = file.resolve(strict=True)
            if Path(target).is_absolute() or not resolved.is_relative_to(canonical):
                raise ValueError("External engine symlink")
            if deployable and any(ignored(part) for part in resolved.relative_to(canonical).parts):
                raise ValueError("Runtime alias depends on omitted build metadata")
            entries.append([relative, "symlink", target])
        elif file.is_dir():
            entries.append([relative, "directory"])
            for child in sorted(file.iterdir()):
                if not deployable or not ignored(child.name):
                    walk(child)
        elif file.is_file():
            entries.append([relative, "file", file.stat().st_mode & 0o111, file.stat().st_size, digest(file)])
        else:
            raise ValueError("Unsupported engine entry")

    walk(root)
    return hashlib.sha256(json.dumps(entries, separators=(",", ":")).encode()).hexdigest()


def new_path(value):
    target = Path(value)
    if not target.is_absolute() or target.exists() or target.is_symlink():
        raise ValueError("Require a new absolute output; never overwrite")
    return target.parent.resolve() / target.name


def record(source_arg, runtime_arg, output_arg):
    source, runtime = Path(source_arg).resolve(strict=True), Path(runtime_arg).resolve(strict=True)
    if (source / ".tag").read_text().strip() != MODULE:
        raise ValueError("Wrong upstream source module")
    verify_underlying(runtime / "lib")
    patches, files = [], []
    for group, field, prefix in (("standalone-qt-pdf", "candidateSha256", "src/3rdparty"),
                                 ("standalone-qt-memory", "releaseSha256", "")):
        folder = REPO / "tests" / group / "engine-patches"
        manifest = json.loads((folder / "source-manifest.json").read_text())
        if manifest["moduleCommit"] != MODULE:
            raise ValueError("Patch module mismatch")
        for item in manifest["files"]:
            relative = Path(prefix) / item["path"]
            if not (source / relative).resolve(strict=True).is_relative_to(source) or digest(source / relative) != item[field]:
                raise ValueError("Pinned candidate source mismatch")
            files.append({"path": relative.as_posix(), "sha256": item[field]})
        selected = ([manifest["releasePatch"]] if field == "releaseSha256" else
                    [{"path": manifest["patch"], "sha256": manifest["patchSha256"]}])
        for item in selected:
            if digest(folder / item["path"]) != item["sha256"]:
                raise ValueError("Patch bytes changed")
            patches.append({"path": (folder / item["path"]).relative_to(REPO).as_posix(), "sha256": item["sha256"]})
    core = runtime / "lib/QtWebEngineCore.framework/Versions/A"
    for name, expected in HEADER_HASHES.items():
        if digest(core / name) != expected:
            raise ValueError("Runtime feature headers differ from the original SDK")
    frameworks = {}
    for name in FRAMEWORKS:
        framework = runtime / "lib" / (name + ".framework")
        info = plistlib.loads((framework / "Versions/A/Resources/Info.plist").read_bytes())
        if info.get("CFBundleVersion") != VERSION:
            raise ValueError("Wrong runtime framework version")
        if run("lipo", "-archs", str(framework / "Versions/A" / name)).strip() != "arm64":
            raise ValueError("Engine is not arm64-only")
        frameworks[name] = inventory(framework)
    for relative in ("Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess",
            "Resources/icudtl.dat", "Resources/qtwebengine_resources.pak",
            "Resources/qtwebengine_resources_100p.pak", "Resources/qtwebengine_resources_200p.pak",
            "Resources/qtwebengine_devtools_resources.pak", "Resources/v8_context_snapshot.arm64.bin",
            "Resources/qtwebengine_locales/en-US.pak"):
        file = core / relative
        if not file.is_file() or not file.stat().st_size:
            raise ValueError("Incomplete engine runtime: " + relative)
    result = {"schema": 1, "kind": "pinned isolated engine inputs; NOT functional acceptance",
              "qtVersion": VERSION, "moduleCommit": MODULE, "patches": patches,
              "sourceFilesChecked": files, "wholeSourceTreeProven": False,
              "frameworkInventories": frameworks, "underlyingQtArm64Builds": UNDERLYING_UUIDS,
              "upstreamArchiveSha1": "e545e2a47265b873f685a513f2768c259799c623",
              "chromiumSourceCommit": "7927ce61b5a302a3da8172eefbe260ec17484bcd",
              "installed": False}
    output = new_path(output_arg)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print("Recorded five pinned source files and two runtime inventories; no build/test/install performed")


def package(base_arg, runtime_arg, manifest_arg, output_arg):
    base, runtime = Path(base_arg).resolve(strict=True), Path(runtime_arg).resolve(strict=True)
    output = new_path(output_arg)
    if output.name != "WizNotePlus Apple Silicon.app" or any(output.is_relative_to(p) for p in
            (Path("/Applications"), Path.home() / "Applications")):
        raise ValueError("Only a new staged main-client candidate outside Applications is allowed")
    if output.is_relative_to(base) or output.is_relative_to(runtime):
        raise ValueError("Output cannot be nested inside its source inputs")
    info = plistlib.loads((base / "Contents/Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != "io.github.defenceforce1.wiznoteplus.applesilicon":
        raise ValueError("Not the main-client bundle")
    inventory(base, deployable=False)  # Never preserve aliases back into the input App.
    run("codesign", "--verify", "--deep", "--strict", str(base))
    if "Signature=adhoc" not in run("codesign", "-dv", str(base)):
        raise ValueError("This candidate helper supports only the existing free ad-hoc route")
    base_qt = plistlib.loads((base / "Contents/Frameworks/QtCore.framework/Versions/A/Resources/Info.plist").read_bytes())
    if base_qt.get("CFBundleVersion") != VERSION:
        raise ValueError("Do not mix underlying Qt versions")
    verify_underlying(base / "Contents/Frameworks")
    manifest = json.loads(Path(manifest_arg).read_text())
    if manifest.get("schema") != 1 or manifest.get("moduleCommit") != MODULE or manifest.get("qtVersion") != VERSION:
        raise ValueError("Wrong engine provenance")
    if manifest.get("underlyingQtArm64Builds") != UNDERLYING_UUIDS:
        raise ValueError("Missing reviewed underlying Qt build identities")
    for patch in manifest.get("patches", []):
        source_patch = (REPO / patch["path"]).resolve(strict=True)
        if not source_patch.is_relative_to(REPO / "tests") or digest(source_patch) != patch["sha256"]:
            raise ValueError("Local source patches changed since provenance was recorded")
    if len(manifest.get("patches", [])) != 2 or len(manifest.get("sourceFilesChecked", [])) != 5:
        raise ValueError("Expected the clean two-patch/five-source-file candidate record")
    for name in FRAMEWORKS:
        if inventory(runtime / "lib" / (name + ".framework")) != manifest["frameworkInventories"][name]:
            raise ValueError("Engine input changed since provenance was recorded")
    old_frameworks = base / "Contents/Frameworks"
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(base, output, symlinks=True, ignore=lambda directory, names:
                    [n + ".framework" for n in FRAMEWORKS] if Path(directory) == old_frameworks else [])
    for name in FRAMEWORKS:
        shutil.copytree(runtime / "lib" / (name + ".framework"), output / "Contents/Frameworks" / (name + ".framework"),
                        symlinks=True, ignore=lambda directory, names: [n for n in names if ignored(n)])
    core = output / "Contents/Frameworks/QtWebEngineCore.framework/Versions/A"
    helper = core / "Helpers/QtWebEngineProcess.app"
    for file, replacement in ((core / "QtWebEngineCore", "@loader_path/../../.."),
            (output / "Contents/Frameworks/QtWebEngineWidgets.framework/Versions/A/QtWebEngineWidgets", "@loader_path/../../.."),
            (helper / "Contents/MacOS/QtWebEngineProcess", "@loader_path/../../../../../../..")):
        paths = re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset", run("otool", "-l", str(file)))
        for old in paths:
            if old.startswith("/"):
                run("install_name_tool", "-delete_rpath", old, str(file))
        if replacement not in paths:
            run("install_name_tool", "-add_rpath", replacement, str(file))
    run("strip", "-x", "-S", str(core / "QtWebEngineCore"))
    notices = output / "Contents/Resources/licenses"
    notices.mkdir(exist_ok=True)
    notice = {**manifest, "mainClientVersion": info.get("CFBundleVersion"),
              "baseMainExecutableSha256": digest(base / "Contents/MacOS/WizNotePlus"),
              "changes": "native PDF destination transform and pinned offscreen-widget final texture ownership"}
    (notices / "Qt-local-engine-changes.json").write_text(json.dumps(notice, indent=2) + "\n")
    local_source = notices / "Qt-local-engine-source"
    local_source.mkdir()  # New candidate only; never replace source notices.
    for patch in manifest["patches"]:
        shutil.copyfile(REPO / patch["path"], local_source / Path(patch["path"]).name)
    shutil.copyfile(REPO / "docs/engine-build.md", local_source / "BUILD.md")
    with tempfile.TemporaryDirectory(prefix=".wiz-engine-sign-", dir=output.parent) as temporary:
        entitlements = Path(temporary) / "helper.plist"
        entitlements.write_bytes(plistlib.dumps({"com.apple.security.cs." + key: True for key in
            ("allow-jit", "allow-unsigned-executable-memory", "disable-executable-page-protection", "disable-library-validation")}))
        run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements", str(entitlements), str(helper))
    for target in (core.parents[1], output / "Contents/Frameworks/QtWebEngineWidgets.framework", output):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", str(target))
    run("codesign", "--verify", "--deep", "--strict", str(output))
    print(run(sys.executable, str(REPO / "build/osx/check-bundle.py"), str(output)).strip())
    print("NEW candidate created; input App/SDK unchanged, ad-hoc and unnotarized; functional/UI acceptance still required")


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "record":
        record(*sys.argv[2:])
    elif len(sys.argv) == 6 and sys.argv[1] == "package":
        package(*sys.argv[2:])
    else:
        raise SystemExit("Usage: engine-candidate.py record SOURCE_ROOT RUNTIME NEW_JSON | package BASE_APP RUNTIME MANIFEST NEW_APP")
