#!/usr/bin/env python3
"""Extract only the owned load-admission/ready and saver-handoff methods."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('owned_method_extract', Path(__file__).with_name('extract-clear.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--saver', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    markers = ['bool WizDocumentWebView::acceptsLoadResult(',
               'bool WizDocumentWebView::takeLoadResult(',
               'void WizDocumentWebView::onDocumentReady(']
    records = []
    for source, name, output in ((args.baseline, 'BeforeView', 'before-load.inc'),
                                 (args.source, 'CurrentView', 'current-load.inc')):
        if source.name == 'AGENTS.md':
            raise ValueError('excluded input')
        data = source.read_bytes()
        methods = [module.extract(data.decode(), marker, False) for marker in markers]
        (args.output_dir / output).write_text('\n'.join(methods).replace('WizDocumentWebView', name))
        records.append({'source': str(source.resolve()), 'sourceSha256': hashlib.sha256(data).hexdigest(),
                        'methods': [{'signature': marker, 'sha256': hashlib.sha256(method.encode()).hexdigest()}
                                    for marker, method in zip(markers, methods)],
                        'onlyTransformation': 'class name substitution'})
    data = args.saver.read_bytes()
    markers = ['void WizDocumentSaverThread::save(',
               'WizDocumentSaverThread::SAVEDATA WizDocumentSaverThread::peekFirst()']
    methods = [module.extract(data.decode(), marker, False) for marker in markers]
    (args.output_dir / 'saver-handoff.inc').write_text('\n'.join(methods).replace('WizDocumentSaverThread', 'SnapshotSaver'))
    records.append({'source': str(args.saver.resolve()), 'sourceSha256': hashlib.sha256(data).hexdigest(),
                    'methods': [{'signature': marker, 'sha256': hashlib.sha256(method.encode()).hexdigest()}
                                for marker, method in zip(markers, methods)],
                    'onlyTransformation': 'class name substitution'})
    (args.output_dir / 'method-source.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    main()
