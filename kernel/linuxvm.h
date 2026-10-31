/* Dynamic Linux guest. NIX is the host.
   RAM and the guest disk are carved from free high memory.
   The 2 MiB count-words map is not used and is not enlarged.
   COM1 and the guest ATA disk are the only devices the proxy allows.
   Every other port and every other GPA stays inside NIX. */
void linux_vmrun(u64 vmcb, u64 *gprs);

static u8 linux_pml4[4096] __attribute__((aligned(4096)));
static u8 linux_pdpt[4096] __attribute__((aligned(4096)));
static u8 linux_pd[4096] __attribute__((aligned(4096)));
static u8 linux_mmio_pd[4096] __attribute__((aligned(4096)));

struct GuestBudget {
  u64 origin;
  u64 ram_bytes;
  u64 disk_bytes;
  u64 sink;
  int full;
};
#ifndef LINUX_FULL_INITRD_BYTES
#define LINUX_FULL_INITRD_BYTES 0
#endif
#define LINUX_TEMPLATE_ADDR 0xb0000000ULL
#define LINUX_SLOT_BYTES (576ULL << 20)
static u32 linux_seed_next;
u8 linux_fpu[512] __attribute__((aligned(16)));
static u64 machine_ram(void);
static int linux_rootfs_ready(void) {
#if LINUX_FULL_INITRD_BYTES > 0
  const u8 *p = (const u8 *)LINUX_TEMPLATE_ADDR;
  return machine_ram() >= (8ULL << 30) && p[0] == 0x1f && p[1] == 0x8b;
#else
  return 0;
#endif
}
static struct GuestBudget linux_budget;
static int linux_have_budget;
static int linux_saw_online;
static int linux_paused;
static char linux_app[16];

enum { GPR_RCX, GPR_RDX, GPR_RBX, GPR_RBP, GPR_RSI, GPR_RDI, GPR_R8, GPR_R9,
       GPR_R10, GPR_R11, GPR_R12, GPR_R13, GPR_R14, GPR_R15, GPR_N };
static u64 linux_gpr[GPR_N];

static u64 machine_ram(void) {
  u32 info = *(volatile u32 *)0x17ff04;
  u32 flags, upper;
  if (info < 0x1000)
    return 0;
  flags = *(volatile u32 *)(u64)info;
  /* mem_upper stops at the PCI hole. Multiboot's E820 map also includes
     usable RAM remapped above 4 GiB. Count lengths, not the highest address. */
  if (flags & (1u << 6)) {
    u32 length = *(volatile u32 *)((u64)info + 44);
    u32 address = *(volatile u32 *)((u64)info + 48);
    u64 offset = 0, total = 0;
    if (address >= 0x1000 && (u64)address + length <= (1ULL << 32)) {
      while (offset + 4 <= length) {
        const u8 *entry = (const u8 *)((u64)address + offset);
        u32 size;
        u64 bytes;
        u32 type;
        memcpy(&size, entry, 4);
        if (size < 20 || (u64)size + 4 > length - offset)
          break;
        memcpy(&bytes, entry + 12, 8);
        memcpy(&type, entry + 20, 4);
        if (type == 1 && bytes <= ~0ULL - total)
          total += bytes;
        offset += (u64)size + 4;
      }
      /* Firmware reserves small parts of conventional RAM. Report capacity
         to a whole MiB so an 8 GiB machine meets the 8 GiB requirement. */
      if (offset == length && total && total <= ~0ULL - 0xfffffULL)
        return (total + 0xfffffULL) & ~0xfffffULL;
    }
  }
  if ((flags & 1) == 0)
    return 0;
  upper = *(volatile u32 *)((u64)info + 8);
  return ((u64)upper + 1024ULL) << 10;
}
static void wipe(void *dst, u64 n) {
  u64 words = n >> 3;
  u64 *ptr = dst;
  u64 zero = 0;
  __asm__ volatile("cld; rep stosq"
                   : "+D"(ptr), "+c"(words), "+a"(zero)
                   :
                   : "memory");
  if (n & 7) {
    u8 *tail = (u8 *)ptr;
    u64 i;
    for (i = 0; i < (n & 7); i++)
      tail[i] = 0;
  }
}
static int linux_machine_ok(void) {
  u64 total = machine_ram();
  if (total >= (8ULL << 30))
    return 1;
  say("NIX needs 8 GiB of memory. This machine has ");
  number((u32)(total >> 20));
  line(" MiB. It was not started.");
  return 0;
}
static int linux_geometry(u64 *base, u64 *sink_out) {
  u64 total = machine_ram();
  u64 limit, sink, reserve;
  u32 mod;
  const u64 slice = LINUX_SLOT_BYTES;
  if (total < (64ULL << 20)) {
    line("Not enough is free for a dynamic guest. It was not started.");
    return 0;
  }
  limit = total & ~0x1FFFFFULL;
  /* NIX currently identity-maps 4 GiB. Keep allocations below the PC PCI
     hole; total machine RAM is still checked independently above. */
  if (limit > (3ULL << 30))
    limit = 3ULL << 30;
  sink = limit - (2ULL << 20);
  reserve = 64ULL << 20;
  mod = *(volatile u32 *)0x17ff14;
  if (mod) {
    u64 mend = (0x10000000ULL + mod + 0x1FFFFFULL) & ~0x1FFFFFULL;
    if (mend > reserve)
      reserve = mend;
  }
  if (reserve + slice > sink) {
    line("Not enough is free for a dynamic guest. It was not started.");
    return 0;
  }
  *base = reserve;
  *sink_out = sink;
  return 1;
}
static u8 *guest_ptr(const struct GuestBudget *b, u64 gpa, u64 len) {
  u64 bytes = b->ram_bytes + b->disk_bytes;
  if (!len || gpa >= bytes || len > bytes - gpa)
    return 0;
  return (u8 *)(b->origin + gpa);
}
static void append_hex(char **p, char *end, u64 v) {
  char tmp[16];
  int n = 0;
  do {
    tmp[n++] = "0123456789abcdef"[v & 15];
    v >>= 4;
  } while (v && n < 16);
  while (n && *p + 1 < end)
    *(*p)++ = tmp[--n];
}
static int text_has(const char *hay, const char *needle) {
  u32 i, j;
  for (i = 0; hay[i]; i++) {
    for (j = 0; needle[j] && hay[i + j] == needle[j]; j++)
      ;
    if (!needle[j])
      return 1;
  }
  return 0;
}

static char uart_line[120];
static unsigned uart_ln;
static u8 uart_ier, uart_lcr, uart_mcr, uart_scr, uart_dll, uart_dlh;
static u8 uart_rx[4096];
static unsigned uart_rr, uart_wc;
static u8 ata_sector[512];
static u8 ata_err, ata_count, ata_lba0, ata_lba1, ata_lba2, ata_drive;
static u32 ata_off;
static int ata_drq, ata_write, ata_error;
static const struct GuestBudget *ata_budget;

static char host_line[48];
static unsigned host_ln;
static void note_line(const char *text) {
  if (text_has(text, "Linux guest online")) {
    linux_saw_online = 1;
    face_linux = 1;
  }
}
static void uart_reset(void) {
  uart_ln = 0;
  uart_ier = uart_lcr = uart_mcr = uart_scr = uart_dll = uart_dlh = 0;
  uart_rr = uart_wc = 0;
  host_ln = 0;
  host_line[0] = 0;
}
static void uart_tx(u8 ch) {
  if (ch == '\r')
    return;
  if (ata_budget && ata_budget->full)
    put((char)ch);
  if (ch != '\n') {
    if (ch >= 32 && ch <= 126 && uart_ln + 1 < sizeof uart_line)
      uart_line[uart_ln++] = (char)ch;
    return;
  }
  uart_line[uart_ln] = 0;
  note_line(uart_line);
  if (!ata_budget || !ata_budget->full)
    line(uart_line);
  uart_ln = 0;
}
static u8 uart_msr(void) {
  u8 m = 0;
  if ((uart_mcr & 0x10) == 0)
    return 0xb0;
  if (uart_mcr & 1)
    m |= 0x20;
  if (uart_mcr & 2)
    m |= 0x10;
  if (uart_mcr & 4)
    m |= 0x40;
  if (uart_mcr & 8)
    m |= 0x80;
  return m;
}
static void uart_push(u8 ch) {
  if (uart_wc >= sizeof uart_rx)
    return;
  uart_rx[(uart_rr + uart_wc) % sizeof uart_rx] = ch;
  uart_wc++;
}
static u8 uart_pop(void) {
  u8 ch;
  if (!uart_wc)
    return 0;
  ch = uart_rx[uart_rr];
  uart_rr = (uart_rr + 1) % sizeof uart_rx;
  uart_wc--;
  return ch;
}
static u8 uart_in(u16 port) {
  u16 off = (u16)(port - 0x3f8);
  int dlab = (uart_lcr & 0x80) != 0;
  if (off == 0)
    return dlab ? uart_dll : uart_pop();
  if (off == 1)
    return dlab ? uart_dlh : uart_ier;
  if (off == 2)
    return ((uart_ier & 1) && uart_wc) ? 0x04 : (uart_ier & 2) ? 0x02 : 0x01;
  if (off == 3)
    return uart_lcr;
  if (off == 4)
    return uart_mcr;
  if (off == 5)
    return (u8)(0x60 | (uart_wc ? 1 : 0));
  if (off == 6)
    return uart_msr();
  return uart_scr;
}
static void uart_out(u16 port, u8 v) {
  u16 off = (u16)(port - 0x3f8);
  int dlab = (uart_lcr & 0x80) != 0;
  if (off == 0) {
    if (dlab)
      uart_dll = v;
    else
      uart_tx(v);
  } else if (off == 1) {
    if (dlab)
      uart_dlh = v;
    else
      uart_ier = (u8)(v & 0x0f);
  } else if (off == 3)
    uart_lcr = v;
  else if (off == 4)
    uart_mcr = v;
  else if (off == 7)
    uart_scr = v;
}

static u32 ata_lba(void) {
  return (u32)ata_lba0 | ((u32)ata_lba1 << 8) | ((u32)ata_lba2 << 16) |
         ((u32)(ata_drive & 15) << 24);
}
static void ata_identify(void) {
  u32 sectors = (u32)(ata_budget->disk_bytes / 512);
  u32 i;
  for (i = 0; i < 512; i++)
    ata_sector[i] = 0;
  ata_sector[0] = 0x40;
  ata_sector[20] = 'N';
  ata_sector[21] = 'I';
  ata_sector[22] = 'X';
  ata_sector[120] = (u8)sectors;
  ata_sector[121] = (u8)(sectors >> 8);
  ata_sector[122] = (u8)(sectors >> 16);
  ata_sector[123] = (u8)(sectors >> 24);
}
static void ata_read_sector(void) {
  u32 lba = ata_lba();
  u64 off = (u64)lba * 512;
  u8 *src;
  u32 i;
  if (!ata_budget->disk_bytes || off + 512 > ata_budget->disk_bytes) {
    ata_error = 1;
    ata_err = 0x04;
    ata_drq = 0;
    return;
  }
  src = guest_ptr(ata_budget, ata_budget->ram_bytes + off, 512);
  if (!src) {
    ata_error = 1;
    ata_err = 0x04;
    ata_drq = 0;
    return;
  }
  for (i = 0; i < 512; i++)
    ata_sector[i] = src[i];
  ata_off = 0;
  ata_drq = 1;
  ata_write = 0;
  ata_error = 0;
}
static void ata_finish_write(void) {
  u32 lba = ata_lba();
  u64 off = (u64)lba * 512;
  u8 *dst;
  u32 i;
  if (!ata_budget->disk_bytes || off + 512 > ata_budget->disk_bytes) {
    ata_error = 1;
    ata_err = 0x04;
    ata_drq = 0;
    return;
  }
  dst = guest_ptr(ata_budget, ata_budget->ram_bytes + off, 512);
  if (!dst) {
    ata_error = 1;
    ata_err = 0x04;
    ata_drq = 0;
    return;
  }
  for (i = 0; i < 512; i++)
    dst[i] = ata_sector[i];
  ata_drq = 0;
  ata_error = 0;
}
static void ata_command(u8 cmd) {
  ata_off = 0;
  ata_error = 0;
  ata_err = 0;
  if (!ata_budget->disk_bytes) {
    ata_error = 1;
    ata_err = 0x04;
    ata_drq = 0;
    return;
  }
  if (cmd == 0xec) {
    ata_identify();
    ata_drq = 1;
    ata_write = 0;
    return;
  }
  if (cmd == 0x20) {
    ata_read_sector();
    return;
  }
  if (cmd == 0x30) {
    ata_drq = 1;
    ata_write = 1;
    return;
  }
  ata_error = 1;
  ata_err = 0x04;
  ata_drq = 0;
}
static u8 ata_status(void) {
  if (ata_error)
    return 0x51;
  if (ata_drq)
    return 0x58;
  return 0x50;
}
static u32 ata_data_read(u32 size) {
  u32 value = 0, i;
  if (!ata_drq || ata_write)
    return 0;
  for (i = 0; i < size && ata_off < 512; i++)
    value |= (u32)ata_sector[ata_off++] << (8 * i);
  if (ata_off >= 512)
    ata_drq = 0;
  return value;
}
static void ata_data_write(u32 size, u32 value) {
  u32 i;
  if (!ata_drq || !ata_write)
    return;
  for (i = 0; i < size && ata_off < 512; i++)
    ata_sector[ata_off++] = (u8)(value >> (8 * i));
  if (ata_off >= 512)
    ata_finish_write();
}
static u32 ata_in(u16 port, u32 size) {
  if (port == 0x3f6 || port == 0x1f7)
    return ata_status();
  if (port == 0x1f0)
    return ata_data_read(size);
  if (port == 0x1f1)
    return ata_err;
  if (port == 0x1f2)
    return ata_count;
  if (port == 0x1f3)
    return ata_lba0;
  if (port == 0x1f4)
    return ata_lba1;
  if (port == 0x1f5)
    return ata_lba2;
  if (port == 0x1f6)
    return (u32)(0xe0 | (ata_drive & 15));
  return 0xff;
}
static void ata_out(u16 port, u32 size, u32 value) {
  if (port == 0x1f0) {
    ata_data_write(size, value);
    return;
  }
  if (port == 0x1f2)
    ata_count = (u8)value;
  else if (port == 0x1f3)
    ata_lba0 = (u8)value;
  else if (port == 0x1f4)
    ata_lba1 = (u8)value;
  else if (port == 0x1f5)
    ata_lba2 = (u8)value;
  else if (port == 0x1f6)
    ata_drive = (u8)value;
  else if (port == 0x1f7)
    ata_command((u8)value);
  else if (port == 0x3f6 && (value & 4)) {
    ata_drq = 0;
    ata_error = 0;
    ata_off = 0;
  }
}

#include "linux_devices.h"

static int translate(const struct GuestBudget *b, struct Vmcb *vm, u64 gva,
                     u64 *out) {
  u64 table, e, shift, idx, page;
  int level, pae, lma;
  if ((vm->save.cr0 & 0x80000000ULL) == 0) {
    *out = gva;
    return 1;
  }
  pae = (vm->save.cr4 & 0x20) != 0;
  lma = (vm->save.efer & 0x400) != 0;
  if (!pae) {
    u8 *pde_slot = guest_ptr(b, (vm->save.cr3 & ~0xfffULL) + ((gva >> 22) << 2), 4);
    u32 pde, pte;
    if (!pde_slot)
      return 0;
    memcpy(&pde, pde_slot, 4);
    if ((pde & 1) == 0)
      return 0;
    if (pde & 0x80) {
      *out = ((u64)pde & 0xffc00000ULL) | (gva & 0x3fffffULL);
      return 1;
    }
    pde_slot = guest_ptr(b, (pde & ~0xfffULL) + (((gva >> 12) & 0x3ff) << 2), 4);
    if (!pde_slot)
      return 0;
    memcpy(&pte, pde_slot, 4);
    if ((pte & 1) == 0)
      return 0;
    *out = (pte & ~0xfffULL) | (gva & 0xfffULL);
    return 1;
  }
  table = vm->save.cr3 & ~0xfffULL;
  level = lma ? 3 : 2;
  for (; level >= 0; level--) {
    u8 *slot;
    shift = 12 + 9 * (u64)level;
    idx = (gva >> shift) & 511;
    slot = guest_ptr(b, table + idx * 8, 8);
    if (!slot)
      return 0;
    memcpy(&e, slot, 8);
    if ((e & 1) == 0)
      return 0;
    page = (1ULL << shift) - 1;
    if (level == 0 || (e & 0x80)) {
      *out = (e & 0x000ffffffffff000ULL & ~page) | (gva & page);
      return 1;
    }
    table = e & 0x000ffffffffff000ULL;
  }
  return 0;
}
static int fetch8(const struct GuestBudget *b, struct Vmcb *vm, u64 gva, u8 *buf) {
  int i;
  for (i = 0; i < 8; i++) {
    u64 gpa;
    u8 *p;
    if (!translate(b, vm, gva + (u64)i, &gpa))
      return 0;
    p = guest_ptr(b, gpa, 1);
    if (!p)
      return 0;
    buf[i] = *p;
  }
  return 1;
}
static u32 io_length(const u8 *op) {
  u32 i = 0;
  while (i < 8 && (op[i] == 0x66 || op[i] == 0x67 || op[i] == 0xf0 ||
                   op[i] == 0xf2 || op[i] == 0xf3 || op[i] == 0x26 ||
                   op[i] == 0x2e || op[i] == 0x36 || op[i] == 0x3e ||
                   op[i] == 0x64 || op[i] == 0x65))
    i++;
  if (i < 8 && op[i] >= 0xe4 && op[i] <= 0xe7)
    return i + 2;
  if (i < 8 && op[i] >= 0xec && op[i] <= 0xef)
    return i + 1;
  if (i < 8 && op[i] >= 0x6c && op[i] <= 0x6f)
    return i + 1;
  return 0;
}
static void advance_io(const struct GuestBudget *b, struct Vmcb *vm) {
  u64 nrip = vm->control.exit_info_2;
  u64 rip = vm->save.rip;
  u8 op[8];
  u32 n;
  vm->control.int_state = 0; /* The emulated instruction completes STI shadow. */
  if (nrip > rip && nrip <= rip + 15) {
    vm->save.rip = nrip;
    return;
  }
  if (fetch8(b, vm, rip, op) && (n = io_length(op)) != 0) {
    vm->save.rip = rip + n;
    return;
  }
  vm->save.rip = rip + 1;
}
static u64 msr_value(struct Vmcb *vm, u64 *gpr) {
  return (vm->save.rax & 0xffffffffULL) | ((gpr[GPR_RDX] & 0xffffffffULL) << 32);
}
static void msr_result(struct Vmcb *vm, u64 *gpr, u64 value) {
  vm->save.rax = value & 0xffffffffULL;
  gpr[GPR_RDX] = (value >> 32) & 0xffffffffULL;
}
static u64 rdtsc_value(void) {
  u32 lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((u64)hi << 32) | lo;
}
static void handle_msr(struct Vmcb *vm, u64 *gpr, int write) {
  u32 index = (u32)gpr[GPR_RCX];
  u64 value = msr_value(vm, gpr);
  if (!write) {
    if (index == 0x10)
      value = rdtsc_value();
    else if (index == 0xc0000080)
      value = vm->save.efer;
    else if (index == 0xc0000081)
      value = vm->save.star;
    else if (index == 0xc0000082)
      value = vm->save.lstar;
    else if (index == 0xc0000083)
      value = vm->save.cstar;
    else if (index == 0xc0000084)
      value = vm->save.sfmask;
    else if (index == 0xc0000100)
      value = vm->save.fs.base;
    else if (index == 0xc0000101)
      value = vm->save.gs.base;
    else if (index == 0xc0000102)
      value = vm->save.kernel_gs_base;
    else if (index == 0x174)
      value = vm->save.sysenter_cs;
    else if (index == 0x175)
      value = vm->save.sysenter_esp;
    else if (index == 0x176)
      value = vm->save.sysenter_eip;
    else if (index == 0x277)
      value = vm->save.g_pat;
    else
      value = 0;
    msr_result(vm, gpr, value);
  } else if (index == 0xc0000080)
    vm->save.efer = value | 0x1000ULL;
  else if (index == 0xc0000081)
    vm->save.star = value;
  else if (index == 0xc0000082)
    vm->save.lstar = value;
  else if (index == 0xc0000083)
    vm->save.cstar = value;
  else if (index == 0xc0000084)
    vm->save.sfmask = value;
  else if (index == 0xc0000100)
    vm->save.fs.base = value;
  else if (index == 0xc0000101)
    vm->save.gs.base = value;
  else if (index == 0xc0000102)
    vm->save.kernel_gs_base = value;
  else if (index == 0x174)
    vm->save.sysenter_cs = value;
  else if (index == 0x175)
    vm->save.sysenter_esp = value;
  else if (index == 0x176)
    vm->save.sysenter_eip = value;
  else if (index == 0x277)
    vm->save.g_pat = value;
  vm->save.rip += 2;
}
static void port_access(struct Vmcb *vm, u32 size, u16 port, int is_in) {
  u32 value = (u32)vm->save.rax;
  if (linux_device_port(vm, size, port, is_in))
    return;
  if (!size)
    size = 1;
  if (size > 4)
    size = 4;
  if (port >= 0x3f8 && port <= 0x3ff) {
    if (is_in)
      vm->save.rax = (vm->save.rax & ~0xffULL) | uart_in(port);
    else
      uart_out(port, (u8)value);
    return;
  }
  if ((port >= 0x1f0 && port <= 0x1f7) || port == 0x3f6) {
    if (is_in)
      vm->save.rax = (vm->save.rax & ~((size == 1 ? 0xffULL : size == 2 ? 0xffffULL : 0xffffffffULL))) |
                     ata_in(port, size);
    else
      ata_out(port, size, value);
    return;
  }
  if (is_in)
    vm->save.rax = (vm->save.rax & ~0xffULL) | 0xff;
}

static int linux_string_io(const struct GuestBudget *b, struct Vmcb *vm,
                           u64 *gpr, u64 info, u32 size, u16 port) {
  int input = info & 1;
  int repeat = info & 8;
  u32 addr_size = (u32)((info >> 7) & 7);
  u64 mask = addr_size == 1 ? 0xffff : addr_size == 2 ? 0xffffffffULL : ~0ULL;
  u64 count = repeat ? gpr[GPR_RCX] & mask : 1;
  u64 *index = &gpr[input ? GPR_RDI : GPR_RSI];
  u64 segment = 0;
  if (!(vm->save.efer & 0x400))
    segment = input ? vm->save.es.base : vm->save.ds.base;
  for (u64 n = 0; n < count; n++) {
    u8 *ptr[4];
    u32 value = 0;
    for (u32 j = 0; j < size; j++) {
      u64 gpa;
      if (!translate(b, vm, segment + (*index & mask) + j, &gpa) ||
          !(ptr[j] = guest_ptr(b, gpa, 1))) return 0;
      if (!input) value |= (u32)*ptr[j] << (j * 8);
    }
    u64 saved = vm->save.rax;
    if (!input) vm->save.rax = value;
    port_access(vm, size, port, input);
    if (input) for (u32 j = 0; j < size; j++)
      *ptr[j] = (u8)(vm->save.rax >> (j * 8));
    vm->save.rax = saved;
    *index = (*index + ((vm->save.rflags & 0x400) ? -(u64)size : size)) & mask;
    if (repeat) gpr[GPR_RCX] = (gpr[GPR_RCX] - 1) & mask;
    if (n == 4095 && n + 1 < count) return 1; /* Yield before continuing REP. */
  }
  advance_io(b, vm);
  return 1;
}

static int map_guest(const struct GuestBudget *b) {
  u64 pages, i;
  u64 *pml4 = (u64 *)linux_pml4;
  u64 *pdpt = (u64 *)linux_pdpt;
  u64 *pd = (u64 *)linux_pd;
  u64 *mmio = (u64 *)linux_mmio_pd;
  if (((u64)linux_pml4 | (u64)linux_pdpt | (u64)linux_pd | (u64)linux_mmio_pd) &
      0xfff)
    return 0;
  wipe(linux_pml4, 4096);
  wipe(linux_pdpt, 4096);
  wipe(linux_pd, 4096);
  wipe(linux_mmio_pd, 4096);
  pml4[0] = (u64)linux_pdpt | 0x27;
  pdpt[0] = (u64)linux_pd | 0x27;
  pdpt[3] = (u64)linux_mmio_pd | 0x27;
  pages = (b->ram_bytes + b->disk_bytes) >> 21;
  if (pages > 512)
    return 0;
  for (i = 0; i < pages; i++)
    pd[i] = (b->origin + (i << 21)) | 0xe7ULL;
  for (i = 0; i < 512; i++)
    mmio[i] = b->sink | 0xe7ULL;
  return 1;
}
static int load_linux(const struct GuestBudget *b) {
  const u8 *image = linux_image;
  u64 image_len = (u64)(linux_image_end - linux_image);
  const u8 *init_image = b->full ? (const u8 *)LINUX_TEMPLATE_ADDR : linux_initrd;
  u64 init_len = b->full ? LINUX_FULL_INITRD_BYTES : (u64)(linux_initrd_end - linux_initrd);
  u32 sects, payload_off;
  u64 payload, init_gpa;
  u8 *boot, *cmd, *gdtp, *dest;
  char cmdline[320];
  char *cur;
  u64 *gdt;
  if (image_len < 0x10000 || image[0x202] != 'H' || image[0x203] != 'd' ||
      image[0x204] != 'r' || image[0x205] != 'S')
    return 0;
  sects = image[0x1f1];
  if (!sects)
    sects = 4;
  payload_off = (sects + 1) * 512;
  if (payload_off >= image_len)
    return 0;
  payload = image_len - payload_off;
  if (payload > b->ram_bytes || 0x100000 + payload >= b->ram_bytes)
    return 0;
  init_gpa = (b->ram_bytes - init_len - 0x1000) & ~0xfffULL;
  if (init_len < 32 || init_gpa < 0x1c00000ULL ||
      init_gpa + init_len > b->ram_bytes)
    return 0;
  wipe((u8 *)b->origin, b->ram_bytes);
  if (b->disk_bytes)
    wipe((u8 *)(b->origin + b->ram_bytes), b->disk_bytes);
  dest = guest_ptr(b, 0x100000, payload);
  boot = guest_ptr(b, 0x10000, 0x1000);
  cmd = guest_ptr(b, 0x20000, sizeof cmdline);
  gdtp = guest_ptr(b, 0x700, 0x20);
  if (!dest || !boot || !cmd || !gdtp)
    return 0;
  memcpy(dest, image + payload_off, payload);
  memcpy(boot + 0x1f1, image + 0x1f1, 0x200);
  boot[0x210] = 0xff;
  boot[0x211] = (u8)(boot[0x211] | 0x80);
  cur = cmdline;
  {
    const char *fixed =
        "console=ttyS0 earlyprintk=serial,ttyS0,115200 noapic nolapic nohpet "
        "pci=conf1 acpi=off lpj=1000000 i8042.nokbd i8042.noaux rdinit=/init "
        "initramfs_options=size=384M "
        "random.trust_bootloader=on "
        "nix.disk=0x";
    while (*fixed && cur + 1 < cmdline + sizeof cmdline)
      *cur++ = *fixed++;
  }
  append_hex(&cur, cmdline + sizeof cmdline, b->ram_bytes);
  if (cur + 1 < cmdline + sizeof cmdline)
    *cur++ = ',';
  append_hex(&cur, cmdline + sizeof cmdline, b->disk_bytes);
  *cur = 0;
  memcpy(cmd, cmdline, (u64)(cur - cmdline) + 1);
  *(u32 *)(boot + 0x218) = (u32)init_gpa;
  *(u32 *)(boot + 0x21c) = (u32)init_len;
  *(u32 *)(boot + 0x228) = 0x20000;
  if (b->full && linux_seed_next < 1024) {
    u8 *seeds = (u8 *)0xbf000000ULL;
    int valid_seed = 1;
    for (int i = 0; i < 8; i++)
      if (seeds[i] != (u8)"NIXRNG01"[i]) valid_seed = 0;
    if (valid_seed) {
      u8 *setup = guest_ptr(b, 0x30000, 80);
      *(u64 *)setup = 0;
      *(u32 *)(setup + 8) = 9; /* Linux SETUP_RNG_SEED. */
      *(u32 *)(setup + 12) = 64;
      u8 *seed = seeds + 8 + (u64)linux_seed_next++ * 64;
      memcpy(setup + 16, seed, 64);
      wipe(seed, 64);
      *(u64 *)(boot + 0x250) = 0x30000;
    }
  }
  if (*(u32 *)(boot + 0x214) == 0)
    *(u32 *)(boot + 0x214) = 0x100000;
  boot[0x1e8] = b->disk_bytes ? 4 : 3;
  {
    struct __attribute__((packed)) {
      u64 addr, size;
      u32 type;
    } *e820 = (void *)(boot + 0x2d0);
    e820[0].addr = 0;
    e820[0].size = 0x9f000;
    e820[0].type = 1;
    e820[1].addr = 0x9f000;
    e820[1].size = 0x61000;
    e820[1].type = 2;
    e820[2].addr = 0x100000;
    e820[2].size = b->ram_bytes - 0x100000;
    e820[2].type = 1;
    if (b->disk_bytes) {
      e820[3].addr = b->ram_bytes;
      e820[3].size = b->disk_bytes;
      e820[3].type = 2;
    }
  }
  memcpy(guest_ptr(b, init_gpa, init_len), init_image, init_len);
  gdt = (u64 *)gdtp;
  gdt[0] = 0;
  gdt[1] = 0x00cf9a000000ffffULL;
  gdt[2] = 0x00cf9a000000ffffULL;
  gdt[3] = 0x00cf92000000ffffULL;
  return 1;
}
static void program_vmcb(const struct GuestBudget *b, struct Vmcb *vm, u64 rip) {
  struct VmcbSeg code = {0x10, 0x0c9b, 0xffffffffu, 0};
  struct VmcbSeg data = {0x18, 0x0c93, 0xffffffffu, 0};
  struct VmcbSeg gdtr = {0, 0, 0x1f, 0x700};
  struct VmcbSeg none = {0, 0, 0, 0};
  memset(vm, 0, sizeof *vm);
  vm->control.intercept = (1ULL << 32) | (1ULL << 33) | (1ULL << 24) |
                          (1ULL << 27) | (1ULL << 28) | (1ULL << 31);
  vm->control.iopm_base_pa = (u64)svm_iopm;
  vm->control.msrpm_base_pa = (u64)svm_msrpm;
  vm->control.asid = 2;
  vm->control.tlb_ctl = 1;
  /* Physical interrupts obey NIX's masked IF. Guest interrupts are injected
     from its private PIC, rather than taking host device IRQs directly. */
  vm->control.int_ctl = 1u << 24;
  vm->control.nested_ctl = 1;
  vm->control.nested_cr3 = (u64)linux_pml4;
  vm->save.es = data;
  vm->save.cs = code;
  vm->save.ss = data;
  vm->save.ds = data;
  vm->save.fs = data;
  vm->save.gs = data;
  vm->save.gdtr = gdtr;
  vm->save.ldtr = none;
  vm->save.idtr = none;
  vm->save.tr = none;
  vm->save.efer = 0x1000;
  vm->save.cr0 = 0x31;
  vm->save.cr4 = 0;
  vm->save.cr3 = 0;
  vm->save.dr6 = 0xffff0ff0;
  vm->save.dr7 = 0x400;
  vm->save.rflags = 2;
  vm->save.rip = rip;
  vm->save.rsp = 0x9000;
  vm->save.g_pat = 0x0007040600070406ULL;
  (void)b;
}
static int linux_tab_slot(int tab);
static int linux_key_ctrl;
static int linux_key_extended;
static int poll_host(void) {
  int c = -1;
  if (inb(0x3fd) & 1) {
    c = (unsigned char)inb(0x3f8);
    if (c == '\r')
      c = '\n';
    if (c == 127)
      c = '\b';
  } else {
    u8 status = inb(0x64);
    u8 scan;
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
    if (!(status & 1))
      return 0;
    scan = inb(0x60);
    if (status & 0x20) {
      int tab;
      if (!mouse_byte(scan))
        return 0;
      tab = face_tab_at(mouse_x, mouse_y);
      if (tab == 0)
        return 2;
      if (tab == 1)
        return 3;
      if (linux_tab_slot(tab) >= 0)
        return 7;
      return 0;
    }
    if (scan == 0xe0) {
      linux_key_extended = 1;
      return 0;
    }
    if (scan == 42 || scan == 54) {
      key_shift = 1;
      return 0;
    }
    if (scan == 29 || scan == 157) {
      linux_key_ctrl = scan == 29;
      linux_key_extended = 0;
      return 0;
    }
    if (linux_key_extended) {
      const char *sequence = 0;
      linux_key_extended = 0;
      if (ata_budget && ata_budget->full) {
        if (scan == 0x48) sequence = "\033[A";
        if (scan == 0x50) sequence = "\033[B";
        if (scan == 0x4d) sequence = "\033[C";
        if (scan == 0x4b) sequence = "\033[D";
        if (scan == 0x47) sequence = "\033[H";
        if (scan == 0x4f) sequence = "\033[F";
        if (scan == 0x53) sequence = "\033[3~";
      }
      if (sequence) while (*sequence) uart_push((u8)*sequence++);
      return 0;
    }
    if (scan == 170 || scan == 182) {
      key_shift = 0;
      return 0;
    }
    if (scan & 0x80)
      return 0;
    c = normal[scan];
    if (key_shift) {
      if (c >= 'a' && c <= 'z')
        c -= 32;
      else if (shifted[scan])
        c = shifted[scan];
    }
    if (!c)
      return 0;
    if (linux_key_ctrl && c >= 'a' && c <= 'z')
      c = c - 'a' + 1;
  }
  if (c == 27) {
    if (ata_budget && ata_budget->full) {
      uart_push(27);
      return 0;
    }
    return 1;
  }
  if (c > 0 && c < 32 && c != '\n' && c != '\b') {
    uart_push((u8)c);
    if (c == 3 || c == 21) {
      host_ln = 0;
      host_line[0] = 0;
      if (face_on) face_sync("");
    }
    return 0;
  }
  if (c == '\b') {
    if (host_ln)
      host_line[--host_ln] = 0;
    if (face_on)
      face_sync(host_line);
    uart_push('\b');
    return 0;
  }
  if (c == '\n') {
    int leave = host_ln == 3 && host_line[0] == 'n' && host_line[1] == 'i' &&
                host_line[2] == 'x';
    host_ln = 0;
    host_line[0] = 0;
    if (face_on)
      face_sync("");
    if (!leave)
      uart_push('\n');
    return leave;
  }
  if (c >= 32 && c <= 126) {
    uart_push((u8)c);
    if (host_ln + 1 < sizeof host_line) {
      host_line[host_ln++] = (char)c;
      host_line[host_ln] = 0;
      if (face_on)
        face_sync(host_line);
    }
  }
  return 0;
}
static int disk_checked(const struct GuestBudget *b) {
  u8 *disk;
  if (b->disk_bytes < 4096)
    return b->disk_bytes == 0;
  disk = (u8 *)(b->origin + b->ram_bytes);
  disk[0] = 'N';
  disk[1] = 'I';
  disk[2] = 'X';
  disk[b->disk_bytes - 1] = 0x5a;
  if (disk[0] != 'N' || disk[b->disk_bytes - 1] != 0x5a)
    return 0;
  disk[0] = disk[1] = disk[2] = 0;
  disk[b->disk_bytes - 1] = 0;
  return 1;
}

enum { LINUX_SLOTS = 4 };
enum { STOP_OK = 1, STOP_FAULT = 2, STOP_NIX = 3, STOP_BUDGET = 4, STOP_PROMPT = 5,
       STOP_TERM = 6, STOP_SWITCH = 7 };
struct LinuxSlot {
  int used;
  int paused;
  int online;
  char name[16];
  struct GuestBudget budget;
  u64 gpr[GPR_N];
  unsigned uart_ln, uart_rr, uart_wc, host_ln;
  u8 uart_ier, uart_lcr, uart_mcr, uart_scr, uart_dll, uart_dlh;
  u8 uart_rx[4096];
  char uart_line[120];
  char host_save[48];
  u8 ata_err, ata_count, ata_lba0, ata_lba1, ata_lba2, ata_drive;
  u32 ata_off;
  int ata_drq, ata_write, ata_error;
  u8 ata_sector[512];
  u8 vmcb[4096];
  u8 fpu[512];
};
static struct LinuxSlot linux_slot[LINUX_SLOTS];
static int linux_switch_to = -1;
static char linux_requested[LINUX_SLOTS][16];
static int linux_requested_n;
static int linux_find(const char *name) {
  int i;
  if (!name || !name[0])
    return -1;
  for (i = 0; i < LINUX_SLOTS; i++)
    if (linux_slot[i].used && eq(linux_slot[i].name, name))
      return i;
  return -1;
}
static int linux_was_requested(const char *name) {
  int i;
  for (i = 0; i < linux_requested_n; i++)
    if (eq(linux_requested[i], name))
      return 1;
  return 0;
}
static void linux_remember(const char *name) {
  if (!name || !name[0] || linux_was_requested(name))
    return;
  if (linux_requested_n >= LINUX_SLOTS)
    return;
  copy(linux_requested[linux_requested_n++], name, 16);
}
static void linux_capture(int i, u64 *gpr) {
  struct LinuxSlot *s;
  if (i < 0 || i >= LINUX_SLOTS)
    return;
  s = &linux_slot[i];
  if (gpr)
    memcpy(s->gpr, gpr, sizeof s->gpr);
  memcpy(s->vmcb, svm_vmcb, sizeof s->vmcb);
  memcpy(s->fpu, linux_fpu, sizeof s->fpu);
  s->online = linux_saw_online;
  s->uart_ln = uart_ln;
  s->uart_rr = uart_rr;
  s->uart_wc = uart_wc;
  s->host_ln = host_ln;
  s->uart_ier = uart_ier;
  s->uart_lcr = uart_lcr;
  s->uart_mcr = uart_mcr;
  s->uart_scr = uart_scr;
  s->uart_dll = uart_dll;
  s->uart_dlh = uart_dlh;
  memcpy(s->uart_rx, uart_rx, sizeof s->uart_rx);
  memcpy(s->uart_line, uart_line, sizeof s->uart_line);
  memcpy(s->host_save, host_line, sizeof s->host_save);
  s->ata_err = ata_err;
  s->ata_count = ata_count;
  s->ata_lba0 = ata_lba0;
  s->ata_lba1 = ata_lba1;
  s->ata_lba2 = ata_lba2;
  s->ata_drive = ata_drive;
  s->ata_off = ata_off;
  s->ata_drq = ata_drq;
  s->ata_write = ata_write;
  s->ata_error = ata_error;
  memcpy(s->ata_sector, ata_sector, sizeof s->ata_sector);
}
static void linux_restore(int i) {
  struct LinuxSlot *s = &linux_slot[i];
  memcpy(svm_vmcb, s->vmcb, sizeof s->vmcb);
  memcpy(linux_fpu, s->fpu, sizeof s->fpu);
  linux_saw_online = s->online;
  uart_ln = s->uart_ln;
  uart_rr = s->uart_rr;
  uart_wc = s->uart_wc;
  host_ln = s->host_ln;
  uart_ier = s->uart_ier;
  uart_lcr = s->uart_lcr;
  uart_mcr = s->uart_mcr;
  uart_scr = s->uart_scr;
  uart_dll = s->uart_dll;
  uart_dlh = s->uart_dlh;
  memcpy(uart_rx, s->uart_rx, sizeof uart_rx);
  memcpy(uart_line, s->uart_line, sizeof uart_line);
  memcpy(host_line, s->host_save, sizeof host_line);
  ata_err = s->ata_err;
  ata_count = s->ata_count;
  ata_lba0 = s->ata_lba0;
  ata_lba1 = s->ata_lba1;
  ata_lba2 = s->ata_lba2;
  ata_drive = s->ata_drive;
  ata_off = s->ata_off;
  ata_drq = s->ata_drq;
  ata_write = s->ata_write;
  ata_error = s->ata_error;
  memcpy(ata_sector, s->ata_sector, sizeof ata_sector);
  ata_budget = &s->budget;
  if (face_on)
    face_sync(host_line);
}
static int linux_tab_slot(int tab) {
  const char *name;
  int i;
  if (tab < 2 || tab - 2 >= face_extra_n)
    return -1;
  name = face_extra_name[tab - 2];
  i = linux_find(name);
  if (i < 0 || i == linux_cur)
    return -1;
  return i;
}

static int host_event(void) {
  int hop = poll_host();
  if (hop == 1)
    return STOP_NIX;
  if (hop == 2)
    return STOP_PROMPT;
  if (hop == 3)
    return STOP_TERM;
  if (hop == 7) {
    linux_switch_to = linux_tab_slot(face_tab_at(mouse_x, mouse_y));
    return STOP_SWITCH;
  }
  return 0;
}
static int linux_pause(int why, u64 *gpr) {
  if (why == STOP_PROMPT || why == STOP_TERM || why == STOP_SWITCH)
    linux_capture(linux_cur, gpr);
  return why;
}
static int run_linux_once(const struct GuestBudget *b, int resume) {
  struct Vmcb *vm = (struct Vmcb *)svm_vmcb;
  u64 gpr[GPR_N];
  u64 rip;
  u32 halts = 0;
  u32 exits = 0;
  u8 *boot;
  ata_budget = b;
  if (!resume) {
    linux_saw_online = 0;
    uart_reset();
    ata_drq = ata_write = ata_error = 0;
    ata_off = 0;
    if (!map_guest(b) || !load_linux(b))
      return STOP_FAULT;
    boot = guest_ptr(b, 0x10000, 4);
    rip = boot ? *(u32 *)(boot + 0x214) : 0;
    if (!rip)
      rip = 0x100000;
    memset(svm_iopm, 0xff, sizeof svm_iopm);
    memset(svm_msrpm, 0xff, sizeof svm_msrpm);
    memset(svm_hsave, 0, sizeof svm_hsave);
    program_vmcb(b, vm, rip);
    memset(gpr, 0, sizeof gpr);
    memset(linux_fpu, 0, sizeof linux_fpu);
    linux_fpu[0] = 0x7f;
    linux_fpu[1] = 3;
    linux_fpu[24] = 0x80;
    linux_fpu[25] = 0x1f;
    gpr[GPR_RSI] = 0x10000;
  } else {
    /* The Linux NPT is shared scratch space. Rebind it to this slot before
       resuming; another tab may have run since its VMCB was captured. */
    if (!map_guest(b))
      return STOP_FAULT;
    vm->control.tlb_ctl = 1;
    memcpy(gpr, linux_slot[linux_cur].gpr, sizeof gpr);
  }
  wrmsr(0xC0000080, rdmsr(0xC0000080) | (1ULL << 12));
  wrmsr(0xC0010117, (u64)svm_hsave);
  /* QEMU keeps the live STAR/LSTAR. A guest syscall would enter NIX
     unless these MSRs are the guest values for the duration of vmrun. */
  u64 host_msr[7];
  host_msr[0] = rdmsr(0xC0000081);
  host_msr[1] = rdmsr(0xC0000082);
  host_msr[2] = rdmsr(0xC0000083);
  host_msr[3] = rdmsr(0xC0000084);
  host_msr[4] = rdmsr(0xC0000100);
  host_msr[5] = rdmsr(0xC0000101);
  host_msr[6] = rdmsr(0xC0000102);
  for (;;) {
    if (++exits > 30000000u && !(b->full && linux_saw_online))
      return STOP_BUDGET;
    wrmsr(0xC0000081, vm->save.star);
    wrmsr(0xC0000082, vm->save.lstar);
    wrmsr(0xC0000083, vm->save.cstar);
    wrmsr(0xC0000084, vm->save.sfmask);
    wrmsr(0xC0000100, vm->save.fs.base);
    wrmsr(0xC0000101, vm->save.gs.base);
    wrmsr(0xC0000102, vm->save.kernel_gs_base);
    linux_interrupts(vm);
    linux_vmrun((u64)vm, gpr);
    wrmsr(0xC0000081, host_msr[0]);
    wrmsr(0xC0000082, host_msr[1]);
    wrmsr(0xC0000083, host_msr[2]);
    wrmsr(0xC0000084, host_msr[3]);
    wrmsr(0xC0000100, host_msr[4]);
    wrmsr(0xC0000101, host_msr[5]);
    wrmsr(0xC0000102, host_msr[6]);
    reload_cr3();
    vm->control.tlb_ctl = 0;
    {
      int why = host_event();
      if (why)
        return linux_pause(why, gpr);
    }
    if (vm->control.exit_code == 0x7b) {
      u64 info = vm->control.exit_info_1;
      u32 size = (u32)((info >> 4) & 7);
      u16 port = (u16)((info >> 16) & 0xffff);
      if (info & 4) {
        if (!size || size > 4 || !linux_string_io(b, vm, gpr, info, size, port))
          return STOP_FAULT;
        continue;
      } else
        port_access(vm, size, port, (int)(info & 1));
      advance_io(b, vm);
      continue;
    }
    if (vm->control.exit_code == 0x7c) {
      handle_msr(vm, gpr, (int)(vm->control.exit_info_1 & 1));
      continue;
    }
    if (vm->control.exit_code == 0x78) {
      vm->save.rip += 1;
      vm->control.int_state = 0;
      /* After /init is up, an idle HLT is not the session ending. */
      if ((vm->save.rflags & 0x200) == 0 && !linux_saw_online && ++halts > 1000)
        return STOP_FAULT;
      continue;
    }
    if (vm->control.exit_code == 0x81) {
      if (!linux_saw_online)
        return STOP_FAULT;
      if (vm->save.rax == 2)
        return STOP_OK;
      if (vm->save.rax == 1) {
        while (!uart_wc) {
          int why = host_event();
          if (why)
            return linux_pause(why, gpr);
          __asm__ volatile("pause");
        }
        vm->save.rip += 3;
        vm->save.rax = uart_pop();
        continue;
      }
      vm->save.rip += 3;
      continue;
    }
    if (vm->control.exit_code == 0x7f)
      return linux_saw_online ? STOP_OK : STOP_FAULT;
    if (vm->control.exit_code == 0x400) {
      say("IOMMU fault GPA ");
      hex64(vm->control.exit_info_2);
      line(".");
      return STOP_FAULT;
    }
    say("SVM exit ");
    hex64(vm->control.exit_code);
    line(".");
    return STOP_FAULT;
  }
}
static void linux_status_line(void) {
  int n = 0;
  int i;
  for (i = 0; i < LINUX_SLOTS; i++)
    if (linux_slot[i].used)
      n++;
  if (!n && !linux_have_budget) {
    line("Hypervisor: dynamic guest RAM and disk. The 2 MiB build guest is separate.");
    return;
  }
  say("Hypervisor: ");
  number((u32)n);
  line(" Linux kernels. Private RAM and disks. The 2 MiB build guest is separate.");
}
static void linux_release(int slot, const char *msg) {
  char name[16];
  int i;
  if (slot < 0 || slot >= LINUX_SLOTS || !linux_slot[slot].used) {
    line(msg);
    return;
  }
  copy(name, linux_slot[slot].name, 16);
  linux_slot[slot].used = 0;
  linux_slot[slot].paused = 0;
  if (linux_cur == slot)
    linux_cur = -1;
  linux_app[0] = 0;
  linux_paused = 0;
  face_linux = 0;
  for (i = 0; i < LINUX_SLOTS; i++)
    if (linux_slot[i].used)
      face_linux = 1;
  face_close_app(name);
  line(msg);
}
static int linux_hold(int why) {
  if (why != STOP_PROMPT && why != STOP_TERM)
    return 0;
  linux_paused = 1;
  if (linux_cur >= 0)
    linux_slot[linux_cur].paused = 1;
  if (why == STOP_TERM) {
    ghost_open = 1;
    ghost_screen();
  } else {
    ghost_open = 0;
    face_load(0);
  }
  return 1;
}
static int linux_session(int slot, int resume) {
  int lives = resume ? 1 : 2;
  linux_cur = slot;
  copy(linux_app, linux_slot[slot].name, 16);
  while (lives) {
    int why = run_linux_once(&linux_slot[slot].budget, resume);
    int next;
    resume = 0;
    if (why == STOP_SWITCH && linux_switch_to >= 0 &&
        linux_switch_to < LINUX_SLOTS && linux_slot[linux_switch_to].used) {
      next = linux_switch_to;
      linux_slot[slot].paused = 1;
      linux_slot[next].paused = 0;
      slot = next;
      linux_cur = next;
      copy(linux_app, linux_slot[slot].name, 16);
      linux_restore(slot);
      next = face_find_extra(linux_slot[slot].name);
      if (next >= 0)
        face_load(2 + next);
      resume = 1;
      lives = 1;
      continue;
    }
    if (linux_hold(why))
      return 1;
    if (why == STOP_OK || why == STOP_NIX) {
      linux_release(slot, "Linux guest stopped. NIX is still the host.");
      return 1;
    }
    if (why == STOP_BUDGET) {
      linux_release(slot,
                    "Linux guest exceeded its exit budget. NIX is still the host.");
      return 1;
    }
    if (--lives <= 0)
      break;
    line("Linux guest faulted. NIX restarted it.");
  }
  linux_release(slot, "Linux guest stopped. NIX is still the host.");
  return 1;
}
static int linux_slot_new(const char *name, struct GuestBudget *b) {
  u64 base, sink;
  int i;
  const u64 slice = LINUX_SLOT_BYTES;
  if (!linux_geometry(&base, &sink))
    return -1;
  for (i = 0; i < LINUX_SLOTS; i++)
    if (!linux_slot[i].used)
      break;
  if (i == LINUX_SLOTS) {
    line("Four kernels are already running. Stop one with nix.");
    return -1;
  }
  if (base + ((u64)i + 1) * slice > sink ||
      base + ((u64)i + 1) * slice > LINUX_TEMPLATE_ADDR) {
    line("Not enough is free for another kernel. It was not started.");
    return -1;
  }
  memset(&linux_slot[i], 0, sizeof linux_slot[i]);
  linux_slot[i].used = 1;
  copy(linux_slot[i].name, name ? name : "linux", 16);
  b->origin = base + (u64)i * slice;
  b->full = linux_rootfs_ready();
  b->ram_bytes = (b->full ? 512ULL : 48ULL) << 20;
  b->disk_bytes = (b->full ? 64ULL : 16ULL) << 20;
  b->sink = sink;
  linux_slot[i].budget = *b;
  linux_devices_reset(i);
  return i;
}
static void linux_guest_enter(const char *name) {
  struct GuestBudget budget;
  int slot;
  if (!svm_ready) {
    line("SVM is not available on this CPU. The dynamic guest was not started.");
    return;
  }
  if ((u64)(linux_image_end - linux_image) < 0x10000) {
    line("Linux 7.3.1 image is not loaded. It was not started.");
    return;
  }
  if (!linux_machine_ok())
    return;
#if LINUX_FULL_INITRD_BYTES > 0 || defined(LINUX_REQUIRE_USERSPACE)
  if (!linux_rootfs_ready()) {
    line("The full Linux userspace template is not loaded. Start with scripts/run.sh.");
    return;
  }
#endif
  slot = linux_slot_new(name, &budget);
  if (slot < 0)
    return;
  linux_budget = budget;
  linux_have_budget = 1;
  say("Dynamic RAM: ");
  number((u32)(budget.ram_bytes >> 20));
  say(" MiB at ");
  hex64(budget.origin);
  line(".");
  say("Dynamic disk: ");
  number((u32)(budget.disk_bytes >> 20));
  say(" MiB at ");
  hex64(budget.origin + budget.ram_bytes);
  line(".");
  line("IOMMU proxy: COM1 and the guest disk. Other devices stay contained.");
  if (budget.disk_bytes && !disk_checked(&budget)) {
    linux_slot[slot].used = 0;
    line("Dynamic disk check failed. It was not started.");
    return;
  }
  if (budget.disk_bytes)
    line("Dynamic disk checked.");
  if (face_open_app(linux_slot[slot].name) < 0) {
    linux_slot[slot].used = 0;
    line("The tab bar is full. It was not started.");
    return;
  }
  copy(linux_app, linux_slot[slot].name, 16);
  linux_paused = 0;
  face_linux = 1;
  linux_cur = slot;
  wipe((u8 *)budget.sink, 2ULL << 20);
  linux_session(slot, 0);
}
static int linux_focus(const char *name) {
  int slot = linux_find(name);
  int face;
  if (slot < 0 || !linux_slot[slot].paused)
    return 0;
  ghost_open = 0;
  linux_paused = 0;
  linux_slot[slot].paused = 0;
  linux_cur = slot;
  copy(linux_app, linux_slot[slot].name, 16);
  face = face_find_extra(name);
  if (face >= 0)
    face_load(2 + face);
  linux_restore(slot);
  linux_session(slot, 1);
  return 1;
}
