#!/usr/bin/env python3
"""Offline version/content checks for the reviewed default component set."""
import argparse
import hashlib
import json
import pathlib
import plistlib
import sys


def verify(lock, source_root=None, resource_root=None, qt_version=None):
    errors = []
    checked = 0
    for group in lock['components']:
        for item in group['files']:
            for kind, root, key in (('source', source_root, 'source'), ('resource', resource_root, 'resource')):
                if root is None or key not in item:
                    continue
                file = root / item[key]
                if not file.is_file():
                    errors.append('{}: missing {} {}'.format(group['name'], kind, item[key]))
                elif hashlib.sha256(file.read_bytes()).hexdigest() != item['sha256']:
                    errors.append('{}: {} content differs from reviewed {}'.format(group['name'], kind, group['version']))
                checked += 1
    for kind, root in (('source', source_root / 'share/files' if source_root else None), ('resource', resource_root)):
        if root is None:
            continue
        for relative in lock['forbidden_resource_paths']:
            if (root / relative).exists():
                errors.append('{}: obsolete component path {}'.format(kind, relative))
        directory = root / 'wizeditor/dependency/mathjax'
        expected = {p['resource'][len('wizeditor/dependency/mathjax/'):]
                    for group in lock['components'] if group['name'] == 'MathJax'
                    for p in group['files']}
        actual = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file()}
        if actual != expected:
            errors.append('{}: MathJax file set differs; stale or missing files'.format(kind))
    if qt_version is not None and qt_version != lock['qt_version']:
        errors.append('Qt: expected {}, found {}'.format(lock['qt_version'], qt_version))
    return errors, checked


def qt_framework_version(framework):
    for relative in ('Resources/Info.plist', 'Versions/A/Resources/Info.plist'):
        file = framework / relative
        if file.is_file():
            data = plistlib.loads(file.read_bytes())
            # Qt's short display version omits the patch (for example6.11).
            return data.get('CFBundleVersion') or data.get('CFBundleShortVersionString')
    raise ValueError('QtCore version metadata is unavailable in the selected Qt prefix')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=pathlib.Path)
    parser.add_argument('--resource-root', type=pathlib.Path)
    parser.add_argument('--qt-version')
    parser.add_argument('--qt-prefix', type=pathlib.Path)
    parser.add_argument('--lock', type=pathlib.Path, default=pathlib.Path(__file__).with_name('components.lock.json'))
    args = parser.parse_args()
    if args.source_root is None and args.resource_root is None:
        parser.error('a source or resource root is required')
    try:
        lock = json.loads(args.lock.read_text())
        version = qt_framework_version(args.qt_prefix / 'lib/QtCore.framework') if args.qt_prefix else args.qt_version
        errors, count = verify(lock, args.source_root, args.resource_root, version)
    except (OSError, ValueError, KeyError) as error:
        print('Component check failed: {}'.format(error), file=sys.stderr)
        return 1
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print('Verified {} reviewed components, {} files{}'.format(len(lock['components']), count, ', Qt ' + version if version else ''))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
