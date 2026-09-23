#!/usr/bin/env bash
# Compatibility entry point for the integrated Desk build.
set -euo pipefail
cd "$(dirname "$0")/.."
bash scripts/fetch-desk-source.sh macos
bash scripts/build-desk-macos.sh
