/* In-guest natural-language assistance. Generated text has no tool authority.
 */
#include "runtime.h"
static int ai_prefix_cached = 0;
static int ai_poll_input(void) {
  if (inb(0x3fd) & 1) {
    u8 c = inb(0x3f8);
    if (c == 27 || c == 3)
      return 1;
  }
  u8 status = inb(0x64);
  if (status & 1) {
    u8 c = inb(0x60);
    if (status & 0x20)
      (void)mouse_byte(c);
    else {
      if (c == 42 || c == 54)
        key_shift = 1;
      if (c == 170 || c == 182)
        key_shift = 0;
      if (c == 1)
        return 1;
    }
  }
  return 0;
}
static void ai_boot(u32 info) {
  if (!info)
    return;
  const u32 *mb = (const u32 *)info;
  /* Multiboot v1: module count/address are words 5/6. QEMU loads the
     pinned bundle as an immutable boot module, separate from personal data. */
  if (!(mb[0] & 8) || mb[5] != 1 || !mb[6])
    return;
  const u32 *module = (const u32 *)mb[6];
  u32 start = module[0], end = module[1];
  if (end <= start || end - start > 256U * 1024U * 1024U)
    return;
  ai_init((const void *)start, end - start);
  ai.poll = ai_poll_input;
}
static void ai_answer(const char *text) {
  if (!ai.ready) {
    line("Local model is not loaded. Attach the NIXAI boot bundle to enable "
         "AI.");
    line("Deterministic Home, Author, Memories, and Ghost remain available.");
    return;
  }
  if (!*text) {
    line("Ask NIX a question or describe the outcome you want.");
    return;
  }
  int tokens[AI_CONTEXT];
  int count = ai_prompt(text, tokens, AI_CONTEXT - 49);
  if (count < 0) {
    line("That request exceeds the local context limit. Please shorten it.");
    return;
  }
  ai.cancelled = 0;
  color = 0xf3;
  line("NIX / local AI  -  SmolLM2 135M Q8");
  color = 0xf0;
  line("Thinking locally. Esc cancels. Generated suggestions do not run "
       "actions.");
  say("Reading request");
  for (int i = ai_prefix_cached ? (int)ai.header[15] : 0; i < count; i++) {
    if (!ai_forward(tokens[i], i, i == count - 1)) {
      line("\nCancelled. Your saved work is unchanged.");
      return;
    }
    if (i + 1 == (int)ai.header[15])
      ai_prefix_cached = 1;
    if ((i + 1) % 8 == 0)
      put('.');
  }
  line("");
  int ended = 0;
  for (int pos = count; pos < count + 48 && pos < AI_CONTEXT; pos++) {
    int token = ai_argmax();
    if (token <= 2) {
      ended = 1;
      break;
    }
    u32 begin = ai.offsets[token], end = ai.offsets[token + 1];
    /* Avoid leaking control sequences or other special-token strings into UI.
     */
    if (token < 17) {
      ended = 1;
      break;
    }
    for (u32 j = begin; j < end; j++) {
      u8 c = (u8)ai.vocabulary[j];
      if (c == '\n' || (c >= 32 && c <= 126))
        put((char)c);
    }
    if (!ai_forward(token, pos, 1)) {
      line("\nCancelled. Your saved work is unchanged.");
      return;
    }
  }
  put('\n');
  if (!ended)
    line("[Response limit reached - ask for a shorter answer.]");
}
