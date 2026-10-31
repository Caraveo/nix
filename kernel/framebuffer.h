/* QEMU Standard VGA / Bochs VBE framebuffer. No host GUI dependency. */
#include "font.h"
#include "logo.h"
static int menu_visible = 0;
static u32 menu_under[304 * 256];
static volatile u32 *pixels;
/* Retain the scene beneath the software pointer, including while text redraws.
 */
static u32 surface[1024 * 768];
static int mouse_x = 720, mouse_y = 440, pointer_on = 0;
static const char pointer_shape[20][15] = {
    "X             ", "XX            ", "X.X           ", "X..X          ",
    "X...X         ", "X....X        ", "X.....X       ", "X......X      ",
    "X.......X     ", "X........X    ", "X.........X   ", "X......XXXXX  ",
    "X...X..X      ", "X..XX..X      ", "X.X  X..X     ", "XX   X..X     ",
    "X     X..X    ", "      X..X    ", "       XX     ", "              "};
static u32 pointer_pixel(int x, int y, u32 background) {
  if (!pointer_on || x < mouse_x || y < mouse_y || x >= mouse_x + 14 ||
      y >= mouse_y + 20)
    return background;
  char c = pointer_shape[y - mouse_y][x - mouse_x];
  return c == 'X' ? 0x171724 : c == '.' ? 0xffffff : background;
}
static void pointer_move(int x, int y) {
  if (!pixels)
    return;
  if (x < 0)
    x = 0;
  if (x > 1023)
    x = 1023;
  if (y < 0)
    y = 0;
  if (y > 767)
    y = 767;
  for (int j = mouse_y; j < mouse_y + 20 && j < 768; j++)
    for (int i = mouse_x; i < mouse_x + 14 && i < 1024; i++)
      pixels[j * 1024 + i] = surface[j * 1024 + i];
  mouse_x = x;
  mouse_y = y;
  for (int j = y; j < y + 20 && j < 768; j++)
    for (int i = x; i < x + 14 && i < 1024; i++)
      pixels[j * 1024 + i] = pointer_pixel(i, j, surface[j * 1024 + i]);
}
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
      if (i >= 0 && i < 1024 && j >= 0 && j < 768) {
        surface[j * 1024 + i] = rgb;
        pixels[j * 1024 + i] = pointer_pixel(i, j, rgb);
      }
}
static void rounded_rect(int x, int y, int w, int h, int radius, u32 rgb) {
  for (int j = 0; j < h; j++) {
    int edge = j < radius        ? radius - 1 - j
               : j >= h - radius ? j - (h - radius)
                                 : 0;
    int inset = 0;
    if (j < radius || j >= h - radius) {
      while (inset < radius &&
             (radius - inset - 1) * (radius - inset - 1) + edge * edge >
                 radius * radius)
        inset++;
    }
    rect(x + inset, y + j, w - 2 * inset, 1, rgb);
  }
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
static void draw_logo(int x, int y, const u8 *alpha, int w, int h,
                      u32 background) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      u8 a = alpha[j * w + i];
      if (a)
        rect(x + i, y + j, 1, 1, blend(0, background, a));
    }
}
static void graphics_splash(void) {
  if (!pixels)
    return;
  rect(0, 0, 1024, 768, palette[15]);
  draw_logo((1024 - NIX_BOOT_W) / 2, (768 - NIX_BOOT_H) / 2, nix_boot_alpha,
            NIX_BOOT_W, NIX_BOOT_H, palette[15]);
  label(368, 604, "STARTING NIX / CORE 0.2", palette[0], palette[15]);
  rect(392, 650, 240, 3, palette[7]);
  rect(392, 650, 80, 3, palette[0]);
}
static void graphics_menu_hide(void) {
  if (!pixels || !menu_visible)
    return;
  menu_visible = 0;
  for (int y = 0; y < 256; y++)
    for (int x = 0; x < 304; x++)
      rect(28 + x, 438 + y, 1, 1, menu_under[y * 304 + x]);
}
static void graphics_menu_toggle(void) {
  if (!pixels)
    return;
  if (menu_visible) {
    graphics_menu_hide();
    return;
  }
  for (int y = 0; y < 256; y++)
    for (int x = 0; x < 304; x++)
      menu_under[y * 304 + x] = surface[(438 + y) * 1024 + 28 + x];
  menu_visible = 1;
  rounded_rect(28, 438, 304, 256, 16, 0xd7d7e1);
  rounded_rect(29, 439, 302, 254, 15, palette[15]);
  draw_logo(48, 454, nix_menu_alpha, NIX_MENU_W, NIX_MENU_H, palette[15]);
  label(88, 455, "NIX / Core 0.2", palette[0], palette[15]);
  const char *items[] = {"Home", "Memories", "Ghost terminal", "Restart / boot",
                         "Shut down / die"};
  for (int i = 0; i < 5; i++)
    label(52, 493 + i * 36, items[i], palette[0], palette[15]);
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
  menu_visible = 0;
  rect(0, 0, 1024, 768, 0x111116);
  rounded_rect(16, 24, 992, 720, 22, palette[15]);
  rounded_rect(16, 24, 992, 57, 22, palette[7]);
  rect(16, 46, 992, 35, palette[7]);
  circle(43, 51, 6, 0xff5f57);
  circle(64, 51, 6, 0xffbd2e);
  circle(85, 51, 6, 0x28c840);
  rounded_rect(112, 32, 176, 42, 9, palette[15]);
  label(130, 42, tab, palette[0], palette[15]);
  label(311, 42, "+", palette[0], palette[7]);
  label(965, 42, "x", palette[0], palette[7]);
  rounded_rect(16, 700, 992, 44, 22, palette[7]);
  rect(16, 700, 992, 22, palette[7]);
  rounded_rect(28, 706, 46, 32, 8, palette[15]);
  draw_logo(39, 710, nix_menu_alpha, NIX_MENU_W, NIX_MENU_H, palette[15]);
  label(88, 711, "NIX", palette[0], palette[7]);
  label(790, 711, "CORE 0.2", palette[8], palette[7]);
}
