#!/usr/bin/env bash
set -euo pipefail
export BRANCH=bundled
export BUILD_VERSION="$(git -C third_party/sunshine describe --tags --exact-match)"
export COMMIT="$(git -C third_party/sunshine rev-parse HEAD)"
cmake -S third_party/sunshine -B build/sunshine-windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF \
  -DSUNSHINE_ENABLE_TRAY=OFF -DSUNSHINE_USE_STATIC_QT=OFF \
  -DSUNSHINE_ASSETS_DIR=assets \
  -DSUNSHINE_PUBLISHER_NAME=coderleexl \
  -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/coderleexl/moonlight-qt \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/coderleexl/moonlight-qt/issues
cmake --build build/sunshine-windows --parallel 4
cmake --install build/sunshine-windows --prefix "$PWD/build/sunshine-payload"
mkdir -p build/sunshine-payload/licenses
cp third_party/sunshine/LICENSE build/sunshine-payload/licenses/Sunshine-LICENSE.txt
printf 'https://github.com/LizardByte/Sunshine\n%s\n' "$COMMIT" > build/sunshine-payload/licenses/SOURCE.txt
# Check the deployable copy: accidental MSYS2 runtime dependencies are a packaging error.
ldd build/sunshine-payload/sunshine.exe | tee build/sunshine-linkage.txt
if grep -E '=> not found|=> /(ucrt64|mingw64|clang64)/bin/' build/sunshine-linkage.txt; then
  echo 'Sunshine has unpackaged runtime dependencies' >&2
  exit 1
fi
