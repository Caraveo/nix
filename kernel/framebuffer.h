/* QEMU Standard VGA / Bochs VBE framebuffer. No host GUI dependency. */
#include "font.h"
static volatile u32 *pixels;
static const u32 palette[16] = {0x1e1e2d, 0x354052, 0x0aaa46, 0x008cbe,
                                0xd23232, 0x8b63a4, 0xbe960a, 0xf0f0f8,
                                0x828291, 0xd2ebfa, 0x28c840, 0x008cbe,
                                0xff5f57, 0xffbd2e, 0xbe960a, 0xfafafc};
static u32 pci_read(u32 bus, u32 device, u32 offset) {
  outw(0xcf8, 0); /* Full dword address follows. */
  u32 address = 0x80000000U | (bus << 16) | (device << 11) | (offset & 252);
  __asm__ volatile("outl %0,%1" ::"a"(address), "Nd"((u16)0xcf8));
  u32 result;
  __asm__ volatile("inl %1,%0" : "=a"(result) : "Nd"((u16)0xcfc));
  return result;
}
static void vbe(u16 index, u16 value) {
  outw(0x1ce, index);
  outw(0x1cf, value);
}
static void rect(int x, int y, int w, int h, u32 rgb) {
  if (!pixels)
    return;
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++)
      pixels[j * 1024 + i] = rgb;
}
static void circle(int x, int y, int r, u32 rgb) {
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r)
        rect(x + i, y + j, 1, 1, rgb);
}
static u32 blend(u32 fg, u32 bg, u32 alpha) {
  u32 r =
      (((fg >> 16) & 255) * alpha + ((bg >> 16) & 255) * (255 - alpha)) / 255;
  u32 g = (((fg >> 8) & 255) * alpha + ((bg >> 8) & 255) * (255 - alpha)) / 255;
  u32 b = ((fg & 255) * alpha + (bg & 255) * (255 - alpha)) / 255;
  return (r << 16) | (g << 8) | b;
}
static void glyph(int x, int y, char ch, u32 fg, u32 bg) {
  u8 c = (u8)ch;
  if (c > 127)
    return;
  for (int j = 0; j < 20; j++)
    for (int i = 0; i < 12; i++) {
      u8 alpha = font_alpha[c][j * 12 + i];
      if (alpha)
        rect(x + i, y + j, 1, 1, blend(fg, bg, alpha));
    }
}
static void label(int x, int y, const char *s, u32 fg, u32 bg) {
  for (; *s; s++, x += 12)
    glyph(x, y, *s, fg, bg);
}
static int graphics_init(void) {
  for (u32 dev = 0; dev < 32; dev++)
    if (pci_read(0, dev, 0) == 0x11111234U) {
      u32 base = pci_read(0, dev, 16) & 0xfffffff0U;
      if (!base)
        return 0;
      outw(0x1ce, 0);
      u16 id = inw(0x1cf);
      if (id < 0xb0c0 || id > 0xb0c5)
        return 0;
      vbe(4, 0);
      vbe(1, 1024);
      vbe(2, 768);
      vbe(3, 32);
      vbe(4, 0x41);
      pixels = (volatile u32 *)base;
      return 1;
    }
  return 0;
}
static void graphics_cell(int x, int y, char c, u8 attr) {
  if (!pixels)
    return;
  int px = 32 + x * 12, py = 52 + y * 26;
  rect(px, py, 12, 26, palette[(attr >> 4) & 15]);
  glyph(px, py + 3, c, palette[attr & 15], palette[(attr >> 4) & 15]);
}
static void graphics_chrome(const char *tab) {
  if (!pixels)
    return;
  rect(0, 0, 1024, 768, 0x111116);
  rect(16, 24, 992, 720, palette[15]);
  rect(16, 24, 992, 57, palette[7]);
  circle(43, 51, 6, 0xff5f57);
  circle(64, 51, 6, 0xffbd2e);
  circle(85, 51, 6, 0x28c840);
  rect(112, 32, 176, 42, palette[15]);
  label(130, 42, tab, palette[0], palette[15]);
  label(311, 42, "+", palette[0], palette[7]);
  label(965, 42, "x", palette[0], palette[7]);
  rect(16, 724, 992, 20, palette[7]);
}
