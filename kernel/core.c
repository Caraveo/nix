/* Core 0.1: freestanding, single-address-space x86 prototype. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
static inline void outb(u16 p, u8 v) {
  __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p));
}
static inline u8 inb(u16 p) {
  u8 v;
  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline void outw(u16 p, u16 v) {
  __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p));
}
static inline u16 inw(u16 p) {
  u16 v;
  __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
void *memset(void *d, int c, unsigned long n) {
  u8 *p = d;
  while (n--)
    *p++ = (u8)c;
  return d;
}
void *memcpy(void *d, const void *s, unsigned long n) {
  u8 *p = d;
  const u8 *q = s;
  while (n--)
    *p++ = *q++;
  return d;
}
static u32 len(const char *s) {
  u32 n = 0;
  while (s[n])
    n++;
  return n;
}
static int eq(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
static int starts(const char *a, const char *b) {
  while (*b)
    if (*a++ != *b++)
      return 0;
  return 1;
}
static int has(const char *a, const char *b) {
  for (; *a; a++)
    if (starts(a, b))
      return 1;
  return 0;
}
static void copy(char *d, const char *s, u32 cap) {
  u32 i = 0;
  while (i + 1 < cap && s[i]) {
    d[i] = s[i];
    i++;
  }
  d[i] = 0;
}
#include "framebuffer.h"
#include "mouse.h"
static int home_visible = 0;
static const char *mouse_action(int x, int y);
static char mouse_pending[64];
static u16 shadow[2000];
static volatile u16 *vga = (u16 *)0xb8000;
static int row = 4, col = 3;
static u8 color = 0xf0;
static void serial(char c) {
  for (u32 t = 0; t < 100000; t++)
    if (inb(0x3fd) & 32) {
      outb(0x3f8, (u8)c);
      return;
    }
}
static void cell(int x, int y, char c, u8 attr) {
  vga[y * 80 + x] = (u16)(u8)c | ((u16)attr << 8);
  graphics_cell(x, y, c, attr);
}
static void bar(int y, const char *s, u8 attr) {
  for (int x = 0; x < 80; x++)
    cell(x, y, ' ', attr);
  for (int x = 0; s[x] && x < 74; x++)
    cell(x + 3, y, s[x], attr);
}
static void cursor(void) {
  static int oldx = 3, oldy = 4;
  if (pixels) {
    u16 old = vga[oldy * 80 + oldx];
    graphics_cell(oldx, oldy, (char)old, (u8)(old >> 8));
    rect(32 + col * 12 + 2, 52 + row * 26 + 22, 8, 2, palette[3]);
    oldx = col;
    oldy = row;
  }
  u16 p = (u16)(row * 80 + col);
  outb(0x3d4, 14);
  outb(0x3d5, (u8)(p >> 8));
  outb(0x3d4, 15);
  outb(0x3d5, (u8)p);
}
static void scroll(void) {
  if (row < 23)
    return;
  for (int y = 3; y < 22; y++)
    for (int x = 0; x < 80; x++)
      cell(x, y, (char)vga[(y + 1) * 80 + x], (u8)(vga[(y + 1) * 80 + x] >> 8));
  for (int x = 0; x < 80; x++)
    cell(x, 22, ' ', 0xf0);
  row = 22;
}
static void put(char c) {
  serial(c);
  if (c == '\n') {
    row++;
    col = 3;
  } else if (c == '\b') {
    if (col == 3 && row > 3) {
      row--;
      col = 77;
    }
    if (col > 3) {
      col--;
      cell(col, row, ' ', color);
    }
  } else if (c >= 32) {
    cell(col++, row, c, color);
    if (col >= 77) {
      row++;
      col = 3;
    }
  }
  scroll();
  cursor();
}
static void say(const char *s) {
  while (*s)
    put(*s++);
}
static void line(const char *s) {
  say(s);
  put('\n');
}
static void number(u32 n) {
  char b[12];
  int i = 0;
  do {
    b[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (i)
    put(b[--i]);
}
static void screen(const char *view) {
  home_visible = starts(view, "HOME");
  for (int i = 0; i < 2000; i++)
    cell(i % 80, i / 80, ' ', 0xf0);
  graphics_chrome(starts(view, "GHOST") ? "shell" : "nix / Core");
  if (!pixels)
    bar(0,
        "n i x     /     C O R E                                     MVP 0.1",
        0x70);
  bar(1, view, 0x70);
  bar(24, "PROMPT FIRST     /     LOCAL STORAGE     /     NETWORK DISCONNECTED",
      0x70);
  row = 4;
  col = 3;
  serial('\n');
  serial('\n');
  cursor();
}
static char key(void) {
  static int shift = 0, extended = 0;
  static const char normal[128] = {
      [1] = 27,    [2] = '1',  [3] = '2',   [4] = '3',   [5] = '4',
      [6] = '5',   [7] = '6',  [8] = '7',   [9] = '8',   [10] = '9',
      [11] = '0',  [12] = '-', [13] = '=',  [14] = '\b', [15] = '\t',
      [16] = 'q',  [17] = 'w', [18] = 'e',  [19] = 'r',  [20] = 't',
      [21] = 'y',  [22] = 'u', [23] = 'i',  [24] = 'o',  [25] = 'p',
      [26] = '[',  [27] = ']', [28] = '\n', [30] = 'a',  [31] = 's',
      [32] = 'd',  [33] = 'f', [34] = 'g',  [35] = 'h',  [36] = 'j',
      [37] = 'k',  [38] = 'l', [39] = ';',  [40] = '\'', [41] = '`',
      [43] = '\\', [44] = 'z', [45] = 'x',  [46] = 'c',  [47] = 'v',
      [48] = 'b',  [49] = 'n', [50] = 'm',  [51] = ',',  [52] = '.',
      [53] = '/',  [57] = ' '};
  static const char shifted[128] = {
      [2] = '!',  [3] = '@',  [4] = '#',  [5] = '$',  [6] = '%',  [7] = '^',
      [8] = '&',  [9] = '*',  [10] = '(', [11] = ')', [12] = '_', [13] = '+',
      [26] = '{', [27] = '}', [39] = ':', [40] = '"', [41] = '~', [43] = '|',
      [51] = '<', [52] = '>', [53] = '?'};
  for (;;) {
    if (inb(0x3fd) & 1) {
      char c = (char)inb(0x3f8);
      if (c == '\r')
        c = '\n';
      if (c == 127)
        c = '\b';
      return c;
    }
    u8 status = inb(0x64);
    if (!(status & 1))
      continue;
    u8 s = inb(0x60);
    if (status & 0x20) {
      if (mouse_byte(s)) {
        const char *action = mouse_action(mouse_x, mouse_y);
        if (action) {
          copy(mouse_pending, action, sizeof(mouse_pending));
          return 1;
        }
      }
      continue;
    }
    if (s == 0xe0) {
      extended = 1;
      continue;
    }
    if (extended) {
      extended = 0;
      continue;
    }
    if (s == 42 || s == 54) {
      shift = 1;
      continue;
    }
    if (s == 170 || s == 182) {
      shift = 0;
      continue;
    }
    if (s & 128)
      continue;
    char c = normal[s];
    if (shift) {
      if (c >= 'a' && c <= 'z')
        c -= 32;
      else if (shifted[s])
        c = shifted[s];
    }
    if (c)
      return c;
  }
}
static void input(char *s, u32 cap) {
  u32 n = 0;
  for (;;) {
    char c = key();
    if (c == 1) {
      /* A click never discards a partially typed command or draft line. */
      if (n)
        continue;
      copy(s, mouse_pending, cap);
      say(s);
      put('\n');
      return;
    }
    if (c == '\n') {
      s[n] = 0;
      put('\n');
      return;
    }
    if (c == '\b') {
      if (n) {
        n--;
        put('\b');
      }
      continue;
    }
    if (c >= 32 && c <= 126 && n + 1 < cap) {
      s[n++] = c;
      put(c);
    }
  }
}
/* Polling ATA PIO driver, primary master only. Every wait has a deadline. */
static void delay(void) {
  for (int i = 0; i < 4; i++)
    (void)inb(0x3f6);
}
static int ready(int drq) {
  for (u32 i = 0; i < 2000000; i++) {
    u8 s = inb(0x1f7);
    if (s == 0 || s == 255)
      return 0;
    if (!(s & 128)) {
      if (s & 0x21)
        return 0;
      if (!drq || (s & 8))
        return 1;
    }
  }
  return 0;
}
static int sector(u32 lba, void *buf, int write) {
  /* Firmware may leave the absent slave selected or a stale ERR bit set.
     Select our master first, then wait only for BSY before issuing a command.
   */
  outb(0x1f6, (u8)(0xe0 | ((lba >> 24) & 15)));
  delay();
  int idle = 0;
  for (u32 i = 0; i < 2000000; i++) {
    u8 status = inb(0x1f7);
    if (status == 0 || status == 255)
      return 0;
    if (!(status & 128)) {
      idle = 1;
      break;
    }
  }
  if (!idle)
    return 0;
  outb(0x1f2, 1);
  outb(0x1f3, (u8)lba);
  outb(0x1f4, (u8)(lba >> 8));
  outb(0x1f5, (u8)(lba >> 16));
  outb(0x1f7, write ? 0x30 : 0x20);
  delay();
  if (!ready(1))
    return 0;
  u16 *p = buf;
  for (int i = 0; i < 256; i++) {
    if (write)
      outw(0x1f0, p[i]);
    else
      p[i] = inw(0x1f0);
  }
  delay();
  return ready(0);
}
static int flush(void) {
  outb(0x1f7, 0xe7);
  delay();
  return ready(0);
}
#define BOOKS 4
#define BODY 4096
#define BANK_SECTORS 34
#define MAGIC 0x3158494eU
struct Book {
  char title[48];
  u32 length;
  char text[BODY];
};
struct Store {
  u32 magic, generation, checksum, count;
  struct Book books[BOOKS];
  u8 pad[800];
};
_Static_assert(sizeof(struct Store) == BANK_SECTORS * 512, "disk layout");
static struct Store store, previous;
static int active = -1, storage_ok = 0, corrupt = 0;
static int selected = -1, dirty = 0;
static u32 hash(const struct Store *s) {
  const u8 *p = (const u8 *)s;
  u32 h = 2166136261U;
  for (u32 i = 0; i < sizeof(*s); i++) {
    u8 b = (i >= 8 && i < 12) ? 0 : p[i];
    h = (h ^ b) * 16777619U;
  }
  return h;
}
static int valid(const struct Store *s) {
  if (s->magic != MAGIC || s->count > BOOKS || s->checksum != hash(s))
    return 0;
  for (u32 i = 0; i < s->count; i++)
    if (s->books[i].length >= BODY || s->books[i].text[s->books[i].length] ||
        s->books[i].title[47])
      return 0;
  return 1;
}
static int readbank(struct Store *s, int bank) {
  for (u32 i = 0; i < BANK_SECTORS; i++)
    if (!sector(1 + bank * BANK_SECTORS + i, (u8 *)s + i * 512, 0))
      return 0;
  return 1;
}
static void load(void) {
  int a = readbank(&store, 0), b = readbank(&previous, 1);
  storage_ok = a && b;
  int va = a && valid(&store), vb = b && valid(&previous);
  if (vb && (!va || previous.generation > store.generation)) {
    memcpy(&store, &previous, sizeof(store));
    active = 1;
  } else if (va)
    active = 0;
  else {
    corrupt = (a && store.magic) || (b && previous.magic);
    memset(&store, 0, sizeof(store));
    store.magic = MAGIC;
  }
}
static int save(void) {
  if (!storage_ok || corrupt)
    return 0;
  struct Store *s = &store;
  u32 oldgen = s->generation, oldhash = s->checksum;
  s->generation++;
  s->checksum = hash(s);
  int next = active == 0 ? 1 : 0; /* Header sector is the commit record, written
                                     after body flush. */
  for (u32 i = 1; i < BANK_SECTORS; i++)
    if (!sector(1 + next * BANK_SECTORS + i, (u8 *)s + i * 512, 1))
      goto fail;
  if (!flush() || !sector(1 + next * BANK_SECTORS, s, 1) || !flush())
    goto fail;
  if (!readbank(&previous, next) || !valid(&previous) ||
      previous.generation != s->generation)
    goto fail;
  active = next;
  dirty = 0;
  return 1;
fail:
  s->generation = oldgen;
  s->checksum = oldhash;
  return 0;
}
static void home(void) {
  screen("HOME  /  What would you like to do?");
  color = 0xf3;
  line("Hi. Welcome to Nix.");
  color = 0xf0;
  line("Your intent is the starting point.");
  line("");
  line("  write a book called My Story    Create an Author workspace");
  line("  open My Story                  Continue a manuscript");
  line("  show my memories               Browse saved writing");
  line("  open terminal                  Launch Ghost / Core edition");
  line("  help                           Explore this Core prototype");
  line("");
  if (corrupt)
    line("Storage damaged: saves disabled. Export/recover the disk on your "
         "Mac.");
  else if (!storage_ok)
    line("Storage unavailable: this session cannot save.");
  else {
    say("Memories online  /  ");
    number(store.count);
    line(" writing workspaces");
  }
  line("");
}
static void memories(void) {
  screen("MEMORIES  /  Your writing, on your own disk");
  if (!store.count)
    line("No Memories yet. Try: write a book called My Story");
  for (u32 i = 0; i < store.count; i++) {
    color = 0xf3;
    number(i + 1);
    say("  ");
    line(store.books[i].title);
    color = 0xf0;
    say("   ");
    number(store.books[i].length);
    line(" bytes  /  Author");
  }
  line("");
  line("Continue with: open <title>     Return with: home");
}
static void author(void) {
  screen("AUTHOR  /  Type a line, press Enter to add it to your manuscript");
  color = 0xf3;
  line(store.books[selected].title);
  color = 0xf0;
  line(":save  :read  :undo  :home  /  prefix ':' text with '::' to write it");
  line("Lines remain in RAM until :save. :home saves before leaving.");
  line("");
}
#include "ghost/shell.h"
static const char *mouse_action(int x, int y) {
  if (y >= 32 && y < 74) {
    if (x >= 950 && x < 995)
      return selected >= 0 ? ":home" : ghost_open ? "exit" : "home";
    if (x >= 300 && x < 340 && selected < 0)
      return ghost_open ? "clear" : "open terminal";
  }
  if (home_visible && x >= 60 && x < 980) {
    int hit = (y - 52) / 26;
    if (hit == 7)
      return "write a book called My Story";
    if (hit == 8)
      return "open My Story";
    if (hit == 9)
      return "show my memories";
    if (hit == 10)
      return "open terminal";
    if (hit == 11)
      return "help";
  }
  return (const char *)0;
}
static void execute(char *s) {
  char lower[256];
  copy(lower, s, sizeof(lower));
  for (u32 i = 0; lower[i]; i++)
    if (lower[i] >= 'A' && lower[i] <= 'Z')
      lower[i] += 32;
  if (ghost_open) {
    ghost_execute(s);
    return;
  }
  if (selected < 0 && (eq(lower, "terminal") || eq(lower, "open terminal") ||
                       eq(lower, "ghost"))) {
    ghost_open = 1;
    ghost_screen();
    return;
  }
  if (selected >= 0) {
    struct Book *b = &store.books[selected];
    if (eq(s, ":save")) {
      line(save() ? "Saved and verified on disk."
                  : "Save failed. Text remains in RAM; do not close QEMU.");
      return;
    }
    if (eq(s, ":home")) {
      if (dirty && !save()) {
        line("Save failed. Still in Author; your RAM draft is retained.");
        return;
      }
      selected = -1;
      home();
      return;
    }
    if (eq(s, ":read")) {
      author();
      line(b->text);
      return;
    }
    if (eq(s, ":undo")) {
      if (b->length) {
        u32 n = b->length - 1;
        while (n && b->text[n - 1] != '\n')
          n--;
        b->text[n] = 0;
        b->length = n;
        dirty = 1;
        line("Last line removed in RAM. :save to commit.");
      }
      return;
    }
    if (s[0] == ':' && s[1] != ':') {
      line("Unknown Author action. Use :save, :read, :undo, :home.");
      return;
    }
    if (s[0] == ':' && s[1] == ':')
      s++;
    u32 n = len(s);
    if (b->length + n + 1 >= BODY) {
      line("Manuscript full (4095 bytes). Export it on your Mac.");
      return;
    }
    memcpy(b->text + b->length, s, n);
    b->length += n;
    b->text[b->length++] = '\n';
    b->text[b->length] = 0;
    dirty = 1;
    return;
  }
  if (eq(lower, "home") || eq(lower, "clear")) {
    home();
    return;
  }
  if (eq(lower, "help")) {
    line("Intent examples: write a book / write a book called <title>");
    line("open <title> / show my memories / home / status / shutdown");
    line("Author: :save, :read, :undo, :home. Four books, 4095 bytes each.");
    line("Ghost is the default terminal. No AI, desktop, games or wallet yet.");
    return;
  }
  if (eq(lower, "show my memories") || eq(lower, "memories")) {
    memories();
    return;
  }
  if (eq(lower, "status")) {
    line("Core 0.1 / x86 protected mode / single address space / C + assembly");
    line("64 MiB VM / framebuffer + PS/2 + serial + ATA PIO / offline");
    say("Committed generation: ");
    number(store.generation);
    line("");
    line("Deterministic intent router. No model inference or isolation yet.");
    return;
  }
  if (eq(lower, "shutdown")) {
    line("Goodbye. All committed Memories remain on your virtual disk.");
    outw(0x604, 0x2000);
    for (;;)
      __asm__ volatile("hlt");
  }
  if (starts(lower, "open ")) {
    for (u32 i = 0; i < store.count; i++) {
      char t[48];
      copy(t, store.books[i].title, 48);
      for (u32 j = 0; t[j]; j++)
        if (t[j] >= 'A' && t[j] <= 'Z')
          t[j] += 32;
      if (eq(lower + 5, t)) {
        selected = (int)i;
        author();
        return;
      }
    }
    line("Memory not found. Try: show my memories");
    return;
  }
  if (has(lower, "write") || has(lower, "author") || has(lower, "manuscript")) {
    if (store.count == BOOKS) {
      line("All four Author slots are occupied. Open an existing Memory.");
      return;
    }
    const char *title = "Untitled";
    for (u32 i = 0; lower[i]; i++)
      if (starts(lower + i, "called ")) {
        title = s + i + 7;
        break;
      }
    if (!*title || len(title) > 47) {
      line("Choose a title between 1 and 47 characters.");
      return;
    }
    for (u32 i = 0; i < store.count; i++)
      if (eq(store.books[i].title, title)) {
        line("That title already exists. Use: open <title>");
        return;
      }
    selected = (int)store.count++;
    struct Book *b = &store.books[selected];
    memset(b, 0, sizeof(*b));
    copy(b->title, title, 48);
    dirty = 1;
    author();
    line("Workspace created in RAM. Start writing, then :save.");
    return;
  }
  if (has(lower, "diablo") || has(lower, "freelancer") || has(lower, "game")) {
    line("Play needs a guest runtime, graphics drivers, and legal game media.");
    line("This Core MVP proves Author first; games are not implemented.");
    return;
  }
  if (has(lower, "wallet") || has(lower, "bitcoin")) {
    line("Wallet is not implemented. Never enter keys or seed phrases here.");
    return;
  }
  if (*s)
    line("Tell me: write a book, open <title>, show my memories, or help.");
}
void kernel_main(u32 magic, u32 info) {
  (void)info;
  outb(0x3f9, 0);
  outb(0x3fb, 0x80);
  outb(0x3f8, 1);
  outb(0x3f9, 0);
  outb(0x3fb, 3);
  outb(0x3fa, 0xc7);
  outb(0x3fc, 0x0b);
  if (magic != 0x2badb002) {
    line("Invalid Multiboot handoff");
    for (;;)
      __asm__ volatile("hlt");
  }
  if (graphics_init())
    vga = shadow;
  load();
  home();
  mouse_init();
  for (;;) {
    color = 0xf3;
    say(selected >= 0 ? "  + " : ghost_open ? "ghost /memories > " : "nix > ");
    color = 0xf0;
    char command[256];
    input(command, sizeof(command));
    execute(command);
  }
}
