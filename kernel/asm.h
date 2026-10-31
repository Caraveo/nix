/* Resident assembler. The image stays in NIX until the next build. */
struct AsmResult {
  unsigned length;
  unsigned entry;
  unsigned origin;
  int ok;
  char error[180];
};

int asm_build(const char *src, unsigned src_len, struct AsmResult *out);
const unsigned char *asm_image(void);
unsigned asm_capacity(void);
