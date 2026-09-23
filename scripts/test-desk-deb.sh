#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sudo apt-get install -y ./build/dist/desk_*_amd64.deb
test "$(dpkg-query -W -f='${Status}' desk)" = 'install ok installed'
desktop-file-validate /usr/share/applications/desk.desktop
export XDG_RUNTIME_DIR="$(mktemp -d)"
trap 'rm -rf "$XDG_RUNTIME_DIR"' EXIT
xvfb-run -a "${DESK_TEST_PYTHON:-python3}" scripts/test-desk-unix.py /usr/bin/desk \
  /usr/lib/desk/host/sunshine/sunshine /usr/lib/desk/host/sunshine/assets/apps.json
sudo apt-get remove -y desk
test ! -e /usr/bin/desk
