#!/usr/bin/env bash
set -euo pipefail
export BRANCH=bundled
bash scripts/apply-sunshine-patches.sh
export BUILD_VERSION="$(git -C third_party/sunshine describe --tags --exact-match)"
export COMMIT="$(git -C third_party/sunshine rev-parse HEAD)"
# Cache compiler outputs only. Always configure, link, install and test this checkout.
# Explicit empty launchers also disable a previous cached configuration for local builds.
cache_launcher=()
if [[ "${DESK_USE_CCACHE:-0}" == 1 ]]; then
  command -v ccache >/dev/null
  mkdir -p build/ccache-sunshine
  export CCACHE_DIR="$(cygpath -am "$PWD/build/ccache-sunshine")"
  export CCACHE_BASEDIR="$(cygpath -am "$PWD")"
  export CCACHE_COMPILERCHECK=content
  export CCACHE_MAXSIZE=1G
  export CCACHE_NAMESPACE=desk-sunshine-windows-release-v1
  ccache --zero-stats
  cache_launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  report_cache_stats() {
    ccache --show-stats --verbose > build/sunshine-ccache-stats.txt || true
    cat build/sunshine-ccache-stats.txt
  }
  trap report_cache_stats EXIT
else
  cache_launcher=(-DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=)
fi
cmake -S third_party/sunshine -B build/sunshine-windows -G Ninja "${cache_launcher[@]}" \
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
printf 'Upstream: https://github.com/LizardByte/Sunshine\n%s\nDesk modifications: https://github.com/coderleexl/moonlight-qt/tree/%s/patches/sunshine\n' "$COMMIT" "$(git rev-parse HEAD)" > build/sunshine-payload/licenses/SOURCE.txt
# Check the deployable copy: accidental MSYS2 runtime dependencies are a packaging error.
ldd build/sunshine-payload/sunshine.exe | tee build/sunshine-linkage.txt
if grep -E '=> not found|=> /(ucrt64|mingw64|clang64)/bin/' build/sunshine-linkage.txt; then
  echo 'Sunshine has unpackaged runtime dependencies' >&2
  exit 1
fi

# Keep test headers outside the shipped host payload. setup-msys2 chooses its own root.
mkdir -p build/native-test-deps/include
cp -a "${MINGW_PREFIX:?}/include/nlohmann" build/native-test-deps/include/
