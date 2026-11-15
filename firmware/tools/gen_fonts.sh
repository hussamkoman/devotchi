#!/usr/bin/env bash
# Regenerates the LVGL fonts in src/components/ui/fonts from the TTFs in
# docs/design/fonts (design-system 3, architecture 7.4). The build never runs
# this; the generated C files are committed. Needs Node (npx), ImageMagick and
# a Python with Pillow (PYTHON, default python3; the ESP-IDF venv has it)
# for the icon font and the lock-up image.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../../docs/design/fonts"
OUT="$HERE/../src/components/ui/fonts"
CONV=(npx --yes lv_font_conv@1.5.3)

FIG_MED="$SRC/Figtree-Medium.ttf"
FIG_BOLD="$SRC/Figtree-Bold.ttf"
MONO="$SRC/JetBrainsMono-Medium.ttf"

# Device charset (protocol 14.1): ASCII, Latin-1 Supplement and the
# typographic marks the server keeps. Figtree has no check mark (U+2713), so
# body and small take it from JetBrains Mono.
DEVICE_RANGE="0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x2190-0x2193,0x2212"
DEVICE_EXTRA="0x2713"
PAIR_LETTERS="ABCDEFGHJKMNPQRSTUVWXYZ"
# display at 4 bpp is about 30 kB of the 100 kB font budget; 2 bpp halves it (design-system 3).
DISPLAY_BPP=2

mkdir -p "$OUT"

gen() {
    local name="$1" size="$2" bpp="$3"
    shift 3
    "${CONV[@]}" --format lvgl --no-compress --size "$size" --bpp "$bpp" \
        --lv-include lvgl.h --lv-font-name "$name" -o "$OUT/$name.c" "$@"
}

# Figtree's default digits are proportional and lv_font_conv cannot select the
# tnum variants, so patch_font.cjs gives the digits one advance and no kerning
# (a countdown or a clock then does not jitter) and sets the line heights of
# design-system 3. Kerning uses classes so the patch can take digits out of it.
gen ui_font_display64 64 "$DISPLAY_BPP" --font "$FIG_BOLD" --symbols "0123456789:- $PAIR_LETTERS" --force-fast-kern-format
gen ui_font_title24 24 4 --font "$FIG_BOLD" -r 0x20-0x7E --force-fast-kern-format
gen ui_font_body18 18 4 --font "$FIG_MED" -r "$DEVICE_RANGE" --font "$MONO" -r "$DEVICE_EXTRA" --force-fast-kern-format
gen ui_font_small14 14 4 --font "$FIG_MED" -r "$DEVICE_RANGE" --font "$MONO" -r "$DEVICE_EXTRA" --force-fast-kern-format
gen ui_font_mono14 14 4 --font "$MONO" -r "$DEVICE_RANGE,$DEVICE_EXTRA" --no-kerning

node "$HERE/patch_font.cjs" "$OUT/ui_font_display64.c" 64 tabular
node "$HERE/patch_font.cjs" "$OUT/ui_font_title24.c" 28 tabular
node "$HERE/patch_font.cjs" "$OUT/ui_font_body18.c" 22 tabular
node "$HERE/patch_font.cjs" "$OUT/ui_font_small14.c" 18 tabular
node "$HERE/patch_font.cjs" "$OUT/ui_font_mono14.c" 18

"${PYTHON:-python3}" -I "$HERE/gen_icons.py"
"${PYTHON:-python3}" -I "$HERE/gen_lockup.py"

cp "$SRC/OFL-Figtree.txt" "$SRC/OFL-JetBrainsMono.txt" "$OUT/"
