#!/usr/bin/env python3
"""Build and run native Qt/TLS transfer tests on the current CI platform only."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = root / 'build' / 'file-transfer-test.noindex'
build.mkdir(parents=True, exist_ok=True)
candidates = [root / 'build/native-test-deps/include/nlohmann/json.hpp', Path('C:/msys64/ucrt64/include/nlohmann/json.hpp'), Path('/usr/include/nlohmann/json.hpp')]
candidates.extend((root / 'build').glob('**/_deps/json-src/include/nlohmann/json.hpp'))
header = next((p for p in candidates if p.is_file()), None)
if header is None:
    raise SystemExit('Build Sunshine first so the pinned JSON headers are available.')
# Copy only nlohmann headers: never add MinGW's C/C++ standard headers to MSVC.
include = build / 'include'
shutil.copytree(header.parent, include / 'nlohmann', dirs_exist_ok=True)
qmake = shutil.which('qmake6') if sys.platform.startswith('linux') else shutil.which('qmake')
if not qmake:
    raise SystemExit('Qt qmake is required')
subprocess.run([qmake, str(root / 'tests/file-transfer/file-transfer.pro'),
                f'JSON_INCLUDE={include.as_posix()}', 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.'], cwd=build, check=True)
subprocess.run(['nmake'] if os.name == 'nt' else ['make', '-j3'], cwd=build, check=True)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
if sys.platform == 'darwin':
    env['DYLD_LIBRARY_PATH'] = str(root / 'libs/mac/lib')
if os.name == 'nt':
    env['PATH'] = str(root / 'libs/windows/lib/x64') + os.pathsep + env['PATH']
program = build / ('file-transfer-test.exe' if os.name == 'nt' else 'file-transfer-test')
result = subprocess.run([str(program), '-o', 'results.txt,txt'], cwd=build, env=env)
print((build / 'results.txt').read_text(encoding='utf-8', errors='replace'), flush=True)
if result.returncode:
    sys.exit(result.returncode)
# Exercise the X11 adapter separately; keep QML screenshot tests deterministic offscreen.
if sys.platform.startswith('linux'):
    xenv = dict(env, QT_QPA_PLATFORM='xcb')
    result = subprocess.run(['xvfb-run', '-a', str(program), 'nativeAdapterReadsOnDemand', '-o', 'results-x11.txt,txt'], cwd=build, env=xenv)
    print((build / 'results-x11.txt').read_text(encoding='utf-8', errors='replace'), flush=True)
    if result.returncode:
        sys.exit(result.returncode)
# Core session, capability and loopback tests use no real system clipboard.
core = root / 'build/native-files-test.noindex'
core.mkdir(parents=True, exist_ok=True)
subprocess.run([qmake, str(root / 'tests/native-files/native-files.pro'), 'CONFIG+=release', 'CONFIG-=debug_and_release', 'DESTDIR=.'], cwd=core, check=True)
subprocess.run(['nmake'] if os.name == 'nt' else ['make', '-j3'], cwd=core, check=True)
core_program = core / ('native-files-test.exe' if os.name == 'nt' else 'native-files-test')
result = subprocess.run([str(core_program), '-o', 'results.txt,txt'], cwd=core, env=env)
print((core / 'results.txt').read_text(encoding='utf-8', errors='replace'), flush=True)
sys.exit(result.returncode)
