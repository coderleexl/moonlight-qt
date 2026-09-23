#!/usr/bin/env python3
"""Record exactly which source commit produced the tested platform packages."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

platform = os.environ['DESK_PLATFORM']
version = Path('app/version.txt').read_text().strip()
expected = {
    'windows': ['Desk-Setup-x64.exe', 'Desk-x64.zip'],
    'macos': ['Desk-macOS-arm64.dmg'],
    'linux-ubuntu24.04': [f'desk_{version}_ubuntu24.04_amd64.deb'],
    'linux-debian13': [f'desk_{version}_debian13_amd64.deb'],
}[platform]
root = Path('build/dist')
files = {}
for name in expected:
    path = root / name
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f'Missing or empty package: {path}')
    files[name] = dict(size=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
manifest = dict(platform=platform, version=version, commit=sha, files=files)
(root / f'manifest-{platform}.json').write_text(json.dumps(manifest, indent=2) + '\n')
