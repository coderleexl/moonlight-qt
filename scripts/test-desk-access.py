#!/usr/bin/env python3
"""Exercise the real bundled host's password handshake; no capture/GPU needed.

Runs only in CI against an explicitly provided helper, with temporary identities.
Never reads the user's Desk configuration or sends passwords over HTTP.
"""
import datetime
import base64
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import uuid
import xml.etree.ElementTree as ET

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.x509.oid import NameOID


def request(path, args, context=None):
    scheme, port = ('https', 48984) if context else ('http', 48989)
    opener = urllib.request.build_opener(
        urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context))
    url = f'{scheme}://127.0.0.1:{port}/{path}?' + urllib.parse.urlencode(args)
    with opener.open(url, timeout=5) as response:
        return ET.fromstring(response.read())


def identity(directory):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Desk CI client')])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(minutes=5))
            .not_valid_after(now + datetime.timedelta(days=1))
            .sign(key, hashes.SHA256()))
    pem = cert.public_bytes(serialization.Encoding.PEM)
    (directory / 'client.pem').write_bytes(pem)
    (directory / 'client.key').write_bytes(key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))
    return key, cert, pem


def handshake(password, key, cert, pem, should_accept):
    session = str(uuid.uuid4())
    salt = secrets.token_bytes(16)
    cipher = Cipher(algorithms.AES(hashlib.sha256(salt + password.encode()).digest()[:16]), modes.ECB())

    def crypt(data, decrypt=False):
        operation = cipher.decryptor() if decrypt else cipher.encryptor()
        return operation.update(data) + operation.finalize()

    def pair(**args):
        tree = request('pair', dict(uniqueid=session, devicename='Desk CI', **args))
        assert tree.get('status_code') == '200', ET.tostring(tree)
        return tree

    tree = pair(phrase='getservercert', salt=salt.hex(), clientcert=pem.hex())
    server_pem = bytes.fromhex(tree.findtext('plaincert'))
    server = x509.load_pem_x509_certificate(server_pem)
    challenge = secrets.token_bytes(16)
    tree = pair(clientchallenge=crypt(challenge).hex())
    response = crypt(bytes.fromhex(tree.findtext('challengeresponse')), decrypt=True)
    assert len(response) == 48
    client_secret = secrets.token_bytes(16)
    digest = hashlib.sha256(response[32:] + cert.signature + client_secret).digest()
    tree = pair(serverchallengeresp=crypt(digest).hex())
    proof = bytes.fromhex(tree.findtext('pairingsecret'))
    server.public_key().verify(proof[16:], proof[:16], padding.PKCS1v15(), hashes.SHA256())
    accepted = hashlib.sha256(challenge + server.signature + proof[:16]).digest() == response[:32]
    assert accepted == should_accept, 'Host password proof did not match expected result'
    if not accepted:
        # Also ensure the host rejects a client that ignores the failed proof.
        tree = pair(clientpairingsecret=(client_secret + key.sign(client_secret, padding.PKCS1v15(), hashes.SHA256())).hex())
        assert tree.findtext('paired') == '0'
        return None
    tree = pair(clientpairingsecret=(client_secret + key.sign(client_secret, padding.PKCS1v15(), hashes.SHA256())).hex())
    assert tree.findtext('paired') == '1'
    return server_pem


def test_file_transfer(directory, context, server_pem):
    """Exercise the real HTTPS route, certificate gate, sandbox and chunk protocol."""
    def files(payload, tls=context, route="files"):
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=tls))
        req = urllib.request.Request('https://127.0.0.1:48984/desk/' + route,
                                     json.dumps(payload).encode(), {'Content-Type': 'application/json'})
        with opener.open(req, timeout=10) as response:
            return response.read()

    # A client may retain its TLS connection after reading a response. The host
    # must frame the response and keep serving other clients without waiting for
    # that client's close_notify (Qt and interrupted clients can delay it).
    for route in ('files', 'clipboard'):
        payload = b'{"op":"list","path":""}' if route == 'files' else b'{"op":"open"}'
        with context.wrap_socket(socket.create_connection(('127.0.0.1', 48984), timeout=3),
                                 server_hostname='localhost') as lingering:
            lingering.sendall((f'POST /desk/{route} HTTP/1.1\r\nHost: localhost\r\n'
                               f'Content-Type: application/json\r\nContent-Length: {len(payload)}\r\n'
                               'Connection: close\r\n\r\n').encode() + payload)
            response = b''
            while b'\r\n\r\n' not in response:
                block = lingering.recv(4096)
                assert block, 'Host closed before sending response headers'
                response += block
            # Deliberately do not reply to the TLS close_notify yet.
            assert json.loads(files({'op': 'list', 'path': ''}))['ok']
            headers = response.split(b'\r\n\r\n', 1)[0].lower()
            assert b'content-length:' in headers, 'Missing response length can stall Qt clients'
    print('HTTPS response framing and delayed TLS shutdown tests passed.')

    unknown = directory / 'unknown'
    unknown.mkdir()
    identity(unknown)
    unpaired = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    unpaired.check_hostname = False
    unpaired.load_verify_locations(cadata=server_pem.decode())
    unpaired.load_cert_chain(unknown / 'client.pem', unknown / 'client.key')
    denied = files({'op': 'list', 'path': ''}, unpaired)
    assert ET.fromstring(denied).get('status_code') == '401', 'Unpaired certificate accessed file endpoint'

    denied_clipboard = files({'op': 'open'}, unpaired, 'clipboard')
    assert ET.fromstring(denied_clipboard).get('status_code') == '401', 'Unpaired certificate accessed clipboard endpoint'
    for operation in ('open', 'exchange', 'read'):
        result = json.loads(files({'op': operation}, route='clipboard'))
        assert not result['ok'] and result['error'] == 'An active streaming session is required', result
    print('Clipboard HTTPS gate passed: unpaired and non-streaming clients rejected.')

    def call(**payload):
        return json.loads(files(payload))
    assert call(op='list', path='')['protocol'] == 1
    for invalid in ('../escape', '/etc/passwd', 'C:/Windows', '.desk-transfers/file', 'file:stream'):
        assert not call(op='stat', path=invalid)['ok']
    assert call(op='mkdir', path='中文 目录')['ok']
    path = '中文 目录/roundtrip.bin'
    data = secrets.token_bytes(600000)
    upload = call(op='begin', path=path, size=len(data), expected='missing')
    assert upload['ok'], upload
    token = upload['token']
    assert not (directory / 'shared' / path).exists(), 'Partial file was published'
    for offset in range(0, len(data), 256 * 1024):
        chunk = data[offset:offset + 256 * 1024]
        assert call(op='write', token=token, offset=offset, data=base64.b64encode(chunk).decode())['ok']
    assert call(op='finish', token=token, sha256=base64.b64encode(hashlib.sha256(data).digest()).decode())['ok']
    info = call(op='stat', path=path)
    downloaded = b''
    while len(downloaded) < info['size']:
        result = call(op='read', path=path, offset=len(downloaded), version=info['version'])
        assert result['ok'], result
        chunk = base64.b64decode(result['data'], validate=True)
        assert chunk, 'Read made no progress'
        downloaded += chunk
    assert downloaded == data
    assert not call(op='begin', path=path, size=0, expected='missing')['ok'], 'Silently overwrote a file'
    token = call(op='begin', path='cancel.bin', size=10, expected='missing')['token']
    assert call(op='cancel', token=token)['ok']
    assert not (directory / 'shared' / 'cancel.bin').exists()
    assert not list((directory / 'shared' / '.desk-transfers').iterdir())
    print('File transfer HTTPS tests passed: certificate authorization, sandbox, UTF-8, chunks, checksum, conflict, cancellation.')


def run(helper, secret):
    with tempfile.TemporaryDirectory(prefix='desk-access-ci-') as temp:
        directory = Path(temp)
        device_id = '123456789'
        (directory / 'credentials').mkdir()
        (directory / 'apps.json').write_text('{"apps": [{"name": "Desktop"}]}')
        settings = dict(port=48989, bind_address='127.0.0.1', address_family='ipv4',
                        origin_web_ui_allowed='pc', upnp='disabled', sunshine_name='Desk CI',
                        file_apps=directory / 'apps.json', file_state=directory / 'state.json',
                        credentials_file=directory / 'state.json',
                        pkey=directory / 'credentials/key.pem', cert=directory / 'credentials/cert.pem',
                        log_path=directory / 'host.log')
        conf = directory / 'sunshine.conf'
        conf.write_text(''.join(f'{k} = {v}\n' for k, v in settings.items()))
        env = dict(os.environ, DESK_ACCESS_PASSWORD=secret, DESK_DEVICE_ID=device_id,
                   DESK_FILE_ROOT=base64.b64encode(str(directory / "shared").encode()).decode())
        with (directory / 'process.log').open('w') as output:
            process = subprocess.Popen([str(helper), str(conf)], cwd=helper.parent,
                                       env=env, stdout=output, stderr=subprocess.STDOUT)
            try:
                for _ in range(60):
                    assert process.poll() is None, 'Host exited before password test'
                    try:
                        info = request('serverinfo', {})
                        break
                    except Exception:
                        time.sleep(1)
                else:
                    raise AssertionError('Host did not become ready')
                assert info.findtext('DeskDeviceId') == device_id
                assert info.findtext('DeskAccessVersion') == '1'
                assert secret.encode() not in ET.tostring(info)
                key, cert, pem = identity(directory)
                handshake(secrets.token_urlsafe(16), key, cert, pem, False)
                state_path = directory / 'state.json'
                if state_path.exists():
                    assert not json.loads(state_path.read_text()).get('root', {}).get('named_devices')
                server = handshake(secret, key, cert, pem, True)
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
                context.check_hostname = False
                context.load_verify_locations(cadata=server.decode())
                context.load_cert_chain(directory / 'client.pem', directory / 'client.key')
                assert request('pair', dict(uniqueid='ci', phrase='pairchallenge'), context).findtext('paired') == '1'
                # A subsequent connection uses the saved certificate, no password.
                assert request('serverinfo', dict(uniqueid='ci'), context).findtext('PairStatus') == '1'
                test_file_transfer(directory, context, server)
                # Repeated cancellation must release pending sessions immediately.
                for _ in range(3):
                    session = str(uuid.uuid4())
                    assert request('pair', dict(uniqueid=session, devicename='Desk CI', phrase='getservercert',
                                               salt=secrets.token_hex(16), clientcert=pem.hex())).get('status_code') == '200'
                    assert request('pair', dict(uniqueid=session, phrase='deskcancel')).get('status_code') == '200'
                # The bounded limiter includes completed/cancelled attempts.
                for _ in range(31):
                    session = str(uuid.uuid4())
                    result = request('pair', dict(uniqueid=session, devicename='Desk CI', phrase='getservercert',
                                                 salt=secrets.token_hex(16), clientcert=pem.hex()))
                    if result.get('status_code') == '429':
                        break
                    assert result.get('status_code') == '200'
                    request('pair', dict(uniqueid=session, phrase='deskcancel'))
                else:
                    raise AssertionError('Access attempt limiter did not activate')
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            assert secret not in (directory / 'process.log').read_text(errors='replace')
    print('Desk password tests passed: discovery metadata, wrong/right password, saved authorization, cancellation, attempt limit.')


def test_parent_death(helper):
    """Kill an owning process without destructors; its host must release ports."""
    with tempfile.TemporaryDirectory(prefix='desk-lifecycle-ci-') as temp:
        directory = Path(temp)
        base = 59589
        (directory / 'credentials').mkdir()
        (directory / 'apps.json').write_text('{"apps": [{"name": "Desktop"}]}')
        settings = dict(port=base, bind_address='127.0.0.1', address_family='ipv4',
                        upnp='disabled', file_apps=directory / 'apps.json',
                        pkey=directory / 'credentials/key.pem', cert=directory / 'credentials/cert.pem',
                        file_state=directory / 'state.json', credentials_file=directory / 'state.json',
                        log_path=directory / 'host.log')
        config = directory / 'sunshine.conf'
        config.write_text(''.join(f'{key} = {value}\n' for key, value in settings.items()))
        pidfile = directory / 'child.pid'
        env = dict(os.environ, DESK_MANAGED_HOST='1')
        wrapper = ('import pathlib,subprocess,sys; '
                   'child=subprocess.Popen(sys.argv[2:],stdin=subprocess.PIPE); '
                   'pathlib.Path(sys.argv[1]).write_text(str(child.pid)); child.wait()')
        def ready():
            try:
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
                with opener.open(f'http://127.0.0.1:{base}/serverinfo', timeout=.5) as response:
                    return response.status == 200
            except Exception:
                return False
        def port_free():
            try:
                with socket.create_connection(('127.0.0.1', base), timeout=.2):
                    return False
            except OSError:
                return True
        def wait_for(check, timeout):
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                if check(): return True
                time.sleep(.1)
            return False
        # Encoder probing took 23 seconds on the Windows runner. Match run()'s
        # startup allowance; keep the separate eight-second shutdown deadline.
        startup_timeout = 60
        child_pid = None
        child_handle = None
        kernel32 = None
        if os.name == 'nt':
            import ctypes
            from ctypes import wintypes
            kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel32.OpenProcess.restype = wintypes.HANDLE
            kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            kernel32.WaitForSingleObject.restype = wintypes.DWORD
            kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
            kernel32.TerminateProcess.restype = wintypes.BOOL
            kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel32.CloseHandle.restype = wintypes.BOOL
        assert port_free(), 'Lifecycle test port is already occupied'
        try:
            with (directory / 'process.log').open('w') as output:
                owner = subprocess.Popen([sys.executable, '-c', wrapper, str(pidfile), str(helper), str(config)],
                                         env=env, stdout=output, stderr=subprocess.STDOUT, cwd=helper.parent)
                try:
                    assert wait_for(lambda: pidfile.exists() and pidfile.stat().st_size > 0, 5), 'Owner did not report its child'
                    child_pid = int(pidfile.read_text())
                    if kernel32:
                        # Retain the exact process object, even after the owner dies.
                        # SYNCHRONIZE | PROCESS_TERMINATE: never terminate by image name.
                        child_handle = kernel32.OpenProcess(0x00100001, False, child_pid)
                        assert child_handle, 'Cannot observe the managed test host'
                    assert wait_for(ready, startup_timeout), 'Managed host did not become ready'
                    owner.kill()  # SIGKILL / TerminateProcess: no orderly parent cleanup.
                    owner.wait(timeout=3)
                    def stopped():
                        return port_free() and (not child_handle or kernel32.WaitForSingleObject(child_handle, 0) == 0)
                    assert wait_for(stopped, 8), 'Host became an orphan after its owner was killed'
                    # The same configuration and ports must work on the next launch.
                    next_host = subprocess.Popen([str(helper), str(config)], stdin=subprocess.PIPE,
                                                 env=env, stdout=output, stderr=subprocess.STDOUT, cwd=helper.parent)
                    try:
                        assert wait_for(ready, startup_timeout), 'Restart failed after parent crash'
                        next_host.stdin.close()
                        next_host.wait(timeout=8)
                        assert port_free(), 'Normal pipe closure did not release host port'
                    finally:
                        if next_host.poll() is None:
                            next_host.kill(); next_host.wait(timeout=3)
                        next_host.stdin.close()
                finally:
                    # Readiness failures can leave a live child without a listening
                    # port. Cleanup must not depend on whether that port is open.
                    try:
                        if kernel32:
                            if child_handle:
                                try:
                                    if kernel32.WaitForSingleObject(child_handle, 0) == 258:  # WAIT_TIMEOUT
                                        kernel32.TerminateProcess(child_handle, 1)
                                    assert kernel32.WaitForSingleObject(child_handle, 8000) == 0, 'Test host did not stop during cleanup'
                                finally:
                                    kernel32.CloseHandle(child_handle)
                        elif child_pid and (owner.poll() is None or not port_free()):
                            # With the owner alive it retains/reaps this exact child.
                            try: os.kill(child_pid, signal.SIGKILL)
                            except ProcessLookupError: pass
                    finally:
                        if owner.poll() is None:
                            owner.kill(); owner.wait(timeout=3)
        finally:
            # Preserve evidence before TemporaryDirectory cleanup, including failures
            # before the HTTP listener starts. These files are uploaded by all jobs.
            logs = Path(__file__).resolve().parent.parent / 'build' / 'preview-smoke'
            logs.mkdir(parents=True, exist_ok=True)
            for name in ('process.log', 'host.log'):
                source = directory / name
                if source.exists(): shutil.copyfile(source, logs / ('lifecycle-' + name))
        print('Managed host lifecycle passed: forced parent death, port release, restart, graceful stop.')


if __name__ == '__main__':
    for secret in ('654321', 'Desk-Office!42', secrets.token_urlsafe(16)):
        run(Path(sys.argv[1]).resolve(), secret)

    test_parent_death(Path(sys.argv[1]).resolve())
