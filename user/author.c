#include "../include/nix.h"
static long nix(long n, long a, long b) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a), "S"(b)
                   : "rcx", "r11", "memory");
  return ret;
}
static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
void main(void) {
  char line[256];
  for (;;) {
    nix(SYS_READLINE, (long)line, 256);
    if (same(line, ":save"))
      nix(SYS_SAVE, 0, 0);
    else if (same(line, ":read"))
      nix(SYS_SHOW, 0, 0);
    else if (same(line, ":undo"))
      nix(SYS_UNDO, 0, 0);
    else if (same(line, ":home"))
      nix(SYS_HOME, 0, 0);
    else if (same(line, ":die"))
      nix(SYS_DIE, 0, 0);
    else if (same(line, ":boot"))
      nix(SYS_BOOT, 0, 0);
    else if (same(line, ":menu"))
      nix(SYS_MENU, 0, 0);
    else if (same(line, ":memories"))
      nix(SYS_MEMORIES, 0, 0);
    else if (same(line, ":terminal"))
      nix(SYS_TERMINAL, 0, 0);
    else if (line[0] == ':' && line[1] && line[1] != ':')
      nix(SYS_BAD, 0, 0);
    else
      nix(SYS_APPEND, (long)line, 0);
  }
}
