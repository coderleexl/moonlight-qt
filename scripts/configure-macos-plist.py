#!/usr/bin/env python3
"""Generate bundle metadata without modifying the checked-in template."""
import plistlib
import sys
from pathlib import Path

source, destination, version, name, identifier = sys.argv[1:]
with open(source, 'rb') as stream:
    info = plistlib.load(stream)
info.update(CFBundleExecutable=name, CFBundleDisplayName=name,
            CFBundleIdentifier=identifier, CFBundleVersion=version,
            CFBundleShortVersionString=version)
info['NSLocalNetworkUsageDescription'] = f'{name} uses the local network to connect to your other computers.'
Path(destination).parent.mkdir(parents=True, exist_ok=True)
with open(destination, 'wb') as stream:
    plistlib.dump(info, stream)
