#!/usr/bin/env python3
"""Copy our candidate, remove unused architecture slices, then re-sign ad-hoc.

Never edits the input or the installed App. Retains the declared app languages
and all rendering/editor/media resources; only explicit unused locales and the
Intel V8 snapshot are omitted. No Qt module is deleted.
This is packaging, not a new main executable build or a notarization operation.
"""
import os
import pathlib
import plistlib
import stat
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def prune_language_resources(output):
    """Used only on a new owned bundle (or a disposable fixture), never input."""
    removed = []
    root = output.resolve(strict=True)
    def omit(locale):
        if not locale.resolve(strict=True).is_relative_to(root):
            raise SystemExit("Refusing language removal through an external resource directory")
        removed.append((str(locale.relative_to(output)), locale.stat().st_size))
        locale.unlink()
    engine = output / "Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales"
    if engine.is_dir():
        for locale in engine.iterdir():
            if locale.is_file() and not locale.is_symlink() and locale.suffix == ".pak" and locale.name not in {"en-US.pak", "en-GB.pak", "zh-CN.pak"}:
                omit(locale)
    locales = output / "Contents/Resources/locales"
    if locales.is_dir():
        for locale in locales.iterdir():
            if locale.is_file() and not locale.is_symlink() and locale.name.startswith(("qt_", "qt_help_", "qtbase_", "wiznote_")) and locale.suffix == ".qm" and locale.stem.rsplit("_", 1)[-1] not in {"en", "US", "CN"}:
                omit(locale)
    for relative in ("Contents/Resources/zh-Hant.lproj/ServicesMenu.strings",
                     "Contents/Resources/files/welcomepage/js/datepicker-zh-TW.js"):
        locale = output / relative
        if locale.is_file() and not locale.is_symlink():
            omit(locale)
    traditional = output / "Contents/Resources/zh-Hant.lproj"
    if traditional.is_dir() and not traditional.is_symlink() and not any(traditional.iterdir()):
        traditional.rmdir()
    return removed


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: thin-arm64-bundle.py SOURCE_APP NEW_ABSOLUTE_APP")
    source = pathlib.Path(sys.argv[1]).resolve(strict=True)
    output_arg = pathlib.Path(sys.argv[2])
    if not output_arg.is_absolute() or output_arg.exists() or output_arg.is_symlink():
        raise SystemExit("Output must be a new absolute App path; existing apps are not overwritten")
    if output_arg.name != "WizNotePlus Apple Silicon.app":
        raise SystemExit("Output must use this project's main-client bundle name")
    with (source / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if info.get("CFBundleIdentifier") != "io.github.defenceforce1.wiznoteplus.applesilicon":
        raise SystemExit("Not this project's candidate")
    run("codesign", "--verify", "--deep", "--strict", str(source))
    output_arg.parent.mkdir(parents=True, exist_ok=True)
    run("ditto", str(source), str(output_arg))
    output = output_arg.resolve(strict=True)
    magic = {bytes.fromhex(value) for value in ("feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
    binaries, seen = [], set()
    before = after = thinned = 0
    for directory, _, names in os.walk(output):
        for name in names:
            binary = pathlib.Path(directory, name).resolve()
            if binary in seen or not binary.is_file():
                continue
            if not binary.is_relative_to(output):
                raise SystemExit("Refusing an external file symlink in candidate")
            seen.add(binary)
            with binary.open("rb") as stream:
                if stream.read(4) not in magic:
                    continue
            arches = run("lipo", "-archs", str(binary)).strip().split()
            if "arm64" not in arches:
                raise SystemExit("Dependency lacks arm64: " + str(binary.relative_to(output)))
            before += binary.stat().st_size
            if arches != ["arm64"]:
                mode = stat.S_IMODE(binary.stat().st_mode)
                fd, temporary = tempfile.mkstemp(prefix=".wiznote-thin-", dir=binary.parent)
                os.close(fd)
                try:
                    run("lipo", str(binary), "-thin", "arm64", "-output", temporary)
                    os.chmod(temporary, mode)
                    os.replace(temporary, binary)
                finally:
                    if os.path.exists(temporary):
                        os.unlink(temporary)
                thinned += 1
            after += binary.stat().st_size
            binaries.append(binary)
    if not binaries:
        raise SystemExit("No Mach-O code found")
    # App Info.plist declares English and Simplified Chinese. This is NOT a
    # blanket removal of files with unfamiliar suffixes: fonts, math, PDF,
    # spellchecking dictionaries, devtools and image plugins are all retained.
    resource_removed = prune_language_resources(output)
    engine = output / "Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources"
    intel_snapshot = engine / "v8_context_snapshot.x86_64.bin"
    if intel_snapshot.is_file() and not intel_snapshot.is_symlink():
        resource_removed.append((str(intel_snapshot.relative_to(output)), intel_snapshot.stat().st_size))
        intel_snapshot.unlink()
    # Work from nested code outward. Existing resource/entitlement seals are
    # retained where present; the current main candidate has no paid identity.
    bundles = [p for p in output.rglob("*") if p.is_dir() and not p.is_symlink() and p.suffix in (".app", ".framework", ".bundle", ".appex")]
    targets = sorted(set(binaries + bundles), key=lambda p: len(p.parts), reverse=True)
    for target in targets + [output]:
        run("codesign", "--force", "--sign", "-", "--timestamp=none",
            "--preserve-metadata=identifier,entitlements,flags,runtime", str(target))
    run("codesign", "--verify", "--deep", "--strict", str(output))
    for binary in binaries:
        if run("lipo", "-archs", str(binary)).strip() != "arm64":
            raise SystemExit("Unexpected remaining architecture: " + str(binary.relative_to(output)))
    print(f"Checked {len(binaries)} Mach-O files; thinned {thinned}; removed {before-after} unused architecture bytes")
    print(f"Removed {len(resource_removed)} explicit unused resource files, {sum(size for _, size in resource_removed)} bytes")
    for name, size in resource_removed:
        print(f"Omitted {name} ({size} bytes)")
    print("Input untouched; editor/math/PDF/media resources retained; output ad-hoc signed, not notarized")


if __name__ == "__main__":
    main()
