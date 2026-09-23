#!/usr/bin/env python3
"""Exercise the real bundled host's password handshake; no capture/GPU needed.

Runs only in CI against an explicitly provided helper, with temporary identities.
Never reads the user's Desk configuration or sends passwords over HTTP.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import secrets
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
        env = dict(os.environ, DESK_ACCESS_PASSWORD=secret, DESK_DEVICE_ID=device_id)
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


if __name__ == '__main__':
    for secret in ('654321', 'Desk-Office!42', secrets.token_urlsafe(16)):
        run(Path(sys.argv[1]).resolve(), secret)
