#!/usr/bin/env python3
"""Extract vendor-supplied license text and attribution from exact SDK SBOMs.

This supplements notices, not full corresponding source or an asset clearance.
No user files or package download scripts are inspected/executed.
"""
import hashlib
import json
import pathlib
import sys

MODULES = ("qtbase", "qtdeclarative", "qtwebengine", "qtwebengine-chromium-webengine",
           "qtwebengine-chromium-pdf", "qtwebchannel", "qtwebsockets", "qtpositioning",
           "qtsvg", "qtserialport", "qtscxml", "qt5compat")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: collect-qt-notices.py QT_SDK NEW_OUTPUT_DIR")
    sdk = pathlib.Path(sys.argv[1]).resolve(strict=True)
    output = pathlib.Path(sys.argv[2])
    if not output.is_absolute() or output.exists():
        raise SystemExit("Use a new absolute output directory")
    notices = {}
    packages, sources, missing = [], [], []
    for module in MODULES:
        path = sdk / "sbom" / (module + "-6.11.3.spdx.json")
        if not path.is_file():
            missing.append(module)
            continue
        raw = path.read_bytes()
        data = json.loads(raw)
        refs = {}
        for item in data.get("hasExtractedLicensingInfos", []):
            text = item.get("extractedText", "")
            if not text or text == "NOASSERTION":
                continue
            digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            refs[item["licenseId"]] = digest
            notices[digest] = text
        for package in data.get("packages", []):
            packages.append({key: package.get(key) for key in
                ("name", "versionInfo", "licenseDeclared", "licenseConcluded", "copyrightText", "downloadLocation")}
                | {"module": module})
        sources.append({"module": module, "sha256": hashlib.sha256(raw).hexdigest(),
                        "file": path.name, "packages": len(data.get("packages", [])), "license_texts": refs})
    if not sources or not notices:
        raise SystemExit("No SDK vendor license evidence found")
    output.mkdir(parents=True)
    text_dir = output / "LicenseTexts"
    text_dir.mkdir()
    for digest, text in sorted(notices.items()):
        (text_dir / (digest + ".txt")).write_text(text, encoding="utf-8")
    (output / "inventory.json").write_text(json.dumps({"sdk_version": "6.11.3", "sources": sources,
        "missing_modules": missing, "packages": packages}, ensure_ascii=False, indent=2), encoding="utf-8")
    (output / "README.txt").write_text(
        "Qt 6.11.3 vendor SPDX license and copyright evidence. License texts are deduplicated by SHA-256.\n"
        "Inventory maps SDK module license references to LicenseTexts files. This is a module-level inventory,\n"
        "not proof that every listed component is used by this app. Some source/version fields are NOASSERTION;\n"
        "this does not replace corresponding source or complete third-party editor/font/asset clearance.\n", encoding="utf-8")
    print(f"Read {len(sources)} vendor SBOMs; {len(notices)} distinct license texts; {len(packages)} module-level components")
    print("Missing modules: " + (", ".join(missing) or "none"))


if __name__ == "__main__":
    main()
