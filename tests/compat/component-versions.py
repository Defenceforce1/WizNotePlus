#!/usr/bin/env python3
"""Focused offline checks for source/build/package version regression gates."""
import argparse
import json
import pathlib
import plistlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('repository', type=pathlib.Path)
    parser.add_argument('temporary_root', type=pathlib.Path)
    parser.add_argument('qt_prefix', type=pathlib.Path)
    parser.add_argument('old_mathjax_entry', type=pathlib.Path)
    args = parser.parse_args()
    repo, root = args.repository.resolve(), args.temporary_root.resolve()
    root.mkdir(parents=True, exist_ok=False)
    verifier = repo / 'build/verify-components.py'
    lock = json.loads((repo / 'build/components.lock.json').read_text())
    checks, failures, rows = 0, 0, []

    def check(value, label):
        nonlocal checks, failures
        checks += 1
        failures += not value
        print(('PASS ' if value else 'FAIL ') + label)

    def run(label, command, expected):
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        rows.append({'case': label, 'status': result.returncode,
                     'output': result.stdout + result.stderr})
        check((result.returncode == 0) == expected, label)
        return result

    import sys
    source = root / 'source'
    for group in lock['components']:
        for item in group['files']:
            dst = source / item['source']
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repo / item['source'], dst)
    command = [sys.executable, str(verifier), '--source-root', str(source)]
    run('reviewed source and full Qt version accepted', command + ['--qt-prefix', str(args.qt_prefix)], True)
    run('different Qt version rejected', command + ['--qt-version', '6.9.0'], False)
    fake_qt = root / 'qt/lib/QtCore.framework/Resources'
    fake_qt.mkdir(parents=True)
    (fake_qt / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleVersion': lock['qt_version'], 'CFBundleShortVersionString': '6.11'}))
    run('Qt patch version uses full build version not short display value', command + ['--qt-prefix', str(root / 'qt')], True)
    math = source / 'share/files/wizeditor/dependency/mathjax'
    entry = math / 'MathJax.js'
    good = entry.read_bytes()
    entry.write_bytes(args.old_mathjax_entry.read_bytes())
    result = run('actual legacy MathJax entry rejected', command, False)
    check('MathJax' in result.stderr, 'legacy failure identifies the component')
    entry.write_bytes(good)
    extra = math / 'config/old-resource.js'
    extra.parent.mkdir()
    extra.write_text('old generated resource fixture\n')
    run('stale old engine directory rejected even with current entry', command, False)
    shutil.rmtree(extra.parent)
    font = math / 'tex-font/svg.js'
    font_bytes = font.read_bytes()
    font.unlink()
    run('missing offline font rejected', command, False)
    font.write_bytes(font_bytes)
    sqlite = source / 'src/database/sqlite3.h'
    sqlite_bytes = sqlite.read_bytes()
    sqlite.write_text('#define SQLITE_VERSION "old fixture"\n')
    run('unreviewed native SQLite input rejected', command, False)
    sqlite.write_bytes(sqlite_bytes)
    run('restored reviewed inputs accepted', command, True)

    # Exercise the real package script only through its failing preflight.
    # No package, copy/sign/deployment command or real App can be started here.
    build = root / 'build'
    (build / 'bin/WizNotePlus.app/Contents/Resources').mkdir(parents=True)
    (build / 'CMakeCache.txt').write_text('WIZNOTE_BUILD_COMPAT_TESTS:BOOL=OFF\n')
    resources = build / 'bin/WizNotePlus.app/Contents/Resources/files'
    shutil.copytree(source / 'share/files', resources)
    (resources / 'wizeditor/dependency/mathjax/MathJax.js').write_bytes(args.old_mathjax_entry.read_bytes())
    output = root / 'never-created-package'
    run('packaging stops on legacy build resources before copy or signing',
        ['bash', str(repo / 'build/osx/package-arm64.sh'), str(args.qt_prefix), str(build), str(output)], False)
    check(not output.exists(), 'failed package preflight creates no output App')

    app = root / 'bundle-check.app'
    shutil.copytree(resources, app / 'Contents/Resources/files')
    core = app / 'Contents/Frameworks/QtCore.framework/Resources'
    core.mkdir(parents=True)
    (core / 'Info.plist').write_bytes((fake_qt / 'Info.plist').read_bytes())
    (app / 'Contents/MacOS').mkdir()
    (app / 'Contents/MacOS/WizNotePlus').write_bytes(b'non-executable fixture')
    result = run('bundle audit rejects old component before Mach-O traversal',
                 [sys.executable, str(repo / 'build/osx/check-bundle.py'), str(app)], False)
    check('MathJax' in result.stderr and 'Checked ' not in result.stdout, 'bundle failure comes from component validation')
    report = {'checks': checks, 'failures': failures, 'cases': rows,
              'noNetworkOrNormalAccountOrSigningOrPackageCreated': True}
    (root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('COMPONENT_VERSION_REPORT ' + json.dumps(report))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
