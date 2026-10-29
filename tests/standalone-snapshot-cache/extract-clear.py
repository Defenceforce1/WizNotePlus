#!/usr/bin/env python3
"""Read only the named owned C++ sources; compile the actual clear method body."""
import argparse
import hashlib
import json
from pathlib import Path


def extract(text, marker='void WizDocumentWebView::clear()', blank_contract=True):
    if text.count(marker) != 1:
        raise ValueError('expected exactly one production clear method')
    start = text.index(marker)
    index = text.index('{', start)
    depth, state = 0, 'code'
    cursor = index
    while cursor < len(text):
        char, pair = text[cursor], text[cursor:cursor + 2]
        if state == 'line':
            if char == '\n':
                state = 'code'
        elif state == 'block':
            if pair == '*/':
                state = 'code'
                cursor += 1
        elif state in ('"', "'"):
            if char == '\\':
                cursor += 1
            elif char == state:
                state = 'code'
        elif pair == '//':
            state = 'line'
            cursor += 1
        elif pair == '/*':
            state = 'block'
            cursor += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                method = text[start:cursor + 1] + '\n'
                if blank_contract and method.count('load(QUrl("about:blank"));') != 1:
                    raise ValueError('unexpected clear navigation contract')
                return method
        cursor += 1
    raise ValueError('unterminated clear method')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--saver', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for file, name, output in ((args.baseline, 'BeforeView', 'before-clear.inc'),
                               (args.source, 'CurrentView', 'current-clear.inc')):
        if file.name == 'AGENTS.md':
            raise ValueError('excluded input')
        data = file.read_bytes()
        method = extract(data.decode('utf-8'))
        (args.output_dir / output).write_text(method.replace('WizDocumentWebView', name))
        records.append({'source': str(file.resolve()), 'sourceSha256': hashlib.sha256(data).hexdigest(),
                        'methodSha256': hashlib.sha256(method.encode()).hexdigest(),
                        'compiledClassName': name, 'onlyTransformation': 'class name substitution'})
    data = args.saver.read_bytes()
    method = extract(data.decode('utf-8'), 'void WizDocumentSaverThread::save(', False)
    (args.output_dir / 'saver-handoff.inc').write_text(method.replace('WizDocumentSaverThread', 'SnapshotSaver'))
    records.append({'source': str(args.saver.resolve()), 'sourceSha256': hashlib.sha256(data).hexdigest(),
                    'methodSha256': hashlib.sha256(method.encode()).hexdigest(),
                    'compiledClassName': 'SnapshotSaver', 'onlyTransformation': 'class name substitution'})
    (args.output_dir / 'method-source.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
