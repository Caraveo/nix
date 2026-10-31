/* Polled i8042 auxiliary port, standard three-byte PS/2 mouse packets. */
static int mouse_ready = 0;
static int controller_write(u16 port, u8 value) {
  for (u32 i = 0; i < 200000; i++)
    if (!(inb(0x64) & 2)) {
      outb(port, value);
      return 1;
    }
  return 0;
}
static int mouse_command(u8 value) {
  if (!controller_write(0x64, 0xd4) || !controller_write(0x60, value))
    return 0;
  for (u32 i = 0; i < 200000; i++) {
    u8 status = inb(0x64);
    if ((status & 0x21) == 0x21)
      return inb(0x60) == 0xfa;
  }
  return 0;
}
static void mouse_init(void) {
  if (!pixels)
    return;
  if (!controller_write(0x64, 0xa8) || !controller_write(0x64, 0x20))
    return;
  int config = -1;
  for (u32 i = 0; i < 200000; i++)
    if (inb(0x64) & 1) {
      config = inb(0x60);
      break;
    }
  if (config < 0)
    return;
  /* Poll both devices with IRQs disabled; preserve keyboard translation. */
  if (!controller_write(0x64, 0x60) ||
      !controller_write(0x60, (u8)(config & ~0x23)))
    return;
  if (!mouse_command(0xf6) || !mouse_command(0xf4))
    return;
  mouse_ready = 1;
  pointer_on = 1;
  pointer_move(mouse_x, mouse_y);
}
static int mouse_byte(u8 byte) {
  static u8 packet[3];
  static int index = 0, buttons = 0;
  if (!mouse_ready)
    return 0;
  if (!index && !(byte & 8))
    return 0;
  packet[index++] = byte;
  if (index < 3)
    return 0;
  index = 0;
  if (packet[0] & 0xc0)
    return 0; /* Ignore overflow packets. */
  int dx = (int)packet[1] - ((packet[0] & 16) ? 256 : 0);
  int dy = (int)packet[2] - ((packet[0] & 32) ? 256 : 0);
  pointer_move(mouse_x + dx, mouse_y - dy);
  int click = (packet[0] & 1) && !(buttons & 1);
  buttons = packet[0] & 7;
  return click;
}
