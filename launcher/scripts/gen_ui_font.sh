#!/usr/bin/env bash
# Regenerate UI font: Vonwaon Bitmap 12px (CC0) only.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FONT="$SCRIPT_DIR/fonts/VonwaonBitmap-12px.ttf"
OUT="$ROOT/main/fonts/ui_font_vonwaon.c"

if [[ ! -f "$FONT" ]]; then
  echo "Missing source font: $FONT" >&2
  echo "See $SCRIPT_DIR/FONT.md" >&2
  exit 1
fi

export OUT FONT
python3 << 'PY'
import os

from fontTools.ttLib import TTFont

OUT = os.environ["OUT"]
FONT = os.environ["FONT"]


def to_ranges(codes):
    parts = []
    start = prev = codes[0]
    for c in codes[1:]:
        if c == prev + 1:
            prev = c
        else:
            parts.append(f"0x{start:x}" if start == prev else f"0x{start:x}-0x{prev:x}")
            start = prev = c
    parts.append(f"0x{start:x}" if start == prev else f"0x{start:x}-0x{prev:x}")
    return parts


von_codes = sorted(TTFont(FONT).getBestCmap().keys())
von_parts = to_ranges(von_codes)
chunks = [von_parts[i : i + 200] for i in range(0, len(von_parts), 200)]

gen_cmd = os.path.join(os.environ.get("TMPDIR", "/tmp"), "esplay-gen_ui_font_cmd.sh")
with open(gen_cmd, "w") as f:
    f.write("#!/bin/bash\nset -e\n")
    f.write("npx --yes lv_font_conv --size 12 --bpp 1 --format lvgl \\\n")
    f.write(f'  --font "{FONT}" \\\n')
    for ch in chunks:
        f.write(f'  -r {",".join(ch)} \\\n')
    f.write("  --lv-font-name ui_font_vonwaon \\\n")
    f.write("  --lv-fallback lv_font_montserrat_14 \\\n")
    f.write("  --lv-include lvgl.h \\\n")
    f.write(f'  -o "{OUT}"\n')

print(f"vonwaon {len(von_codes)} glyphs, {len(chunks)} range chunks")
PY

GEN_CMD="${TMPDIR:-/tmp}/esplay-gen_ui_font_cmd.sh"
chmod +x "$GEN_CMD"
"$GEN_CMD"
rm -f "$GEN_CMD"

# ESP-IDF LVGL uses "lvgl.h", not "lvgl/lvgl.h".
sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "$OUT"

echo "Wrote $OUT ($(wc -c < "$OUT") bytes)"
