#!/usr/bin/env python3
"""Publish only a complete, tested, same-commit set of Desk packages."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

repo = os.environ['GITHUB_REPOSITORY']
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
version = Path('app/version.txt').read_text().strip()
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    raise RuntimeError('app/version.txt must contain a numeric major.minor.patch version')
tag = f'desk-v{version}'
ref = os.environ.get('GITHUB_REF', '')
if ref.startswith('refs/tags/') and ref != f'refs/tags/{tag}':
    raise RuntimeError(f'Tag {ref} does not match app/version.txt ({tag})')
root = Path('build/release')
expected = {
    'windows': {'Desk-Setup-x64.exe', 'Desk-x64.zip'},
    'macos': {'Desk-macOS-arm64.dmg'},
    'linux-ubuntu24.04': {f'desk_{version}_ubuntu24.04_amd64.deb'},
    'linux-debian13': {f'desk_{version}_debian13_amd64.deb'},
}
checksums = {}
for platform, names in expected.items():
    manifest = json.loads((root / f'manifest-{platform}.json').read_text())
    if (manifest['commit'], manifest['version'], manifest['platform']) != (sha, version, platform):
        raise RuntimeError(f'{platform}: package source identity does not match this workflow')
    if set(manifest['files']) != names:
        raise RuntimeError(f'{platform}: incomplete or unexpected package list')
    for name, metadata in manifest['files'].items():
        path = root / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if path.stat().st_size != metadata['size'] or digest != metadata['sha256']:
            raise RuntimeError(f'Package checksum mismatch: {name}')
        checksums[name] = digest
(root / 'SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name, digest in sorted(checksums.items())))
checksums['SHA256SUMS.txt'] = hashlib.sha256((root / 'SHA256SUMS.txt').read_bytes()).hexdigest()

def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)

# Create the tag through Git before publishing. Release creation verifies it and
# never attempts to retarget an existing release through the Releases API.
existing = subprocess.run(['git', 'rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}'], text=True, capture_output=True)
if existing.returncode == 0:
    if existing.stdout.strip() != sha:
        raise RuntimeError(f'{tag} already points to another commit. Increment app/version.txt before publishing.')
else:
    subprocess.run(['git', 'tag', tag, sha], check=True)
    subprocess.run(['git', 'push', 'origin', f'refs/tags/{tag}'], check=True)
notes = root / 'release-notes.md'
notes.write_text(f'''## Desk {version}

同一提交构建的三个平台版本，均内置从源码编译的 Sunshine。

| 平台 | 下载 |
| --- | --- |
| Windows x64 | `Desk-Setup-x64.exe`（安装版）或 `Desk-x64.zip`（免安装） |
| macOS Apple Silicon | `Desk-macOS-arm64.dmg`，将 Desk 拖入 Applications |
| Ubuntu 24.04 amd64 | `desk_{version}_ubuntu24.04_amd64.deb`，使用 `sudo apt install ./desk_{version}_ubuntu24.04_amd64.deb` |
| Debian 13 amd64 | `desk_{version}_debian13_amd64.deb`，使用 `sudo apt install ./desk_{version}_debian13_amd64.deb` |

两个 DEB 分别在对应发行版编译并进行干净环境安装测试，请按系统版本选择，不要混装。

### 主机功能

在“本机”页启动内置 Sunshine；允许其他局域网设备连接时，先关闭“仅本机测试”。连接地址为 `主机 IP:48989`，管理网页为主机上的 `https://127.0.0.1:48990`。Desk 主机支持设备识别码与访问密码连接，无需网页输入配对 PIN；连接原版 Sunshine 时仍使用 PIN 配对。

退出 Desk 会停止它启动的主机进程，不安装 Sunshine 系统服务。macOS 需授予屏幕录制及辅助功能权限；Linux 使用 X11 或 Wayland/Portal 捕获，并为活动桌面用户安装 uinput/uhid 规则，不授予 cap_sys_admin。Linux 直接 KMS 捕获和 NVFBC 未启用。

### 验证与限制

三端通过构建、主机进程管理、客户端及内置 Sunshine Web 启动检查；Windows/DEB 另有安装和卸载检查；两个 DEB 还分别在干净的 Debian 13 / Ubuntu 24.04 容器验证依赖安装、GUI 启动和主机授权协议。CI 不验证真实 GPU 串流、锁屏或 UAC。

Windows 安装包未签名；macOS 为临时签名，未进行 Apple 公证，系统可能要求在安全设置中确认打开。macOS 包在 macOS 26 ARM64 上构建；其他系统版本与 Linux 发行版兼容性需分别验证。

提交：`{sha}`。完整性校验见 `SHA256SUMS.txt`。
[构建与测试记录](https://github.com/{repo}/actions/runs/{os.environ['GITHUB_RUN_ID']}) · [Sunshine 源码](https://github.com/LizardByte/Sunshine/tree/63d35f702ee9e362e43263742981836ec0710384)
''')
result = subprocess.run(['gh', 'release', 'view', tag, '--repo', repo, '--json', 'isDraft,assets'], text=True, capture_output=True)
if result.returncode:
    gh('release', 'create', tag, '--repo', repo, '--verify-tag', '--draft', '--title', f'Desk {version}', '--notes-file', str(notes))
    draft = True
else:
    draft = json.loads(result.stdout)['isDraft']
if draft:
    gh('release', 'upload', tag, *[str(root / name) for name in checksums], '--repo', repo, '--clobber')

# Query the asset collection explicitly. The release metadata response can
# contain an empty embedded assets array even after all uploads are available.
# Reruns must never replace published packages; verify them instead.
# Draft releases must be resolved through the authenticated release command.
release = json.loads(gh('release', 'view', tag, '--repo', repo, '--json', 'databaseId'))
release['id'] = release['databaseId']
assets_path = f"repos/{repo}/releases/{release['id']}/assets?per_page=100"
assets = {asset['name']: asset for asset in json.loads(gh('api', assets_path))}
if set(assets) != set(checksums):
    raise RuntimeError('Release assets do not exactly match the complete package set')
for name, digest in checksums.items():
    asset = assets[name]
    if asset['state'] != 'uploaded' or asset['size'] != (root / name).stat().st_size:
        raise RuntimeError(f'Incomplete release upload: {name}')
    if asset.get('digest') != f'sha256:{digest}':
        raise RuntimeError(f'Release asset digest mismatch: {name}')
if draft:
    gh('release', 'edit', tag, '--repo', repo, '--title', f'Desk {version}', '--notes-file', str(notes), '--draft=false', '--prerelease=false', '--latest')

# Read without authentication so a remaining draft cannot be mistaken for a public release.
public_url = f'https://api.github.com/repos/{repo}/releases/tags/{tag}'
for attempt in range(6):
    try:
        with urllib.request.urlopen(public_url, timeout=20) as response:
            public = json.load(response)
        if public['draft'] or public['id'] != release['id']:
            raise RuntimeError('Release is not publicly available')
        with urllib.request.urlopen(f'https://api.github.com/{assets_path}', timeout=20) as response:
            public['assets'] = json.load(response)
        if {a['name'] for a in public['assets']} != set(checksums):
            raise RuntimeError('Published release is not publicly complete')
        for asset in public['assets']:
            if asset.get('digest') != f"sha256:{checksums[asset['name']]}" or asset['state'] != 'uploaded':
                raise RuntimeError(f"Public asset verification failed: {asset['name']}")
        break
    except Exception:
        if attempt == 5:
            raise
        time.sleep(5)
url = public['html_url']
with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
    summary.write(f'## Published: [Desk {version}]({url})\n\n')
    for asset in public['assets']:
        summary.write(f"- [{asset['name']}]({asset['browser_download_url']})\n")
print(f'Published and verified: {url}')
