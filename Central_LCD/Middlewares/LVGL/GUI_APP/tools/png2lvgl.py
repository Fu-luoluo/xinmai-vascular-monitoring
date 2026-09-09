#!/usr/bin/env python3
"""Convert boot PNG to LVGL RGB565 C source."""
from PIL import Image
import sys

SRC = sys.argv[1]
OUT = sys.argv[2]
TARGET_W = int(sys.argv[3]) if len(sys.argv) > 3 else 240
MAX_H = int(sys.argv[4]) if len(sys.argv) > 4 else 200

im = Image.open(SRC).convert("RGBA")
w0, h0 = im.size
pixels = im.load()
threshold = 245
left, top, right, bottom = w0, h0, 0, 0
for y in range(h0):
    for x in range(w0):
        r, g, b, a = pixels[x, y]
        if a > 10 and (r < threshold or g < threshold or b < threshold):
            left = min(left, x)
            top = min(top, y)
            right = max(right, x)
            bottom = max(bottom, y)
if right > left and bottom > top:
    im = im.crop((left, top, right + 1, bottom + 1))

sw, sh = im.size
scale = TARGET_W / sw
nh = int(sh * scale)
if nh > MAX_H:
    scale = MAX_H / sh
    nw, nh = int(sw * scale), MAX_H
else:
    nw, nh = TARGET_W, nh
im = im.resize((nw, nh), Image.Resampling.LANCZOS)
rgb = Image.new("RGB", (nw, nh), (255, 255, 255))
rgb.paste(im, mask=im.split()[3])
w, h = rgb.size


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


data = []
for y in range(h):
    for x in range(w):
        r, g, b = rgb.getpixel((x, y))
        data.append(rgb565(r, g, b))

c_lines = []
for i in range(0, len(data), 6):
    chunk = data[i : i + 6]
    line = "  " + ", ".join(
        f"0x{(c & 0xFF):02x}, 0x{((c >> 8) & 0xFF):02x}" for c in chunk
    )
    if i + 6 < len(data):
        line += ","
    c_lines.append(line)

header = f"""/**
 * @file img_boot_brand.c
 * Boot brand banner (RGB565), auto-generated
 */
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_BOOT_BRAND
#define LV_ATTRIBUTE_IMG_IMG_BOOT_BRAND
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_IMG_BOOT_BRAND uint8_t img_boot_brand_map[] = {{
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
  /*Pixel format: Red: 5 bit, Green: 6 bit, Blue: 5 bit*/
"""

footer = f"""
#endif
}};

const lv_img_dsc_t img_boot_brand = {{
  .header.always_zero = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = {w * h * 2},
  .header.cf = LV_IMG_CF_TRUE_COLOR,
  .data = img_boot_brand_map,
}};
"""

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write(header)
    f.write("\n".join(c_lines))
    f.write("\n")
    f.write(footer)

print(f"OK {w}x{h} {w*h*2} bytes -> {OUT}")
