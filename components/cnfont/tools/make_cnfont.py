#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_cnfont.py — 从 TTF 生成嵌入式点阵中文字库（供 cnfont 组件使用）

输出格式（小端）：
  magic   4 bytes  = b"CNFB"
  version u16      = 1
  unit    u8       = 格子边长（32，格子为 32x32）
  stride  u8       = 每行字节数（32/8 = 4）
  rows    u8       = 行数（32）
  rsv     u8       = 0
  count   u32      = 字形数量
  codepoints       u32 * count（按 Unicode 升序）
  bitmaps          stride*rows(=128) * count，1bpp，MSB first

字符集：GBK 覆盖的 CJK 统一表意文字（约 2.1 万，含繁体）+ 常用全角标点。
用法：
  python make_cnfont.py <input.ttf> <output.bin>
默认：
  python make_cnfont.py
  -> 输入 ../../../fonts/LXGWWenKai-Regular.ttf
     输出 ../../../fonts/cnfont32.bin
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

UNIT = 32          # 格子边长
FONT_PX = 30       # 字体像素大小（略小于格子，留边）

# GBK 之外再补充的常用全角标点 / 符号
EXTRA_PUNCT = [
    0x3001, 0x3002, 0xFF0C, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B,
    0x2026, 0x2014, 0x00B7, 0x300C, 0x300D, 0x300E, 0x300F,
    0xFF08, 0xFF09, 0x3010, 0x3011, 0x300A, 0x300B, 0x3008, 0x3009,
    0x201C, 0x201D, 0x2018, 0x2019, 0x2605, 0x2606, 0x2192,
]


def collect_codepoints():
    """GBK 可编码的 CJK 基本区汉字 + 补充标点（升序去重）"""
    cps = set()
    for cp in range(0x4E00, 0x9FFF + 1):
        try:
            chr(cp).encode("gbk")
        except UnicodeEncodeError:
            continue
        cps.add(cp)
    for cp in EXTRA_PUNCT:
        cps.add(cp)
    return sorted(cps)


def render(font, cp):
    """渲染单字 -> 128 字节 1bpp（MSB first）"""
    img = Image.new("1", (UNIT, UNIT), 0)
    d = ImageDraw.Draw(img)
    # anchor='mm'：按字体度量中心居中；y 再上移 1px 做视觉补偿
    d.text((UNIT / 2, UNIT / 2 - 1), chr(cp), font=font, fill=1, anchor="mm")
    px = img.load()
    out = bytearray((UNIT // 8) * UNIT)
    k = 0
    for y in range(UNIT):
        for byte_i in range(UNIT // 8):
            b = 0
            for bit in range(8):
                if px[byte_i * 8 + bit, y]:
                    b |= 0x80 >> bit
            out[k] = b
            k += 1
    return bytes(out)


def main():
    here = Path(__file__).resolve()
    fonts_dir = here.parents[3] / "fonts"
    ttf_path = Path(sys.argv[1]) if len(sys.argv) > 1 else fonts_dir / "LXGWWenKai-Regular.ttf"
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else fonts_dir / "cnfont32.bin"

    if not ttf_path.is_file():
        sys.exit(f"找不到字体文件: {ttf_path}")

    font = ImageFont.truetype(str(ttf_path), FONT_PX)
    cps = collect_codepoints()

    with open(out_path, "wb") as f:
        f.write(b"CNFB")
        f.write(struct.pack("<HBBBB", 1, UNIT, UNIT // 8, UNIT, 0))
        f.write(struct.pack("<I", len(cps)))
        for cp in cps:
            f.write(struct.pack("<I", cp))
        for n, cp in enumerate(cps):
            f.write(render(font, cp))
            if n % 2000 == 0:
                print(f"  渲染 {n}/{len(cps)}")

    size = out_path.stat().st_size
    print(f"完成: {out_path}")
    print(f"字形数: {len(cps)}，文件大小: {size/1024/1024:.2f} MiB")


if __name__ == "__main__":
    main()
