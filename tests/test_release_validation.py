"""Publishing must stop before mutating GitHub when build artifacts disagree."""
import hashlib
import json
import os
from pathlib import Path
import runpy
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


if __name__ == '__main__':
    unittest.main()
