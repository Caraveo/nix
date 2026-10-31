/* Build guest. It can see only the 2 MiB the hypervisor maps. */
#include "../include/nix.h"
extern unsigned char counter_elf[];
extern unsigned char counter_elf_end[];
static void vmmcall(unsigned status) {
  __asm__ volatile(".byte 0x0f, 0x01, 0xd9" ::"a"(status) : "memory");
}
static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
/* Absolute symbol at guest physical 0. A null literal would be deleted. */
extern struct NixMail mailbox;
void main(void) {
  char recipe[64];
  unsigned i = 0;
  while (i < 63 && mailbox.recipe[i]) {
    recipe[i] = mailbox.recipe[i];
    i++;
  }
  recipe[i] = 0;
  if (same(recipe, "probe-fault")) {
    *(volatile unsigned *)0x400000 = 1;
    vmmcall(1);
  }
  mailbox.status = 1;
  mailbox.elf_len = 0;
  if (same(recipe, "count-words")) {
    unsigned n = (unsigned)(counter_elf_end - counter_elf);
    if (n <= sizeof mailbox.elf && counter_elf[0] == 0x7f) {
      for (i = 0; i < n; i++)
        mailbox.elf[i] = counter_elf[i];
      mailbox.elf_len = n;
      mailbox.status = 0;
    }
  }
  vmmcall(mailbox.status);
}
