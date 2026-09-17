"""Render the actual C++ particle engine on the host. uv run validation/render_frame.py"""
from pathlib import Path
import struct
import subprocess
import zlib

root = Path(__file__).resolve().parents[1]
output = root / "validation-output"
output.mkdir(exist_ok=True)
subprocess.run(["c++", "-std=c++11", "-O2", str(root / "validation/visualizer.cpp"),
                "-o", str(output / "visualizer-test")], check=True)
subprocess.run([str(output / "visualizer-test"), str(output / "lumina.ppm")], check=True)
data = (output / "lumina.ppm").read_bytes()
_, dimensions, _, pixels = data.split(b"\n", 3)
width, height = map(int, dimensions.split())


def chunk(kind, payload):
    return (struct.pack("!I", len(payload)) + kind + payload +
            struct.pack("!I", zlib.crc32(kind + payload) & 0xffffffff))


raw = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
(output / "lumina.png").write_bytes(
    b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0)) +
    chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
print(output / "lumina.png")
