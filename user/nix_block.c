/* Linux block bridge. Only NIX's per-instance ATA proxy is visible here. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/nbd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DISK_BYTES (20ULL * 1024 * 1024 * 1024)
static long host_call(long call) {
  __asm__ volatile(".byte 0x0f, 0x01, 0xd9" : "+a"(call) :: "memory");
  return call;
}
static int transfer(int fd, void *data, size_t count, int write_data) {
  unsigned char *p = data;
  while (count) {
    ssize_t n = write_data ? write(fd, p, count) : read(fd, p, count);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    p += n;
    count -= (size_t)n;
  }
  return 0;
}
static int ata_wait(int drq) {
  for (unsigned i = 0; i < 1000000; i++) {
    unsigned char s = inb(0x1f7);
    if (s == 0xff || s == 0 || (s & 0x21)) return -1;
    if (!(s & 0x80) && (!drq || (s & 8))) return 0;
  }
  return -1;
}
static int ata_sector(uint32_t lba, void *data, int write_data) {
  if (lba >= DISK_BYTES / 512) return -1;
  outb((unsigned char)(0xe0 | ((lba >> 24) & 15)), 0x1f6);
  outb(1, 0x1f2);
  outb((unsigned char)lba, 0x1f3);
  outb((unsigned char)(lba >> 8), 0x1f4);
  outb((unsigned char)(lba >> 16), 0x1f5);
  outb(write_data ? 0x30 : 0x20, 0x1f7);
  if (ata_wait(1)) return -1;
  unsigned long words = 256;
  unsigned short port = 0x1f0;
  if (write_data) {
    const void *p = data;
    __asm__ volatile("cld; rep outsw" : "+S"(p), "+c"(words) : "d"(port) : "memory");
  } else {
    void *p = data;
    __asm__ volatile("cld; rep insw" : "+D"(p), "+c"(words) : "d"(port) : "memory");
  }
  return ata_wait(0);
}
static int ata_flush(void) {
  outb(0xe7, 0x1f7);
  return ata_wait(0);
}
struct __attribute__((packed)) Request {
  uint32_t magic, type;
  unsigned char handle[8];
  uint64_t offset;
  uint32_t length;
};
struct __attribute__((packed)) Reply {
  uint32_t magic, error;
  unsigned char handle[8];
};
static int serve(int fd) {
  if (ioperm(0x1f0, 8, 1) || ioperm(0x3f6, 1, 1)) return 1;
  for (;;) {
    struct Request req;
    struct Reply reply = { .magic = htonl(NBD_REPLY_MAGIC), .error = 0 };
    if (transfer(fd, &req, sizeof req, 0)) return 0;
    if (ntohl(req.magic) != NBD_REQUEST_MAGIC) return 1;
    uint32_t type = ntohl(req.type), command = type & 0xffff;
    uint32_t length = ntohl(req.length);
    uint64_t offset = be64toh(req.offset);
    memcpy(reply.handle, req.handle, sizeof reply.handle);
    if (command == NBD_CMD_DISC) return 0;
    if (command == NBD_CMD_FLUSH) {
      reply.error = htonl(ata_flush() ? EIO : 0);
      if (transfer(fd, &reply, sizeof reply, 1)) return 1;
      continue;
    }
    if (length > 4 * 1024 * 1024 || (length & 511) || (offset & 511) ||
        offset > DISK_BYTES || length > DISK_BYTES - offset) return 1;
    unsigned char *data = malloc(length ? length : 1);
    if (!data) return 1;
    /* Port I/O exits before Linux can fault in the destination page. Commit
       each destination page before the host emulates REP INSW. */
    if (command == NBD_CMD_READ) {
      volatile unsigned char *touch = data;
      for (uint32_t i = 0; i < length; i += 512) touch[i] = 0;
      if (length) touch[length - 1] = 0;
    }
    int error = 0;
    if (command == NBD_CMD_WRITE && transfer(fd, data, length, 0)) {
      free(data);
      return 1;
    }
    if (command != NBD_CMD_READ && command != NBD_CMD_WRITE) error = EOPNOTSUPP;
    for (uint32_t i = 0; !error && i < length; i += 512)
      if (ata_sector((uint32_t)((offset + i) / 512), data + i,
                     command == NBD_CMD_WRITE)) error = EIO;
    if (!error && (type & NBD_CMD_FLAG_FUA) && ata_flush()) error = EIO;
    reply.error = htonl((uint32_t)error);
    if (transfer(fd, &reply, sizeof reply, 1) ||
        (!error && command == NBD_CMD_READ && transfer(fd, data, length, 1))) {
      free(data);
      return 1;
    }
    free(data);
  }
}
int main(int argc, char **argv) {
  (void)argc;
  if (strstr(argv[0], "ready")) {
    if (geteuid() || host_call(3)) return 1;
    return 0;
  }
  if (strstr(argv[0], "stop") || !strcmp(strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0], "nix")) {
    if (geteuid()) return 1;
    sync();
    host_call(2);
    return 1;
  }
  int sockets[2];
  int device = open("/dev/nbd0", O_RDWR | O_CLOEXEC);
  if (device < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) {
    perror("nix-block");
    return 1;
  }
  if (ioctl(device, NBD_SET_BLKSIZE, 512) ||
      ioctl(device, NBD_SET_SIZE_BLOCKS, DISK_BYTES / 512) ||
      ioctl(device, NBD_SET_FLAGS, NBD_FLAG_HAS_FLAGS | NBD_FLAG_SEND_FLUSH | NBD_FLAG_SEND_FUA) ||
      ioctl(device, NBD_SET_SOCK, sockets[0])) {
    perror("nix-block setup");
    return 1;
  }
  pid_t child = fork();
  if (child < 0) return 1;
  if (!child) {
    close(device);
    close(sockets[0]);
    return serve(sockets[1]);
  }
  close(sockets[1]);
  int result = ioctl(device, NBD_DO_IT);
  int saved = errno;
  ioctl(device, NBD_CLEAR_QUE);
  ioctl(device, NBD_CLEAR_SOCK);
  close(sockets[0]);
  close(device);
  waitpid(child, NULL, 0);
  if (result && saved != EPIPE) { errno = saved; perror("nix-block"); return 1; }
  return 0;
}
