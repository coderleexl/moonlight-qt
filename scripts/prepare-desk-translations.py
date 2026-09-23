#!/usr/bin/env python3
"""Compile updated translations during CI packaging, before rcc embeds them."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
tool = shutil.which('lrelease') or shutil.which('lrelease6')
if not tool and Path('/usr/lib/qt6/bin/lrelease').is_file():
    tool = '/usr/lib/qt6/bin/lrelease'
if not tool:
    raise SystemExit('Qt lrelease is required to package Desk translations')
for locale in ('zh_CN', 'zh_TW'):
    source = root / 'app' / 'languages' / f'qml_{locale}.ts'
    subprocess.run([tool, str(source), '-qm', str(source.with_suffix('.qm'))], check=True)
