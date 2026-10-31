/* Typed intents. Names come from the model. This file has no OS catalog.
   A second install of the same pod is refused unless the user asked to replace it. */
static int intent_on, intent_step, intent_verb;
static char intent_name[32];
static char installed_name[8][32];
static int installed_used[8];

static const char *intent_prompt(void) {
  if (!intent_on)
    return 0;
  return "[y/n] > ";
}
static void intent_clear(void) {
  intent_on = 0;
  intent_step = 0;
  intent_verb = 0;
  intent_name[0] = 0;
}
static int boundary_at(const char *s, u32 i, int after) {
  char c = after ? s[i] : (i ? s[i - 1] : 0);
  if (!after && !i)
    return 1;
  if (!c)
    return 1;
  if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
    return 0;
  return 1;
}
static int has_word(const char *s, const char *w) {
  u32 n = len(w);
  for (u32 i = 0; s[i]; i++) {
    if (!starts(s + i, w))
      continue;
    if (!boundary_at(s, i, 0) || !boundary_at(s, i + n, 1))
      continue;
    return 1;
  }
  return 0;
}
static int allows_replace(const char *s) {
  return has_word(s, "reinstall") || has_word(s, "overwrite") ||
         has_word(s, "again") || has_word(s, "replace");
}
static int name_ok(const char *s) {
  u32 n = 0;
  int alnum = 0;
  if (!*s)
    return 0;
  for (; s[n]; n++) {
    char c = s[n];
    if (n + 1 >= sizeof intent_name)
      return 0;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
      alnum = 1;
    else if (c != ' ' && c != '.' && c != '_' && c != '-')
      return 0;
  }
  return alnum && s[n - 1] != ' ';
}
static int installed_find(const char *name) {
  for (int i = 0; i < 8; i++)
    if (installed_used[i] && eq(installed_name[i], name))
      return i;
  return -1;
}
static void intent_show(void) {
  if (intent_step == 0) {
    if (intent_verb == 2)
      say("Replace pod ");
    else if (intent_verb == 3)
      say("Launch pod ");
    else if (intent_verb == 4)
      say("Remove pod ");
    else
      say("Install ");
    say(intent_name);
    if (intent_verb == 1)
      line(" as a pod? [y/n]");
    else
      line("? [y/n]");
    return;
  }
  line(intent_verb == 2   ? "Intent reinstall"
       : intent_verb == 3 ? "Intent launch"
       : intent_verb == 4 ? "Intent remove"
                          : "Intent install");
  say("  pod: ");
  line(intent_name);
  line("  authority: dios. The model cannot confirm.");
  line("Confirm as dios? [y/n]");
}
static int intent_yes(const char *s) { return eq(s, "y") || eq(s, "yes"); }
static int intent_no(const char *s) { return eq(s, "n") || eq(s, "no"); }
static void intent_commit(void) {
  int slot = installed_find(intent_name);
  if (intent_verb == 4) {
    if (slot >= 0)
      installed_used[slot] = 0;
    kernel_unclone(intent_name);
    say("Pod ");
    say(intent_name);
    line(" removed.");
    return;
  }
  if (intent_verb == 3) {
    if (kernel_is_clone(intent_name)) {
      kernel_run_clone(intent_name);
      return;
    }
    say("Pod ");
    say(intent_name);
    line(" was not started. A container runtime is not in this kernel.");
    return;
  }
  if (slot < 0) {
    for (int i = 0; i < 8; i++)
      if (!installed_used[i]) {
        slot = i;
        break;
      }
  }
  if (slot < 0) {
    line("Pod table is full. Nothing was installed.");
    return;
  }
  copy(installed_name[slot], intent_name, 32);
  installed_used[slot] = 1;
  say("Pod ");
  say(intent_name);
  line(intent_verb == 2 ? " replaced." : " recorded.");
  line("A container runtime is not in this kernel, so it was not started.");
}
static int intent_begin(int verb, const char *name, const char *user) {
  if (!name_ok(name))
    return 0;
  int slot = installed_find(name);
  if (verb == 1 && slot >= 0) {
    say("Pod ");
    say(name);
    line(" is already installed. It was not overwritten.");
    return 1;
  }
  if (verb == 2 && !allows_replace(user)) {
    say("Pod ");
    say(name);
    line(" was not replaced. Say reinstall, overwrite, again, or replace.");
    return 1;
  }
  if ((verb == 3 || verb == 4) && slot < 0 && !kernel_is_clone(name)) {
    say("Pod ");
    say(name);
    line(" is not installed.");
    return 1;
  }
  if (verb == 2 && slot < 0)
    verb = 1;
  intent_on = 1;
  intent_step = 0;
  intent_verb = verb;
  copy(intent_name, name, sizeof intent_name);
  intent_show();
  return 1;
}
static int parse_intent_line(const char *line, const char *user) {
  while (*line == ' ')
    line++;
  if (!starts(line, "intent "))
    return 0;
  line += 7;
  int verb = 0;
  if (starts(line, "reinstall ")) {
    verb = 2;
    line += 10;
  } else if (starts(line, "install ")) {
    verb = 1;
    line += 8;
  } else if (starts(line, "launch ")) {
    verb = 3;
    line += 7;
  } else if (starts(line, "remove ")) {
    verb = 4;
    line += 7;
  } else
    return 0;
  while (*line == ' ')
    line++;
  char name[32];
  u32 n = 0;
  while (line[n] && line[n] != '\n' && n + 1 < sizeof name) {
    name[n] = line[n];
    n++;
  }
  while (n && name[n - 1] == ' ')
    n--;
  name[n] = 0;
  if (line[n] && line[n] != '\n' && line[n] != ' ')
    return 0;
  return intent_begin(verb, name, user);
}
static int intent_from_text(const char *generated, const char *user) {
  if (!generated || intent_on)
    return 0;
  char lower_user[256];
  copy(lower_user, user ? user : "", sizeof lower_user);
  for (u32 i = 0; lower_user[i]; i++)
    if (lower_user[i] >= 'A' && lower_user[i] <= 'Z')
      lower_user[i] += 32;
  const char *s = generated;
  while (*s) {
    while (*s == ' ')
      s++;
    char line[96];
    u32 n = 0;
    while (*s && *s != '\n' && n + 1 < sizeof line)
      line[n++] = *s++;
    line[n] = 0;
    while (*s && *s != '\n')
      s++;
    if (*s == '\n')
      s++;
    for (u32 i = 0; line[i]; i++)
      if (line[i] >= 'A' && line[i] <= 'Z')
        line[i] += 32;
    if (parse_intent_line(line, lower_user))
      return 1;
  }
  return 0;
}
static void intent_answer(const char *text, const char *lower) {
  (void)text;
  if (!intent_yes(lower) && !intent_no(lower)) {
    line("Please answer y or n. The intent is still waiting.");
    return;
  }
  if (intent_no(lower)) {
    intent_clear();
    line("Intent cancelled.");
    return;
  }
  if (intent_step == 0) {
    intent_step = 1;
    intent_show();
    return;
  }
  int verb = intent_verb;
  char name[32];
  copy(name, intent_name, sizeof name);
  intent_clear();
  intent_verb = verb;
  copy(intent_name, name, sizeof intent_name);
  intent_commit();
  intent_clear();
}
