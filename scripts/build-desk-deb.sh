#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
jobs="${JOBS:-4}"
python3 scripts/prepare-desk-translations.py
version="$(cat app/version.txt)"
arch="$(dpkg --print-architecture)"
[[ "$arch" == amd64 ]] || { echo 'This DEB build currently targets amd64'; exit 1; }
export BRANCH=bundled
bash scripts/apply-sunshine-patches.sh
export BUILD_VERSION="$(git -C third_party/sunshine describe --tags --exact-match)"
export COMMIT="$(git -C third_party/sunshine rev-parse HEAD)"
cmake -S third_party/sunshine -B build/desk-sunshine-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DSUNSHINE_ASSETS_DIR=/usr/lib/desk/host/sunshine/assets \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
  -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_ENABLE_DRM=OFF -DBOOST_USE_STATIC=ON \
  -DSUNSHINE_PUBLISHER_NAME=coderleexl \
  -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/coderleexl/moonlight-qt \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/coderleexl/moonlight-qt/issues
cmake --build build/desk-sunshine-linux --parallel "$jobs"
mkdir -p build/desk-linux build/dist
cd build/desk-linux
qmake6 -r "$repo_root/moonlight-qt.pro" CONFIG+=host_preview CONFIG+=disable-libplacebo PREFIX=/usr
make release -j"$jobs"
stage="$repo_root/build/desk-deb-root"
mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/usr/lib/desk/host/sunshine" \
  "$stage/usr/share/applications" "$stage/usr/share/icons/hicolor/scalable/apps" \
  "$stage/usr/share/doc/desk" "$stage/usr/lib/udev/rules.d" "$stage/usr/lib/modules-load.d"
install -m755 app/desk "$stage/usr/bin/desk"
install -m755 "$repo_root/build/desk-sunshine-linux/sunshine" "$stage/usr/lib/desk/host/sunshine/sunshine"
cp -aL "$repo_root/build/desk-sunshine-linux/assets" "$stage/usr/lib/desk/host/sunshine/"
install -m644 "$repo_root/packaging/linux/desk.desktop" "$stage/usr/share/applications/"
install -m644 "$repo_root/app/res/moonlight.svg" "$stage/usr/share/icons/hicolor/scalable/apps/io.github.coderleexl.Desk.svg"
install -m644 "$repo_root/packaging/linux/70-desk-input.rules" "$stage/usr/lib/udev/rules.d/"
printf 'uinput\nuhid\n' > "$stage/usr/lib/modules-load.d/desk.conf"
install -m755 "$repo_root/packaging/linux/postinst" "$stage/DEBIAN/postinst"
cp "$repo_root/LICENSE" "$stage/usr/share/doc/desk/Moonlight-LICENSE"
cp "$repo_root/third_party/sunshine/LICENSE" "$stage/usr/share/doc/desk/Sunshine-LICENSE"
printf 'Upstream: https://github.com/LizardByte/Sunshine\n%s\nDesk modifications: https://github.com/coderleexl/moonlight-qt/tree/%s/patches/sunshine\n' "$COMMIT" "$(git rev-parse HEAD)" > "$stage/usr/share/doc/desk/SOURCE.txt"
cat > "$stage/usr/share/doc/desk/README" <<'EOF'
Desk for Ubuntu 24.04 amd64
Includes Sunshine; no separate host installation is needed.
Start hosting in Desk. Disable Local test only to allow LAN connections.
The host exits with Desk. No system service or automatic startup is installed.
X11 and portal/Wayland capture are included. Direct KMS capture and NVFBC are not enabled.
Wayland capture may require approval in the desktop portal.
Input uses uinput/uhid; reconnect your desktop session if input permissions are not yet applied.
No broad cap_sys_admin capability is granted to the host.
GPU streaming must be verified on the target hardware.
EOF
# Resolve ELF dependencies from the actual installed system packages.
mkdir -p debian
cat > debian/control <<'EOF'
Source: desk
Section: net
Priority: optional
Maintainer: coderleexl <coderleexl@users.noreply.github.com>

Package: desk
Architecture: amd64
Description: Desk remote desktop streaming
EOF
elf_deps="$(dpkg-shlibdeps -O -e"$stage/usr/bin/desk" -e"$stage/usr/lib/desk/host/sunshine/sunshine" | sed -n 's/^shlibs:Depends=//p')"
test -n "$elf_deps"
cat > "$stage/DEBIAN/control" <<EOF
Package: desk
Version: $version
Architecture: $arch
Section: net
Priority: optional
Maintainer: coderleexl <coderleexl@users.noreply.github.com>
Homepage: https://github.com/coderleexl/moonlight-qt
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Depends: $elf_deps, qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtquick-layouts, qml6-module-qtqml-workerscript, qml6-module-qtqml-models, qt6-qpa-plugins, qt6-wayland, libqt6svg6, udev, kmod
Recommends: xdg-desktop-portal
Description: Desk remote desktop streaming with integrated Sunshine
 Native Qt desktop client and a private, application-managed Sunshine host.
 Built for Ubuntu 24.04 amd64. No Sunshine system service is installed.
EOF
chmod -R go-w "$stage"
dpkg-deb --root-owner-group --build "$stage" "$repo_root/build/dist/desk_${version}_${arch}.deb"
