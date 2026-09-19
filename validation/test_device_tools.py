"""Run native tool protocol/queue tests; cache a hash-verified cJSON 1.7.19 test dependency."""
import hashlib
from pathlib import Path
import subprocess
import urllib.request

root = Path(__file__).resolve().parents[1]
cache = root / '.cache/cjson-1.7.19'
cache.mkdir(parents=True, exist_ok=True)
hashes = {'cJSON.c': '298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d',
          'cJSON.h': '25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee'}
for name, expected in hashes.items():
    path = cache / name
    if not path.exists():
        with urllib.request.urlopen(f'https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/{name}', timeout=30) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f'Unexpected checksum: {name}')
        path.write_bytes(data)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'Unexpected cached checksum: {name}')
obj = cache / 'cJSON.o'
subprocess.run(['clang', '-c', str(cache / 'cJSON.c'), '-o', str(obj)], check=True)
for name in ('device_tool_protocol', 'local_tool_queue', 'device_settings'):
    target = cache / name
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                    f'-I{cache}', f'-I{root / "validation/local_tool_stubs"}',
                    str(root / f'validation/{name}.cpp'), str(obj), '-o', str(target)], check=True)
    subprocess.run([str(target)], check=True)
