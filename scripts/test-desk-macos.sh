#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
mountpoint="$repo_root/build/desk-dmg-mounted"
mkdir -p "$mountpoint"
hdiutil attach build/dist/Desk-macOS-arm64.dmg -mountpoint "$mountpoint" -nobrowse
trap 'hdiutil detach "$mountpoint"' EXIT
app="$mountpoint/Desk.app"
codesign --verify --deep --strict "$app"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist")" = io.github.coderleexl.Desk
# Bundles must not rely on the build machine's Homebrew/Qt installation.
python3 - "$app" <<'PY'
from pathlib import Path
import subprocess
import sys
root = Path(sys.argv[1])
for path in root.rglob('*'):
    if path.is_file() and not path.is_symlink():
        kind = subprocess.check_output(['file', '-b', str(path)], text=True)
        if 'Mach-O' in kind:
            libs = subprocess.check_output(['otool', '-L', str(path)], text=True)
            assert not any(prefix in libs for prefix in ('/opt/homebrew/', '/usr/local/', '/Users/runner/')), libs
PY
python3 scripts/test-desk-unix.py "$app/Contents/MacOS/Desk" \
  "$app/Contents/Helpers/Sunshine.app/Contents/MacOS/Sunshine" \
  "$app/Contents/Helpers/Sunshine.app/Contents/Resources/assets/apps.json"
