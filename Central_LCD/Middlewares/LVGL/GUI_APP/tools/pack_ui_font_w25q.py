#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Extract glyph bitmaps from Han font .c files into ui_font_pack.bin for W25Q.

Requires ui_font_w25q.c/h in Middlewares/LVGL/font/ and fonts stripped to use
ui_font_get_bitmap_w25q callbacks. If W25Q font mode is not enabled, this script
exits without modifying sources.
"""

from __future__ import annotations

import sys
from pathlib import Path

FONT_DIR = Path(__file__).resolve().parents[2] / "font"
W25Q_H = FONT_DIR / "ui_font_w25q.h"
FONT_20 = FONT_DIR / "lv_font_source_han_sans_bold_20.c"
FONT_14 = FONT_DIR / "lv_font_source_han_sans_bold_14.c"
OUT_BIN = FONT_DIR / "ui_font_pack.bin"


def main() -> int:
    if not W25Q_H.exists():
        print("pack_ui_font_w25q: ui_font_w25q.h not found — embedded font mode, skip.")
        return 0
    print("pack_ui_font_w25q: W25Q pack not implemented in this tree yet.")
    print(f"  Expected inputs: {FONT_20.name}, {FONT_14.name}")
    print(f"  Output: {OUT_BIN}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
