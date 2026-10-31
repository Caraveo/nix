#include "../include/nix.h"
static long nix(long n, long a, long b) {
  long ret;
  __asm__ volatile("syscall"
                   : "=a"(ret)
                   : "a"(n), "D"(a), "S"(b)
                   : "rcx", "r11", "memory");
  return ret;
}
static void putu(char *d, unsigned n) {
  char tmp[16];
  int i = 0;
  do {
    tmp[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (i)
    *d++ = tmp[--i];
  *d = 0;
}
static unsigned count(const char *s) {
  unsigned n = 0, in = 0;
  for (; *s; s++) {
    if (*s == ' ' || *s == '\n' || *s == '\t')
      in = 0;
    else if (!in) {
      n++;
      in = 1;
    }
  }
  return n;
}
void main(void) {
  char buf[4096];
  char out[16];
  if (nix(SYS_MEM, (long)buf, 4096) != 0) {
    nix(SYS_READLINE, (long)buf, 200);
  }
  putu(out, count(buf));
  nix(SYS_WRITE, (long)out, 0);
  nix(SYS_EXIT, 0, 0);
}
