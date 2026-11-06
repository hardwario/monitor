#!/bin/sh
# Generates every application icon file from the master PNG hwio-monitor-source.png (512x512 RGBA,
# ADR-0272, replacing the v1 icon of plan/features/0015 A3): the Linux hicolor PNGs
# hwio-monitor-{16,24,32,48,64,128,256,512}.png, the Windows hwio-monitor.ico (16..256 px) and the
# macOS hwio-monitor.icns (16..512 px). Run once when the master icon changes; all outputs are
# committed (Git LFS, .gitattributes).
#
# Smaller sizes are scaled down from the master (Lanczos). The .icns has no 1024 px (512@2x)
# representation: the master is only 512 px and is never upscaled. A 1024 px master would add it.
#
# Needs: python3 with Pillow (e.g. `apt install python3-pil`).
# Usage: packaging/common/icons/extract.sh
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I - "$here/hwio-monitor-source.png" "$here" <<'PY'
import sys
from PIL import Image

source, out = sys.argv[1], sys.argv[2]
master = Image.open(source).convert("RGBA")
assert master.size[0] == master.size[1] and master.size[0] >= 512, master.size
if master.size[0] != 512:
    master = master.resize((512, 512), Image.LANCZOS)
for size in (16, 24, 32, 48, 64, 128, 256, 512):
    image = master if size == 512 else master.resize((size, size), Image.LANCZOS)
    image.save(f"{out}/hwio-monitor-{size}.png", optimize=True)
    print(f"hwio-monitor-{size}.png")
master.save(f"{out}/hwio-monitor.ico",
            sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
print("hwio-monitor.ico")
# The .icns is written by hand (PNG entries): Pillow's own writer would add a 1024 px entry by upscaling.
import io, struct
entries = []
for kind, size in (("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128), ("ic08", 256),
                   ("ic09", 512), ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)):
    buffer = io.BytesIO()
    (master if size == 512 else master.resize((size, size), Image.LANCZOS)).save(buffer, "PNG")
    data = buffer.getvalue()
    entries.append(kind.encode("ascii") + struct.pack(">I", 8 + len(data)) + data)
body = b"".join(entries)
with open(f"{out}/hwio-monitor.icns", "wb") as handle:
    handle.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)
print("hwio-monitor.icns")
PY
