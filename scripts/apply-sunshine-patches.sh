#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Later patches may change the context of earlier patches. Check the whole
# applied stack in a temporary Git index so repeat builds remain idempotent.
if python3 - <<'PYTHON'
import os
from pathlib import Path
import subprocess
import tempfile
patches = sorted(Path('patches/sunshine').glob('*.patch'))
paths = sorted({line[6:] for patch in patches for line in patch.read_text().splitlines()
                if line.startswith('+++ b/')})
with tempfile.TemporaryDirectory(prefix='desk-patch-index-') as directory:
    env = dict(os.environ, GIT_INDEX_FILE=directory + '/index')
    def git(*args):
        return subprocess.run(['git', '-C', 'third_party/sunshine', *args], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    applied = git('read-tree', 'HEAD') and git('add', '-A', '--', *paths)
    for patch in reversed(patches):
        if not applied or not git('apply', '--cached', '--reverse', str(patch.resolve())):
            raise SystemExit(1)
PYTHON
then
  exit 0
fi
for patch in patches/sunshine/*.patch; do
  if git -C third_party/sunshine apply --reverse --check "$PWD/$patch" 2>/dev/null; then
    continue
  fi
  git -C third_party/sunshine apply --check "$PWD/$patch"
  git -C third_party/sunshine apply "$PWD/$patch"
done
