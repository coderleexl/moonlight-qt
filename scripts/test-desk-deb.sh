#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sudo apt-get install -y ./build/dist/desk_*_amd64.deb
test "$(dpkg-query -W -f='${Status}' desk)" = 'install ok installed'
if [[ -n "${DESK_EXPECT_FFMPEG:-}" ]]; then
  # Catch dependency resolution silently upgrading away from the test baseline.
  for package in libavcodec61 libavutil59 libswscale8; do
    test "$(dpkg-query -W -f='${Version}' "$package")" = "$DESK_EXPECT_FFMPEG"
  done
fi
desktop-file-validate /usr/share/applications/desk.desktop
export XDG_RUNTIME_DIR="$(mktemp -d)"
trap 'rm -rf "$XDG_RUNTIME_DIR"' EXIT
# Resolve shared-library symbols at startup, including FFmpeg calls that a GUI
# smoke test might otherwise leave unbound until an actual streaming session.
export LD_BIND_NOW=1
xvfb-run -a "${DESK_TEST_PYTHON:-python3}" scripts/test-desk-unix.py /usr/bin/desk \
  /usr/lib/desk/host/sunshine/sunshine /usr/lib/desk/host/sunshine/assets/apps.json
sudo apt-get remove -y desk
test ! -e /usr/bin/desk
