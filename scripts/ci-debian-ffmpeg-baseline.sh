#!/usr/bin/env bash
# CI-only compatibility baseline; this script is never shipped in the DEB.
set -euo pipefail
[[ "${GITHUB_ACTIONS:-}" == true && "$(id -u)" == 0 && -f /.dockerenv ]] || {
    echo 'Run only in the disposable GitHub Actions Debian container' >&2
    exit 1
}
. /etc/os-release
[[ "$ID:$VERSION_ID" == debian:13 ]] || exit 1

# Keep normal Debian repositories for everything except the FFmpeg family.
# Retain signature verification while accepting the archived Release expiry.
cat > /etc/apt/sources.list.d/desk-ci-ffmpeg.sources <<'EOF'
Types: deb
URIs: https://snapshot.debian.org/archive/debian-security/20260401T000000Z/
Suites: trixie-security
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
Check-Valid-Until: no
EOF
cat > /etc/apt/preferences.d/desk-ci-ffmpeg <<'EOF'
Package: libavcodec61 libavcodec-dev libavformat61 libavformat-dev libavutil59 libavutil-dev libswscale8 libswscale-dev libswresample5 libswresample-dev libavfilter10 libavfilter-dev libavdevice61 libavdevice-dev libpostproc58 libpostproc-dev ffmpeg
Pin: version 7:7.1.3-0+deb13u1
Pin-Priority: 1001

Package: *
Pin: origin snapshot.debian.org
Pin-Priority: 50
EOF
apt-get -o Acquire::Retries=3 update
for package in libavcodec61 libavcodec-dev libavutil59 libswscale8; do
    candidate="$(LC_ALL=C apt-cache policy "$package" | sed -n 's/^  Candidate: //p')"
    [[ "$candidate" == 7:7.1.3-0+deb13u1 ]] || {
        echo "Wrong FFmpeg baseline: $package candidate is $candidate" >&2
        exit 1
    }
done
