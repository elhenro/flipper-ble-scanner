#!/usr/bin/env bash
# renders the app's real draw code on the host with flipper's u8g2 fonts -> docs/*.png
# needs: cc, python3, vendor/momentum checkout (./build.sh fetches it)
set -euo pipefail
cd "$(dirname "$0")/../.."

U8G2=vendor/momentum/lib/u8g2
TMP=$(mktemp -d)
trap 'rm -r "$TMP"' EXIT

U8G2_SRC=$(ls $U8G2/*.c | grep -v u8g2_glue.c)
# shellcheck disable=SC2086
cc -O1 -w -I tools/screenshots/mock -I app -I $U8G2 \
  tools/screenshots/shots.c tools/screenshots/canvas.c \
  app/view*.c app/vendor.c app/uuid.c $U8G2_SRC \
  -lm -Wl,-dead_strip -o "$TMP/shots" # drops u8g2 pbm/xbm export, its helpers aren't vendored
"$TMP/shots" "$TMP"

mkdir -p docs
python3 -I - "$TMP" docs <<'EOF'
import struct, sys, zlib, pathlib

SCALE, PAD = 4, 12
BG, FG = (255, 140, 43), (28, 24, 20)  # backlit lcd orange, near black pixels

def png(path, rows):
    h, w = len(rows), len(rows[0]) // 3
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
for f in sorted(src.glob("*.bin")):
    fb = f.read_bytes()
    on = lambda x, y: fb[(y // 8) * 128 + x] >> (y % 8) & 1
    w, h = 128 * SCALE + 2 * PAD, 64 * SCALE + 2 * PAD
    rows = []
    for py in range(h):
        row = bytearray()
        for px in range(w):
            x, y = (px - PAD) // SCALE, (py - PAD) // SCALE
            inside = 0 <= px - PAD < 128 * SCALE and 0 <= py - PAD < 64 * SCALE
            row += bytes(FG if inside and on(x, y) else BG)
        rows.append(row)
    png(out / f"{f.stem}.png", rows)
    print("wrote", out / f"{f.stem}.png")
EOF
