#!/usr/bin/env python3
"""Include corresponding source alongside distributable GPL payload artifacts."""
import pathlib
import tarfile

root = pathlib.Path(__file__).resolve().parents[1]
entries = ['Makefile', 'README.md', 'TESTING.md', 'LICENSE', 'NOTICE', '.gitignore',
           'include', 'src', 'native', 'media', 'config', 'tools', 'tests', '.github', 'web', 'third_party']
with tarfile.open(root / 'dist/keyboardsense-source.tar.gz', 'w:gz') as archive:
    for entry in entries:
        path = root / entry
        paths = sorted(path.rglob('*')) if path.is_dir() else [path]
        for source in paths:
            if source.is_file() and '__pycache__' not in source.parts and source.suffix != '.pyc':
                archive.add(source, arcname=pathlib.Path('keyboardsense') / source.relative_to(root), recursive=False)
