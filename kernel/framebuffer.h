/* QEMU Standard VGA / Bochs VBE framebuffer. No host GUI dependency. */
#include "font.h"
#include "logo.h"
static int menu_visible = 0;
static u32 menu_under[304 * 256];
static volatile u32 *pixels;
/* Retain the scene beneath the software pointer, including while text redraws.
 */
static u32 surface[1024 * 768];
static int mouse_x = 900, mouse_y = 700, pointer_on = 0;
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
static void draw_mark(int x, int y, const u8 *alpha, int w, int h, u32 ink,
                      u32 background) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      u8 a = alpha[j * w + i];
      if (a)
        rect(x + i, y + j, 1, 1, blend(ink, background, a));
    }
}
static void glyph_scaled(int x, int y, char ch, u32 fg, u32 bg, int scale) {
  u8 c = (u8)ch;
  if (c > 127 || scale < 1)
    return;
  for (int j = 0; j < 20; j++)
    for (int i = 0; i < 12; i++) {
      u8 alpha = font_alpha[c][j * 12 + i];
      if (alpha)
        rect(x + i * scale, y + j * scale, scale, scale, blend(fg, bg, alpha));
    }
}
static void graphics_splash(void) {
  if (!pixels)
    return;
  rect(0, 0, 1024, 768, palette[15]);
  draw_logo((1024 - NIX_BOOT_W) / 2, (768 - NIX_BOOT_H) / 2, nix_boot_alpha,
            NIX_BOOT_W, NIX_BOOT_H, palette[15]);
  label(368, 604, "STARTING NIX / CORE 0.3", palette[0], palette[15]);
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
  label(88, 455, "NIX / Core 0.3", palette[0], palette[15]);
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
      pixels = (volatile u32 *)(u64)base;
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
  label(790, 711, "CORE 0.3", palette[8], palette[7]);
}
/* Home matches the dark welcome: mark, heading, prompt bar. */
static int face_on = 0;
static int face_capture = 0;
static int face_confirm = 0;
static int face_linux = 0;
enum { FACE_LOGS = 6, FACE_EXTRA = 4 };
static char face_rows[12][78];
static int face_rows_n, face_col, face_typed_n;
static char face_typed[48];
static char face_log[FACE_LOGS][12][78];
static int face_log_n[FACE_LOGS];
static int face_log_col[FACE_LOGS];
static char face_log_typed[FACE_LOGS][48];
static int face_log_typed_n[FACE_LOGS];
static int face_app;
static char face_extra_name[FACE_EXTRA][16];
static int face_extra_n;
static int face_tab_x[FACE_LOGS];
static int face_tab_w[FACE_LOGS];
static int face_tab_n;
static char face_ram_note[72];
enum {
  FACE_BG = 0x211713,
  FACE_CARD = 0x362720,
  FACE_LINE = 0x4a382e,
  FACE_INK = 0xf7edea,
  FACE_MUTED = 0xcbb8ae,
  FACE_PEACH = 0xe29a76,
  FACE_PROMPT = 0x1e1713
};
static void graphics_face_prompt(void);
static void graphics_face_paint(void);
static void face_save(void) {
  int a = face_app;
  if (a < 0 || a >= FACE_LOGS)
    return;
  if (face_rows_n < 12)
    face_rows[face_rows_n][face_col] = 0;
  face_log_n[a] = face_rows_n;
  face_log_col[a] = face_col;
  for (int i = 0; i < 12; i++)
    copy(face_log[a][i], face_rows[i], 78);
  copy(face_log_typed[a], face_typed, 48);
  face_log_typed_n[a] = face_typed_n;
}
static void face_restore(void) {
  int a = face_app;
  face_rows_n = face_log_n[a];
  face_col = face_log_col[a];
  for (int i = 0; i < 12; i++)
    copy(face_rows[i], face_log[a][i], 78);
  copy(face_typed, face_log_typed[a], 48);
  face_typed_n = face_log_typed_n[a];
}
static void face_load(int app) {
  if (app < 0 || app >= FACE_LOGS)
    app = 0;
  if (app != face_app)
    face_save();
  face_app = app;
  face_restore();
  if (face_on && pixels)
    graphics_face_paint();
}
static void face_clear(void) {
  face_rows_n = 0;
  face_col = 0;
  face_typed_n = 0;
  face_typed[0] = 0;
  for (int i = 0; i < 12; i++)
    face_rows[i][0] = 0;
  face_save();
}
static int face_find_extra(const char *name) {
  for (int i = 0; i < face_extra_n; i++)
    if (eq(face_extra_name[i], name))
      return i;
  return -1;
}
static int face_open_app(const char *name) {
  int i = face_find_extra(name);
  if (i < 0) {
    if (face_extra_n >= FACE_EXTRA)
      return -1;
    i = face_extra_n++;
    copy(face_extra_name[i], name, 16);
  }
  face_load(2 + i);
  face_clear();
  if (face_on && pixels)
    graphics_face_paint();
  return 2 + i;
}
static void face_close_app(const char *name) {
  int i = face_find_extra(name);
  int slot, from;
  if (i < 0)
    return;
  slot = 2 + i;
  for (int j = i; j + 1 < face_extra_n; j++) {
    copy(face_extra_name[j], face_extra_name[j + 1], 16);
    from = 3 + j;
    face_log_n[2 + j] = face_log_n[from];
    face_log_col[2 + j] = face_log_col[from];
    face_log_typed_n[2 + j] = face_log_typed_n[from];
    for (int r = 0; r < 12; r++)
      copy(face_log[2 + j][r], face_log[from][r], 78);
    copy(face_log_typed[2 + j], face_log_typed[from], 48);
  }
  face_extra_n--;
  if (face_app == slot || face_app >= 2 + face_extra_n)
    face_load(0);
  else if (face_on && pixels)
    graphics_face_paint();
}
static void face_pill(int x, int y, int w, const char *icon, const char *text,
                      int on) {
  u32 fill = on ? FACE_CARD : FACE_BG;
  rounded_rect(x, y, w, 36, 12, FACE_LINE);
  rounded_rect(x + 1, y + 1, w - 2, 34, 11, fill);
  label(x + 14, y + 8, icon, FACE_PEACH, fill);
  label(x + 32, y + 8, text, on ? FACE_INK : FACE_MUTED, fill);
}
static void face_pill_text(int x, int y, int w, const char *text, int on) {
  u32 fill = on ? FACE_CARD : FACE_BG;
  rounded_rect(x, y, w, 36, 12, FACE_LINE);
  rounded_rect(x + 1, y + 1, w - 2, 34, 11, fill);
  label(x + 16, y + 8, text, on ? FACE_INK : FACE_MUTED, fill);
}
static void face_tabs(void) {
  int x = 24;
  char label[20];
  face_tab_n = 2;
  face_tab_x[0] = x;
  face_tab_w[0] = 118;
  face_pill(x, 68, 118, "*", "Prompt", face_app == 0);
  x += 126;
  face_tab_x[1] = x;
  face_tab_w[1] = 148;
  face_pill(x, 68, 148, ">", "Terminal", face_app == 1);
  x += 156;
  for (int i = 0; i < face_extra_n && face_tab_n < FACE_LOGS; i++) {
    int w;
    label[0] = '#';
    copy(label + 1, face_extra_name[i], 16);
    w = 32 + (int)len(label) * 12;
    if (w < 116)
      w = 116;
    if (x + w > 1000)
      break;
    face_tab_x[face_tab_n] = x;
    face_tab_w[face_tab_n] = w;
    face_pill_text(x, 68, w, label, face_app == 2 + i);
    x += w + 8;
    face_tab_n++;
  }
}
static int face_tab_at(int x, int y) {
  if (y < 68 || y >= 104)
    return -1;
  for (int i = 0; i < face_tab_n; i++)
    if (x >= face_tab_x[i] && x < face_tab_x[i] + face_tab_w[i])
      return i;
  return -1;
}
static void face_heading(void) {
  const char *lead = "Hi. Welcome to ";
  const char *name = "Nix.";
  int scale = 3;
  int width = (int)(len(lead) + len(name)) * 12 * scale;
  int x = (1024 - width) / 2;
  int y = 300;
  for (int i = 0; lead[i]; i++, x += 12 * scale)
    glyph_scaled(x, y, lead[i], FACE_INK, FACE_BG, scale);
  for (int i = 0; name[i]; i++, x += 12 * scale)
    glyph_scaled(x + 6, y, name[i], FACE_PEACH, FACE_BG, scale);
}
static void graphics_face_paint(void) {
  const char *kicker = "A COMPUTER THAT STARTS WITH YOU.";
  const char *space = "Your space. Your intent.";
  const char *foot = "Start with an idea. Make this space yours.";
  if (!pixels)
    return;
  menu_visible = 0;
  rect(0, 0, 1024, 768, FACE_BG);
  draw_mark(24, 14, nix_menu_alpha, NIX_MENU_W, NIX_MENU_H, FACE_INK, FACE_BG);
  label(56, 16, "nix", FACE_INK, FACE_BG);
  label((1024 - (int)len(space) * 12) / 2, 16, space, FACE_MUTED, FACE_BG);
  rect(0, 52, 1024, 1, FACE_LINE);
  face_tabs();
  if (face_app == 0 && !face_rows_n) {
    label((1024 - (int)len(kicker) * 12) / 2, 250, kicker, FACE_PEACH, FACE_BG);
    face_heading();
    if (face_ram_note[0])
      label((1024 - (int)len(face_ram_note) * 12) / 2, 430, face_ram_note,
            FACE_MUTED, FACE_BG);
  } else {
    u32 paper = face_app == 0 ? FACE_BG : FACE_PROMPT;
    int y = 132;
    if (face_app != 0)
      rounded_rect(24, 120, 976, 476, 16, FACE_PROMPT);
    for (int i = 0; i < face_rows_n && y <= 560; i++, y += 22)
      label(36, y, face_rows[i], FACE_INK, paper);
    if (face_app >= 2 && !face_rows_n)
      label(36, 160, "Linux 7.3.1 console", FACE_MUTED, FACE_PROMPT);
  }
  graphics_face_prompt();
  if (face_app >= 2)
    foot = "Type nix and Enter to leave this app.";
  label((1024 - (int)len(foot) * 12) / 2, 728, foot, FACE_MUTED, FACE_BG);
}
static void graphics_face_prompt(void) {
  int x = 28, y = 612, w = 968, h = 72;
  int bx = 936, by = 626;
  if (!pixels)
    return;
  rounded_rect(x, y, w, h, 18, FACE_LINE);
  rounded_rect(x + 1, y + 1, w - 2, h - 2, 17, FACE_PROMPT);
  draw_mark(x + 16, y + 24, nix_menu_alpha, NIX_MENU_W, NIX_MENU_H, FACE_PEACH,
            FACE_PROMPT);
  if (!face_typed_n && face_confirm)
    label(x + 52, y + 26, "Type y to confirm, or n to cancel", FACE_PEACH,
          FACE_PROMPT);
  else if (!face_typed_n && face_app >= 2)
    label(x + 52, y + 26, "linux >", FACE_INK, FACE_PROMPT);
  else if (!face_typed_n && face_app == 1)
    label(x + 52, y + 26, "Ghost terminal", FACE_MUTED, FACE_PROMPT);
  else if (!face_typed_n)
    label(x + 52, y + 26, "What would you like to do?", FACE_MUTED, FACE_PROMPT);
  else {
    int tx = x + 52;
    for (int i = 0; i < face_typed_n; i++, tx += 12)
      glyph(tx, y + 26, face_typed[i], FACE_INK, FACE_PROMPT);
    rect(tx + 2, y + 28, 2, 16, FACE_INK);
  }
  rounded_rect(bx, by, 44, 44, 12, FACE_PEACH);
  rect(bx + 20, by + 12, 4, 4, FACE_BG);
  rect(bx + 16, by + 16, 4, 4, FACE_BG);
  rect(bx + 24, by + 16, 4, 4, FACE_BG);
  rect(bx + 12, by + 20, 4, 4, FACE_BG);
  rect(bx + 28, by + 20, 4, 4, FACE_BG);
  rect(bx + 20, by + 20, 4, 14, FACE_BG);
}
static void graphics_face_arrow(void) { graphics_face_prompt(); }
static const char *face_hit(int x, int y) {
  static char cmd[32];
  int tab;
  if (!face_on || face_confirm)
    return 0;
  tab = face_tab_at(x, y);
  if (tab < 0)
    return 0;
  if (tab == 0)
    return "focus prompt";
  if (tab == 1)
    return "focus terminal";
  if (tab - 2 >= face_extra_n)
    return 0;
  copy(cmd, "focus ", 32);
  {
    u32 n = 6;
    const char *s = face_extra_name[tab - 2];
    while (*s && n + 1 < sizeof cmd)
      cmd[n++] = *s++;
    cmd[n] = 0;
  }
  return cmd;
}
static void face_sync(const char *s) {
  if (!face_on || !pixels)
    return;
  u32 i = 0;
  while (s[i] && i + 1 < sizeof(face_typed)) {
    face_typed[i] = s[i];
    i++;
  }
  face_typed[i] = 0;
  face_typed_n = (int)i;
  graphics_face_arrow();
}
/* The console keeps 12 finished lines. A full log scrolls. It must not
   drop the new characters and leave blank rows. */
static void face_make_room(void) {
  if (face_rows_n < 12)
    return;
  for (int i = 1; i < 12; i++)
    copy(face_rows[i - 1], face_rows[i], 78);
  face_rows[11][0] = 0;
  face_rows_n = 11;
  face_col = 0;
}
static void face_note(char c) {
  if (!face_on || !face_capture || !pixels)
    return;
  if (c == '\r' || c == '\b')
    return;
  if (c == '\n') {
    face_make_room();
    face_rows[face_rows_n][face_col] = 0;
    face_rows_n++;
    face_col = 0;
    face_save();
    graphics_face_paint();
    return;
  }
  if (c < 32 || c > 126)
    return;
  if (face_col >= 72)
    face_note('\n');
  face_make_room();
  if (face_rows_n >= 12)
    return;
  face_rows[face_rows_n][face_col++] = c;
  face_rows[face_rows_n][face_col] = 0;
}
