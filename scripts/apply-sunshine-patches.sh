#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for patch in patches/sunshine/*.patch; do
  if git -C third_party/sunshine apply --reverse --check "$PWD/$patch" 2>/dev/null; then
    continue
  fi
  git -C third_party/sunshine apply --check "$PWD/$patch"
  git -C third_party/sunshine apply "$PWD/$patch"
done
