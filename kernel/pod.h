/* Ring-3 pods and the AMD SVM build guest. Included once from core.c. */
#include "../include/nix.h"
#include "asm.h"

extern u8 author_elf[], author_elf_end[], guest_bin[], guest_bin_end[];
extern u8 intent_src[], intent_src_end[], self_src[], self_src_end[];
extern u8 linux_image[], linux_image_end[], linux_initrd[], linux_initrd_end[];
extern u64 exc_table[32];
extern char fault_stack_top[], __bss_end[];
void syscall_entry(void);
void irq_entry(void);
void user_launch(u64 rip, u64 rsp);
void user_return(void) __attribute__((noreturn));
void svm_vmrun(u64 vmcb);
int user_fault;
long user_result;

static int arch_ready, svm_ready;
static int memory_grant = -1;
static int confirm_plan, await_recipe, plan_force, plan_has_grant, plan_grant;
static int plan_mode;
static char plan_recipe[64];
static char plan_clone[32];
static int kernel_built;
static u32 kernel_len, kernel_entry;
static u8 kernel_image[65536];
static char clone_name[8][32];
static int clone_used[8];

#define PTE_P 1ULL
#define PTE_W 2ULL
#define PTE_U 4ULL
#define PTE_NX (1ULL << 63)
/* Identity window at 32 MiB. The top 32 KiB is the pod stack. */
#define USER_BASE 0x2000000ULL
#define USER_STACK_BASE 0x21F8000ULL
#define USER_END 0x2200000ULL
#define POD_MAGIC 0x31445850u
#define POD_LBA 2048u
#define POD_SECS 65u

struct __attribute__((packed)) PodDisk {
  u32 magic, version, length, reserved;
  u64 hash;
  char recipe[32];
  u32 prev_version, prev_length;
  u64 prev_hash;
  char prev_recipe[32];
  u8 elf[16384];
  u8 prev[16384];
};
_Static_assert(sizeof(struct PodDisk) <= POD_SECS * 512, "pod record");
_Static_assert(sizeof(struct NixMail) < 0x7000, "guest mailbox");

static u8 pod_raw[POD_SECS * 512] __attribute__((aligned(512)));
static u8 pod_shadow[POD_SECS * 512] __attribute__((aligned(512)));
static struct PodDisk *podrec(void) { return (struct PodDisk *)pod_raw; }

static u8 svm_hsave[4096] __attribute__((aligned(4096)));
static u8 svm_iopm[12288] __attribute__((aligned(4096)));
static u8 svm_msrpm[8192] __attribute__((aligned(4096)));
static u8 svm_vmcb[4096] __attribute__((aligned(4096)));
static u8 npt_pml4[4096] __attribute__((aligned(4096)));
static u8 npt_pdpt[4096] __attribute__((aligned(4096)));
static u8 npt_pd[4096] __attribute__((aligned(4096)));

struct __attribute__((packed)) VmcbSeg {
  u16 selector, attrib;
  u32 limit;
  u64 base;
};
struct __attribute__((packed)) VmcbCtrl {
  u16 intercept_cr_read, intercept_cr_write, intercept_dr_read, intercept_dr_write;
  u32 intercept_exceptions;
  u64 intercept;
  u8 reserved_1[44];
  u64 iopm_base_pa, msrpm_base_pa, tsc_offset;
  u32 asid;
  u8 tlb_ctl;
  u8 reserved_2[3];
  u32 int_ctl, int_vector, int_state;
  u8 reserved_3[4];
  u64 exit_code, exit_info_1, exit_info_2;
  u32 exit_int_info, exit_int_info_err;
  u64 nested_ctl;
  u8 reserved_4[16];
  u32 event_inj, event_inj_err;
  u64 nested_cr3, lbr_ctl;
  u8 reserved_5[832];
};
struct __attribute__((packed)) VmcbSave {
  struct VmcbSeg es, cs, ss, ds, fs, gs, gdtr, ldtr, idtr, tr;
  u8 reserved_1[43];
  u8 cpl;
  u8 reserved_2[4];
  u64 efer;
  u8 reserved_3[112];
  u64 cr4, cr3, cr0, dr7, dr6, rflags, rip;
  u8 reserved_4[88];
  u64 rsp;
  u8 reserved_5[24];
  u64 rax, star, lstar, cstar, sfmask, kernel_gs_base;
  u64 sysenter_cs, sysenter_esp, sysenter_eip, cr2;
  u8 reserved_6[32];
  u64 g_pat, dbgctl, br_from, br_to, last_excp_from, last_excp_to;
};
struct __attribute__((packed)) Vmcb {
  struct VmcbCtrl control;
  struct VmcbSave save;
};
_Static_assert(sizeof(struct VmcbCtrl) == 1024, "vmcb control");
_Static_assert(__builtin_offsetof(struct Vmcb, save.efer) == 0x4d0, "efer");
_Static_assert(__builtin_offsetof(struct Vmcb, save.cr0) == 0x558, "cr0");
_Static_assert(__builtin_offsetof(struct Vmcb, save.rip) == 0x578, "rip");
_Static_assert(__builtin_offsetof(struct Vmcb, save.rsp) == 0x5d8, "rsp");
_Static_assert(__builtin_offsetof(struct Vmcb, save.rax) == 0x5f8, "rax");

static void hex64(u64 value) {
  char out[17];
  for (int i = 15; i >= 0; i--) {
    u8 digit = value & 15;
    out[i] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    value >>= 4;
  }
  out[16] = 0;
  say(out);
}
static u64 fnv64(const u8 *bytes, u32 count) {
  u64 hash = 14695981039346656037ULL;
  for (u32 i = 0; i < count; i++) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}
static void wrmsr(u32 msr, u64 value) {
  __asm__ volatile("wrmsr" ::"c"(msr), "a"((u32)value), "d"((u32)(value >> 32)));
}
static u64 rdmsr(u32 msr) {
  u32 low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((u64)high << 32) | low;
}
static void reload_cr3(void) {
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}
static u64 *user_pt(void) { return (u64 *)0x1c2000; }
static void map_user_stack(u64 *pt) {
  u32 first = (u32)((USER_STACK_BASE - USER_BASE) >> 12);
  for (u32 index = first; index < 512; index++)
    pt[index] = (USER_BASE + ((u64)index << 12)) | PTE_P | PTE_W | PTE_U | PTE_NX;
}

static void arch_init(void) {
  if ((u64)__bss_end > 0x2000000ULL) {
    line("Kernel BSS overlaps the user pod window.");
    return;
  }
  static u8 gdt[0x38] __attribute__((aligned(16)));
  static struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2, reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3, iomap;
  } tss;
  memset(gdt, 0, sizeof gdt);
  u64 descriptors[5] = {0, 0x00af9a000000ffffULL, 0x00cf92000000ffffULL,
                        0x00cff2000000ffffULL, 0x00affa000000ffffULL};
  for (int i = 0; i < 5; i++)
    memcpy(gdt + i * 8, &descriptors[i], 8);
  memset(&tss, 0, sizeof tss);
  tss.rsp0 = (u64)fault_stack_top;
  tss.iomap = sizeof tss;
  u64 base = (u64)&tss;
  u32 limit = sizeof tss - 1;
  u8 *desc = gdt + 0x28;
  desc[0] = (u8)limit;
  desc[1] = (u8)(limit >> 8);
  desc[2] = (u8)base;
  desc[3] = (u8)(base >> 8);
  desc[4] = (u8)(base >> 16);
  desc[5] = 0x89;
  desc[6] = (u8)((limit >> 16) & 0x0f);
  desc[7] = (u8)(base >> 24);
  desc[8] = (u8)(base >> 32);
  desc[9] = (u8)(base >> 40);
  desc[10] = (u8)(base >> 48);
  desc[11] = (u8)(base >> 56);
  struct __attribute__((packed)) {
    u16 limit;
    u64 base;
  } gdtr = {sizeof gdt - 1, (u64)gdt};
  __asm__ volatile("lgdt %0" ::"m"(gdtr));
  __asm__ volatile("mov $0x10, %%ax; mov %%ax, %%ss" ::: "ax");
  __asm__ volatile("ltr %0" ::"r"((u16)0x28));
  static struct __attribute__((packed)) {
    u16 off0, sel;
    u8 ist, type;
    u16 off1;
    u32 off2, zero;
  } idt[256];
  memset(idt, 0, sizeof idt);
  for (int i = 0; i < 256; i++) {
    u64 off = i < 32 ? exc_table[i] : (u64)irq_entry;
    idt[i].off0 = (u16)off;
    idt[i].sel = 0x08;
    idt[i].type = 0x8e;
    idt[i].off1 = (u16)(off >> 16);
    idt[i].off2 = (u32)(off >> 32);
  }
  struct __attribute__((packed)) {
    u16 limit;
    u64 base;
  } idtr = {sizeof idt - 1, (u64)idt};
  __asm__ volatile("lidt %0" ::"m"(idtr));
  outb(0x21, 0xff);
  outb(0xa1, 0xff);
  u64 *pml4 = (u64 *)0x180000;
  u64 *pdpt = (u64 *)0x181000;
  u64 *pd = (u64 *)0x182000;
  pml4[0] |= 4;
  pdpt[0] |= 4;
  pd[16] = 0x1c2000 | 7;
  memset(user_pt(), 0, 4096);
  reload_cr3();
  wrmsr(0xC0000080, rdmsr(0xC0000080) | (1ULL << 12));
  wrmsr(0xC0000081, (0x10ULL << 48) | (0x08ULL << 32));
  wrmsr(0xC0000082, (u64)syscall_entry);
  wrmsr(0xC0000084, 0x700);
  unsigned eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000001u));
  int svm = (ecx & 4) != 0;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x8000000au));
  svm_ready = svm && (edx & 1);
  arch_ready = 1;
}

static int user_span(u64 addr, u64 bytes) {
  return bytes && bytes <= 0x200000 && addr >= 0x2000000ULL &&
         addr <= 0x2200000ULL - bytes;
}
static int copy_user_str(u64 addr, char *dest, u32 cap) {
  if (!cap || addr < 0x2000000ULL || addr >= 0x2200000ULL)
    return 0;
  u32 i = 0;
  while (i + 1 < cap && addr + i < 0x2200000ULL) {
    char ch = ((char *)addr)[i];
    dest[i++] = ch;
    if (!ch)
      return 1;
  }
  return 0;
}
static int map_elf(const u8 *elf, u64 length, u64 *entry) {
  if (length < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[4] != 2)
    return 0;
  u16 machine, phentsize, phnum;
  u64 phoff, entryv;
  memcpy(&machine, elf + 18, 2);
  memcpy(&entryv, elf + 24, 8);
  memcpy(&phoff, elf + 32, 8);
  memcpy(&phentsize, elf + 54, 2);
  memcpy(&phnum, elf + 56, 2);
  if (machine != 62 || !phnum || phnum > 8 || phentsize < 56 || phoff > length ||
      phoff + (u64)phnum * phentsize > length)
    return 0;
  struct {
    u64 vaddr, offset, filesz, memsz;
  } loads[8];
  int count = 0;
  u64 *pt = user_pt();
  memset(pt, 0, 4096);
  for (u16 i = 0; i < phnum; i++) {
    const u8 *header = elf + phoff + (u64)i * phentsize;
    u32 type, flags;
    u64 offset, vaddr, filesz, memsz;
    memcpy(&type, header, 4);
    memcpy(&flags, header + 4, 4);
    memcpy(&offset, header + 8, 8);
    memcpy(&vaddr, header + 16, 8);
    memcpy(&filesz, header + 32, 8);
    memcpy(&memsz, header + 40, 8);
    if (type != 1 || !memsz)
      continue;
    if ((flags & 1) && (flags & 2))
      return 0;
    if (vaddr < USER_BASE || vaddr + memsz > USER_STACK_BASE || offset > length ||
        filesz > memsz || filesz > length - offset)
      return 0;
    u64 flagsv = (flags & 2) ? (PTE_P | PTE_W | PTE_U | PTE_NX)
                 : (flags & 1) ? (PTE_P | PTE_U)
                               : (PTE_P | PTE_U | PTE_NX);
    for (u64 page = vaddr & ~0xfffULL; page < vaddr + memsz; page += 0x1000) {
      u32 index = (u32)((page - USER_BASE) >> 12);
      if (index >= (u32)((USER_STACK_BASE - USER_BASE) >> 12))
        return 0;
      pt[index] = page | flagsv;
    }
    loads[count].vaddr = vaddr;
    loads[count].offset = offset;
    loads[count].filesz = filesz;
    loads[count].memsz = memsz;
    count++;
  }
  map_user_stack(pt);
  if (entryv < USER_BASE || entryv >= USER_STACK_BASE)
    return 0;
  u64 entry_flags = pt[(entryv - 0x2000000ULL) >> 12];
  if (!(entry_flags & PTE_P) || (entry_flags & PTE_NX))
    return 0;
  reload_cr3();
  for (int i = 0; i < count; i++) {
    memcpy((void *)loads[i].vaddr, elf + loads[i].offset, loads[i].filesz);
    if (loads[i].memsz > loads[i].filesz)
      memset((void *)(loads[i].vaddr + loads[i].filesz), 0,
             loads[i].memsz - loads[i].filesz);
  }
  *entry = entryv;
  return 1;
}
static void enter_author(int created) {
  author();
  if (created)
    line("Workspace created in RAM. Start writing, then :save.");
  if (!arch_ready) {
    line("Pod isolation is not available.");
    selected = -1;
    return;
  }
  u64 entry = 0;
  pod_kind = POD_AUTHOR;
  user_fault = 0;
  if (!map_elf(author_elf, (u64)(author_elf_end - author_elf), &entry)) {
    pod_kind = POD_NONE;
    line("Author pod image rejected.");
    selected = -1;
    return;
  }
  user_launch(entry, 0x2200000ULL);
  pod_kind = POD_NONE;
  if (user_fault) {
    line("Pod faulted. NIX is still running.");
    selected = -1;
  }
}

static void pod_readline(u64 addr, u64 capin) {
  u32 cap = capin > 256 ? 256 : (u32)capin;
  if (cap < 2 || !user_span(addr, cap)) {
    user_fault = 1;
    user_return();
  }
  char buffer[256];
  for (;;) {
    prompt();
    input(buffer, cap);
    if (!power_pending)
      break;
    char lower[256];
    copy(lower, buffer, sizeof lower);
    for (u32 i = 0; lower[i]; i++)
      if (lower[i] >= 'A' && lower[i] <= 'Z')
        lower[i] += 32;
    if (eq(lower, "y") || eq(lower, "yes")) {
      power_pending = 0;
      power_off();
      continue;
    }
    if (!*lower || eq(lower, "n") || eq(lower, "no")) {
      power_pending = 0;
      line("Shutdown cancelled.");
      continue;
    }
    line("Please answer y or n. NIX is still running.");
  }
  memset((void *)addr, 0, cap);
  memcpy((void *)addr, buffer, len(buffer) + 1);
}

unsigned long syscall_dispatch(unsigned long number, unsigned long arg1,
                               unsigned long arg2) {
  char text[256];
  switch (number) {
  case SYS_WRITE:
    if (!copy_user_str(arg1, text, sizeof text)) {
      user_fault = 1;
      user_return();
    }
    line(text);
    return 0;
  case SYS_READLINE:
    pod_readline(arg1, arg2);
    return 0;
  case SYS_EXIT:
    user_result = 0;
    user_return();
  case SYS_SAVE:
    author_save();
    return 0;
  case SYS_SHOW:
    author_show();
    return 0;
  case SYS_UNDO:
    author_undo();
    return 0;
  case SYS_APPEND:
    if (selected < 0 || !copy_user_str(arg1, text, sizeof text)) {
      user_fault = 1;
      user_return();
    }
    author_apply(text);
    return 0;
  case SYS_DIE:
    power_request();
    return 0;
  case SYS_BOOT:
    power_boot();
    return 0;
  case SYS_MENU:
    graphics_menu_toggle();
    return 0;
  case SYS_HOME:
    if (leave_home())
      user_return();
    return 0;
  case SYS_MEMORIES:
    if (leave_memories())
      user_return();
    return 0;
  case SYS_TERMINAL:
    if (leave_terminal())
      user_return();
    return 0;
  case SYS_MEM: {
    if (pod_kind != POD_COUNTER || memory_grant < 0 ||
        memory_grant >= (int)store.count || !user_span(arg1, arg2)) {
      line("Memory grant: denied.");
      return (unsigned long)-1;
    }
    u32 bytes = store.books[memory_grant].length;
    if (arg2 <= bytes) {
      line("Memory grant: denied.");
      return (unsigned long)-1;
    }
    memcpy((void *)arg1, store.books[memory_grant].text, bytes + 1);
    return 0;
  }
  case SYS_BAD:
    line("Unknown Author action. Use :save, :read, :undo, :home.");
    return 0;
  default:
    user_fault = 1;
    user_return();
  }
}

void kernel_fault(u64 error, u64 rip, u64 cr2) {
  say("Kernel fault err ");
  number((u32)error);
  say(" rip ");
  hex64(rip);
  say(" cr2 ");
  hex64(cr2);
  line("");
  for (;;)
    __asm__ volatile("hlt");
}

static int pod_store(void) {
  if (!storage_ok)
    return 0;
  for (u32 i = 0; i < POD_SECS; i++)
    if (!sector(POD_LBA + i, pod_raw + i * 512, 1))
      return 0;
  return flush();
}
static void pod_load(void) {
  memset(pod_raw, 0, sizeof pod_raw);
  if (!storage_ok)
    return;
  for (u32 i = 0; i < POD_SECS; i++) {
    if (!sector(POD_LBA + i, pod_raw + i * 512, 0)) {
      memset(pod_raw, 0, sizeof pod_raw);
      return;
    }
  }
  struct PodDisk *disk = podrec();
  if (disk->magic != POD_MAGIC || disk->length < 64 ||
      disk->length > sizeof disk->elf ||
      disk->elf[0] != 0x7f)
    memset(pod_raw, 0, sizeof pod_raw);
}
static int pod_installed(void) {
  struct PodDisk *disk = podrec();
  return disk->magic == POD_MAGIC && disk->length >= 64 &&
         disk->length <= sizeof disk->elf &&
         disk->elf[0] == 0x7f && eq(disk->recipe, "count-words");
}
static int svm_launch(u64 rip, u64 *fault_gpa);
static int svm_guest(const char *recipe, u64 *fault_gpa) {
  *fault_gpa = 0;
  if (!svm_ready) {
    line("SVM is not available on this CPU.");
    return 0;
  }
  if (((u64)svm_vmcb | (u64)svm_hsave | (u64)svm_iopm | (u64)svm_msrpm |
       (u64)npt_pml4) &
      0xfff) {
    line("SVM structures are not page aligned.");
    return 0;
  }
  u8 *ram = (u8 *)0x3000000;
  u64 guest_len = (u64)(guest_bin_end - guest_bin);
  if (0x8000 + guest_len > 0x1FC000) {
    line("Build guest overlaps its page tables.");
    return 0;
  }
  memset(ram, 0, 0x200000);
  memcpy(ram + 0x8000, guest_bin, guest_len);
  struct NixMail *mail = (struct NixMail *)ram;
  copy(mail->recipe, recipe, 64);
  return svm_launch(0x8000, fault_gpa);
}
static int svm_launch(u64 rip, u64 *fault_gpa) {
  u8 *ram = (u8 *)0x3000000;
  u64 *pml4 = (u64 *)(ram + 0x1FC000);
  u64 *pdpt = (u64 *)(ram + 0x1FD000);
  u64 *pd = (u64 *)(ram + 0x1FE000);
  /* Accessed on every level. Dirty only on leaves, so the walk
     does not have to store those bits through nested paging. */
  pml4[0] = 0x1FD000 | 0x27;
  pdpt[0] = 0x1FE000 | 0x27;
  pd[0] = 0xe7;
  pd[2] = 0x400000 | 0xe7;
  memset(npt_pml4, 0, 4096);
  memset(npt_pdpt, 0, 4096);
  memset(npt_pd, 0, 4096);
  ((u64 *)npt_pml4)[0] = (u64)npt_pdpt | 0x27;
  ((u64 *)npt_pdpt)[0] = (u64)npt_pd | 0x27;
  /* QEMU walks nested pages as user, so the leaf needs the user bit. */
  ((u64 *)npt_pd)[0] = 0x3000000 | 0xe7;
  memset(svm_iopm, 0xff, sizeof svm_iopm);
  memset(svm_msrpm, 0xff, sizeof svm_msrpm);
  memset(svm_vmcb, 0, sizeof svm_vmcb);
  memset(svm_hsave, 0, sizeof svm_hsave);
  struct Vmcb *vm = (struct Vmcb *)svm_vmcb;
  vm->control.intercept = (1ULL << 32) | (1ULL << 33) | (1ULL << 24) |
                          (1ULL << 27) | (1ULL << 28) | (1ULL << 31);
  vm->control.iopm_base_pa = (u64)svm_iopm;
  vm->control.msrpm_base_pa = (u64)svm_msrpm;
  vm->control.asid = 1;
  vm->control.tlb_ctl = 1;
  vm->control.nested_ctl = 1;
  vm->control.nested_cr3 = (u64)npt_pml4;
  struct VmcbSeg code = {0x08, 0x0a9b, 0xffffffffu, 0};
  struct VmcbSeg data = {0x10, 0x0c93, 0xffffffffu, 0};
  vm->save.cs = code;
  vm->save.ds = data;
  vm->save.es = data;
  vm->save.ss = data;
  vm->save.fs = data;
  vm->save.gs = data;
  /* QEMU rejects a guest EFER without SVME. The guest is still not the host. */
  vm->save.efer = 0x1500;
  vm->save.cr0 = 0x80000021;
  vm->save.cr4 = 0x20;
  vm->save.cr3 = 0x1FC000;
  vm->save.dr6 = 0xffff0ff0;
  vm->save.dr7 = 0x400;
  vm->save.rflags = 2;
  vm->save.rip = rip;
  vm->save.rsp = 0x7000;
  vm->save.g_pat = 0x0007040600070406ULL;
  wrmsr(0xC0000080, rdmsr(0xC0000080) | (1ULL << 12));
  wrmsr(0xC0010117, (u64)svm_hsave);
  svm_vmrun((u64)svm_vmcb);
  reload_cr3();
  if (vm->control.exit_code == 0x400) {
    *fault_gpa = vm->control.exit_info_2;
    return 1;
  }
  if (vm->control.exit_code == 0x81)
    return 1;
  say("SVM exit ");
  hex64(vm->control.exit_code);
  line(".");
  return 0;
}
static int install_counter(const struct NixMail *mail) {
  if (mail->status || mail->elf_len < 64 || mail->elf_len > sizeof mail->elf ||
      mail->elf[0] != 0x7f || mail->elf[1] != 'E') {
    line("Build guest returned no pod. Nothing was installed.");
    return 0;
  }
  memcpy(pod_shadow, pod_raw, sizeof pod_raw);
  struct PodDisk *disk = podrec();
  if (disk->magic == POD_MAGIC && disk->length) {
    disk->prev_version = disk->version;
    disk->prev_length = disk->length;
    disk->prev_hash = disk->hash;
    memcpy(disk->prev_recipe, disk->recipe, 32);
    memcpy(disk->prev, disk->elf, disk->length);
  }
  u32 next = disk->magic == POD_MAGIC ? disk->version + 1 : 1;
  disk->magic = POD_MAGIC;
  disk->version = next;
  disk->length = mail->elf_len;
  disk->hash = fnv64(mail->elf, mail->elf_len);
  copy(disk->recipe, "count-words", 32);
  memcpy(disk->elf, mail->elf, mail->elf_len);
  if (!pod_store()) {
    memcpy(pod_raw, pod_shadow, sizeof pod_raw);
    pod_store();
    line("Install failed. Previous pod restored.");
    return 0;
  }
  say("Installed count-words version ");
  number(disk->version);
  line(".");
  say("Manifest ");
  hex64(disk->hash);
  line(".");
  return 1;
}
static void hypervisor_line(void) {
  line("Hypervisor: AMD SVM with nested paging. Guest map is 2 MiB.");
}
/* NIX is not Linux. ask kernel requests Linux 7.3.1 as a guest.
   ask debian, ubuntu, or centos requests that GNU stack and a boot. */
static int clone_find(const char *name) {
  for (int i = 0; i < 8; i++)
    if (clone_used[i] && eq(clone_name[i], name))
      return i;
  return -1;
}
static int kernel_is_clone(const char *name) { return clone_find(name) >= 0; }
static void kernel_unclone(const char *name) {
  int slot = clone_find(name);
  if (slot >= 0)
    clone_used[slot] = 0;
}
static int linux_asked, stack_debian, stack_ubuntu, stack_centos;
static int stack_was_asked(const char *name) {
  if (eq(name, "debian"))
    return stack_debian;
  if (eq(name, "ubuntu"))
    return stack_ubuntu;
  if (eq(name, "centos"))
    return stack_centos;
  return 0;
}
static void stack_mark(const char *name) {
  if (eq(name, "debian"))
    stack_debian = 1;
  else if (eq(name, "ubuntu"))
    stack_ubuntu = 1;
  else if (eq(name, "centos"))
    stack_centos = 1;
}
static int linux_stack_name(const char *name) {
  return eq(name, "debian") || eq(name, "ubuntu") || eq(name, "centos");
}
#include "linuxvm.h"
static void linux_guest_request(void) {
  const char *name = plan_clone[0] ? plan_clone : "kernel";
  linux_asked = 1;
  linux_remember(name);
  line("NIX is not Linux. NIX stays the host.");
  line("Guest kernel is Linux 7.3.1 from kernel.org.");
  line("NIX_BUILD_KERNEL linux-7.3.1");
  say("Opening #");
  say(name);
  line(".");
  linux_guest_enter(name);
}
static void stack_boot_request(const char *name) {
  const char *userspace = "GNU userspace.";
  stack_mark(name);
  if (eq(name, "debian"))
    userspace = "Debian GNU userspace.";
  else if (eq(name, "ubuntu"))
    userspace = "Ubuntu GNU userspace.";
  else if (eq(name, "centos"))
    userspace = "CentOS GNU userspace.";
  line("NIX is not Linux. NIX stays the host.");
  say("Stack: Linux 7.3.1 and ");
  line(userspace);
  say("NIX_BOOT_GUEST ");
  line(name);
  linux_guest_enter(name);
}
static void asm_self_check(void) {
  struct AsmResult result;
  unsigned n = (unsigned)(self_src_end - self_src);
  const unsigned char *image;
  unsigned i;
  int found = 0;
  if (!asm_build((const char *)self_src, n, &result) || !result.ok ||
      result.entry != 0x8000) {
    line(result.error[0] ? result.error : "Assembler self-check failed.");
    return;
  }
  image = asm_image();
  for (i = 0; i + 2 < result.length; i++) {
    if (image[i] == 0x0f && image[i + 1] == 0x01 && image[i + 2] == 0xd9) {
      found = 1;
      break;
    }
  }
  line(found ? "Assembler self-check passed." : "Assembler self-check failed.");
}
static void kernel_run_clone(const char *name) {
  u8 *ram = (u8 *)0x3000000;
  u64 fault = 0;
  struct Vmcb *vm;
  char msg[96];
  u32 i = 0;
  if (!kernel_built || !kernel_len || kernel_len > sizeof kernel_image ||
      kernel_entry != 0x8000 || 0x8000u + kernel_len > 0x1FC000u) {
    line("Kernel image is not runnable. It was not started.");
    return;
  }
  if (!svm_ready) {
    line("SVM is not available on this CPU.");
    return;
  }
  if (((u64)svm_vmcb | (u64)svm_hsave | (u64)svm_iopm | (u64)svm_msrpm |
       (u64)npt_pml4) &
      0xfff) {
    line("SVM structures are not page aligned.");
    return;
  }
  memset(ram, 0, 0x200000);
  memcpy(ram + 0x8000, kernel_image, kernel_len);
  hypervisor_line();
  if (!svm_launch(kernel_entry, &fault)) {
    line("Clone stopped. NIX is still running.");
    return;
  }
  vm = (struct Vmcb *)svm_vmcb;
  if (vm->control.exit_code != 0x81 || fault || vm->save.rax != 0) {
    line("Clone stopped. NIX is still running.");
    return;
  }
  while (i + 1 < sizeof msg) {
    u8 c = ram[0x1000 + i];
    if (c < 32 || c > 126)
      break;
    msg[i++] = (char)c;
  }
  msg[i] = 0;
  if (!i) {
    line("Clone stopped. NIX is still running.");
    return;
  }
  line(msg);
  say("Guest ");
  say(name);
  line(" is the kernel built from source. NIX is still the host.");
}
static void enact_plan(void) {
  if (plan_mode == 1) {
    plan_mode = 0;
    linux_guest_request();
    return;
  }
  if (plan_mode == 2) {
    plan_mode = 0;
    stack_boot_request(plan_clone);
    return;
  }
  if (eq(plan_recipe, "probe-fault")) {
    u64 fault = 0;
    hypervisor_line();
    if (!svm_guest("probe-fault", &fault) || fault < 0x400000ULL ||
        fault >= 0x600000ULL) {
      line("Probe did not stop on a nested page fault.");
      return;
    }
    line("Nested page fault. Guest stopped. Memories unchanged.");
    return;
  }
  if (pod_installed() && !plan_force) {
    memory_grant = plan_has_grant ? plan_grant : -1;
    line("Reused installed pod count-words. Nothing was built.");
    if (plan_has_grant) {
      say("Memory grant: ");
      line(store.books[plan_grant].title);
      line("The build guest did not receive it.");
    }
    return;
  }
  u64 fault = 0;
  hypervisor_line();
  if (!svm_guest("count-words", &fault) || fault) {
    line("Build guest did not finish.");
    return;
  }
  if (!install_counter((struct NixMail *)0x3000000))
    return;
  memory_grant = plan_has_grant ? plan_grant : -1;
  if (plan_has_grant) {
    say("Memory grant: ");
    line(store.books[plan_grant].title);
    line("The build guest did not receive it.");
  }
}
static int find_book(const char *title) {
  char want[48], have[48];
  copy(want, title, 48);
  for (u32 i = 0; want[i]; i++)
    if (want[i] >= 'A' && want[i] <= 'Z')
      want[i] += 32;
  for (u32 i = 0; i < store.count; i++) {
    copy(have, store.books[i].title, 48);
    for (u32 j = 0; have[j]; j++)
      if (have[j] >= 'A' && have[j] <= 'Z')
        have[j] += 32;
    if (eq(want, have))
      return (int)i;
  }
  return -1;
}
static void show_plan(void) {
  face_confirm = 1;
  if (plan_mode == 1 || plan_mode == 2)
    line("NIX is not Linux. NIX stays the host.");
  line("Plan");
  if (plan_mode == 1) {
    say("  ask: open Linux 7.3.1 inside NIX as #");
    line(plan_clone[0] ? plan_clone : "kernel");
    line(linux_rootfs_ready() ? "  suggest: full Linux runtime for this intent. NIX stays the host"
                             : "  suggest: guest kernel only. NIX stays the host");
  } else if (plan_mode == 2) {
    say("  ask: install ");
    say(plan_clone);
    line(" stack on Linux 7.3.1");
    line("  suggest: dynamic RAM and disk inside NIX");
  } else {
    say("  ask: build vetted recipe ");
    line(plan_recipe);
    line("  suggest: catalog tool counter");
  }
  if (install_bytes >= (10ULL << 30))
    line("  storage: 10 GiB install disk is attached");
  else
    line("  storage: an install needs 10 GiB");
  line((plan_mode == 1 || plan_mode == 2) && linux_rootfs_ready()
           ? "  network: per-intent adapter when attached by the launcher"
           : "  network: denied");
  if (plan_has_grant) {
    say("  memories: ");
    say(store.books[plan_grant].title);
    line(" granted to the native pod only");
  } else
    line("  memories: none");
  line("  authority: dios. The model cannot confirm.");
  line("Confirm as dios? [y/n]");
  confirm_plan = 1;
}
static int ask_again_word(const char *s) {
  return eq(s, "again") || eq(s, "reinstall") || eq(s, "overwrite") ||
         eq(s, "replace");
}
static int kernel_name_used(const char *name) {
  return linux_find(name) >= 0 || linux_was_requested(name) || instance_find(name) >= 0;
}
static void kernel_suffix(char *out, const char *base, int n) {
  u32 i = 0;
  char rev[4];
  int r = 0;
  while (base[i] && i + 1 < 16) {
    out[i] = base[i];
    i++;
  }
  do {
    rev[r++] = (char)('0' + (n % 10));
    n /= 10;
  } while (n && r < 4);
  while (r && i + 1 < 16)
    out[i++] = rev[--r];
  out[i] = 0;
}
static int kernel_token(const char **p, char *out, int cap) {
  const char *s = *p;
  int n = 0;
  while (*s == ' ')
    s++;
  if (!*s)
    return 0;
  while (s[n] && s[n] != ' ') {
    char c = s[n];
    int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
             c == '-' || c == '.';
    if (!ok || n + 1 >= cap)
      return -1;
    out[n++] = c;
  }
  out[n] = 0;
  *p = s + n;
  return 1;
}
static int kernel_pick(const char *base, int again, char *out) {
  char buf[16];
  int n;
  if (!kernel_name_used(base)) {
    copy(out, base, 16);
    return 1;
  }
  if (!again)
    return 0;
  for (n = 0; n < INSTANCE_MAX; n++) {
    kernel_suffix(buf, base, n);
    if (!kernel_name_used(buf)) {
      copy(out, buf, 16);
      return 1;
    }
  }
  return -1;
}
static int kernel_plan(const char *rest) {
  char base[16];
  char picked[16];
  int again = 0;
  int named_instance = 0;
  int pick;
  copy(base, "kernel", 16);
  while (*rest == ' ')
    rest++;
  while (*rest) {
    char word[16];
    int got = kernel_token(&rest, word, 16);
    if (got < 0) {
      line("Kernel names are short words, such as linux.");
      return 1;
    }
    if (!got)
      break;
    if (eq(word, "-n")) {
      char nbuf[16];
      int named = kernel_token(&rest, nbuf, 16);
      if (named <= 0 || nbuf[0] == '-') {
        line("Use ask kernel -n linux.");
        return 1;
      }
      named_instance = 1;
      copy(base, nbuf, 16);
      continue;
    }
    if (ask_again_word(word)) {
      again = 1;
      continue;
    }
    return 0;
  }
  if (named_instance && !again && linux_focus(base)) return 1;
  if (named_instance && !again && instance_find(base) >= 0) {
    linux_guest_enter(base); return 1;
  }
  pick = kernel_pick(base, again, picked);
  if (pick == 0) {
    line("NIX is not Linux. Linux 7.3.1 was already requested. It was not "
         "overwritten.");
    return 1;
  }
  if (pick < 0) {
    line("Four kernels are already running. Stop one with nix.");
    return 1;
  }
  if (named_instance && instance_create(picked, INSTANCE_KERNEL, "") < 0) return 1;
  plan_mode = 1;
  plan_has_grant = 0;
  plan_grant = -1;
  copy(plan_clone, picked, sizeof plan_clone);
  copy(plan_recipe, "kernel", sizeof plan_recipe);
  show_plan();
  return 1;
}
static int resident_ask(const char *rest) {
  if (starts(rest, "open ")) {
    const char *name = rest + 5;
    if (*name == '#' || *name == '@') name++;
    if (linux_focus(name)) return 1;
    int id = instance_find(name);
    if (id < 0) { line("Saved instance not found. Use ask list."); return 1; }
    linux_guest_enter(instances.instance[id].name); return 1;
  }
  if (eq(rest, "list")) {
    if (!instance_load()) line("No dedicated instance store is attached.");
    for (int i = 0; i < INSTANCE_MAX; i++) if (instances.instance[i].used) {
      say("#"); say(instances.instance[i].name);
      line(linux_find(instances.instance[i].name) >= 0 ? " (running)" : " (saved)");
    }
    for (int i = 0; i < LINUX_SLOTS; i++)
      if (linux_slot[i].used && linux_slot[i].budget.store_id < 0) {
        say("#"); say(linux_slot[i].name); line(" (temporary, running)");
      }
    return 1;
  }
  if (starts(rest, "delete ")) {
    if (!starts(rest + 7, "--confirm ")) {
      line("Deletion removes the saved filesystem. Use ask delete --confirm NAME."); return 1;
    }
    const char *target = rest + 17;
    if (*target == '#' || *target == '@') target++;
    int id = instance_find(target);
    if (id < 0) { line("Saved instance not found. Use ask list."); return 1; }
    if (linux_find(target) >= 0) { line("Stop the instance with nix before deleting it."); return 1; }
    line(instance_delete(id) ? "Saved instance deleted." : "Could not delete the saved instance.");
    return 1;
  }
  char name[32];
  char word[16];
  u32 n = 0;
  int again = 0;
  while (*rest == ' ')
    rest++;
  if (!*rest)
    return 0;
  while (rest[n] && rest[n] != ' ') {
    char c = rest[n];
    int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
             c == '_' || c == '-';
    if (!ok || n + 1 >= sizeof name)
      return 0;
    name[n++] = c;
  }
  name[n] = 0;
  rest += n;
  while (*rest == ' ')
    rest++;
  if (eq(name, "kernel"))
    return kernel_plan(rest);
  if (*rest) {
    u32 m = 0;
    while (rest[m] && rest[m] != ' ' && m + 1 < sizeof word) {
      char c = rest[m];
      word[m++] = c;
    }
    word[m] = 0;
    if (rest[m] || !ask_again_word(word))
      return 0;
    again = 1;
  }
  if (eq(name, "kernel")) {
    if (linux_asked && !again) {
      line("NIX is not Linux. Linux 7.3.1 was already requested. It was not "
           "overwritten.");
      return 1;
    }
    plan_mode = 1;
    plan_clone[0] = 0;
    plan_has_grant = 0;
    plan_grant = -1;
    copy(plan_recipe, "kernel", sizeof plan_recipe);
    show_plan();
    return 1;
  }
  if (eq(name, "bsd")) {
    line("NIX is not Linux. BSD is not Linux 7.3.1.");
    line("A FreeBSD kernel is not built, so it was not started.");
    return 1;
  }
  if (linux_stack_name(name)) {
    if (stack_was_asked(name) && !again) {
      say("NIX is not Linux. ");
      say(name);
      line(" stack was already requested. It was not overwritten.");
      return 1;
    }
    plan_mode = 2;
    plan_has_grant = 0;
    plan_grant = -1;
    copy(plan_clone, name, sizeof plan_clone);
    copy(plan_recipe, name, sizeof plan_recipe);
    show_plan();
    return 1;
  }
  return 0;
}
static int arm_plan(const char *text) {
  plan_mode = 0;
  while (*text == ' ')
    text++;
  u32 i = 0;
  while (text[i] && text[i] != ' ' && i + 1 < sizeof plan_recipe) {
    char ch = text[i];
    plan_recipe[i++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
  }
  plan_recipe[i] = 0;
  const char *rest = text + i;
  while (*rest == ' ')
    rest++;
  plan_has_grant = 0;
  plan_grant = -1;
  if (*rest) {
    char word[8];
    u32 n = 0;
    while (rest[n] && rest[n] != ' ' && n + 1 < sizeof word) {
      char ch = rest[n];
      word[n++] = (char)(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    }
    word[n] = 0;
    if (!eq(word, "grant")) {
      line("Unknown plan clause. Use: build <recipe> grant <title>");
      return 1;
    }
    const char *title = rest + n;
    while (*title == ' ')
      title++;
    if (!*title) {
      line("Name the Memory to grant, or omit grant.");
      return 1;
    }
    int book = find_book(title);
    if (book < 0) {
      line("Memory not found. The build was not started.");
      return 1;
    }
    plan_has_grant = 1;
    plan_grant = book;
  }
  if (eq(plan_recipe, "kernel")) {
    if (plan_has_grant) {
      line("The kernel image cannot receive a Memory.");
      return 1;
    }
    if (linux_asked && !plan_force) {
      line("NIX is not Linux. Linux 7.3.1 was already requested. It was not "
           "overwritten.");
      return 1;
    }
    plan_mode = 1;
    plan_clone[0] = 0;
    show_plan();
    return 1;
  }
  if (!eq(plan_recipe, "count-words") && !eq(plan_recipe, "probe-fault")) {
    line("Unknown recipe. The catalog has count-words.");
    return 1;
  }
  if (eq(plan_recipe, "probe-fault") && plan_has_grant) {
    line("probe-fault cannot receive a Memory.");
    return 1;
  }
  if (eq(plan_recipe, "count-words") && pod_installed() && !plan_force &&
      !plan_has_grant) {
    line("Reused installed pod count-words. Nothing was built.");
    return 1;
  }
  show_plan();
  return 1;
}
static int plan_command(const char *lower) {
  return eq(lower, "build") || starts(lower, "build ") || eq(lower, "rebuild") ||
         starts(lower, "rebuild ") || eq(lower, "run") || starts(lower, "run ") ||
         starts(lower, "rollback") || starts(lower, "fetch") ||
         eq(lower, "pod fault");
}
static int plan_escape(const char *lower) {
  return eq(lower, "home") || eq(lower, "help") || eq(lower, "status") ||
         eq(lower, "menu") || eq(lower, "boot") || eq(lower, "die") ||
         eq(lower, "shutdown") || eq(lower, "clear") || eq(lower, "memories") ||
         eq(lower, "ghost") || eq(lower, "terminal") ||
         eq(lower, "open terminal") || eq(lower, "show my memories") ||
         starts(lower, "open ") || starts(lower, "write ") ||
         starts(lower, "show ") || starts(lower, "ask ") ||
         starts(lower, "i want ");
}
static void run_counter(void) {
  if (!pod_installed()) {
    line("Pod counter is not installed. Build count-words first.");
    return;
  }
  if (!arch_ready) {
    line("Pod isolation is not available.");
    return;
  }
  u64 entry = 0;
  pod_kind = POD_COUNTER;
  user_fault = 0;
  if (!map_elf(podrec()->elf, podrec()->length, &entry)) {
    pod_kind = POD_NONE;
    line("Installed pod image rejected.");
    return;
  }
  user_launch(entry, 0x2200000ULL);
  pod_kind = POD_NONE;
  if (user_fault)
    line("Pod faulted. NIX is still running.");
}
static void pod_fault_cmd(void) {
  if (!arch_ready) {
    line("Pod isolation is not available.");
    return;
  }
  u64 *pt = user_pt();
  memset(pt, 0, 4096);
  pt[0] = USER_BASE | PTE_P | PTE_U;
  map_user_stack(pt);
  reload_cr3();
  ((u8 *)0x2000000)[0] = 0x0f;
  ((u8 *)0x2000000)[1] = 0x0b;
  user_fault = 0;
  user_launch(0x2000000ULL, 0x2200000ULL);
  line(user_fault ? "Pod faulted. NIX is still running."
                  : "Pod fault did not fault.");
}
static void rollback_counter(void) {
  struct PodDisk *disk = podrec();
  if (disk->magic != POD_MAGIC || !disk->prev_length) {
    line("Nothing to roll back.");
    return;
  }
  memcpy(pod_shadow, pod_raw, sizeof pod_raw);
  memcpy(disk->elf, disk->prev, disk->prev_length);
  disk->length = disk->prev_length;
  disk->version = disk->prev_version;
  disk->hash = disk->prev_hash;
  memcpy(disk->recipe, disk->prev_recipe, 32);
  disk->prev_length = 0;
  if (!pod_store()) {
    memcpy(pod_raw, pod_shadow, sizeof pod_raw);
    line("Rollback failed. The installed pod was kept.");
    return;
  }
  line("Rolled back to the previous pod.");
}
static int shell_plan(const char *text, const char *lower) {
  if (confirm_plan) {
    if (eq(lower, "y") || eq(lower, "yes")) {
      confirm_plan = 0;
      face_confirm = 0;
      if (face_on)
        face_clear();
      enact_plan();
      return 1;
    }
    if (!*lower || eq(lower, "n") || eq(lower, "no")) {
      confirm_plan = 0;
      face_confirm = 0;
      plan_mode = 0;
      if (face_on)
        face_clear();
      line("Build cancelled.");
      return 1;
    }
    line("Please answer y or n. The plan is still waiting for dios.");
    return 1;
  }
  if (await_recipe) {
    int command = plan_command(lower);
    await_recipe = 0;
    if (!command && plan_escape(lower))
      return 0;
    if (!command)
      return arm_plan(text);
  }
  if (selected >= 0 || ghost_open)
    return 0;
  if (eq(lower, "pod fault")) {
    pod_fault_cmd();
    return 1;
  }
  if (eq(lower, "asm self")) {
    asm_self_check();
    return 1;
  }
  if (starts(lower, "fetch")) {
    line("Network denied. The catalog is local.");
    return 1;
  }
  if (eq(lower, "run counter")) {
    run_counter();
    return 1;
  }
  if (eq(lower, "run") || starts(lower, "run ")) {
    line("The catalog can run counter.");
    return 1;
  }
  if (eq(lower, "rollback counter") || eq(lower, "rollback")) {
    rollback_counter();
    return 1;
  }
  if (eq(lower, "build") || eq(lower, "rebuild")) {
    await_recipe = 1;
    plan_force = eq(lower, "rebuild");
    line("Which recipe should be built? The catalog has count-words.");
    return 1;
  }
  if (starts(lower, "rebuild ")) {
    plan_force = 1;
    return arm_plan(text + 8);
  }
  if (starts(lower, "build ")) {
    plan_force = 0;
    return arm_plan(text + 6);
  }
  return 0;
}
