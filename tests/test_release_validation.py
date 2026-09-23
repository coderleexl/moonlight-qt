"""Publishing must stop before mutating GitHub when build artifacts disagree."""
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/publish-desk-release.py'
SHA = 'a' * 40


class ReleaseValidationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.previous = Path.cwd()
        os.chdir(self.temp.name)
        self.addCleanup(os.chdir, self.previous)
        Path('app').mkdir()
        Path('app/version.txt').write_text('6.1.1\n')
        self.root = Path('build/release')
        self.root.mkdir(parents=True)
        for platform, names in {
            'windows': ['Desk-Setup-x64.exe', 'Desk-x64.zip'],
            'macos': ['Desk-macOS-arm64.dmg'],
            'linux-ubuntu24.04': ['desk_6.1.1_ubuntu24.04_amd64.deb'],
            'linux-debian13': ['desk_6.1.1_debian13_amd64.deb'],
        }.items():
            files = {}
            for name in names:
                content = ('test package ' + name).encode()
                (self.root / name).write_bytes(content)
                files[name] = dict(size=len(content), sha256=hashlib.sha256(content).hexdigest())
            (self.root / f'manifest-{platform}.json').write_text(json.dumps(
                dict(platform=platform, version='6.1.1', commit=SHA, files=files)))

    def reject(self, error):
        with patch.dict(os.environ, {'GITHUB_REPOSITORY': 'test/desk', 'GITHUB_REF': 'refs/heads/master'}), \
             patch('subprocess.check_output', return_value=SHA + '\n') as reads, \
             patch('subprocess.run') as mutations:
            with self.assertRaises(error):
                runpy.run_path(str(SCRIPT), run_name='__main__')
            mutations.assert_not_called()
            self.assertEqual(reads.call_count, 1)  # Only git rev-parse; no GitHub calls.

    def test_missing_platform_stops_publication(self):
        (self.root / 'manifest-linux-debian13.json').unlink()
        self.reject(FileNotFoundError)

    def test_ubuntu_package_cannot_replace_debian_package(self):
        path = self.root / 'manifest-linux-debian13.json'
        data = json.loads(path.read_text())
        data['files'] = json.loads((self.root / 'manifest-linux-ubuntu24.04.json').read_text())['files']
        path.write_text(json.dumps(data))
        self.reject(RuntimeError)

    def test_different_commit_stops_publication(self):
        path = self.root / 'manifest-macos.json'
        data = json.loads(path.read_text())
        data['commit'] = 'b' * 40
        path.write_text(json.dumps(data))
        self.reject(RuntimeError)

    def test_corrupt_package_stops_publication(self):
        (self.root / 'Desk-x64.zip').write_bytes(b'corrupt')
        self.reject(RuntimeError)

    def published_release(self, missing_public_assets=False, draft=False):
        metadata = dict(id=42, draft=False, assets=[], html_url='https://github.com/test/desk/releases/tag/desk-v6.1.1')

        def assets():
            # The script creates SHA256SUMS.txt before querying GitHub.
            return [dict(name=p.name, size=p.stat().st_size, state='uploaded',
                         digest='sha256:' + hashlib.sha256(p.read_bytes()).hexdigest(),
                         browser_download_url='https://example.invalid/' + p.name)
                    for p in self.root.iterdir()
                    if p.name.startswith(('Desk-', 'desk_')) or p.name == 'SHA256SUMS.txt']

        def read(cmd, **kwargs):
            if cmd == ['git', 'rev-parse', 'HEAD']:
                return SHA + '\n'
            if cmd[:3] == ['gh', 'release', 'view']:
                self.assertEqual(cmd[-1], 'databaseId')
                return json.dumps(dict(databaseId=42))
            if cmd[:3] in (['gh', 'release', 'upload'], ['gh', 'release', 'edit']):
                self.assertTrue(draft)
                return ''
            self.assertEqual(cmd[:2], ['gh', 'api'])
            self.assertIn('/releases/42/assets?', cmd[2])
            return json.dumps(assets() if '/assets?' in cmd[2] else metadata)

        def run(cmd, **kwargs):
            if cmd[:3] == ['git', 'rev-parse', '--verify']:
                return subprocess.CompletedProcess(cmd, 0, stdout=SHA + '\n')
            self.assertEqual(cmd[:3], ['gh', 'release', 'view'])
            return subprocess.CompletedProcess(cmd, 0, stdout=json.dumps(dict(isDraft=draft)))

        def public(url, **kwargs):
            data = ([] if missing_public_assets else assets()) if '/assets?' in url else metadata
            return io.BytesIO(json.dumps(data).encode())

        with patch.dict(os.environ, dict(GITHUB_REPOSITORY='test/desk', GITHUB_REF='refs/heads/master',
                                        GITHUB_RUN_ID='123', GITHUB_STEP_SUMMARY=str(self.root / 'summary.md'))), \
             patch('subprocess.check_output', side_effect=read), \
             patch('subprocess.run', side_effect=run), \
             patch('urllib.request.urlopen', side_effect=public), \
             patch('time.sleep'):
            runpy.run_path(str(SCRIPT), run_name='__main__')

    def test_empty_embedded_assets_uses_asset_collection(self):
        self.published_release()
        self.assertIn('Published:', (self.root / 'summary.md').read_text())

    def test_draft_resolves_by_database_id_before_publishing(self):
        self.published_release(draft=True)
        self.assertIn('Published:', (self.root / 'summary.md').read_text())

    def test_missing_public_asset_collection_still_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'not publicly complete'):
            self.published_release(missing_public_assets=True)


if __name__ == '__main__':
    unittest.main()
