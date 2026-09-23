#!/usr/bin/env bash
# Incremental client build, install, and Spotlight refresh on this Mac only.
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'This script is for local macOS development.' >&2; exit 2; }
cd "$(dirname "$0")/.."
case "${1:-}" in
  '')
    mkdir -p build/desk-macos-local
    python3 scripts/prepare-desk-translations.py
    qmake -r moonlight-qt.pro CONFIG+=host_preview "QMAKE_APPLE_DEVICE_ARCHS=$(uname -m)" \
      -o build/desk-macos-local/Makefile
    # In-source generated moc files can suppress shadow-build moc rules.
    make -C build/desk-macos-local/app -f Makefile.Release compiler_moc_source_make_all
    # qmake changes VERSION_STR flags without invalidating existing objects.
    # Rebuild its three consumers when the version changes (or no stamp exists).
    if ! cmp -s app/version.txt build/desk-macos-local/.compiled-version; then
      rm -f build/desk-macos-local/app/release/main.o \
        build/desk-macos-local/app/release/autoupdatechecker.o \
        build/desk-macos-local/app/release/systemproperties.o
    fi
    make -C build/desk-macos-local release -j8
    cp app/version.txt build/desk-macos-local/.compiled-version
    ;;
  --install-only) ;;
  *) echo 'Usage: bash scripts/dev-macos.sh [--install-only]' >&2; exit 2 ;;
esac
source_app="$PWD/build/desk-macos-local/app/Desk.app"
test -x "$source_app/Contents/MacOS/Desk"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$source_app/Contents/Info.plist")" = io.github.coderleexl.Desk

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
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f /Applications/Desk.app
mdimport /Applications/Desk.app
cmp "$source_app/Contents/MacOS/Desk" /Applications/Desk.app/Contents/MacOS/Desk
open /Applications/Desk.app
echo 'Updated /Applications/Desk.app and refreshed Spotlight.'
