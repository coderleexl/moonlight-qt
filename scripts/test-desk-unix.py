#!/usr/bin/env python3
"""Smoke-test installed Desk and its bundled host, without claiming GPU streaming."""
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.request

client, helper, apps = map(Path, sys.argv[1:4])
assert all(p.is_file() for p in (client, helper, apps))
assert subprocess.check_output([str(client), '--version'], text=True).startswith('Desk ')
logs = Path('build/desk-smoke')
logs.mkdir(parents=True, exist_ok=True)
owned = []

def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

try:
    with (logs / 'client.log').open('w') as output:
        process = subprocess.Popen([str(client)], stdout=output, stderr=subprocess.STDOUT)
        owned.append(process)
        time.sleep(8)
        assert process.poll() is None, 'Desk exited during startup'
        stop(process)
    text = (logs / 'client.log').read_text()
    assert not any(error in text for error in ('QQmlApplicationEngine failed', 'failed to load component', 'is not installed')), text
    with tempfile.TemporaryDirectory(prefix='desk-host-smoke-') as temp:
        config = Path(temp)
        (config / 'credentials').mkdir()
        shutil.copyfile(apps, config / 'apps.json')
        conf = config / 'sunshine.conf'
        settings = dict(port=48989, bind_address='127.0.0.1', address_family='ipv4',
                        origin_web_ui_allowed='pc', upnp='disabled', sunshine_name='Desk CI',
                        file_apps=config / 'apps.json', file_state=config / 'sunshine_state.json',
                        credentials_file=config / 'sunshine_state.json',
                        pkey=config / 'credentials/key.pem', cert=config / 'credentials/cert.pem',
                        log_path=config / 'sunshine.log')
        conf.write_text(''.join(f'{key} = {value}\n' for key, value in settings.items()))
        with (logs / 'host.log').open('w') as output:
            process = subprocess.Popen([str(helper), str(conf)], cwd=helper.parent,
                                       stdout=output, stderr=subprocess.STDOUT)
            owned.append(process)
            try:
                # Only this test's loopback endpoint uses an unverified, self-signed certificate.
                context = ssl._create_unverified_context()
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                                     urllib.request.HTTPSHandler(context=context))
                for attempt in range(45):
                    assert process.poll() is None, 'Bundled Sunshine exited during startup'
                    try:
                        with opener.open('https://127.0.0.1:48990/', timeout=2) as response:
                            if response.status == 200:
                                break
                    except Exception:
                        time.sleep(1)
                else:
                    raise RuntimeError('Bundled Sunshine web interface did not become ready')
            finally:
                stop(process)
    (logs / 'result.txt').write_text('Desk GUI and bundled Sunshine web startup passed. GPU streaming was not tested.\n')
    print((logs / 'result.txt').read_text())
finally:
    for process in owned:
        stop(process)
