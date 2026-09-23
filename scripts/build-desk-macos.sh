#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
jobs="${JOBS:-3}"
export BRANCH=bundled
export BUILD_VERSION="$(git -C third_party/sunshine describe --tags --exact-match)"
export COMMIT="$(git -C third_party/sunshine rev-parse HEAD)"
cmake -S third_party/sunshine -B build/desk-sunshine-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_INSTALL_PREFIX="$repo_root/build/desk-sunshine-stage" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
  -DSUNSHINE_BUILD_HOMEBREW=OFF -DBOOST_USE_STATIC=ON \
  -DSUNSHINE_PUBLISHER_NAME=coderleexl \
  -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/coderleexl/moonlight-qt \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/coderleexl/moonlight-qt/issues
cmake --build build/desk-sunshine-macos --parallel "$jobs"
SHOULD_SIGN=false cmake --install build/desk-sunshine-macos
mkdir -p build/desk-macos build/dist
cd build/desk-macos
qmake -r "$repo_root/moonlight-qt.pro" CONFIG+=host_preview QMAKE_APPLE_DEVICE_ARCHS=arm64
make release -j"$jobs"
app_bundle="$PWD/app/Desk.app"
macdeployqt "$app_bundle" -qmldir="$repo_root/app/gui" -no-codesign
mkdir -p "$app_bundle/Contents/Helpers" "$app_bundle/Contents/Resources/licenses"
ditto "$repo_root/build/desk-sunshine-stage/Sunshine.app" "$app_bundle/Contents/Helpers/Sunshine.app"
cp "$repo_root/LICENSE" "$app_bundle/Contents/Resources/licenses/Moonlight-LICENSE.txt"
cp "$repo_root/third_party/sunshine/LICENSE" "$app_bundle/Contents/Resources/licenses/Sunshine-LICENSE.txt"
printf 'https://github.com/LizardByte/Sunshine\n%s\n' "$COMMIT" > "$app_bundle/Contents/Resources/licenses/SOURCE.txt"
xattr -cr "$app_bundle"
codesign --force --deep --sign - "$app_bundle/Contents/Helpers/Sunshine.app"
codesign --force --deep --sign - "$app_bundle"
codesign --verify --deep --strict "$app_bundle"
# A simple drag-to-Applications disk image, requiring no third-party DMG tooling.
image_root="$repo_root/build/desk-dmg-root"
mkdir -p "$image_root"
ditto "$app_bundle" "$image_root/Desk.app"
ln -sfn /Applications "$image_root/Applications"
hdiutil create -volname Desk -srcfolder "$image_root" -format UDZO -ov "$repo_root/build/dist/Desk-macOS-arm64.dmg"
