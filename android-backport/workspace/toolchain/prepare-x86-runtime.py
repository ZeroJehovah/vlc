#!/usr/bin/env python3
"""Extract official Ubuntu amd64 runtime packages for task-local QEMU tools."""
import gzip
import hashlib
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent
index = gzip.decompress((root / 'amd64-packages.gz').read_bytes()).decode()
wanted = {'libc6', 'libstdc++6', 'libgcc-s1', 'zlib1g'}
found = set()
for paragraph in index.split('\n\n'):
    fields = dict(line.split(': ', 1) for line in paragraph.splitlines()
                  if ': ' in line and not line.startswith(' '))
    name = fields.get('Package')
    if name not in wanted or name in found:
        continue
    found.add(name)
    package = root / pathlib.Path(fields['Filename']).name
    subprocess.run(['curl', '-4', '-L', '--fail', '--retry', '3',
                    'https://archive.ubuntu.com/ubuntu/' + fields['Filename'],
                    '-o', str(package)], check=True)
    assert hashlib.sha256(package.read_bytes()).hexdigest() == fields['SHA256']
    subprocess.run(['dpkg-deb', '-x', str(package), str(root / 'x86-root')], check=True)
    print(name, fields['Version'], flush=True)
assert found == wanted, wanted - found
