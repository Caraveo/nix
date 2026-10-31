/* Ghost for Core: built-ins operate on the Memory broker, never host files. */
#include "parser.h"
static int ghost_open = 0;
static void ghost_screen(void) {
  screen("GHOST  /  shell                                           Light");
  line("Ghost / Core edition");
  line("Your terminal, on request. Type help for supported commands.");
  line("");
}
static int ghost_find(const char *name) {
  for (u32 i = 0; i < store.count; i++)
    if (eq(name, store.books[i].title))
      return (int)i;
  return -1;
}
static int ghost_join(char *dest, struct GhostToken *t, int count) {
  u32 n = 0;
  for (int i = 0; i < count; i++) {
    if (t[i].kind != G_WORD)
      return 0;
    u32 k = len(t[i].text);
    if (n + k + 2 > 256)
      return 0;
    if (i)
      dest[n++] = ' ';
    memcpy(dest + n, t[i].text, k);
    n += k;
  }
  dest[n] = 0;
  return 1;
}
static int ghost_builtin(struct GhostToken *t, int count) {
  char args[256];
  if (!count)
    return 0;
  int redirect = -1;
  for (int i = 1; i < count; i++)
    if (t[i].kind == G_OUT || t[i].kind == G_APPEND) {
      redirect = i;
      break;
    }
  if (redirect >= 0) {
    if (!eq(t[0].text, "echo") || redirect + 2 != count ||
        t[redirect + 1].kind != G_WORD ||
        !ghost_join(args, t + 1, redirect - 1)) {
      line("Use: echo \"text\" >> \"Existing title\" (append only).");
      return 2;
    }
    if (t[redirect].kind == G_OUT) {
      line("Overwrite disabled in this MVP. Use >> to append or edit <title>.");
      return 2;
    }
    int id = ghost_find(t[redirect + 1].text);
    if (id < 0) {
      line("Memory not found. Create one through Author first.");
      return 1;
    }
    struct Book *b = &store.books[id];
    u32 n = len(args);
    if (b->length + n + 1 >= BODY) {
      line("Memory is full.");
      return 1;
    }
    /* Preserve a RAM copy so a failed commit does not silently change content.
     */
    u32 old = b->length;
    memcpy(b->text + old, args, n);
    b->length += n;
    b->text[b->length++] = '\n';
    b->text[b->length] = 0;
    if (!save()) {
      b->length = old;
      b->text[old] = 0;
      line("Save failed. Append was rolled back in RAM.");
      return 1;
    }
    line("Memory appended, saved, and verified.");
    return 0;
  }
  if (!ghost_join(args, t + 1, count - 1)) {
    line("Unsupported operator in command.");
    return 2;
  }
  const char *cmd = t[0].text;
  if (eq(cmd, "die") || eq(cmd, "shutdown")) {
    power_request();
    return 0;
  }
  if (eq(cmd, "boot")) {
    power_boot();
    return 0;
  }
  if (eq(cmd, "help")) {
    line("Ghost built-ins for Core");
    line("  list / ls       cat <title>       edit <title>");
    line("  echo <text>     echo <text> >> \"Existing title\"");
    line("  pwd             cd /memories      whoami");
    line("  uname           hostname          clear / exit");
    line("  die / shutdown (y/n)              boot (restart)");
    line("  Quoted words and chains: ;  &&  ||");
    line("Pipes, processes, expansion, networking and overwrite need OS "
         "services.");
    return 0;
  }
  if (eq(cmd, "exit")) {
    ghost_open = 0;
    home();
    return 0;
  }
  if (eq(cmd, "clear")) {
    ghost_screen();
    return 0;
  }
  if (eq(cmd, "pwd")) {
    line("/memories");
    return 0;
  }
  if (eq(cmd, "cd")) {
    if (!*args || eq(args, "/memories") || eq(args, "."))
      return 0;
    line("Only /memories is mounted.");
    return 1;
  }
  if (eq(cmd, "whoami")) {
    line("owner");
    return 0;
  }
  if (eq(cmd, "hostname")) {
    line("nix-core");
    return 0;
  }
  if (eq(cmd, "uname")) {
    line("NIX Core 0.2 i686 / Ghost Core edition");
    return 0;
  }
  if (eq(cmd, "echo") || eq(cmd, "print")) {
    line(args);
    return 0;
  }
  if (eq(cmd, "ls") || eq(cmd, "list")) {
    for (u32 i = 0; i < store.count; i++) {
      say(store.books[i].title);
      say("  ");
      number(store.books[i].length);
      line(" bytes");
    }
    if (!store.count)
      line("No Memories yet.");
    return 0;
  }
  if (eq(cmd, "cat") || eq(cmd, "spill") || eq(cmd, "edit")) {
    int id = ghost_find(args);
    if (id < 0) {
      line("Memory not found.");
      return 1;
    }
    if (eq(cmd, "edit")) {
      ghost_open = 0;
      selected = id;
      author();
    } else
      line(store.books[id].text);
    return 0;
  }
  line("Command unavailable in Ghost Core. Type help.");
  return 127;
}
static void ghost_execute(const char *s) {
  struct GhostParse p;
  if (!ghost_tokenize(s, &p)) {
    line("Ghost: unterminated quote or command exceeds parser limits.");
    return;
  }
  /* Validate unsupported syntax before any command can mutate a Memory. */
  for (int i = 0; i < p.count; i++)
    if (p.tokens[i].kind == G_PIPE || p.tokens[i].kind == G_IN ||
        p.tokens[i].kind == G_BG) {
      line("Ghost Core has no pipes, process jobs or stdin redirection yet.");
      return;
    }
  for (int i = 0; i < p.count; i++)
    if (p.tokens[i].kind == G_AND || p.tokens[i].kind == G_OR ||
        p.tokens[i].kind == G_SEMI) {
      if (i == 0 || i == p.count - 1 || p.tokens[i - 1].kind != G_WORD ||
          p.tokens[i + 1].kind != G_WORD) {
        line("Ghost: expected a command beside the chain operator.");
        return;
      }
    }
  int status = 0, start = 0;
  enum GhostKind gate = G_SEMI;
  for (int i = 0; i <= p.count; i++) {
    enum GhostKind kind = i == p.count ? G_SEMI : p.tokens[i].kind;
    if (kind == G_AND || kind == G_OR || kind == G_SEMI) {
      if (gate == G_SEMI || (gate == G_AND && status == 0) ||
          (gate == G_OR && status != 0))
        status = ghost_builtin(p.tokens + start, i - start);
      if (!ghost_open || power_pending)
        return;
      gate = kind;
      start = i + 1;
    }
  }
}
