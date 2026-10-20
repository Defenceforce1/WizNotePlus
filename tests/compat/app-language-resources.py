#!/usr/bin/env python3
"""Exercise the packaging language policy only on fresh disposable resources."""
import importlib.util
from pathlib import Path
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("language_policy", root / "build/osx/thin-arm64-bundle.py")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)
checks = 0


def check(condition, label):
    global checks
    checks += 1
    print(("PASS " if condition else "FAIL ") + label, flush=True)
    if not condition:
        raise SystemExit(1)


with tempfile.TemporaryDirectory(prefix="wiz-language-fixture-") as temporary:
    bundle = Path(temporary, "synthetic.app")
    engine = "Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources/qtwebengine_locales/"
    locales = "Contents/Resources/locales/"
    remove = [engine + "zh-TW.pak", engine + "fr.pak", locales + "wiznote_zh_TW.qm",
              locales + "qt_zh_TW.qm", locales + "qtbase_zh_TW.qm", locales + "qt_help_zh_TW.qm",
              "Contents/Resources/zh-Hant.lproj/ServicesMenu.strings",
              "Contents/Resources/files/welcomepage/js/datepicker-zh-TW.js"]
    keep = [engine + name for name in ("en-US.pak", "en-GB.pak", "zh-CN.pak")]
    keep += [locales + name for name in ("wiznote_zh_CN.qm", "qtbase_zh_CN.qm", "qt_help_zh_CN.qm", "qt_en.qm", "other_zh_TW.qm")]
    keep += ["Contents/Resources/files/welcomepage/js/datepicker-zh-CN.js",
             "Contents/Resources/zh-Hans.lproj/ServicesMenu.strings",
             "Contents/Resources/English.lproj/ServicesMenu.strings",
             "Contents/Resources/math/font-zh_TW.woff", "Contents/Resources/editor.js"]
    for relative in remove + keep:
        file = bundle / relative
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(relative.encode("utf-8"))
    symlink = bundle / locales / "qt_unknown.qm"
    symlink.symlink_to("qt_en.qm")
    original = {relative: (bundle / relative).read_bytes() for relative in keep}
    result = policy.prune_language_resources(bundle)
    check({name for name, _ in result} == set(remove), "only the specified unused UI catalogs and Traditional resources are removed")
    for relative, content in original.items():
        check((bundle / relative).read_bytes() == content, "retained English Chinese editor math and unknown assets keep exact bytes")
    check(symlink.is_symlink(), "unknown resource symlinks are not followed or removed")
    check(not (bundle / "Contents/Resources/zh-Hant.lproj").exists(), "only the emptied Traditional localization directory is removed")
    check(policy.prune_language_resources(bundle) == [], "language pruning is idempotent")
    external = Path(temporary, "external-locales")
    external.mkdir()
    outside = external / "qtbase_zh_TW.qm"
    outside.write_bytes(b"do-not-touch")
    second = Path(temporary, "second.app/Contents/Resources")
    second.mkdir(parents=True)
    (second / "locales").symlink_to(external, target_is_directory=True)
    refused = False
    try:
        policy.prune_language_resources(Path(temporary, "second.app"))
    except SystemExit:
        refused = True
    check(refused and outside.read_bytes() == b"do-not-touch", "external directory symlink refuses deletion and preserves outside bytes")
print(f"app-language-resources checks {checks} failures 0")
