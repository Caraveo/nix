/* Shared pod ABI. Numbers are stable for Core 0.3.
   syscall args are rax, rdi, rsi. The kernel returns in rax and
   preserves every other GPR except rcx and r11. */
#define SYS_WRITE 1
#define SYS_READLINE 2
#define SYS_EXIT 3
#define SYS_SAVE 4
#define SYS_SHOW 5
#define SYS_UNDO 6
#define SYS_APPEND 7
#define SYS_DIE 8
#define SYS_BOOT 9
#define SYS_MENU 10
#define SYS_HOME 11
#define SYS_MEM 12
#define SYS_BAD 13
#define SYS_MEMORIES 14
#define SYS_TERMINAL 15

/* Build-guest mailbox at guest physical 0. It ends before the guest stack. */
struct NixMail {
  char recipe[64];
  unsigned status;
  unsigned elf_len;
  unsigned char elf[16384];
};
