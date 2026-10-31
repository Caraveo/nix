/* Resident two-pass assembler for NIX.
   64-bit Intel syntax. Jumps, immediates, and displacements use long forms
   so a forward label cannot change an instruction's length.
   The machine image stays in asm_a until the next build. */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

#include "asm.h"

#define SYM_MAX 2048
#define SYM_NAME 48
#define ASM_CAP (1024u * 1024u)

struct Sym {
  char name[SYM_NAME];
  u64 value;
  int defined;
};

static struct Sym syms[SYM_MAX];
static int sym_count;
static u8 asm_a[ASM_CAP];
static u8 asm_b[ASM_CAP];
static char asm_err[180];
static int asm_failed;
static int asm_line;
static int asm_emit;
static u8 *asm_out;
static u32 asm_cap;
static u64 asm_loc;
static u64 asm_origin;
static int asm_have_org;
static u32 asm_filled;

static void asm_fail(const char *msg) {
  u32 i = 0, j = 0;
  char prefix[24];
  unsigned n = (unsigned)asm_line;
  if (asm_failed)
    return;
  asm_failed = 1;
  prefix[i++] = 'l';
  prefix[i++] = 'i';
  prefix[i++] = 'n';
  prefix[i++] = 'e';
  prefix[i++] = ' ';
  {
    char tmp[12];
    int k = 0;
    if (!n)
      tmp[k++] = '0';
    while (n) {
      tmp[k++] = (char)('0' + n % 10);
      n /= 10;
    }
    while (k)
      prefix[i++] = tmp[--k];
  }
  prefix[i++] = ':';
  prefix[i++] = ' ';
  prefix[i] = 0;
  for (j = 0; prefix[j] && j + 1 < sizeof asm_err; j++)
    asm_err[j] = prefix[j];
  for (i = 0; msg[i] && j + 1 < sizeof asm_err; i++, j++)
    asm_err[j] = msg[i];
  asm_err[j] = 0;
}

static int asm_eq_n(const char *s, const char *e, const char *lit);
static int asm_eq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb + 32);
    if (ca != cb)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

static int asm_ident_start(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.';
}
static int asm_ident_char(char c) {
  return asm_ident_start(c) || (c >= '0' && c <= '9') || c == '$' || c == '@';
}

static void asm_put(u8 v) {
  u64 index;
  if (asm_failed)
    return;
  if (!asm_have_org) {
    asm_origin = asm_loc;
    asm_have_org = 1;
  }
  if (asm_loc < asm_origin) {
    asm_fail("location fell below origin");
    return;
  }
  index = asm_loc - asm_origin;
  if (index >= asm_cap) {
    asm_fail("image is larger than 1 MiB");
    return;
  }
  if (asm_emit)
    asm_out[index] = v;
  asm_loc++;
  if (asm_loc - asm_origin > asm_filled)
    asm_filled = (u32)(asm_loc - asm_origin);
}

static struct Sym *asm_find(const char *name, u32 nlen) {
  int i;
  if (nlen + 1 > SYM_NAME)
    return 0;
  for (i = 0; i < sym_count; i++) {
    u32 k = 0;
    while (k < nlen && syms[i].name[k] == name[k])
      k++;
    if (k == nlen && syms[i].name[k] == 0)
      return &syms[i];
  }
  return 0;
}

static struct Sym *asm_define(const char *name, u32 nlen, u64 value, int check) {
  struct Sym *s = asm_find(name, nlen);
  u32 i;
  if (nlen + 1 > SYM_NAME) {
    asm_fail("symbol name is too long");
    return 0;
  }
  if (s) {
    if (check && s->defined && s->value != value)
      asm_fail("symbol changed between passes");
    s->defined = 1;
    s->value = value;
    return s;
  }
  if (sym_count >= SYM_MAX) {
    asm_fail("symbol table is full");
    return 0;
  }
  s = &syms[sym_count++];
  for (i = 0; i < nlen; i++)
    s->name[i] = name[i];
  s->name[nlen] = 0;
  s->defined = 1;
  s->value = value;
  return s;
}

static const char *xp;
static const char *xp_end;
static int xp_undef;

static void xp_skip(void) {
  while (xp < xp_end && (*xp == ' ' || *xp == '\t'))
    xp++;
}

static int xp_number(i64 *out) {
  int base = 10;
  i64 v = 0;
  int any = 0;
  if (xp + 1 < xp_end && xp[0] == '0' && (xp[1] == 'x' || xp[1] == 'X')) {
    base = 16;
    xp += 2;
  } else if (xp + 1 < xp_end && xp[0] == '0' && (xp[1] == 'b' || xp[1] == 'B')) {
    base = 2;
    xp += 2;
  }
  while (xp < xp_end) {
    int d = -1;
    char c = *xp;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    if (d < 0 || d >= base)
      break;
    v = v * base + d;
    xp++;
    any = 1;
  }
  if (!any)
    return 0;
  *out = v;
  return 1;
}

static int xp_primary(i64 *out);

static int xp_unary(i64 *out) {
  xp_skip();
  if (xp < xp_end && (*xp == '+' || *xp == '-')) {
    char op = *xp++;
    i64 v;
    if (!xp_unary(&v))
      return 0;
    *out = op == '-' ? -v : v;
    return 1;
  }
  return xp_primary(out);
}

static int xp_term(i64 *out) {
  i64 v;
  if (!xp_unary(&v))
    return 0;
  for (;;) {
    char op;
    i64 r;
    xp_skip();
    if (xp >= xp_end || (*xp != '*' && *xp != '/'))
      break;
    op = *xp++;
    if (!xp_unary(&r))
      return 0;
    if (op == '/' && r == 0) {
      asm_fail("division by zero");
      return 0;
    }
    v = op == '*' ? v * r : v / r;
  }
  *out = v;
  return 1;
}

static int xp_expr(i64 *out) {
  i64 v;
  if (!xp_term(&v))
    return 0;
  for (;;) {
    char op;
    i64 r;
    xp_skip();
    if (xp >= xp_end || (*xp != '+' && *xp != '-'))
      break;
    op = *xp++;
    if (!xp_term(&r))
      return 0;
    v = op == '+' ? v + r : v - r;
  }
  *out = v;
  return 1;
}

static int xp_primary(i64 *out) {
  xp_skip();
  if (xp >= xp_end) {
    asm_fail("missing expression");
    return 0;
  }
  if (*xp == '(') {
    xp++;
    if (!xp_expr(out))
      return 0;
    xp_skip();
    if (xp >= xp_end || *xp != ')') {
      asm_fail("missing closing parenthesis");
      return 0;
    }
    xp++;
    return 1;
  }
  if (*xp == '$' && (xp + 1 >= xp_end || !asm_ident_char(xp[1]))) {
    xp++;
    *out = (i64)asm_loc;
    return 1;
  }
  if (*xp == '\'') {
    xp++;
    if (xp >= xp_end) {
      asm_fail("bad character");
      return 0;
    }
    if (*xp == '\\' && xp + 1 < xp_end) {
      xp++;
      if (*xp == 'n')
        *out = 10;
      else if (*xp == '0')
        *out = 0;
      else
        *out = (u8)*xp;
      xp++;
    } else {
      *out = (u8)*xp++;
    }
    if (xp >= xp_end || *xp != '\'') {
      asm_fail("bad character");
      return 0;
    }
    xp++;
    return 1;
  }
  if (asm_ident_start(*xp)) {
    const char *start = xp;
    struct Sym *s;
    while (xp < xp_end && asm_ident_char(*xp))
      xp++;
    s = asm_find(start, (u32)(xp - start));
    if (!s || !s->defined) {
      xp_undef = 1;
      *out = 0;
      return 1;
    }
    *out = (i64)s->value;
    return 1;
  }
  if (!xp_number(out)) {
    asm_fail("bad expression");
    return 0;
  }
  return 1;
}

static int asm_eval(const char *s, const char *end, i64 *out) {
  xp = s;
  xp_end = end;
  xp_undef = 0;
  if (!xp_expr(out))
    return 0;
  xp_skip();
  if (xp != xp_end) {
    asm_fail("trailing junk in expression");
    return 0;
  }
  if (xp_undef) {
    if (asm_emit) {
      asm_fail("undefined symbol");
      return 0;
    }
    *out = 0;
  }
  return 1;
}

struct Reg {
  int ok, code, size, high, need_rex;
};

static struct Reg asm_reg(const char *s, u32 n) {
  static const char *names[] = {
      "al",  "cl",  "dl",  "bl",  "ah",  "ch",  "dh",  "bh",  "spl", "bpl",
      "sil", "dil", "ax",  "cx",  "dx",  "bx",  "sp",  "bp",  "si",  "di",
      "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "rax", "rcx",
      "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
  static const u8 code[] = {0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 0, 1, 2,
                            3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7, 0, 1,
                            2, 3, 4, 5, 6, 7};
  static const u8 size[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
                            2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8,
                            8, 8, 8, 8, 8, 8};
  struct Reg r;
  char buf[8];
  u32 i;
  int k;
  r.ok = 0;
  r.code = 0;
  r.size = 0;
  r.high = 0;
  r.need_rex = 0;
  if (n >= sizeof buf)
    return r;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z')
      c = (char)(c + 32);
    buf[i] = c;
  }
  buf[n] = 0;
  if (n >= 2 && buf[0] == 'r' && buf[1] >= '0' && buf[1] <= '9') {
    int num = buf[1] - '0';
    u32 rest = 2;
    if (n > 2 && buf[2] >= '0' && buf[2] <= '9') {
      num = num * 10 + buf[2] - '0';
      rest = 3;
    }
    if (num < 8 || num > 15)
      return r;
    r.ok = 1;
    r.code = num;
    r.need_rex = 1;
    if (rest == n) {
      r.size = 8;
      return r;
    }
    if (rest + 1 == n && buf[rest] == 'd') {
      r.size = 4;
      return r;
    }
    if (rest + 1 == n && buf[rest] == 'w') {
      r.size = 2;
      return r;
    }
    if (rest + 1 == n && buf[rest] == 'b') {
      r.size = 1;
      return r;
    }
    r.ok = 0;
    return r;
  }
  for (k = 0; k < 36; k++) {
    if (asm_eq(buf, names[k])) {
      r.ok = 1;
      r.code = code[k];
      r.size = size[k];
      if (k >= 4 && k <= 7)
        r.high = 1;
      if (k >= 8 && k <= 11)
        r.need_rex = 1;
      return r;
    }
  }
  return r;
}

struct Mem {
  int kind; /* 0 base, 1 rip, 2 absolute, 3 register */
  int base, index, scale;
  int has_disp, need_rex, high;
  i64 disp;
  int size;
};

struct Op {
  int kind; /* 0 none, 1 reg, 2 imm, 3 mem */
  struct Reg reg;
  struct Mem mem;
  i64 imm;
  int size;
};

static const char *asm_skip(const char *s, const char *end) {
  while (s < end && (*s == ' ' || *s == '\t'))
    s++;
  return s;
}

static int asm_take_size(const char **ps, const char *end, int *size) {
  const char *s = asm_skip(*ps, end);
  const char *t = s;
  while (t < end && asm_ident_char(*t))
    t++;
  if (asm_eq_n(s, t, "byte"))
    *size = 1;
  else if (asm_eq_n(s, t, "word"))
    *size = 2;
  else if (asm_eq_n(s, t, "dword"))
    *size = 4;
  else if (asm_eq_n(s, t, "qword"))
    *size = 8;
  else
    return 0;
  s = asm_skip(t, end);
  t = s;
  while (t < end && asm_ident_char(*t))
    t++;
  if ((u32)(t - s) == 3 && (s[0] == 'p' || s[0] == 'P') &&
      (s[1] == 't' || s[1] == 'T') && (s[2] == 'r' || s[2] == 'R'))
    s = asm_skip(t, end);
  *ps = s;
  return 1;
}

static int asm_eq_n(const char *s, const char *e, const char *lit) {
  u32 n = (u32)(e - s);
  u32 i = 0;
  while (lit[i])
    i++;
  if (i != n)
    return 0;
  for (i = 0; i < n; i++) {
    char c = s[i];
    if (c >= 'A' && c <= 'Z')
      c = (char)(c + 32);
    if (c != lit[i])
      return 0;
  }
  return 1;
}

static int asm_parse_mem(const char *s, const char *end, struct Mem *m) {
  int saw_sym = 0;
  i64 disp = 0;
  int sign = 1;
  int any = 0;
  m->kind = 0;
  m->base = -1;
  m->index = -1;
  m->scale = 1;
  m->has_disp = 0;
  m->need_rex = 0;
  m->high = 0;
  m->disp = 0;
  s = asm_skip(s, end);
  if (s < end && asm_eq_n(s, s + 3 < end ? s + 3 : end, "rel")) {
    const char *t = s + 3;
    i64 v;
    if (t < end && !asm_ident_char(*t)) {
      s = asm_skip(t, end);
      if (!asm_eval(s, end, &v))
        return 0;
      m->kind = 1;
      m->disp = v;
      return 1;
    }
  }
  while (s < end) {
    const char *t;
    s = asm_skip(s, end);
    if (s >= end)
      break;
    if (*s == '+') {
      sign = 1;
      s++;
      any = 1;
      continue;
    }
    if (*s == '-') {
      sign = -1;
      s++;
      any = 1;
      continue;
    }
    t = s;
    if (asm_ident_start(*s)) {
      struct Reg r;
      while (t < end && asm_ident_char(*t))
        t++;
      r = asm_reg(s, (u32)(t - s));
      if (r.ok) {
        const char *u = asm_skip(t, end);
        int scale = 1;
        if (sign < 0) {
          asm_fail("a register cannot be subtracted");
          return 0;
        }
        if (u < end && *u == '*') {
          i64 sc = 0;
          u = asm_skip(u + 1, end);
          xp = u;
          xp_end = end;
          xp_undef = 0;
          if (!xp_number(&sc) || (sc != 1 && sc != 2 && sc != 4 && sc != 8)) {
            asm_fail("scale must be 1, 2, 4, or 8");
            return 0;
          }
          scale = (int)sc;
          u = xp;
          if (m->index >= 0) {
            asm_fail("too many index registers");
            return 0;
          }
          if (r.size != 8) {
            asm_fail("address register must be 64-bit");
            return 0;
          }
          if (r.code == 4) {
            asm_fail("rsp cannot be an index");
            return 0;
          }
          m->index = r.code;
          m->scale = scale;
          s = u;
          sign = 1;
          any = 1;
          continue;
        }
        if (m->base < 0 && scale == 1) {
          m->base = r.code;
          if (r.size != 8) {
            asm_fail("address register must be 64-bit");
            return 0;
          }
        } else {
          if (m->index >= 0) {
            asm_fail("too many index registers");
            return 0;
          }
          if (r.code == 4) {
            asm_fail("rsp cannot be an index");
            return 0;
          }
          if (r.size != 8) {
            asm_fail("address register must be 64-bit");
            return 0;
          }
          m->index = r.code;
          m->scale = 1;
        }
        s = t;
        sign = 1;
        any = 1;
        continue;
      }
      saw_sym = 1;
    }
    {
      const char *stop = s;
      int depth = 0;
      i64 v;
      while (stop < end) {
        if (*stop == '(')
          depth++;
        else if (*stop == ')')
          depth--;
        else if (!depth && (*stop == '+' || *stop == '-'))
          break;
        stop++;
      }
      if (!asm_eval(s, stop, &v))
        return 0;
      disp += sign * v;
      m->has_disp = 1;
      s = stop;
      sign = 1;
      any = 1;
    }
  }
  if (!any && m->base < 0 && m->index < 0) {
    asm_fail("empty memory operand");
    return 0;
  }
  m->disp = disp;
  if (m->base < 0 && m->index < 0)
    m->kind = saw_sym ? 1 : 2;
  else
    m->kind = 0;
  (void)any;
  return 1;
}

struct Enc {
  u8 b[20];
  int n;
};

static void enc_byte(struct Enc *e, u8 v) {
  if (e->n < (int)sizeof e->b)
    e->b[e->n++] = v;
}

static void enc_u32(struct Enc *e, u32 v) {
  enc_byte(e, (u8)v);
  enc_byte(e, (u8)(v >> 8));
  enc_byte(e, (u8)(v >> 16));
  enc_byte(e, (u8)(v >> 24));
}

static void enc_flush(struct Enc *e) {
  int i;
  for (i = 0; i < e->n; i++)
    asm_put(e->b[i]);
}

static int enc_modrm(struct Enc *e, int rex, const u8 *opc, int opc_n, int reg,
                     struct Mem *m) {
  int rr = reg >= 8;
  int rf = reg & 7;
  int mod, rm, sib = -1, disp_bytes = 0;
  i64 disp = 0;
  int rip_at = -1;
  int i;
  if (m->kind == 3) {
    if (m->high && (rex || m->base >= 8)) {
      asm_fail("high byte register cannot use rex");
      return 0;
    }
    if (m->need_rex && m->base < 8 && !rex)
      rex = 0x40;
    mod = 3;
    rm = m->base & 7;
    if (m->base >= 8)
      rex |= 1;
  } else if (m->kind == 1) {
    mod = 0;
    rm = 5;
    disp_bytes = 4;
    disp = 0;
  } else if (m->kind == 2) {
    if (asm_emit && (m->disp < (i64)(-2147483647 - 1) || m->disp > 2147483647)) {
      asm_fail("absolute address does not fit");
      return 0;
    }
    mod = 0;
    rm = 4;
    sib = 0x25;
    disp_bytes = 4;
    disp = m->disp;
  } else if (m->index >= 0 || (m->base >= 0 && (m->base & 7) == 4)) {
    int scale = m->scale == 2 ? 1 : m->scale == 4 ? 2 : m->scale == 8 ? 3 : 0;
    int index = m->index < 0 ? 4 : (m->index & 7);
    int base = m->base < 0 ? 5 : (m->base & 7);
    rm = 4;
    sib = (scale << 6) | (index << 3) | base;
    if (m->base < 0) {
      mod = 0;
      disp_bytes = 4;
      disp = m->disp;
    } else if (!m->has_disp && (m->base & 7) != 5) {
      mod = 0;
    } else if (!m->has_disp) {
      mod = 1;
      disp_bytes = 1;
      disp = 0;
    } else {
      mod = 2;
      disp_bytes = 4;
      disp = m->disp;
    }
    if (m->index >= 8)
      rex |= 2;
    if (m->base >= 8)
      rex |= 1;
  } else {
    rm = m->base & 7;
    if (m->base >= 8)
      rex |= 1;
    if (!m->has_disp && (m->base & 7) != 5) {
      mod = 0;
    } else if (!m->has_disp) {
      mod = 1;
      disp_bytes = 1;
    } else {
      mod = 2;
      disp_bytes = 4;
      disp = m->disp;
    }
  }
  if (rr)
    rex |= 4;
  if (rex)
    enc_byte(e, (u8)(0x40 | rex));
  for (i = 0; i < opc_n; i++)
    enc_byte(e, opc[i]);
  enc_byte(e, (u8)((mod << 6) | (rf << 3) | rm));
  if (sib >= 0)
    enc_byte(e, (u8)sib);
  if (m->kind == 1)
    rip_at = e->n;
  if (disp_bytes == 1)
    enc_byte(e, (u8)disp);
  else if (disp_bytes == 4)
    enc_u32(e, (u32)disp);
  if (rip_at >= 0) {
    i64 rel = m->disp - (i64)(asm_loc + (u64)e->n);
    if (asm_emit && (rel < (i64)(-2147483647 - 1) || rel > 2147483647)) {
      asm_fail("rip displacement does not fit");
      return 0;
    }
    e->b[rip_at] = (u8)rel;
    e->b[rip_at + 1] = (u8)((u64)rel >> 8);
    e->b[rip_at + 2] = (u8)((u64)rel >> 16);
    e->b[rip_at + 3] = (u8)((u64)rel >> 24);
  }
  return 1;
}

static int asm_parse_op(const char *s, const char *end, struct Op *op) {
  int size = 0;
  struct Reg r;
  const char *t;
  op->kind = 0;
  op->size = 0;
  op->imm = 0;
  op->mem.kind = 0;
  op->mem.base = -1;
  op->mem.index = -1;
  op->mem.scale = 1;
  op->mem.has_disp = 0;
  op->mem.need_rex = 0;
  op->mem.high = 0;
  op->mem.disp = 0;
  op->mem.size = 0;
  op->reg.ok = 0;
  s = asm_skip(s, end);
  if (s >= end) {
    asm_fail("missing operand");
    return 0;
  }
  if (asm_take_size(&s, end, &size))
    op->size = size;
  s = asm_skip(s, end);
  t = s;
  while (t < end && asm_ident_char(*t))
    t++;
  if ((u32)(t - s) == 6 && asm_eq_n(s, t, "offset")) {
    s = asm_skip(t, end);
    op->kind = 2;
    return asm_eval(s, end, &op->imm);
  }
  if (*s == '[') {
    const char *q = end - 1;
    while (q > s && (*q == ' ' || *q == '\t'))
      q--;
    if (*q != ']') {
      asm_fail("missing closing bracket");
      return 0;
    }
    if (!asm_parse_mem(s + 1, q, &op->mem))
      return 0;
    op->mem.size = op->size;
    op->kind = 3;
    return 1;
  }
  if (asm_ident_start(*s)) {
    t = s;
    while (t < end && asm_ident_char(*t))
      t++;
    r = asm_reg(s, (u32)(t - s));
    if (r.ok && asm_skip(t, end) == end) {
      if (op->size && op->size != r.size) {
        asm_fail("operand size does not match the register");
        return 0;
      }
      op->kind = 1;
      op->reg = r;
      op->size = r.size;
      return 1;
    }
  }
  op->kind = 2;
  if (!asm_eval(s, end, &op->imm))
    return 0;
  return 1;
}

static int split_ops(char *s, char **ops, int max) {
  int n = 0, depth = 0, quote = 0;
  char *p;
  ops[n++] = s;
  for (p = s; *p; p++) {
    if (quote) {
      if (*p == '\\' && p[1]) {
        p++;
        continue;
      }
      if (*p == quote)
        quote = 0;
      continue;
    }
    if (*p == '"' || *p == '\'') {
      quote = *p;
      continue;
    }
    if (*p == '[')
      depth++;
    else if (*p == ']')
      depth--;
    else if (*p == ',' && !depth) {
      *p = 0;
      if (n >= max) {
        asm_fail("too many operands");
        return -1;
      }
      ops[n++] = p + 1;
    }
  }
  return n;
}

static int fits32(i64 v) {
  return v >= (i64)(-2147483647 - 1) && v <= 2147483647;
}

static struct Mem mem_reg(struct Reg *r);
static int emit_imm_rm(int rex_w, u8 opc, int ext, struct Op *rm, i64 imm,
                       int imm_bytes) {
  struct Enc e;
  struct Mem m;
  u8 op[1];
  int rex = rex_w ? 8 : 0;
  e.n = 0;
  op[0] = opc;
  if (rm->kind == 1)
    m = mem_reg(&rm->reg);
  else
    m = rm->mem;
  if (!enc_modrm(&e, rex, op, 1, ext, &m))
    return 0;
  if (imm_bytes == 1)
    enc_byte(&e, (u8)imm);
  else if (imm_bytes == 4)
    enc_u32(&e, (u32)imm);
  enc_flush(&e);
  return !asm_failed;
}

static int force_low(struct Reg *r) { return r->need_rex && r->code < 8; }

static int emit_reg_rm(int rex_w, const u8 *opc, int opc_n, struct Reg *reg,
                       struct Mem *mem) {
  struct Enc e;
  int rex = rex_w ? 8 : 0;
  e.n = 0;
  if (force_low(reg))
    rex |= 0x40;
  if (reg->high && rex) {
    asm_fail("high byte register cannot use rex");
    return 0;
  }
  if (!enc_modrm(&e, rex, opc, opc_n, reg->code, mem))
    return 0;
  enc_flush(&e);
  return !asm_failed;
}

static struct Mem mem_reg(struct Reg *r) {
  struct Mem m;
  m.kind = 3;
  m.base = r->code;
  m.index = -1;
  m.scale = 1;
  m.has_disp = 0;
  m.need_rex = r->need_rex;
  m.high = r->high;
  m.disp = 0;
  m.size = r->size;
  return m;
}

static int emit_alu(int op, struct Op *dst, struct Op *src) {
  int size = dst->size ? dst->size : src->size;
  if (dst->kind == 1 && src->kind == 1) {
    u8 opc[1];
    struct Mem m;
    if (dst->reg.size != src->reg.size) {
      asm_fail("operand sizes differ");
      return 0;
    }
    opc[0] = (u8)(op * 8 + (size == 1 ? 2 : 3));
    m = mem_reg(&src->reg);
    return emit_reg_rm(size == 8, opc, 1, &dst->reg, &m);
  }
  if (dst->kind == 1 && src->kind == 3) {
    u8 opc[1];
    if (!size)
      size = dst->reg.size;
    opc[0] = (u8)(op * 8 + (size == 1 ? 2 : 3));
    return emit_reg_rm(size == 8, opc, 1, &dst->reg, &src->mem);
  }
  if (dst->kind == 3 && src->kind == 1) {
    u8 opc[1];
    size = src->reg.size;
    opc[0] = (u8)(op * 8 + (size == 1 ? 0 : 1));
    return emit_reg_rm(size == 8, opc, 1, &src->reg, &dst->mem);
  }
  if ((dst->kind == 1 || dst->kind == 3) && src->kind == 2) {
    if (dst->kind == 1)
      size = dst->reg.size;
    if (!size) {
      asm_fail("operand size required");
      return 0;
    }
    if (size == 1)
      return emit_imm_rm(0, 0x80, op, dst, src->imm, 1);
    if (!fits32(src->imm)) {
      asm_fail("immediate does not fit");
      return 0;
    }
    return emit_imm_rm(size == 8, 0x81, op, dst, src->imm, 4);
  }
  asm_fail("unsupported operands");
  return 0;
}

static int emit_mov(struct Op *dst, struct Op *src) {
  if (dst->kind == 1 && src->kind == 2) {
    struct Enc e;
    int rex = 0;
    e.n = 0;
    if (dst->reg.high && dst->reg.size == 1) {
      enc_byte(&e, (u8)(0xB0 + dst->reg.code));
      enc_byte(&e, (u8)src->imm);
      enc_flush(&e);
      return 1;
    }
    if (dst->reg.size == 8) {
      rex = 8;
      if (dst->reg.code >= 8)
        rex |= 1;
      enc_byte(&e, (u8)(0x40 | rex));
      enc_byte(&e, (u8)(0xB8 + (dst->reg.code & 7)));
      enc_u32(&e, (u32)src->imm);
      enc_u32(&e, (u32)((u64)src->imm >> 32));
      enc_flush(&e);
      return 1;
    }
    if (force_low(&dst->reg) || dst->reg.code >= 8) {
      rex = dst->reg.code >= 8 ? 1 : 0x40;
      if (dst->reg.size == 2)
        enc_byte(&e, 0x66);
      enc_byte(&e, (u8)(0x40 | (rex == 0x40 ? 0 : rex)));
    } else if (dst->reg.size == 2)
      enc_byte(&e, 0x66);
    enc_byte(&e, (u8)((dst->reg.size == 1 ? 0xB0 : 0xB8) + (dst->reg.code & 7)));
    if (dst->reg.size == 1)
      enc_byte(&e, (u8)src->imm);
    else if (dst->reg.size == 2) {
      enc_byte(&e, (u8)src->imm);
      enc_byte(&e, (u8)(src->imm >> 8));
    } else
      enc_u32(&e, (u32)src->imm);
    enc_flush(&e);
    return !asm_failed;
  }
  if (dst->kind == 1 && (src->kind == 1 || src->kind == 3)) {
    u8 opc[1];
    struct Mem m;
    int size = dst->reg.size;
    opc[0] = size == 1 ? 0x8A : 0x8B;
    m = src->kind == 1 ? mem_reg(&src->reg) : src->mem;
    if (src->kind == 1 && src->reg.size != size) {
      asm_fail("operand sizes differ");
      return 0;
    }
    return emit_reg_rm(size == 8, opc, 1, &dst->reg, &m);
  }
  if (dst->kind == 3 && src->kind == 1) {
    u8 opc[1];
    opc[0] = src->reg.size == 1 ? 0x88 : 0x89;
    return emit_reg_rm(src->reg.size == 8, opc, 1, &src->reg, &dst->mem);
  }
  if (dst->kind == 3 && src->kind == 2) {
    if (!dst->size) {
      asm_fail("operand size required");
      return 0;
    }
    if (dst->size == 1)
      return emit_imm_rm(0, 0xC6, 0, dst, src->imm, 1);
    if (!fits32(src->imm)) {
      asm_fail("immediate does not fit");
      return 0;
    }
    return emit_imm_rm(dst->size == 8, 0xC7, 0, dst, src->imm, 4);
  }
  asm_fail("unsupported mov");
  return 0;
}

static int emit_simple(const u8 *b, int n) {
  int i;
  for (i = 0; i < n; i++)
    asm_put(b[i]);
  return !asm_failed;
}

static int emit_rel(u8 opc, int opc2, struct Op *op, int total) {
  i64 rel;
  if (op->kind != 2) {
    asm_fail("expected a label");
    return 0;
  }
  rel = op->imm - (i64)(asm_loc + (u64)total);
  if (asm_emit && !fits32(rel)) {
    asm_fail("branch target does not fit");
    return 0;
  }
  asm_put(opc);
  if (opc2 >= 0)
    asm_put((u8)opc2);
  asm_put((u8)rel);
  asm_put((u8)((u64)rel >> 8));
  asm_put((u8)((u64)rel >> 16));
  asm_put((u8)((u64)rel >> 24));
  return !asm_failed;
}

static int emit_jcc(int cc, struct Op *op) {
  return emit_rel(0x0F, 0x80 + cc, op, 6);
}

static int one_rm(u8 opc, int ext, struct Op *op) {
  struct Mem m;
  struct Reg fake;
  u8 code[1];
  int size, rex_w;
  if (op->kind == 1) {
    m = mem_reg(&op->reg);
    size = op->reg.size;
  } else if (op->kind == 3) {
    if (!op->size) {
      asm_fail("operand size required");
      return 0;
    }
    m = op->mem;
    size = op->size;
  } else {
    asm_fail("expected register or memory");
    return 0;
  }
  if (size == 1)
    opc = (u8)(opc == 0xF7 ? 0xF6 : opc == 0xFF ? 0xFE : opc);
  rex_w = size == 8 && !(opc == 0xFF && (ext == 2 || ext == 4));
  fake.ok = 1;
  fake.code = ext;
  fake.size = size;
  fake.high = 0;
  fake.need_rex = 0;
  code[0] = opc;
  return emit_reg_rm(rex_w, code, 1, &fake, &m);
}

static int emit_shift(int ext, struct Op *dst, struct Op *src, int by_cl) {
  u8 opc = 0xC1;
  int size = dst->kind == 1 ? dst->reg.size : dst->size;
  if (!size) {
    asm_fail("operand size required");
    return 0;
  }
  if (size == 1)
    opc = by_cl ? 0xD2 : 0xC0;
  else if (by_cl)
    opc = 0xD3;
  if (by_cl)
    return emit_imm_rm(size == 8, opc, ext, dst, 0, 0);
  return emit_imm_rm(size == 8, opc, ext, dst, src->imm, 1);
}

static const char *asm_zend(const char *s) {
  while (*s)
    s++;
  return s;
}

static int asm_insn(char *s) {
  char *ops[3];
  int nops, prefix = 0, jcc;
  char mnem[32];
  const char *p;
  const char *limit;
  u32 mi = 0;
  struct Op a, b, c;
  static const char *jname[] = {"jo",  "jno", "jb",  "jae", "je",  "jne",
                                "jbe", "ja",  "js",  "jns", "jp",  "jnp",
                                "jl",  "jge", "jle", "jg"};
  static const char *jalias[] = {"",    "",    "jnae", "jnb", "jz",  "jnz",
                                 "jna", "jnbe", "",    "",    "jpe", "jpo",
                                 "jnge","jnl",  "jng", "jnle"};
  limit = asm_zend(s);
  p = asm_skip(s, limit);
  for (;;) {
    const char *t = p;
    while (t < limit && asm_ident_char(*t))
      t++;
    if (asm_eq_n(p, t, "lock"))
      prefix = 0xF0;
    else if (asm_eq_n(p, t, "rep") || asm_eq_n(p, t, "repe") ||
             asm_eq_n(p, t, "repz"))
      prefix = 0xF3;
    else if (asm_eq_n(p, t, "repne") || asm_eq_n(p, t, "repnz"))
      prefix = 0xF2;
    else
      break;
    p = asm_skip(t, limit);
  }
  while (p < limit && asm_ident_char(*p) && mi + 1 < sizeof mnem)
    mnem[mi++] = *p++;
  mnem[mi] = 0;
  if (!mi) {
    asm_fail("missing mnemonic");
    return 0;
  }
  p = asm_skip(p, limit);
  nops = 0;
  if (*p) {
    nops = split_ops((char *)p, ops, 3);
    if (nops < 0)
      return 0;
  }
  if (prefix)
    asm_put((u8)prefix);
  if (!asm_eq(mnem, "rep") && nops == 0) {
    const u8 *sb = 0;
    int sn = 0;
    u8 tmp[4];
    if (asm_eq(mnem, "nop")) {
      tmp[0] = 0x90;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "ret")) {
      tmp[0] = 0xC3;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "leave")) {
      tmp[0] = 0xC9;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "hlt")) {
      tmp[0] = 0xF4;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cld")) {
      tmp[0] = 0xFC;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "std")) {
      tmp[0] = 0xFD;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "clc")) {
      tmp[0] = 0xF8;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "stc")) {
      tmp[0] = 0xF9;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cmc")) {
      tmp[0] = 0xF5;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cli")) {
      tmp[0] = 0xFA;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "sti")) {
      tmp[0] = 0xFB;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "int3")) {
      tmp[0] = 0xCC;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "ud2")) {
      tmp[0] = 0x0F;
      tmp[1] = 0x0B;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "cpuid")) {
      tmp[0] = 0x0F;
      tmp[1] = 0xA2;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "syscall")) {
      tmp[0] = 0x0F;
      tmp[1] = 0x05;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "sysret")) {
      tmp[0] = 0x48;
      tmp[1] = 0x0F;
      tmp[2] = 0x07;
      sn = 3;
      sb = tmp;
    } else if (asm_eq(mnem, "vmmcall")) {
      tmp[0] = 0x0F;
      tmp[1] = 0x01;
      tmp[2] = 0xD9;
      sn = 3;
      sb = tmp;
    } else if (asm_eq(mnem, "pushfq")) {
      tmp[0] = 0x9C;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "popfq")) {
      tmp[0] = 0x9D;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cwde")) {
      tmp[0] = 0x98;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cdqe")) {
      tmp[0] = 0x48;
      tmp[1] = 0x98;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "cdq")) {
      tmp[0] = 0x99;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cqo")) {
      tmp[0] = 0x48;
      tmp[1] = 0x99;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "movsb")) {
      tmp[0] = 0xA4;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "movsw")) {
      tmp[0] = 0x66;
      tmp[1] = 0xA5;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "movsd")) {
      tmp[0] = 0xA5;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "movsq")) {
      tmp[0] = 0x48;
      tmp[1] = 0xA5;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "stosb")) {
      tmp[0] = 0xAA;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "stosq")) {
      tmp[0] = 0x48;
      tmp[1] = 0xAB;
      sn = 2;
      sb = tmp;
    } else if (asm_eq(mnem, "lodsb")) {
      tmp[0] = 0xAC;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "scasb")) {
      tmp[0] = 0xAE;
      sn = 1;
      sb = tmp;
    } else if (asm_eq(mnem, "cmpsb")) {
      tmp[0] = 0xA6;
      sn = 1;
      sb = tmp;
    }
    if (sb)
      return emit_simple(sb, sn);
  }
  if (nops == 1 &&
      (asm_eq(mnem, "jmp") || asm_eq(mnem, "call") || asm_eq(mnem, "push") ||
       asm_eq(mnem, "pop") || asm_eq(mnem, "inc") || asm_eq(mnem, "dec") ||
       asm_eq(mnem, "neg") || asm_eq(mnem, "not") || asm_eq(mnem, "mul") ||
       asm_eq(mnem, "div") || asm_eq(mnem, "idiv"))) {
    if (!asm_parse_op(ops[0], asm_zend(ops[0]), &a))
      return 0;
    if (asm_eq(mnem, "jmp") && a.kind == 2)
      return emit_rel(0xE9, -1, &a, 5);
    if (asm_eq(mnem, "call") && a.kind == 2)
      return emit_rel(0xE8, -1, &a, 5);
    if (asm_eq(mnem, "jmp"))
      return one_rm(0xFF, 4, &a);
    if (asm_eq(mnem, "call"))
      return one_rm(0xFF, 2, &a);
    if (asm_eq(mnem, "inc"))
      return one_rm(0xFF, 0, &a);
    if (asm_eq(mnem, "dec"))
      return one_rm(0xFF, 1, &a);
    if (asm_eq(mnem, "not"))
      return one_rm(0xF7, 2, &a);
    if (asm_eq(mnem, "neg"))
      return one_rm(0xF7, 3, &a);
    if (asm_eq(mnem, "mul"))
      return one_rm(0xF7, 4, &a);
    if (asm_eq(mnem, "idiv") || asm_eq(mnem, "div"))
      return one_rm(0xF7, asm_eq(mnem, "div") ? 6 : 7, &a);
    if (asm_eq(mnem, "push") && a.kind == 2) {
      if (!fits32(a.imm)) {
        asm_fail("immediate does not fit");
        return 0;
      }
      asm_put(0x68);
      asm_put((u8)a.imm);
      asm_put((u8)((u64)a.imm >> 8));
      asm_put((u8)((u64)a.imm >> 16));
      asm_put((u8)((u64)a.imm >> 24));
      return 1;
    }
    if ((asm_eq(mnem, "push") || asm_eq(mnem, "pop")) && a.kind == 1) {
      int rex = 0;
      if (a.reg.size != 8 && a.reg.size != 2) {
        asm_fail("push and pop take a 64-bit register");
        return 0;
      }
      if (a.reg.size == 2)
        asm_put(0x66);
      if (a.reg.code >= 8)
        rex = 0x41;
      else if (force_low(&a.reg))
        rex = 0x40;
      if (rex)
        asm_put((u8)rex);
      asm_put((u8)((asm_eq(mnem, "push") ? 0x50 : 0x58) + (a.reg.code & 7)));
      return 1;
    }
  }
  jcc = -1;
  {
    int i;
    for (i = 0; i < 16; i++)
      if (asm_eq(mnem, jname[i]) || (jalias[i][0] && asm_eq(mnem, jalias[i])))
        jcc = i;
  }
  if (jcc >= 0 && nops == 1) {
    if (!asm_parse_op(ops[0], asm_zend(ops[0]), &a))
      return 0;
    return emit_jcc(jcc, &a);
  }
  if (nops == 2) {
    int alu = -1, sh = -1;
    if (!asm_parse_op(ops[0], asm_zend(ops[0]), &a) ||
        !asm_parse_op(ops[1], asm_zend(ops[1]), &b))
      return 0;
    if (asm_eq(mnem, "mov"))
      return emit_mov(&a, &b);
    if (asm_eq(mnem, "lea") && a.kind == 1 && b.kind == 3) {
      u8 opc[1];
      opc[0] = 0x8D;
      return emit_reg_rm(a.reg.size == 8, opc, 1, &a.reg, &b.mem);
    }
    if (asm_eq(mnem, "add"))
      alu = 0;
    else if (asm_eq(mnem, "or"))
      alu = 1;
    else if (asm_eq(mnem, "adc"))
      alu = 2;
    else if (asm_eq(mnem, "sbb"))
      alu = 3;
    else if (asm_eq(mnem, "and"))
      alu = 4;
    else if (asm_eq(mnem, "sub"))
      alu = 5;
    else if (asm_eq(mnem, "xor"))
      alu = 6;
    else if (asm_eq(mnem, "cmp"))
      alu = 7;
    if (alu >= 0)
      return emit_alu(alu, &a, &b);
    if (asm_eq(mnem, "test") && a.kind == 1 && b.kind == 1) {
      u8 opc[1];
      struct Mem m = mem_reg(&b.reg);
      opc[0] = a.reg.size == 1 ? 0x84 : 0x85;
      return emit_reg_rm(a.reg.size == 8, opc, 1, &a.reg, &m);
    }
    if (asm_eq(mnem, "xchg") && a.kind == 1 && b.kind == 1) {
      u8 opc[1];
      struct Mem m = mem_reg(&b.reg);
      opc[0] = a.reg.size == 1 ? 0x86 : 0x87;
      return emit_reg_rm(a.reg.size == 8, opc, 1, &a.reg, &m);
    }
    if (asm_eq(mnem, "imul") && a.kind == 1 && b.kind == 1) {
      u8 opc[2];
      struct Mem m = mem_reg(&b.reg);
      opc[0] = 0x0F;
      opc[1] = 0xAF;
      return emit_reg_rm(a.reg.size == 8, opc, 2, &a.reg, &m);
    }
    if ((asm_eq(mnem, "movzx") || asm_eq(mnem, "movsx") ||
         asm_eq(mnem, "movsxd")) &&
        a.kind == 1 && (b.kind == 1 || b.kind == 3)) {
      u8 opc[2];
      struct Mem m = b.kind == 1 ? mem_reg(&b.reg) : b.mem;
      int src = b.kind == 1 ? b.reg.size : b.size;
      if (!src) {
        asm_fail("operand size required");
        return 0;
      }
      if (asm_eq(mnem, "movsxd") || (asm_eq(mnem, "movsx") && src == 4)) {
        opc[0] = 0x63;
        return emit_reg_rm(1, opc, 1, &a.reg, &m);
      }
      opc[0] = 0x0F;
      if (asm_eq(mnem, "movzx"))
        opc[1] = src == 1 ? 0xB6 : 0xB7;
      else
        opc[1] = src == 1 ? 0xBE : 0xBF;
      return emit_reg_rm(a.reg.size == 8, opc, 2, &a.reg, &m);
    }
    if (asm_eq(mnem, "shl") || asm_eq(mnem, "sal"))
      sh = 4;
    else if (asm_eq(mnem, "shr"))
      sh = 5;
    else if (asm_eq(mnem, "sar"))
      sh = 7;
    else if (asm_eq(mnem, "rol"))
      sh = 0;
    else if (asm_eq(mnem, "ror"))
      sh = 1;
    if (sh >= 0) {
      int by_cl = b.kind == 1 && b.reg.size == 1 && b.reg.code == 1;
      if (!by_cl && b.kind != 2) {
        asm_fail("shift needs an immediate or cl");
        return 0;
      }
      return emit_shift(sh, &a, &b, by_cl);
    }
  }
  if (nops == 3 && asm_eq(mnem, "imul")) {
    struct Mem m;
    struct Enc e;
    u8 opc[1];
    if (!asm_parse_op(ops[0], asm_zend(ops[0]), &a) ||
        !asm_parse_op(ops[1], asm_zend(ops[1]), &b) ||
        !asm_parse_op(ops[2], asm_zend(ops[2]), &c))
      return 0;
    if (a.kind != 1 || c.kind != 2 || (b.kind != 1 && b.kind != 3)) {
      asm_fail("unsupported imul");
      return 0;
    }
    if (!fits32(c.imm)) {
      asm_fail("immediate does not fit");
      return 0;
    }
    m = b.kind == 1 ? mem_reg(&b.reg) : b.mem;
    opc[0] = 0x69;
    e.n = 0;
    if (!enc_modrm(&e, a.reg.size == 8 ? 8 : 0, opc, 1, a.reg.code, &m))
      return 0;
    enc_u32(&e, (u32)c.imm);
    enc_flush(&e);
    return !asm_failed;
  }
  asm_fail("unknown instruction");
  return 0;
}

static int emit_item(char *s, int width) {
  const char *end;
  i64 v;
  int i;
  s = (char *)asm_skip(s, asm_zend(s));
  if (*s == '"' || *s == '\'') {
    char q = *s++;
    if (width != 1) {
      asm_fail("strings belong in db");
      return 0;
    }
    while (*s && *s != q) {
      char c = *s++;
      if (c == '\\' && *s) {
        c = *s++;
        if (c == 'n')
          c = '\n';
        else if (c == '0')
          c = 0;
        else if (c == 't')
          c = '\t';
      }
      asm_put((u8)c);
    }
    return !asm_failed;
  }
  end = asm_zend(s);
  if (!asm_eval(s, end, &v))
    return 0;
  if (asm_emit) {
    if (width == 1 && (v < -128 || v > 255)) {
      asm_fail("db value does not fit");
      return 0;
    }
    if (width == 2 && (v < -32768 || v > 65535)) {
      asm_fail("dw value does not fit");
      return 0;
    }
    if (width == 4 && !fits32(v) && (u64)v > 0xffffffffu) {
      asm_fail("dd value does not fit");
      return 0;
    }
  }
  for (i = 0; i < width; i++)
    asm_put((u8)((u64)v >> (8 * i)));
  return !asm_failed;
}

static int emit_data(char *s, int width) {
  char *ops[16];
  int n, i;
  n = split_ops(s, ops, 16);
  if (n < 0)
    return 0;
  for (i = 0; i < n; i++)
    if (!emit_item(ops[i], width))
      return 0;
  return 1;
}

static int asm_directive(char *s) {
  char name[24];
  u32 i = 0;
  const char *p = s;
  i64 v;
  if (*p == '.')
    p++;
  while (*p && asm_ident_char(*p) && i + 1 < sizeof name)
    name[i++] = *p++;
  name[i] = 0;
  p = asm_skip(p, asm_zend(p));
  if (asm_eq(name, "org")) {
    if (!asm_eval(p, asm_zend(p), &v))
      return 0;
    if (!asm_have_org) {
      asm_origin = (u64)v;
      asm_loc = (u64)v;
      asm_have_org = 1;
      return 1;
    }
    if ((u64)v < asm_loc) {
      asm_fail("org moves backward");
      return 0;
    }
    while (asm_loc < (u64)v)
      asm_put(0);
    return !asm_failed;
  }
  if (asm_eq(name, "bits")) {
    if (!asm_eq(p, "64")) {
      asm_fail("resident assembler is 64-bit");
      return 0;
    }
    return 1;
  }
  if (asm_eq(name, "db") || asm_eq(name, "byte") || asm_eq(name, "ascii") ||
      asm_eq(name, "asciz")) {
    if (!emit_data((char *)p, 1))
      return 0;
    if (asm_eq(name, "asciz"))
      asm_put(0);
    return !asm_failed;
  }
  if (asm_eq(name, "dw") || asm_eq(name, "word"))
    return emit_data((char *)p, 2);
  if (asm_eq(name, "dd") || asm_eq(name, "long"))
    return emit_data((char *)p, 4);
  if (asm_eq(name, "dq") || asm_eq(name, "quad"))
    return emit_data((char *)p, 8);
  if (asm_eq(name, "resb") || asm_eq(name, "resw") || asm_eq(name, "resd") ||
      asm_eq(name, "resq")) {
    int width = asm_eq(name, "resb") ? 1 : asm_eq(name, "resw") ? 2
                                       : asm_eq(name, "resd")   ? 4
                                                                : 8;
    u64 n;
    if (!asm_eval(p, asm_zend(p), &v) || xp_undef) {
      asm_fail("reserve needs a constant");
      return 0;
    }
    n = (u64)v * (u64)width;
    while (n--)
      asm_put(0);
    return !asm_failed;
  }
  if (asm_eq(name, "align")) {
    u64 n, i;
    if (!asm_eval(p, asm_zend(p), &v) || v <= 0 || (v & (v - 1))) {
      asm_fail("align needs a power of two");
      return 0;
    }
    n = (u64)v;
    i = 0;
    while ((asm_loc & (n - 1)) && i < n) {
      asm_put(0x90);
      i++;
    }
    return !asm_failed;
  }
  if (asm_eq(name, "section") || asm_eq(name, "segment") ||
      asm_eq(name, "global") || asm_eq(name, "globl") || asm_eq(name, "extern"))
    return 1;
  return -1;
}

static int asm_statement(char *s) {
  char *body = s;
  const char *p;
  char name[SYM_NAME];
  u32 nlen = 0;
  while (*body == ' ' || *body == '\t')
    body++;
  if (!*body || *body == ';')
    return 1;
  if (*body == '[') {
    char *q = body + 1;
    while (*q && *q != ']')
      q++;
    if (*q == ']') {
      *q = 0;
      return asm_directive(body + 1) == 1;
    }
  }
  p = body;
  if (asm_ident_start(*p)) {
    while (*p && asm_ident_char(*p) && nlen + 1 < SYM_NAME)
      name[nlen++] = *p++;
    name[nlen] = 0;
    if (*p == ':') {
      if (!asm_define(name, nlen, asm_loc, asm_emit))
        return 0;
      body = (char *)p + 1;
      while (*body == ' ' || *body == '\t')
        body++;
      if (!*body)
        return 1;
    } else {
      const char *q = asm_skip(p, asm_zend(p));
      if (asm_eq_n(q, q + 3, "equ") && (q[3] == 0 || q[3] == ' ' || q[3] == '\t')) {
        i64 v;
        q = asm_skip(q + 3, asm_zend(q));
        if (!asm_eval(q, asm_zend(q), &v))
          return 0;
        if (asm_emit && xp_undef) {
          asm_fail("undefined symbol");
          return 0;
        }
        return asm_define(name, nlen, (u64)v, asm_emit) != 0;
      }
    }
  }
  {
    const char *t = body;
    if (t[0] == 't' && t[5] == ' ' && asm_eq_n(t, t + 5, "times")) {
      i64 count;
      const char *rest;
      i64 n;
      t = asm_skip(t + 5, asm_zend(t));
      rest = t;
      while (*rest && *rest != ' ' && *rest != '\t')
        rest++;
      if (!asm_eval(t, rest, &count) || xp_undef || count < 0 || count > 4096) {
        asm_fail("times needs a constant from 0 to 4096");
        return 0;
      }
      rest = asm_skip(rest, asm_zend(rest));
      for (n = 0; n < count; n++) {
        char copy[256];
        u32 k = 0;
        while (rest[k] && k + 1 < sizeof copy) {
          copy[k] = rest[k];
          k++;
        }
        copy[k] = 0;
        if (!asm_statement(copy))
          return 0;
      }
      return 1;
    }
  }
  {
    int directed = asm_directive(body);
    if (directed == 0)
      return 0;
    if (directed == 1)
      return !asm_failed;
  }
  return asm_insn(body);
}

static void asm_reset_loc(void) {
  asm_loc = 0;
  asm_origin = 0;
  asm_have_org = 0;
  asm_filled = 0;
  asm_failed = 0;
  asm_err[0] = 0;
  asm_line = 0;
}

static int asm_pass(const char *src, u32 len, int emit, u8 *out, u32 cap) {
  u32 i = 0;
  asm_reset_loc();
  asm_emit = emit;
  asm_out = out;
  asm_cap = cap;
  while (i < len && !asm_failed) {
    char buf[512];
    u32 n = 0, k;
    int quote = 0;
    asm_line++;
    while (i < len && src[i] != '\n' && n + 1 < sizeof buf)
      buf[n++] = src[i++];
    if (i < len && src[i] != '\n' && n + 1 >= sizeof buf) {
      asm_fail("line is too long");
      return 0;
    }
    if (i < len && src[i] == '\n')
      i++;
    if (n && buf[n - 1] == '\r')
      n--;
    buf[n] = 0;
    for (k = 0; k < n; k++) {
      if (quote) {
        if (buf[k] == '\\' && k + 1 < n) {
          k++;
          continue;
        }
        if (buf[k] == quote)
          quote = 0;
        continue;
      }
      if (buf[k] == '"' || buf[k] == '\'') {
        quote = buf[k];
        continue;
      }
      if (buf[k] == ';') {
        buf[k] = 0;
        break;
      }
    }
    if (!asm_statement(buf))
      return 0;
  }
  return !asm_failed;
}

static int asm_once(const char *src, u32 len, u8 *out, u32 cap, struct AsmResult *out_r) {
  struct Sym *entry;
  u32 i;
  sym_count = 0;
  for (i = 0; i < cap && i < 64; i++)
    out[i] = 0;
  if (!asm_pass(src, len, 0, out, cap))
    goto bad;
  if (!asm_pass(src, len, 1, out, cap))
    goto bad;
  out_r->ok = 1;
  out_r->length = asm_filled;
  out_r->origin = (unsigned)asm_origin;
  entry = asm_find("entry", 5);
  out_r->entry = entry && entry->defined ? (unsigned)entry->value : out_r->origin;
  out_r->error[0] = 0;
  return 1;
bad:
  out_r->ok = 0;
  out_r->length = 0;
  out_r->entry = 0;
  out_r->origin = 0;
  for (i = 0; asm_err[i] && i + 1 < sizeof out_r->error; i++)
    out_r->error[i] = asm_err[i];
  out_r->error[i] = 0;
  return 0;
}

const unsigned char *asm_image(void) { return asm_a; }
unsigned asm_capacity(void) { return ASM_CAP; }

int asm_build(const char *src, unsigned src_len, struct AsmResult *out) {
  struct AsmResult other;
  u32 i;
  if (!src || !out) {
    return 0;
  }
  if (!asm_once(src, src_len, asm_b, ASM_CAP, &other)) {
    *out = other;
    return 0;
  }
  if (!asm_once(src, src_len, asm_a, ASM_CAP, out))
    return 0;
  if (out->length != other.length || out->entry != other.entry) {
    out->ok = 0;
    for (i = 0; i + 1 < sizeof out->error; i++) {
      const char *m = "assembler was not stable";
      if (!m[i])
        break;
      out->error[i] = m[i];
    }
    out->error[i] = 0;
    return 0;
  }
  for (i = 0; i < out->length; i++) {
    if (asm_a[i] != asm_b[i]) {
      out->ok = 0;
      {
        const char *m = "assembler was not stable";
        for (i = 0; m[i] && i + 1 < sizeof out->error; i++)
          out->error[i] = m[i];
        out->error[i] = 0;
      }
      return 0;
    }
  }
  return 1;
}

#ifdef ASM_HOST
#include <stdio.h>
#include <stdlib.h>
static unsigned long fnv(const unsigned char *p, unsigned n) {
  unsigned long h = 14695981039346656037UL;
  unsigned i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211UL;
  }
  return h;
}
static char *read_all(const char *path, unsigned *len) {
  FILE *f = fopen(path, "rb");
  char *b;
  long n;
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  rewind(f);
  b = malloc((size_t)n + 1);
  if (!b)
    return 0;
  if (fread(b, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(b);
    return 0;
  }
  fclose(f);
  b[n] = 0;
  *len = (unsigned)n;
  return b;
}
int main(int argc, char **argv) {
  unsigned len = 0;
  char *src;
  struct AsmResult r;
  unsigned i;
  if (argc < 3) {
    fprintf(stderr, "usage: asm-host source.bin-out\n");
    return 2;
  }
  src = read_all(argv[1], &len);
  if (!src) {
    perror(argv[1]);
    return 1;
  }
  if (!asm_build(src, len, &r)) {
    fprintf(stderr, "FAIL %s\n", r.error);
    return 1;
  }
  {
    FILE *o = fopen(argv[2], "wb");
    if (!o)
      return 1;
    fwrite(asm_image(), 1, r.length, o);
    fclose(o);
  }
  printf("ok length=%u entry=0x%x origin=0x%x fnv=0x%lx\n", r.length, r.entry,
         r.origin, fnv(asm_image(), r.length));
  for (i = 0; i < r.length && i < 96; i++)
    printf("%02x%s", asm_image()[i], (i % 16) == 15 ? "\n" : " ");
  printf("\n");
  free(src);
  return 0;
}
#endif
