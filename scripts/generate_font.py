#!/usr/bin/env python3
"""Regenerate Core's antialiased Hack glyph atlas. Optional dependency: Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
font = ImageFont.truetype(str(root/'third_party/hack/Hack-Regular.ttf'), 17)
lines = ['/* Generated from Hack Regular. See third_party/hack/LICENSE.md. */',
         'static const unsigned char font_alpha[128][240] = {']
for code in range(128):
    bitmap = Image.new('L', (12, 20))
    if code >= 32:
        ImageDraw.Draw(bitmap).text((1, 16), chr(code), font=font, fill=255, anchor='ls')
    pixels = list(bitmap.getdata())
    lines.append('  {' + ','.join(map(str, pixels)) + '},')
lines.append('};')
(root/'kernel/font.h').write_text('\n'.join(lines) + '\n')
