/* Power transitions stay deterministic and commit dirty Author work first. */
static int power_pending = 0;
static int power_commit(void) {
  if (dirty && !save()) {
    line("Save failed. Power action cancelled; your draft remains in RAM.");
    return 0;
  }
  if (storage_ok && !flush()) {
    line("Disk flush failed. Power action cancelled.");
    return 0;
  }
  return 1;
}
static void power_request(void) {
  graphics_menu_hide();
  power_pending = 1;
  line("CAUTION: Shut down NIX? [y/n]");
  line("Unsaved Author text will be saved before power-off. n keeps NIX "
       "running.");
}
static void power_off(void) {
  if (!power_commit())
    return;
  line("Goodbye. All committed Memories remain on your virtual disk.");
  outw(0x604, 0x2000);
  outw(0xb004, 0x2000);
  for (;;)
    __asm__ volatile("hlt");
}
static void power_boot(void) {
  graphics_menu_hide();
  if (!power_commit())
    return;
  line("Restarting NIX. Saved Memories will be reloaded.");
  if (controller_write(0x64, 0xfe)) {
    for (volatile u32 i = 0; i < 100000; i++)
      __asm__ volatile("pause");
  }
  outb(0xcf9, 0x06);
  for (;;)
    __asm__ volatile("hlt");
}
static void boot_pause(void) {
  /* PIT channel 2 one-shots: approximately one second of visible boot branding.
     Keep the speaker disabled and leave timer channel 0 untouched. */
  u8 original = inb(0x61);
  for (int tick = 0; tick < 20; tick++) {
    outb(0x61, (u8)(original & ~3));
    outb(0x43, 0xb0);
    outb(0x42, (u8)59659);
    outb(0x42, (u8)(59659 >> 8));
    outb(0x61, (u8)((original & ~2) | 1));
    for (u32 i = 0; i < 2000000; i++)
      if (inb(0x61) & 32)
        break;
  }
  outb(0x61, original);
}
