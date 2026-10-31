#!/usr/bin/env python3
"""Embed alpha masks from the original black NIX emblem; requires Pillow only to regenerate."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
source=Image.open(root/'assets/brand/NIX.png').convert('RGBA')
alpha=source.getchannel('A');alpha=alpha.crop(alpha.getbbox())
lines=['/* Generated from assets/brand/NIX.png. Original artwork is unchanged. */']
for name,maximum in [('boot',320),('menu',24)]:
    im=alpha.copy();im.thumbnail((maximum,maximum),Image.Resampling.LANCZOS)
    w,h=im.size
    lines.extend([f'#define NIX_{name.upper()}_W {w}',f'#define NIX_{name.upper()}_H {h}',f'static const unsigned char nix_{name}_alpha[{w*h}] = {{'])
    data=im.tobytes()
    for off in range(0,len(data),64):lines.append('  '+','.join(map(str,data[off:off+64]))+',')
    lines.append('};')
(root/'kernel/logo.h').write_text('\n'.join(lines)+'\n')
