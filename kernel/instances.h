/* Named intent disks live on the secondary IDE store, never Memories.
   Two checksummed registry banks protect names across interrupted updates. */
enum { INSTANCE_MAX = 6, INSTANCE_KERNEL = 1, INSTANCE_PIONEER = 2,
       INSTANCE_CORE = 3, INSTANCE_READY = 1 };
#define INSTANCE_SECTORS (20u * 1024u * 1024u * 2u)
#define INSTANCE_DATA_LBA 2048u
#define INSTANCE_MAGIC 0x3145524f5453504eULL
struct __attribute__((packed)) PersistentInstance {
  u32 used, kind, ram_mib, flags;
  char name[32];
  char repository[160];
  u32 start_lba, reserved;
  u8 base_hash[32];
};
struct __attribute__((packed)) InstanceRegistry {
  u64 magic;
  u32 version, generation, checksum, count;
  u64 capacity;
  struct PersistentInstance instance[INSTANCE_MAX];
  u8 padding[2576];
};
_Static_assert(sizeof(struct InstanceRegistry) == 4096, "instance registry size");
static struct InstanceRegistry instances, instance_bank[2];
static int instances_loaded;
static u32 instance_capacity;

static int instance_wait(int data) {
  for (u32 i = 0; i < 1000000; i++) {
    u8 s = inb(0x177);
    if (s == 0 || s == 0xff || (s & 0x21)) return 0;
    if (!(s & 0x80) && (!data || (s & 8))) return 1;
  }
  return 0;
}
static int instance_identify(void) {
  u16 words[256];
  const char *serial = "NIX-PIONEER-STORE";
  if (inb(0x177) == 0xff) return 0;
  outb(0x176, 0xe0);
  for (int i = 0; i < 4; i++) (void)inb(0x376);
  outb(0x172, 0);
  outb(0x173, 0);
  outb(0x174, 0);
  outb(0x175, 0);
  outb(0x177, 0xec);
  if (!instance_wait(1)) return 0;
  for (u32 i = 0; i < 256; i++) words[i] = inw(0x170);
  for (u32 i = 0; serial[i]; i++) {
    u8 c = (u8)(words[10 + i / 2] >> ((i & 1) ? 0 : 8));
    if (c != (u8)serial[i]) return 0;
  }
  instance_capacity = words[60] | ((u32)words[61] << 16);
  return instance_capacity >= INSTANCE_DATA_LBA + INSTANCE_SECTORS &&
         instance_capacity <= 0x10000000u;
}
static int instance_sector(u32 lba, void *buf, int write) {
  u16 *words = buf;
  if (!instance_capacity || lba >= instance_capacity || lba >= 0x10000000u)
    return 0;
  if (!instance_wait(0)) return 0;
  outb(0x176, (u8)(0xe0 | ((lba >> 24) & 15)));
  for (int i = 0; i < 4; i++) (void)inb(0x376);
  outb(0x172, 1);
  outb(0x173, (u8)lba);
  outb(0x174, (u8)(lba >> 8));
  outb(0x175, (u8)(lba >> 16));
  outb(0x177, write ? 0x30 : 0x20);
  if (!instance_wait(1)) return 0;
  for (u32 i = 0; i < 256; i++) {
    if (write) outw(0x170, words[i]);
    else words[i] = inw(0x170);
  }
  return instance_wait(0);
}
static int instance_flush(void) {
  if (!instance_capacity || !instance_wait(0)) return 0;
  outb(0x176, 0xe0);
  outb(0x177, 0xe7);
  return instance_wait(0);
}
static u32 instance_checksum(const struct InstanceRegistry *r) {
  const u8 *p = (const u8 *)r;
  u32 hash = 2166136261u;
  for (u32 i = 0; i < sizeof *r; i++) {
    u8 c = i >= 16 && i < 20 ? 0 : p[i];
    hash = (hash ^ c) * 16777619u;
  }
  return hash;
}
static int instance_bank_valid(const struct InstanceRegistry *r) {
  if (r->magic != INSTANCE_MAGIC || r->version != 1 ||
      r->count > INSTANCE_MAX || r->checksum != instance_checksum(r)) return 0;
  u32 count = 0;
  for (u32 i = 0; i < INSTANCE_MAX; i++) {
    const struct PersistentInstance *e = &r->instance[i];
    if (!e->used) continue;
    if (!e->name[0] || e->name[31] || e->repository[159] ||
        e->kind < INSTANCE_KERNEL || e->kind > INSTANCE_CORE ||
        e->start_lba != INSTANCE_DATA_LBA + i * INSTANCE_SECTORS ||
        (u64)e->start_lba + INSTANCE_SECTORS > instance_capacity) return 0;
    count++;
  }
  return count == r->count;
}
static int instance_load(void) {
  if (instances_loaded) return instances_loaded > 0;
  if (!instance_identify()) { instances_loaded = -1; return 0; }
  for (u32 bank = 0; bank < 2; bank++)
    for (u32 sector = 0; sector < 8; sector++)
      if (!instance_sector(bank * 8 + sector,
                           (u8 *)&instance_bank[bank] + sector * 512, 0)) {
        instances_loaded = -2;
        return 0;
      }
  int a = instance_bank_valid(&instance_bank[0]);
  int b = instance_bank_valid(&instance_bank[1]);
  if (a || b) {
    u32 best = !a ? 1 : !b ? 0 :
        (int)(instance_bank[1].generation - instance_bank[0].generation) > 0;
    memcpy(&instances, &instance_bank[best], sizeof instances);
  } else {
    const u8 *p = (const u8 *)instance_bank;
    for (u32 i = 0; i < sizeof instance_bank; i++)
      if (p[i]) { instances_loaded = -2; return 0; }
    memset(&instances, 0, sizeof instances);
    instances.magic = INSTANCE_MAGIC;
    instances.version = 1;
    instances.capacity = instance_capacity;
  }
  instances_loaded = 1;
  return 1;
}
static int instance_commit(void) {
  u32 old_generation = instances.generation, old_checksum = instances.checksum;
  instances.generation++;
  instances.checksum = instance_checksum(&instances);
  u32 bank = instances.generation & 1;
  for (u32 sector = 0; sector < 8; sector++)
    if (!instance_sector(bank * 8 + sector,
                          (u8 *)&instances + sector * 512, 1)) goto failed;
  if (!instance_flush()) goto failed;
  for (u32 sector = 0; sector < 8; sector++)
    if (!instance_sector(bank * 8 + sector,
                          (u8 *)&instance_bank[bank] + sector * 512, 0)) goto failed;
  if (!instance_bank_valid(&instance_bank[bank]) ||
      instance_bank[bank].generation != instances.generation) goto failed;
  return 1;
failed:
  instances.generation = old_generation;
  instances.checksum = old_checksum;
  return 0;
}
static int instance_find(const char *name) {
  if (!instance_load()) return -1;
  for (int i = 0; i < INSTANCE_MAX; i++)
    if (instances.instance[i].used &&
        (eq(instances.instance[i].name, name) ||
         (instances.instance[i].repository[0] && eq(instances.instance[i].repository, name))))
      return i;
  return -1;
}
static int instance_create(const char *name, u32 kind, const char *repository) {
  if (!instance_load()) {
    line(instances_loaded == -2 ? "The instance registry is damaged. It was not overwritten."
                               : "The dedicated instance store is not attached.");
    return -1;
  }
  int found = instance_find(repository && *repository ? repository : name);
  if (found >= 0) {
    if (instances.instance[found].kind != kind) {
      line("That name belongs to another kind of instance. It was not overwritten.");
      return -1;
    }
    return found;
  }
  for (int i = 0; i < INSTANCE_MAX; i++) {
    if (instances.instance[i].used ||
        (u64)INSTANCE_DATA_LBA + ((u64)i + 1) * INSTANCE_SECTORS > instance_capacity)
      continue;
    struct PersistentInstance *e = &instances.instance[i];
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->kind = kind;
    e->ram_mib = kind == INSTANCE_KERNEL ? 512 : 4096;
    e->start_lba = INSTANCE_DATA_LBA + (u32)i * INSTANCE_SECTORS;
    copy(e->name, name, sizeof e->name);
    copy(e->repository, repository ? repository : "", sizeof e->repository);
    instances.count++;
    if (instance_commit()) return i;
    memset(e, 0, sizeof *e);
    instances.count--;
    line("The instance name could not be saved. Existing instances were preserved.");
    return -1;
  }
  line("The instance store is full. Remove an unused saved instance first.");
  return -1;
}
static int instance_disk_io(int id, u32 lba, void *buf, int write) {
  if (id < 0 || id >= INSTANCE_MAX || !instances.instance[id].used ||
      lba >= INSTANCE_SECTORS) return 0;
  return instance_sector(instances.instance[id].start_lba + lba, buf, write);
}
static int instance_mark_ready(int id) {
  if (id < 0 || id >= INSTANCE_MAX || !instances.instance[id].used) return 0;
  if (instances.instance[id].flags & INSTANCE_READY) return 1;
  instances.instance[id].flags |= INSTANCE_READY;
  if (instance_commit()) return 1;
  instances.instance[id].flags &= ~INSTANCE_READY;
  return 0;
}
static int instance_delete(int id) {
  if (id < 0 || id >= INSTANCE_MAX || !instances.instance[id].used) return 0;
  struct PersistentInstance backup = instances.instance[id];
  memset(&instances.instance[id], 0, sizeof instances.instance[id]);
  instances.count--;
  if (instance_commit()) return 1;
  instances.instance[id] = backup;
  instances.count++;
  return 0;
}
