/* Freestanding C adaptation of caraveo/ghost src/parser.rs tokenizer.
 * Upstream 6f584f47abc5144d70169e401ea513759521b903, retained in third_party.
 * Bounded storage replaces Rust String/Vec. No environment expansion yet.
 */
enum GhostKind {
  G_WORD,
  G_PIPE,
  G_AND,
  G_OR,
  G_SEMI,
  G_IN,
  G_OUT,
  G_APPEND,
  G_BG
};
struct GhostToken {
  enum GhostKind kind;
  char *text;
};
struct GhostParse {
  struct GhostToken tokens[32];
  char decoded[256];
  int count;
};
static int ghost_space(char c) { return c == ' ' || c == '\t'; }
static int ghost_special(char c) {
  return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}
static int ghost_tokenize(const char *input, struct GhostParse *p) {
  int i = 0, n = 0;
  p->count = 0;
  while (input[i]) {
    if (ghost_space(input[i])) {
      i++;
      continue;
    }
    if (p->count == 32)
      return 0;
    struct GhostToken *t = &p->tokens[p->count++];
    t->text = &p->decoded[n];
    char c = input[i];
    t->kind = G_WORD;
    if (ghost_special(c)) {
      i++;
      if (c == '|') {
        t->kind = G_PIPE;
        if (input[i] == '|') {
          t->kind = G_OR;
          i++;
        }
      }
      if (c == '&') {
        t->kind = G_BG;
        if (input[i] == '&') {
          t->kind = G_AND;
          i++;
        }
      }
      if (c == ';')
        t->kind = G_SEMI;
      if (c == '<')
        t->kind = G_IN;
      if (c == '>') {
        t->kind = G_OUT;
        if (input[i] == '>') {
          t->kind = G_APPEND;
          i++;
        }
      }
    } else if (c == '\'' || c == '"') {
      i++;
      while (input[i] && input[i] != c) {
        if (n >= 254)
          return 0;
        p->decoded[n++] = input[i++];
      }
      if (!input[i])
        return 0;
      i++;
    } else {
      while (input[i] && !ghost_space(input[i]) && !ghost_special(input[i])) {
        if (n >= 254)
          return 0;
        p->decoded[n++] = input[i++];
      }
    }
    if (n >= 256)
      return 0;
    p->decoded[n++] = 0;
  }
  return 1;
}
