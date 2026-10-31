/* Linux userspace for the dynamic guest. NIX is the host. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static long call3(long n, long a, long b, long c) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a), "S"(b), "d"(c)
                   : "rcx", "r11", "memory");
  return ret;
}
static void outb(u16 p, u8 v);
static void emit(const char *s, int ports) {
  unsigned long n = 0;
  while (s[n])
    n++;
  call3(1, 1, (long)s, (long)n);
  if (!ports)
    return;
  while (*s)
    outb(0x3f8, (u8)*s++);
}
static void emit_u(u32 n, int ports) {
  char rev[12], out[12];
  int i = 0, j = 0;
  do {
    rev[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (i)
    out[j++] = rev[--i];
  out[j] = 0;
  emit(out, ports);
}
static void outb(u16 p, u8 v) {
  __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(p));
}
static u8 inb(u16 p) {
  u8 v;
  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
  return v;
}
static void outw(u16 p, u16 v) {
  __asm__ volatile("outw %0, %1" ::"a"(v), "Nd"(p));
}
static u16 inw(u16 p) {
  u16 v;
  __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p));
  return v;
}
static int wait_drq(void) {
  for (int i = 0; i < 100000; i++) {
    u8 s = inb(0x1f7);
    if (s & 1)
      return 0;
    if (s & 0x08)
      return 1;
  }
  return 0;
}
static void ata_cmd(u32 lba, u8 cmd) {
  outb(0x1f6, (u8)(0xe0 | ((lba >> 24) & 15)));
  outb(0x1f2, 1);
  outb(0x1f3, (u8)lba);
  outb(0x1f4, (u8)(lba >> 8));
  outb(0x1f5, (u8)(lba >> 16));
  outb(0x1f7, cmd);
}
static void finish(void) {
  for (;;)
    __asm__ volatile(".byte 0x0f, 0x01, 0xd9");
}
static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return !*a && !*b;
}
static int begins(const char *s, const char *p) {
  while (*p && *s == *p) {
    s++;
    p++;
  }
  return !*p;
}
static void trim(char *s) {
  char *r = s;
  char *w = s;
  while (*r == ' ')
    r++;
  while (*r)
    *w++ = *r++;
  *w = 0;
  while (w > s && w[-1] == ' ')
    *--w = 0;
}
/* rax 1: NIX waits for one key. rax 2: leave the guest. */
static int wait_byte(void) {
  unsigned long ch;
  __asm__ volatile("mov $1, %%rax\n\t.byte 0x0f, 0x01, 0xd9"
                   : "=a"(ch)::"memory");
  return (int)(ch & 0xff);
}
static void leave_host(void) __attribute__((noreturn));
static void leave_host(void) {
  for (;;)
    __asm__ volatile("mov $2, %%rax\n\t.byte 0x0f, 0x01, 0xd9" ::: "rax", "memory");
}
static void command(char *buf, u32 mib, int disk_ok, int ports) {
  trim(buf);
  if (!buf[0])
    return;
  if (same(buf, "exit") || same(buf, "nix")) {
    leave_host();
    return;
  }
  if (same(buf, "help")) {
    emit("help   uname   disk   echo   exit\n", ports);
    emit("nix and Enter returns to NIX.\n", ports);
    return;
  }
  if (same(buf, "uname")) {
    emit("Linux 7.3.1 x86_64. NIX is the host.\n", ports);
    return;
  }
  if (same(buf, "disk")) {
    if (!disk_ok) {
      emit("Guest disk is not attached.\n", ports);
      return;
    }
    emit("Guest disk ", ports);
    emit_u(mib, ports);
    emit(" MiB.\n", ports);
    return;
  }
  if (same(buf, "echo")) {
    emit("\n", ports);
    return;
  }
  if (begins(buf, "echo ")) {
    emit(buf + 5, ports);
    emit("\n", ports);
    return;
  }
  emit("No program named ", ports);
  emit(buf, ports);
  emit(".\n", ports);
  emit("help   uname   disk   echo   exit\n", ports);
}
static void shell(u32 mib, int disk_ok, int ports) __attribute__((noreturn));
static void shell(u32 mib, int disk_ok, int ports) {
  char buf[48];
  emit("Linux 7.3.1 is running inside NIX.\n", ports);
  emit("Type help. Type nix and Enter to return.\n", ports);
  for (;;) {
    int n = 0;
    emit("linux > \n", ports);
    for (;;) {
      int c = wait_byte();
      if (c == '\n' || c == '\r')
        break;
      if (c == '\b' || c == 127) {
        if (n)
          n--;
        continue;
      }
      if (c >= 32 && c <= 126 && n + 1 < (int)sizeof buf)
        buf[n++] = (char)c;
    }
    buf[n] = 0;
    command(buf, mib, disk_ok, ports);
  }
}
void _start(void) {
  u16 ident[256];
  u16 sector[256];
  u32 i, sectors, mib;
  long io;
  emit("Linux guest online. NIX is the host.\n", 0);
  io = call3(172, 3, 0, 0);
  if (io != 0) {
    emit("Guest disk stays behind the IOMMU proxy.\n", 0);
    finish();
  }
  emit("Linux guest online. NIX is the host.\n", 1);
  ata_cmd(0, 0xec);
  if (!wait_drq()) {
    emit("Guest disk is not attached.\n", 1);
    shell(0, 0, 1);
  }
  for (i = 0; i < 256; i++)
    ident[i] = inw(0x1f0);
  if (ident[0] != 0x0040) {
    emit("Guest disk is not attached.\n", 1);
    shell(0, 0, 1);
  }
  sectors = (u32)ident[60] | ((u32)ident[61] << 16);
  mib = sectors / 2048;
  for (i = 0; i < 256; i++)
    sector[i] = 0;
  sector[0] = 0x4958;
  sector[1] = 0x4b44;
  ata_cmd(0, 0x30);
  if (!wait_drq()) {
    emit("Guest disk is not attached.\n", 1);
    shell(0, 0, 1);
  }
  for (i = 0; i < 256; i++)
    outw(0x1f0, sector[i]);
  ata_cmd(0, 0x20);
  if (!wait_drq()) {
    emit("Guest disk is not attached.\n", 1);
    shell(0, 0, 1);
  }
  for (i = 0; i < 256; i++)
    sector[i] = inw(0x1f0);
  if (sector[0] != 0x4958 || sector[1] != 0x4b44) {
    emit("Guest disk is not attached.\n", 1);
    shell(0, 0, 1);
  }
  emit("Guest disk ", 1);
  emit_u(mib, 1);
  emit(" MiB online.\n", 1);
  shell(mib, 1, 1);
}
