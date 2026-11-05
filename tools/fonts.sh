#!/usr/bin/env bash
# regenerates src/ui/fonts from Montserrat and the icon codepoints used in src/ui/icons.{h,c}
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

conv text_14 --font "$MONTSERRAT_DIR/Montserrat-Medium.ttf" -r "$text" --size 14
conv text_18 --font "$MONTSERRAT_DIR/Montserrat-SemiBold.ttf" -r "$text" --size 18
conv text_28 --font "$MONTSERRAT_DIR/Montserrat-SemiBold.ttf" -r "$text" --size 28
conv clock_56 --font "$MONTSERRAT_DIR/Montserrat-Medium.ttf" -r "0x20,0x30-0x3A" --size 56
conv icons_20 --font "$MDI_TTF" -r "$icons" --size 20
conv icons_32 --font "$MDI_TTF" -r "$icons" --size 32
conv icons_72 --font "$MDI_TTF" -r "$icons" --size 72
