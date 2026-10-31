/* Per-tab PC devices. Only a PIO-only NE2000 adapter is passed through;
   guests never receive PCI access to host disks or DMA-capable devices. */
struct LinuxDevices {
  u8 mask[2], base[2], init[2], icw4[2], isr[2], read_isr[2];
  u32 pci_addr, pci_bar;
  u16 pci_command, nic_port;
  u8 bar_probe, nic_command, nic_mask, cmos_index;
  u64 last_tick;
};
static struct LinuxDevices linux_devices[4];
static int linux_cur = -1;
static u64 linux_tick_cycles = 4000000;
static inline u32 linux_inl(u16 p) {
  u32 v;
  __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static inline void linux_outl(u16 p, u32 v) {
  __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p));
}
static u32 linux_pci_host(u8 dev, u8 off) {
  linux_outl(0xcf8, 0x80000000u | ((u32)dev << 11) | off);
  return linux_inl(0xcfc);
}
static int linux_network_present(void) {
  for (u8 dev = 0; dev < 32; dev++)
    if (linux_pci_host(dev, 0) == 0x802910ec) return 1;
  return 0;
}
static void linux_devices_reset(int slot) {
  struct LinuxDevices *d = &linux_devices[slot];
  int n = 0;
  memset(d, 0, sizeof *d);
  d->mask[0] = d->mask[1] = 0xff;
  d->base[0] = 8;
  d->base[1] = 0x70;
  d->pci_bar = 0x301;
  d->pci_command = 1;
  for (u8 dev = 0; dev < 32; dev++) {
    if (linux_pci_host(dev, 0) != 0x802910ec)
      continue;
    if (n++ != slot)
      continue;
    d->nic_port = (u16)(0xc000 + slot * 0x100);
    linux_outl(0xcf8, 0x80000000u | ((u32)dev << 11) | 0x10);
    linux_outl(0xcfc, d->nic_port | 1u);
    linux_outl(0xcf8, 0x80000000u | ((u32)dev << 11) | 4);
    linux_outl(0xcfc, 1); /* I/O only; no bus mastering. */
    outb(d->nic_port + 0x1f, inb(d->nic_port + 0x1f));
    break;
  }
}
static u32 linux_pci_config(struct LinuxDevices *d, u32 off) {
  if (!d->nic_port || (d->pci_addr & 0x80ffff00u) != 0x80001800u)
    return 0xffffffffu;
  if (off == 0) return 0x802910ec;
  if (off == 4) return d->pci_command;
  if (off == 8) return 0x02000000;
  if (off == 0x10) return d->bar_probe ? 0xffffffe1u : d->pci_bar;
  if (off == 0x3c) return 0x109; /* INTA, IRQ9. */
  return 0;
}
static int linux_device_port(struct Vmcb *vm, u32 size, u16 port, int input) {
  struct LinuxDevices *d = &linux_devices[linux_cur];
  u32 v = (u32)vm->save.rax, mask = size == 1 ? 0xff : size == 2 ? 0xffff : ~0u;
  u32 off;
  if (port == 0xcf8) {
    if (input) v = d->pci_addr; else d->pci_addr = v;
  } else if (port >= 0xcfc && port <= 0xcff) {
    off = (d->pci_addr & 0xfc) + port - 0xcfc;
    if (input) v = linux_pci_config(d, off & ~3u) >> ((off & 3) * 8);
    else if (d->nic_port && (d->pci_addr & 0x80ffff00u) == 0x80001800u) {
      if (off == 0x10) {
        d->bar_probe = v == 0xffffffffu;
        if (!d->bar_probe) d->pci_bar = (v & 0xffe0) | 1;
      } else if (off == 4) d->pci_command = (u16)v;
    }
  } else if (port == 0x20 || port == 0x21 || port == 0xa0 || port == 0xa1) {
    int p = port >= 0xa0;
    if (input) v = (port & 1) ? d->mask[p] : d->read_isr[p] ? d->isr[p] : 0;
    else if (!(port & 1)) {
      if (v & 0x10) { d->init[p] = 1; d->icw4[p] = v & 1; d->isr[p] = 0; }
      else if (v & 0x20) {
        if (v & 0x40) d->isr[p] &= (u8)~(1u << (v & 7));
        else for (int i = 0; i < 8; i++) if (d->isr[p] & (1u << i)) {
          d->isr[p] &= (u8)~(1u << i); break;
        }
      } else if ((v & 0x18) == 8) d->read_isr[p] = v & 1;
    } else if (d->init[p]) {
      if (d->init[p] == 1) d->base[p] = (u8)(v & 0xf8);
      if (++d->init[p] >= (d->icw4[p] ? 4 : 3)) d->init[p] = 0;
    } else d->mask[p] = (u8)v;
  } else if (port == 0x70) {
    if (input) v = d->cmos_index; else d->cmos_index = (u8)v;
  } else if (port == 0x71) {
    if (input) { outb(0x70, d->cmos_index); v = inb(0x71); }
  } else if (port == 0x80) {
    if (input) v = 0xff;
  } else if ((port >= 0x40 && port <= 0x43) || port == 0x61) {
    /* Only the selected guest uses the PIT; the NIX scheduler keeps IF=0. */
    if (input) v = inb(port); else outb(port, (u8)v);
  } else if (d->nic_port && (d->pci_command & 1) &&
             port >= (d->pci_bar & 0xffe0) && port < (d->pci_bar & 0xffe0) + 32) {
    off = port - (d->pci_bar & 0xffe0);
    u16 real = (u16)(d->nic_port + off);
    if (input) v = size == 1 ? inb(real) : size == 2 ? inw(real) : linux_inl(real);
    else {
      if (off == 0) d->nic_command = (u8)v;
      if (off == 15 && !(d->nic_command & 0xc0)) d->nic_mask = (u8)v;
      if (size == 1) outb(real, (u8)v);
      else if (size == 2) outw(real, (u16)v);
      else linux_outl(real, v);
    }
  } else return 0;
  if (input) vm->save.rax = (vm->save.rax & ~(u64)mask) | (v & mask);
  return 1;
}
static void linux_interrupts(struct Vmcb *vm) {
  struct LinuxDevices *d = &linux_devices[linux_cur];
  u32 lo, hi;
  u64 now;
  int irq = -1;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  now = ((u64)hi << 32) | lo;
  if (!(vm->save.rflags & 0x200) || (vm->control.int_state & 1) ||
      (vm->control.event_inj & 0x80000000u)) return;
  if (!(d->mask[0] & 1) && !(d->isr[0] & 1) && now - d->last_tick >= linux_tick_cycles) {
    irq = 0;
    d->last_tick = now;
  } else if (ata_budget && ata_budget->full &&
             !(d->mask[0] & 0x10) && !(d->isr[0] & 0x10) &&
             (((uart_ier & 1) && uart_wc) || (uart_ier & 2))) irq = 4;
  else if (d->nic_port && !(d->mask[0] & 4) && !(d->mask[1] & 2) &&
           !(d->isr[1] & 2) && d->nic_mask) {
    outb(d->nic_port, d->nic_command & 0x3b);
    u8 pending = inb(d->nic_port + 7) & d->nic_mask & 0x7f;
    outb(d->nic_port, d->nic_command & ~4u);
    if (pending) irq = 9;
  }
  if (irq < 0) return;
  if (irq >= 8) { d->isr[1] |= 1u << (irq - 8); d->isr[0] |= 4; }
  else d->isr[0] |= 1u << irq;
  vm->control.event_inj = 0x80000000u | (d->base[irq >= 8] + (irq & 7));
}
