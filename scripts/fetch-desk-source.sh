#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git -c core.longpaths=true submodule update --init --recursive --jobs 4 moonlight-common-c/moonlight-common-c qmdnsengine/qmdnsengine app/SDL_GameControllerDB
git -c core.longpaths=true submodule update --init third_party/sunshine
common=(third-party/build-deps third-party/libdisplaydevice third-party/libvirtualhid third-party/lizardbyte-common third-party/moonlight-common-c third-party/Simple-Web-Server)
case "${1:?platform required}" in
  windows) extra=(third-party/nvapi third-party/ViGEmClient) ;;
  macos) extra=(third-party/TPCircularBuffer) ;;
  linux) extra=(third-party/glad third-party/wayland-protocols third-party/wlr-protocols third-party/plasma-wayland-protocols) ;;
  *) exit 2 ;;
esac
git -c core.longpaths=true -C third_party/sunshine submodule update --init --recursive --depth 1 --jobs 4 "${common[@]}" "${extra[@]}"
