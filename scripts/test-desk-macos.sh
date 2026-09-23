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
            # otool prints the inspected file's absolute path as an unindented
            # heading. Only indented entries describe linked libraries.
            dependencies = [line.strip() for line in libs.splitlines() if line.startswith('\t')]
            external = [line for line in dependencies
                        if line.startswith(('/opt/homebrew/', '/usr/local/', '/Users/runner/'))]
            assert not external, f'{path}: external dependencies: {external}'
PY
python3 scripts/test-desk-unix.py "$app/Contents/MacOS/Desk" \
  "$app/Contents/Helpers/Sunshine.app/Contents/MacOS/Sunshine" \
  "$app/Contents/Helpers/Sunshine.app/Contents/Resources/assets/apps.json"
