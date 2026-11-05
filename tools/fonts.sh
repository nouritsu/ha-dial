#!/usr/bin/env bash
# regenerates src/ui/fonts from Roboto and the icon codepoints used in src/ui/icons.{h,c}
set -euo pipefail
cd "$DEVENV_ROOT"

out=src/ui/fonts
mkdir -p "$out"
text="0x20-0x7E,0xA0-0xFF,0x2019,0x2026"
icons=$(grep -ho 'U000F[0-9A-F]\{4\}' src/ui/icons.h src/ui/icons.c | sed 's/^U000/0x/' | sort -u | paste -sd,)

conv() {
  local name=$1
  shift
  npx --yes lv_font_conv@1.5.3 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
    --lv-font-name "$name" -o "$out/$name.c" "$@"
}

conv text_16 --font "$ROBOTO_DIR/Roboto-Medium.ttf" -r "$text" --size 16
conv text_20 --font "$ROBOTO_DIR/Roboto-Medium.ttf" -r "$text" --size 20
conv text_36 --font "$ROBOTO_DIR/Roboto-Medium.ttf" -r "0x20-0x7E,0xB0" --size 36
conv clock_64 --font "$ROBOTO_DIR/Roboto-Regular.ttf" -r "0x20,0x30-0x3A" --size 64
conv icons_24 --font "$MDI_TTF" -r "$icons" --size 24
conv icons_28 --font "$MDI_TTF" -r "$icons" --size 28
conv icons_72 --font "$MDI_TTF" -r "$icons" --size 72
