#!/bin/bash
# Development build: one Moonlight.app with a private Sunshine helper.
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo 'The integrated host prototype currently supports macOS only.' >&2
    exit 1
fi
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
for tool in git cmake ninja pkg-config brew qmake macdeployqt npm; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
build_dir="$repo_root/build/sunshine"
stage_dir="$repo_root/build/sunshine-stage"
app_bundle="$repo_root/app/Moonlight.app"
jobs="${JOBS:-8}"

git submodule update --init third_party/sunshine
git -C third_party/sunshine submodule update --init --recursive --jobs 4 \
    third-party/build-deps third-party/libdisplaydevice third-party/libvirtualhid \
    third-party/lizardbyte-common third-party/moonlight-common-c \
    third-party/Simple-Web-Server third-party/TPCircularBuffer

sunshine_tag="$(git -C third_party/sunshine describe --tags --exact-match)"
BRANCH=bundled BUILD_VERSION="$sunshine_tag" \
COMMIT="$(git -C third_party/sunshine rev-parse HEAD)" TAG="$sunshine_tag" \
cmake -S third_party/sunshine -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$stage_dir" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_BUILD_HOMEBREW=OFF \
    -DSUNSHINE_PUBLISHER_NAME=coderleexl \
    -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/coderleexl/moonlight-qt \
    -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/coderleexl/moonlight-qt/issues \
    "$@"
cmake --build "$build_dir" --parallel "$jobs"
# Full install is required: common assets and web UI are not in Runtime alone.
SHOULD_SIGN=false cmake --install "$build_dir"
helper_bundle="$stage_dir/Sunshine.app"
test -x "$helper_bundle/Contents/MacOS/Sunshine"
test -f "$helper_bundle/Contents/Resources/assets/apps.json"
test -d "$helper_bundle/Contents/Resources/assets/web"

qmake moonlight-qt.pro
make release -j"$jobs"
macdeployqt "$app_bundle" -qmldir="$repo_root/app/gui" -no-codesign
mkdir -p "$app_bundle/Contents/Helpers"
ditto "$helper_bundle" "$app_bundle/Contents/Helpers/Sunshine.app"
license_dir="$app_bundle/Contents/Helpers/Sunshine.app/Contents/Resources/licenses"
mkdir -p "$license_dir"
cp third_party/sunshine/LICENSE "$license_dir/Sunshine-LICENSE"
{
    echo 'Sunshine source: https://github.com/LizardByte/Sunshine'
    git -C third_party/sunshine rev-parse HEAD
    echo 'Integration source: https://github.com/coderleexl/moonlight-qt'
} > "$license_dir/SOURCE.txt"
# Local development signing only. Distribution needs Developer ID + notarization.
xattr -cr "$app_bundle"
codesign --force --deep --sign - "$app_bundle/Contents/Helpers/Sunshine.app"
codesign --force --deep --sign - "$app_bundle"
codesign --verify --deep --strict "$app_bundle"
echo "Integrated development build: $app_bundle"
