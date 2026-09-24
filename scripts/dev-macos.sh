#!/usr/bin/env bash
# Incremental client build, install, and Spotlight refresh on this Mac only.
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'This script is for local macOS development.' >&2; exit 2; }
cd "$(dirname "$0")/.."
# Keep development bundles out of Spotlight; migrate the existing build in place.
if [[ -d build/desk-macos-local && ! -e build/desk-macos-local.noindex ]]; then
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister \
    -u "$PWD/build/desk-macos-local/app/Desk.app" || true
  mv build/desk-macos-local build/desk-macos-local.noindex
fi
case "${1:-}" in
  '')
    mkdir -p build/desk-macos-local.noindex
    python3 scripts/prepare-desk-translations.py
    qmake -r moonlight-qt.pro CONFIG+=host_preview "QMAKE_APPLE_DEVICE_ARCHS=$(uname -m)" \
      -o build/desk-macos-local.noindex/Makefile
    # In-source generated moc files can suppress shadow-build moc rules.
    make -C build/desk-macos-local.noindex/app -f Makefile.Release compiler_moc_source_make_all
    # qmake changes VERSION_STR flags without invalidating existing objects.
    # Rebuild its three consumers when the version changes (or no stamp exists).
    if ! cmp -s app/version.txt build/desk-macos-local.noindex/.compiled-version; then
      rm -f build/desk-macos-local.noindex/app/release/main.o \
        build/desk-macos-local.noindex/app/release/autoupdatechecker.o \
        build/desk-macos-local.noindex/app/release/systemproperties.o
    fi
    # Leave CPU and memory available for the editor during local development.
    make -C build/desk-macos-local.noindex release -j"${JOBS:-2}"
    cp app/version.txt build/desk-macos-local.noindex/.compiled-version
    ;;
  --install-only) ;;
  *) echo 'Usage: bash scripts/dev-macos.sh [--install-only]' >&2; exit 2 ;;
esac
source_app="$PWD/build/desk-macos-local.noindex/app/Desk.app"
test -x "$source_app/Contents/MacOS/Desk"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$source_app/Contents/Info.plist")" = io.github.coderleexl.Desk

# A newly linked executable points to the development Qt installation. Always
# deploy it before combining it with the installed app's Cocoa plugins.
macdeployqt "$source_app" -qmldir="$PWD/app/gui" -no-codesign
python3 - "$source_app" <<'PYDEPLOY'
from pathlib import Path
import subprocess
import sys
app = Path(sys.argv[1]).resolve()
frameworks = app / 'Contents/Frameworks'
for plugin in (app / 'Contents/PlugIns/sqldrivers').glob('*.dylib'):
    if plugin.name != 'libqsqlite.dylib':
        plugin.unlink()
external = []
for path in app.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open('rb') as stream:
        if stream.read(4) not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            continue
    ident = subprocess.check_output(['otool', '-D', str(path)], text=True).splitlines()
    if len(ident) > 1 and ident[1].startswith('/') and frameworks in path.parents:
        subprocess.run(['install_name_tool', '-id', '@rpath/' + path.relative_to(frameworks).as_posix(), str(path)], check=True)
    for line in subprocess.check_output(['otool', '-L', str(path)], text=True).splitlines():
        if not line.startswith('\t'):
            continue
        dep = line.strip().split(' (')[0]
        if dep.startswith('/') and not dep.startswith(('/usr/lib/', '/System/Library/')):
            external.append(f'{path.name}: {dep}')
if external:
    raise SystemExit('Installation stopped: external dependencies\n' + '\n'.join(external))
PYDEPLOY

# Desk handles one SIGTERM by ending its session and quitting normally. Do not
# use a second signal (which forces exit), or kill other Moonlight installations.
python3 - <<'PY'
import os
import signal
import subprocess
import time

target = '/Applications/Desk.app/Contents/MacOS/Desk'
def running():
    output = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True)
    return [int(fields[0]) for line in output.splitlines()
            if len(fields := line.strip().split(None, 1)) == 2 and fields[1] == target]

for pid in running():
    print(f'Closing Desk gracefully (PID {pid})...', flush=True)
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
deadline = time.monotonic() + 30
while running():
    if time.monotonic() >= deadline:
        raise SystemExit('Desk is still exiting; installation stopped. Retry --install-only after it closes.')
    time.sleep(0.2)
PY

ditto "$source_app" /Applications/Desk.app
xattr -cr /Applications/Desk.app
codesign --force --deep --sign - /Applications/Desk.app
codesign --verify --deep --strict /Applications/Desk.app
# ditto preserves the old shadow-build bundle timestamp. Invalidate the icon
# cache before registering or launching the replacement application.
touch /Applications/Desk.app
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "$source_app" || true
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Desk.app
mdimport -i /Applications/Desk.app
cmp "$source_app/Contents/MacOS/Desk" /Applications/Desk.app/Contents/MacOS/Desk
open /Applications/Desk.app
# LaunchServices accepting the launch does not mean the app survived startup.
python3 - <<'PYSTART'
import subprocess
import time
time.sleep(5)
processes = subprocess.check_output(['ps', '-axo', 'comm='], text=True).splitlines()
if '/Applications/Desk.app/Contents/MacOS/Desk' not in processes:
    raise SystemExit('Desk exited during startup; inspect its latest /tmp/Desk-*.log before reporting success.')
PYSTART
echo 'Updated /Applications/Desk.app and refreshed Spotlight.'
