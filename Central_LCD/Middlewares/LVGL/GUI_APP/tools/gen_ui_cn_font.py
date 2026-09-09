#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regenerate lv_font_source_han_sans_bold_20/14.c with all UI Chinese glyphs."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(__file__).resolve().parent
FONT_DIR = ROOT.parent / "font"
OUT_20 = FONT_DIR / "lv_font_source_han_sans_bold_20.c"
OUT_14 = FONT_DIR / "lv_font_source_han_sans_bold_14.c"
FONT_OTF = TOOLS / "NotoSansSC-Bold.otf"
FONT_ZIP_URL = (
    "https://github.com/googlefonts/noto-cjk/releases/download/"
    "Sans2.004/18_NotoSansSC.zip"
)
FONT_ZIP = TOOLS / "NotoSansSC.zip"

PPG_QUALITY_LEGACY = "信质稳性良好不灌注静足校准中"

# Legacy subset (keep compatibility with older screens).
LEGACY_SYMBOLS = (
    "跳过首页监测历史开始停止完成腕部指心率血氧共条暂无记录存储未就绪"
    "设备脱落请重新佩戴传感器点击后重试即将返回无线设置仅支持扫描忘记网络"
    "请先测量连接取消中正在已失败找到输入密码并配网脉波，。…：√"
    + PPG_QUALITY_LEGACY
)

# 14px: monitor vitals + quality row (compact Han subset).
SYMBOLS_14 = (
    "腕指心率血氧信号质量稳定性良好不足弱灌注保静止波动重新测量请校准中，·…：√"
)

SCAN_FILES = [
    ROOT / "ui_strings.h",
    ROOT / "ui_history.c",
    ROOT.parent.parent / "Algorithm" / "pwv" / "pwv_calib.c",
]


def collect_symbols_20() -> str:
    chars: set[str] = set(LEGACY_SYMBOLS)
    pat = re.compile(r'"((?:\\.|[^"\\])*)"')

    for path in SCAN_FILES:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in pat.finditer(text):
            s = m.group(1)
            for ch in s:
                if ord(ch) >= 0x4E00 or ch in "·，。…：":
                    chars.add(ch)

    chars.update("自动")
    return "".join(sorted(chars, key=lambda c: ord(c)))


def ensure_font() -> None:
    if FONT_OTF.exists() and FONT_OTF.stat().st_size > 100000:
        return
    TOOLS.mkdir(parents=True, exist_ok=True)
    if not FONT_ZIP.exists() or FONT_ZIP.stat().st_size < 1000000:
        print(f"Downloading {FONT_ZIP.name} ...")
        urllib.request.urlretrieve(FONT_ZIP_URL, FONT_ZIP)
    print("Extracting NotoSansSC-Bold.otf ...")
    with zipfile.ZipFile(FONT_ZIP, "r") as zf:
        for name in zf.namelist():
            if name.endswith("NotoSansSC-Bold.otf"):
                with zf.open(name) as src, FONT_OTF.open("wb") as dst:
                    dst.write(src.read())
                print(f"Saved {FONT_OTF}")
                return
    raise FileNotFoundError("NotoSansSC-Bold.otf not found in zip")


def run_lv_font_conv(size: int, symbols: str, out_c: Path) -> None:
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        raise RuntimeError("npx not found; install Node.js")

    cmd = [
        npx,
        "--yes",
        "lv_font_conv",
        "--bpp",
        "2",
        "--size",
        str(size),
        "--no-compress",
        "--font",
        str(FONT_OTF),
        "--symbols",
        symbols,
        "--range",
        "32-126",
        "--format",
        "lvgl",
        "-o",
        str(out_c),
    ]
    print(f"Running lv_font_conv {size}px ...")
    subprocess.run(cmd, check=True, cwd=ROOT)
    print(f"Wrote {out_c}")


def maybe_pack_w25q() -> None:
    pack = TOOLS / "pack_ui_font_w25q.py"
    w25q_h = FONT_DIR / "ui_font_w25q.h"
    if not pack.exists() or not w25q_h.exists():
        print("W25Q font pack skipped (ui_font_w25q not enabled).")
        return
    print("Running pack_ui_font_w25q.py ...")
    subprocess.run([sys.executable, str(pack)], check=True, cwd=ROOT)


def main() -> int:
    symbols_20 = collect_symbols_20()
    print(f"20px symbol count: {len(symbols_20)}")
    print(f"14px symbol count: {len(SYMBOLS_14)}")
    ensure_font()
    FONT_DIR.mkdir(parents=True, exist_ok=True)

    run_lv_font_conv(20, symbols_20, OUT_20)
    run_lv_font_conv(14, SYMBOLS_14, OUT_14)
    maybe_pack_w25q()
    return 0


if __name__ == "__main__":
    sys.exit(main())
